/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smpp_client.c                                                    */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contain functions regarding to initialisation          */
/*              : and termination of the working of smpp client                    */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#include<stdio.h>
#include<signal.h>
#include "gwlib/gwlib.h"
#include "include/smpp_client.h"
#include "include/smsc.h"
#include "gwlib/smpp_pdu.h"
#include "gwlib/process_sock.h"
static Octstr *filename;
static Octstr *logfile;

int giManagelogInterval;
//[sanchal][170309][to store the interval of log checking sizewise as well as day wise]
long giLogSize;
//[sanchal][050109][to store size of log read from conffile]



/**************************************************************************************/
/* Declaration of Local Variables.                                                    */
/**************************************************************************************/

List *outgoing_msg;
List *flow_threads;
List *suspended;
Counter *outgoing_msg_counter;
Counter *incoming_msg_counter;
volatile sig_atomic_t smpp_client_status;
volatile sig_atomic_t restart = 0; 

long queue_thread;
int dndcheck_flag = 0;
long log_file_index;/*log index of the log file*/
long log_file_thread_id;/*thread to handle log file names*/
Octstr *access_file_name = NULL;

/*Process conn related data strct*/
volatile enum process_program_status process_program_status = starting_up;
PROCESS_CLIENT* dbbox_client;/*client to dbbox*/
long dbbox_q_limit = -1;/*max no of msgs if dbbox is not connected*/
SERVER_DATA *forward_server;/*forward server related data*/
SERVER_DATA *reverse_server;/*reverse server related data*/
SERVER_DATA *dbdata_server;
/**************************************************************************************/
/* Declaration of Static Variables.                                                   */
/**************************************************************************************/

static Mutex *status_mutex; 
static time_t start_time; 
static volatile sig_atomic_t smpp_client_todo = 0;


enum {
	CLIENT_LOGREOPEN = 1,
	CLIENT_CHECKLEAKS = 2
};


/**************************************************************************************/
/* Forward declaration of local functions used in this file.                          */
/**************************************************************************************/

static int start_smsc(Cfg *cfg);
static void set_shutdown_status(void);
static void signal_handler(int signum);
static void setup_signal_handlers(void);
static int check_config(Cfg *cfg);
static int check_args(int i, int argc, char **argv);
static int initiate_smsc(Cfg *cfg);
static Cfg *init_smpp_client(Cfg *cfg);
static void queue_recv_thread(void *arg);

static int smpp_client_suspend(void);
static int smpp_client_resume(void);
static int smpp_client_stop_smsc(Octstr *id);
static int smpp_client_restart_smsc(Octstr *id);
static int smpp_client_restart(void);
static void empty_msg_lists(void);

/**************************************************************************************/
/* Purpose : function used to start the smsc connection and set the status of smsc    */
/* Input   : pointer to cfg which is used to read the configuration file.             */
/* Output  : return 0 if successfully started the smsc.                               */     
/**************************************************************************************/

static int start_smsc(Cfg *cfg)
{
	static int started = 0;
	if (started) return 0;

	start2_smsc(cfg);

	started = 1;
	return 0;
}

/**************************************************************************************/
/* Purpose : function used to set the shutdown status of smpp-client.                 */
/**************************************************************************************/

static void set_shutdown_status(void)
{
	sig_atomic_t old = smpp_client_status;
	smpp_client_status = CLIENT_SHUTDOWN;

	if (old == CLIENT_SUSPENDED)
		gwlist_remove_producer(suspended);
}

/**************************************************************************************/
/* Purpose : function used to handle the generated signal manually.                   */
/* Input   : signal number of signal generated.                                       */
/**************************************************************************************/

static void signal_handler(int signum)
{

	struct mis_update mis_update;

	//printf("-------INSIDE SIGNAL RECEIVED------------------ : %d\n",signum);
	info(0,"--------------SIGNAL RECEIVED--------------- %d",signum);

	if (!gwthread_shouldhandlesignal(signum))
		return;

	switch (signum) {
		case SIGINT:              
		case SIGTERM:
			{

				if (smpp_client_status != CLIENT_SHUTDOWN && smpp_client_status != CLIENT_DEAD) 
				{
					smpp_client_status = CLIENT_SHUTDOWN;
					//send_msg_forward_q(FORWARD_MSGQ_TYPE,TERMINATE_SYSTEM_THREAD,NULL,NULL);
				}
				else if (smpp_client_status == CLIENT_SHUTDOWN) {
					smpp_client_status = CLIENT_DEAD;
				}
				else if (smpp_client_status == CLIENT_DEAD) {
					panic(0, "Cannot die by its own will");
				}


				//shutdown_process_server(forward_server);
				//shutdown_process_server(reverse_server);
				//send_msg_system_q(SYSTEM_Q_FIRST_INFO,SHUTDOWN_SERVER_SYSTEM,0,NULL);
				//send_msg_mis_q(UPDATE_MIS_TABLE,SHUTDOWN_DB_SYSTEM,0,&mis_update);
				break;
			}

		case SIGSEGV:
			{

				/*Printfs are required cos we are not sure whether debug will print information or not.*/
				int j, nptrs;
				void *buffer[50];
				char **strings;
				char funcname[100];
				nptrs = backtrace(buffer, 50);

				info( 0, "backtrace() returned %d addresses",nptrs);
				gwthread_funcname(gwthread_self(),funcname);
				error(0, "SIGSEGV Generated by Process:%ld Thread:%ld Name:%s\n",gwthread_self_pid(),gwthread_self(),funcname);

				smpp_client_status = CLIENT_SHUTDOWN;

				int *p = NULL;
				*p = 10;strings = backtrace_symbols(buffer, nptrs);
				if(strings == NULL)
				{
					perror("backtrace_symbols");
					exit(EXIT_FAILURE);
				}
				for(j = 0; j < nptrs; j++)
				{
					info( 0, "Function Value is :%s",strings[j]);
				}
				gw_free(strings);
				//printf("\nsignal_handler : %s\n",strsignal(signum));

				/*
				   pthread_detach(pthread_self());
				   pthread_exit(pthread_self());
				   */
				exit(0);
				break;
			}


		case SIGHUP:
			smpp_client_todo |= CLIENT_LOGREOPEN;
			break;

		case SIGQUIT:
			smpp_client_todo |= CLIENT_CHECKLEAKS;  
			break;
	}

}

/**************************************************************************************/
/* Purpose : function used setup the signal handler.                                  */
/**************************************************************************************/

static void setup_signal_handlers(void)
{
	struct sigaction act;

	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGPIPE, &act, NULL);
	//sigaction(SIGSEGV, &act, NULL);
}

/**************************************************************************************/
/* Purpose : function used to check the configuration file.                           */
/* Input   : pointer to cfg structure.                                                */
/* Output  : return 0 if successfully find the SMPP-Client group in the configuration */
/*	   : file.                                                                    */
/*         : return -1 if there is no SMPP-Client group in the configuration file.    */
/**************************************************************************************/

static int check_config(Cfg *cfg)
{
	CfgGroup *grp;
	grp = cfg_get_single_group(cfg, octstr_imm("SMPP-Client"));
	if (grp == NULL)
		return -1;
	return 0;
}

/******************************************************************************************/
/* Purpose : function used to check the run time arguments.                               */
/* Input   : varibale i for and argc and argv for couting and storing run time parameters */
/* Output  : return 0 if successfully parsed the run time parameters.                     */
/*         : return -1 if wrong run time parameter is passed.                             */  
/******************************************************************************************/

static int check_args(int i, int argc, char **argv) 
{
	if (strcmp(argv[i], "-S")==0 || strcmp(argv[i], "--suspended")==0)
		smpp_client_status = CLIENT_SUSPENDED;
	else
		return -1;

	return 0;
}

/******************************************************************************************/
/* Purpose : function used to initiate the smsc connection.                               */
/* Input   : pointer to cfg structure used to read configuration file.                    */ 
/******************************************************************************************/

static int initiate_smsc (Cfg *cfg)
{
	List *list;
	list = cfg_get_multi_group(cfg, octstr_imm("smsc"));
	info(0,"Total no of SMSC Configured is %d",gwlist_len(list));
	if (list != NULL) {
		start_smsc(cfg);
		gwlist_destroy(list, NULL);        
	}
	debug("smpp_client",0,"SMSC Initialization completed");	
}
/******************************************************************************************/
/* Purpose : This thread is responsible for following things
   1. Day wise log file changed
   2. Handling for dbbox clinets connect, disconnects.
   3. Handling of forward server,reverse server client connected
 ******************************************************************************************/
static void manage_log_thread(void *arg)
{
	Octstr *date_string;
	Octstr *compare_string;
	struct mis_update mis_update;
	/*Testing*/
	Octstr *log_name;
	Cfg *cfg;
	CfgGroup *grp;
	Octstr *host;
	long port_num;
	long reconnect_delay;
	long client_timeout;
	long ping_delay;
	long ping_ack_timeout;
	cfg = (Cfg*)arg;

	int ret = 0;
	/*socket realted initialization*/
	grp = cfg_get_single_group(cfg, octstr_imm("core"));
	host = cfg_get(grp, octstr_imm("smsc-host"));
	if(host != NULL)
		warning(0,"smsc-host parameter is not used using localhost");

	if(cfg_get_integer(&ping_delay,grp,octstr_imm("smsc-ping-delay")) == -1)
		ping_delay = 10;
	if(cfg_get_integer(&dndcheck_flag,grp,octstr_imm("dndcheck_flag")) == -1)
		panic(0,"dndcheck_flag is not specified");

	if(cfg_get_integer(&ping_ack_timeout,grp,octstr_imm("smsc-pingack-timeout")) == -1)
		ping_ack_timeout = 5;

	if(cfg_get_integer(&client_timeout,grp,octstr_imm("smsc-client-timeout")) == -1)
		client_timeout = 2*(ping_delay + ping_ack_timeout);	

	if(cfg_get_integer(&port_num,grp,octstr_imm("smsc-forward-port")) == -1)
		panic(0,"smsc-forward-port is not specified");

	forward_server = init_start_server(&port_num,1,client_timeout);
	octstr_destroy(host);

	if(cfg_get_integer(&port_num,grp,octstr_imm("smsc-reverse-port")) == -1)
		panic(0,"smsc-reverse-port is not specified");


	reverse_server = init_start_server(&port_num,1,client_timeout);

	if(cfg_get_integer(&port_num,grp,octstr_imm("dbdata-port")) == -1)
		panic(0,"dbdata-port is not specified");


	dbdata_server = init_start_server(&port_num,1,client_timeout);

	host = cfg_get(grp, octstr_imm("dbbox-host"));
	if(host == NULL)
		panic(0,"dbbox-host is not specfied");
	if(cfg_get_integer(&port_num,grp,octstr_imm("dbbox-port")) == -1)
		panic(0,"dbbox-port is not specified");
	if(cfg_get_integer(&reconnect_delay,grp,octstr_imm("dbbox-reconnect-delay")) == -1)
	{
		warning(0,"dbbox-reconnect-delay is not specified,using default as 10 sec");
		reconnect_delay = 10;
	}	
	if(cfg_get_integer(&dbbox_q_limit,grp,octstr_imm("dbbox-unconnect-q-limit")) == -1)
		dbbox_q_limit = -1;	
	if(cfg_get_integer(&ping_delay,grp,octstr_imm("dbbox-ping-delay")) == -1)
		ping_delay = 10;

	if(cfg_get_integer(&ping_ack_timeout,grp,octstr_imm("dbbox-pingack-timeout")) == -1)
		ping_ack_timeout = 5;

	dbbox_client = init_start_client(host,port_num,reconnect_delay,ping_delay,ping_ack_timeout);
	if(dbbox_client != NULL)
	{
		info(0,"Initialised dbbox Thread id :%d",dbbox_client->thread_id);
		/*Sendin admind cmd to register conn id*/
		Conn_Msg *msg;
		msg = conn_msg_create(admin);
		msg->admin.command = 1;
		msg->admin.conn_id = octstr_create("smpp-smsc");
		send_msg_process_client(dbbox_client,msg);
		dbbox_client->conn_id = octstr_create("dbbox-client");
	}
	else
	{
		panic(0,"Cannot connect to dbbox");
	}
	octstr_destroy(host);

	date_string = get_log_format_datetime();
	gwlist_add_producer(flow_threads);
	while(smpp_client_status == CLIENT_RUNNING)
	{
		/*Initilaise all wake up thread ids to manage thread*/
		if(dbbox_client != NULL && dbbox_client->wakeup_thread_id == -1)
			dbbox_client->wakeup_thread_id = gwthread_self();
		if(forward_server != NULL && forward_server->wakeup_thread_id == -1)
			forward_server->wakeup_thread_id = gwthread_self();
		if(dbdata_server != NULL && dbdata_server->wakeup_thread_id == -1)
			dbdata_server->wakeup_thread_id = gwthread_self();
		if(reverse_server != NULL && reverse_server->wakeup_thread_id == -1)
			reverse_server->wakeup_thread_id = gwthread_self();
		/*check for commands received by the thread*/
		if(forward_server->server_cmd > 0)
		{
			/*Forward server receives no command from sever thread*/
			forward_server->server_cmd = SERVER_CMD_UNDEFINED;
		}
		//		if(dbdata_server->server_cmd > 0)
		//              {
		//                    /*Forward server receives no command from sever thread*/
		//                  dbdata_server->server_cmd = SERVER_CMD_UNDEFINED;
		//        }

		if(reverse_server->server_cmd > 0)
		{
			/*as soon as client is connected on the reverse port send smsc information and current dbbox status
			  based on which esmes will be connected*/
			if(reverse_server->server_cmd == SERVER_CMD_CLIENT_CONNECTED)
			{
				Conn_Msg *msg;
				info(0,"Client connected on reverse port");
				send_system_info_init();
				msg = conn_msg_create(admin);
				if(dbbox_client != NULL && dbbox_client->status == PROCESS_CONNECTED)
					msg->admin.command = admin_cmd_dbbox_connected;
				else
					msg->admin.command = admin_cmd_dbbox_disconnected;
				//printf("\nSending admin command -------------- :%d\n\n",msg->admin.command);	
				send_msg_reverse_server(msg);
			}
			reverse_server->server_cmd = SERVER_CMD_UNDEFINED;
		}
		//		info(0,"dbdata_server->server_cmd = %d dbdata_server->server_cmd = %d ",dbdata_server->server_cmd,dbdata_server->server_cmd);
		if(dbdata_server->server_cmd > 0)
		{
			/*as soon as client is connected on the reverse port send smsc information and current dbbox status
			 * 					based on which esmes will be connected*/
			if(dbdata_server->server_cmd == SERVER_CMD_CLIENT_CONNECTED)
			{
				Conn_Msg *msg;
				info(0,"Client connected on dbdata port");
				send_system_info_init();
				msg = conn_msg_create(admin);

				msg->admin.command = 808;

				send_msg_dbdata_server(msg);
			}
			dbdata_server->server_cmd = SERVER_CMD_UNDEFINED;
		}

		if(dbbox_client != NULL&& dbbox_client->client_cmd > 0)
		{
			/*if dbbox is  disonnected no records could be sent to database
			  send this message to server so that it unbind all esme
			  if dbbox gets connected the message conveyed to server to allow accepting connections
			  Moreover resiger to server again*/
			Conn_Msg *msg1;
			msg1 = conn_msg_create(admin);
			if(dbbox_client->client_cmd == SERVER_CMD_SERVER_DISCONNECTED)
			{
				msg1->admin.command = admin_cmd_dbbox_disconnected;
				info(0,"APPLICATION RECEIVED DBBOX CLIENT DISCONNECT");
			}
			/*if dbbox client gets connected register again with the server*/
			if(dbbox_client->client_cmd == SERVER_CMD_SERVER_CONNECTED)
			{
				info(0,"APPLICATION RECEIVED DBBOX CLIENT CONNECT");
				msg1->admin.command = admin_cmd_dbbox_connected;
				Conn_Msg *msg;
				msg = conn_msg_create(admin);
				msg->admin.command = 1;
				msg->admin.conn_id = octstr_create("smpp-smsc");
				send_msg_process_client(dbbox_client,msg);

			}
			send_msg_reverse_server(msg1);
			dbbox_client->client_cmd = SERVER_CMD_UNDEFINED;
		}
		/*date wise log file handling*/
		compare_string = get_log_format_datetime();
		if(octstr_compare(date_string,compare_string) != 0)
		{
			octstr_destroy(date_string);
			date_string = octstr_duplicate(compare_string);
			ret  = change_logfile_name(log_file_index);
			if(ret == 0)
			{
				log_name = log_file_name(log_file_index);
				//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
				octstr_destroy(log_name);
			}
			if(access_file_name != NULL)
			{
				log_name = change_access_file_name(access_file_name);
				if(log_name != NULL)
				{
					octstr_destroy(access_file_name);
					access_file_name = octstr_duplicate(log_name);
					octstr_destroy(log_name);
					//info(0, "Access Log file changed successfully:%s",octstr_get_cstr(access_file_name));					
				}
			}
			change_smsc_logfile();
			/*
			   send_msg_change_log_file();	
			   send_msgdb_change_log_file();

			   send_msg_system_q(SYSTEM_Q_FIRST_INFO,CHANGE_LOG_FILE,0,NULL);
			   send_msg_mis_q(UPDATE_MIS_TABLE,CHANGE_DB_LOG_FILE,0,&mis_update);			
			   */
		}
		//[sanchal][16032009][Code Added for sizewise access-log]
		else
		{

			if( access_file_name != NULL)
			{
				ret = accesslog_file_size( giLogSize, access_file_name);
				// Check the size of file
				// if the file crosses accesslog-file-size limit
				// then create another file by appending -no to
				// the original name
				if( ret == 0);
				//info(0,"Accesslog file name changed successfully");
				////printf("\n =========> AccessLog file name changed successfully /n");
			}

			if ((log_file_index != -1) )
			{
				ret = change_logfile_size( log_file_index, giLogSize );
				if( ret == 0)
				{
					log_name = log_file_name(log_file_index);
					//info(0, "Log file changed successfully: %s",octstr_get_cstr(log_name));
					octstr_destroy(log_name);
				}
			}
			change_smsc_logfile_onsize(giLogSize);
		}


		octstr_destroy(compare_string);
		//gwthread_sleep(60);	
		gwthread_sleep(giManagelogInterval);
	}

	octstr_destroy(date_string);
	gwlist_remove_producer(flow_threads);
	info( 0, "%s terminates.", __func__);
}

/****************************************************************************************/
/* Purpose : function used to initiate the smpp-client.                                 */
/* Input   : pointer to cfg structure used to read configuration file.                  */
/****************************************************************************************/

static Cfg *init_smpp_client(Cfg *cfg)
{
	CfgGroup *grp;
	Octstr *log,*val;
	long loglevel;
	int lf, m;
	lf = m = 1;
	char szTempVar[28];    //[sanchal][200309]
	Octstr *retVallog = NULL; //[sanchal][200309]
	Octstr *retVal = NULL;	//[sanchal][060409]
	Octstr *manage_log_interval = NULL; //[sanchal][060409]

	grp = cfg_get_single_group(cfg, octstr_imm("core"));
	//[sanchal] [170309][Reading the log file size from conf file]
	/* Get Access Log File size from configuration parameter */
	if (cfg_get_integer(&giLogSize, grp, octstr_imm("log-file-size")) != -1 ) {
		info(0, " Log Size is : %ld", giLogSize);
	}
	else
	{
		giLogSize = 1;
		info(0, " Default Log Size of 1MB is set");
		// [ Default Log Size is 1MB ]
	}

	//[sanchal][170309][ Reading the interval from configuration file]
	if ((manage_log_interval = cfg_get(grp, octstr_imm("log-interval"))) != NULL) {
		strcpy( szTempVar,octstr_get_cstr(manage_log_interval));
		giManagelogInterval = atoi(szTempVar);
		info(0, "Manage Log Interval %d", giManagelogInterval);
	}
	else
	{
		giManagelogInterval = 60;
		//[ Default mange log interval as not given in conf file]
		info(0, "Default Manage Log Interval %d", giManagelogInterval);	
	}			 

	octstr_destroy(manage_log_interval);
	grp = cfg_get_single_group(cfg, octstr_imm("core"));

	log = cfg_get(grp, octstr_imm("log-file"));

	if (log != NULL) 
	{
		/*
		   Octstr *log_file = NULL;
		   log_file = logfile_append_time(log);
		   if (cfg_get_integer(&loglevel, grp, octstr_imm("log-level")) == -1)
		   loglevel = 0;
		   log_file_index = log_open(octstr_get_cstr(log_file), loglevel, GW_NON_EXCL);
		   logfile = octstr_duplicate(log_file);
		   log_file_thread_id = gwthread_create(manage_log_thread,NULL);
		   octstr_destroy(log_file);	
		   log_file = log_file_name(log_file_index);
		//printf("\n\nORIGINAL FILE NAME :%d :%s\n",log_file_index,octstr_get_cstr(logfile));
		octstr_destroy(log);
		octstr_destroy(log_file);	
		*/
		/*commented by jigisha*/	
		log_file_thread_id = gwthread_create(manage_log_thread,cfg);
		retVallog = get_latst_filename(log);
		if( retVallog == NULL )
		{
			Octstr *logfile = NULL;
			logfile = logfile_append_time(log);
			if(cfg_get_integer(&loglevel, grp, octstr_imm("log-level")) == -1)
				loglevel = 0;
			log_file_index = log_open(octstr_get_cstr(logfile), loglevel, GW_NON_EXCL);
			info(0, "init_smpp_client: New logfile Name %s ", octstr_get_cstr(logfile));
			////printf(" \n\n --->> logfile : %s \n",octstr_get_cstr(logfile));
			octstr_destroy(logfile);
		}
		else
		{
			if(cfg_get_integer(&loglevel, grp, octstr_imm("log-level")) == -1)
				loglevel = 0;
			log_file_index = log_open(octstr_get_cstr(retVallog), loglevel, GW_NON_EXCL);
			////printf(" \n\n --->> retVallog : %s \n",octstr_get_cstr(retVallog));
			info(0, "init_smpp_client: Latest logfile Name %s ", octstr_get_cstr(retVallog));
		}

		octstr_destroy(retVallog);
		octstr_destroy(log);

	}

	if ((log = cfg_get(grp, octstr_imm("access-log-time"))) != NULL) {
		lf = (octstr_case_compare(log, octstr_imm("gmt")) == 0) ? 0 : 1;
		octstr_destroy(log);
	}

	m = 1;
	//[sanchal][170309][Open the access-log File]
	if ((log = cfg_get(grp, octstr_imm("access-log"))) != NULL)
	{
		info(0, "init_smpp_client: access-file-name---1 %s ", octstr_get_cstr(log));	
		Octstr *tempaccesslog_name;
		retVal = get_latst_filename(log);
		info(0, "init_smpp_client: access-file-name---return variable %s ", (retVal ? octstr_get_cstr(retVal):""));	
		if( retVal == NULL )
		{
			Octstr *logfile;
			logfile = logfile_append_time(log);
			alog_open(octstr_get_cstr(logfile), lf, m ? 0 : 1);
			tempaccesslog_name = octstr_duplicate(logfile);
			info(0, "init_smpp_client: access-file-name---3 %s ", octstr_get_cstr(tempaccesslog_name));	
			octstr_destroy(logfile);
		}
		else
		{
			tempaccesslog_name = octstr_duplicate(retVal);
			alog_open(octstr_get_cstr(retVal), lf, m ? 0 : 1);
			info(0, "init_smpp_client: access-file-name---4 %s ", octstr_get_cstr(tempaccesslog_name));	
		}

		access_file_name = tempaccesslog_name;

		//[sanchal][29072009]
		alog("alog_begins");

		octstr_destroy(log);
		octstr_destroy(retVal);
		retVal = NULL;
		//printf("\n-------------- access-file-name :---2 %s \n",octstr_get_cstr(access_file_name));
		info(0, "init_smpp_client: access-file-name---2 %s ", octstr_get_cstr(access_file_name));	
	}

	outgoing_msg = gwlist_create();
	outgoing_msg_counter = counter_create();
	incoming_msg_counter = counter_create();
	status_mutex = mutex_create();
	setup_signal_handlers();
	//log_file_thread_id = gwthread_create(manage_log_thread,cfg);
	debug("smpp_client",0,"SMPP Initialization completed");
}
/**************************************************************************************/
/*-----------NOT REQUIRED IN THIS DESIGN--------*/
/* Purpose : Thread function used to extact the values from message queue and route   */
/*         : the incoming msg.                                                        */
/* Input   : NULL pointer.                                                            */
/**************************************************************************************/

static void queue_recv_thread(void *arg)
{
	Msg *msg = NULL;
	int ret; 
	struct sm_info *data = NULL;
	//gwlist_add_producer(flow_threads);
	/*if(smpp_client_status == CLIENT_SUSPENDED) {
	  info(0, "Queue Recv Thread: SMPP Client is started in Suspend State, Sleeping");
	  gwthread_sleep(100);
	  queue_recv_thread(NULL);
	  }
	  */
	debug("smpp_client",0,"Queue Recv Thread is ready to receive and route the message");

	while(smpp_client_status != CLIENT_SHUTDOWN)
	{
		//msg->sm_info->pdu = recv_msg_forward_q(FORWARD_MSGQ_TYPE,0,msg->routing_info);
		if(msg->sm_info->pdu == NULL) 
		{
			msg_destroy(msg);	
			break;
		}
		msg->routing_info = gw_malloc(sizeof(struct forward_queue_data));


		msg->sm_info->account_name  = octstr_create(msg->routing_info->account_name);
		msg->sm_info->account_msg_id = octstr_create(msg->routing_info->account_msg_id);
		msg->sm_info->req_time = octstr_create(msg->routing_info->req_time);
		debug("smpp-client",0,"Read Msg Acc name :%s message ID:%s",octstr_get_cstr(msg->sm_info->account_name),octstr_get_cstr(msg->sm_info->account_msg_id));	
		/*
		//printf("\n\\\\\\\\\\\\\\\\\\\\\\\\MSG RECV :%s 	:%s\n",\
		msg->routing_info->req_time,octstr_get_cstr(msg->sm_info->req_time));
		*/


		smsc2_rout(msg);		  
	}
	//gwlist_remove_producer(flow_threads);
}

/**************************************************************************************/
/* Purpose : Local function used to destroy the list and counter                      */
/**************************************************************************************/

static void empty_msg_lists(void)
{
	Msg *msg;
	/*
	   if (gwlist_len(outgoing_msg) > 0)
	   debug("smpp-client", 0, "Remaining Messages => %ld outgoing", gwlist_len(outgoing_msg));
	   */
	info(0, "Total Messages (Submit_sm) Successfully Sent to SMSC : Sent = %ld",
			counter_value(outgoing_msg_counter));
	info(0, "Total Messages (Deliver_sm/Data_sm) Received from SMSC : Received = %ld",
			counter_value(incoming_msg_counter));
	//gwlist_destroy(outgoing_msg, msg_destroy);
	counter_destroy(outgoing_msg_counter);
	counter_destroy(incoming_msg_counter); 
}

struct smsc_conn_list
{
	Octstr *smsc_id;
	Octstr *smsc_type;
	int conn_status;
	int conn_mode;
};
typedef struct smsc_conn_list SMSC_Conn_List;
/*
   void valid_smsc_find( Octstr *dedicated_smsc, Octstr *list_prefer_smsc1, Octstr **ospreferred_smsc, Octstr **osallowed_smsc) {

   Octstr *number_type = NULL;
   Octstr *prefer_smsc = NULL;
   SMSC_Conn_List *smsc_conn_list;
   List *list_prefer_smsc;
   list_prefer_smsc = octstr_split(list_prefer_smsc1, octstr_imm(";")); 

   int i, j, k = 0;
   int index_not_allowed = 0;
   int index_preferred_smsc = 0;
   int index_allowed_smsc = 0;
   char preferred_smsc[256];
   char allowed_smsc[256];


   for (i = 0; i < 256; i++) {
   preferred_smsc[i] = -1;
   allowed_smsc[i] = -1;
   }

   if (dedicated_smsc != NULL) {
   for (i = 0; i < gwlist_len(smsc_list); i++) {
   smsc_conn_list = gwlist_get(smsc_list, i);

   if ((octstr_compare(dedicated_smsc, smsc_conn_list->smsc_id) == 0) &&
   ((octstr_compare(number_type, smsc_conn_list->smsc_type) == 0) ||
   octstr_compare(smsc_conn_list->smsc_type, octstr_imm("all")) == 0) &&
   ((smsc_conn_list->conn_mode == 1) || (smsc_conn_list->conn_mode == 3))
   ) {
   preferred_smsc[index_preferred_smsc++] = i;
   break;
   }
   }
   }

   if (list_prefer_smsc != NULL) {
   for (i = 0; i < gwlist_len(list_prefer_smsc); i++) {
   prefer_smsc = gwlist_get(list_prefer_smsc, i);
   for (j = 0; j < gwlist_len(smsc_list); j++) {
   smsc_conn_list = gwlist_get(smsc_list, j);
   if ((octstr_compare(prefer_smsc, smsc_conn_list->smsc_id) == 0) &&
   ((octstr_compare(number_type, smsc_conn_list->smsc_type) == 0) ||
   octstr_compare(smsc_conn_list->smsc_type, octstr_imm("all")) == 0) &&
   ((smsc_conn_list->conn_mode == 1) || (smsc_conn_list->conn_mode == 3))
   ) {
   preferred_smsc[index_preferred_smsc++] = j;
   break;
   }
   }
   }
   }

   debug("test.smpp", 0, "Preferred SMSCs :%d ", index_preferred_smsc);

   for (i = 0; i < gwlist_len(smsc_list); i++) {

   if (index_preferred_smsc > 0) {
   for (j = 0; j <= index_preferred_smsc; j++) {
   if (preferred_smsc[j] == i) {
   debug("test.smpp", 0, "SMSC at index :%d is already in preferred list", j);
   index_not_allowed = 1;
   break;
   }
   }
   if (index_not_allowed) {
   index_not_allowed = 0;
   continue;
   }
   }

smsc_conn_list = gwlist_get(smsc_list, i);
debug("test.smpp", 0, "Checking Allowed SMSC :%s   :%s :%d", octstr_get_cstr(smsc_conn_list->smsc_id), octstr_get_cstr(smsc_conn_list->smsc_type), smsc_conn_list->conn_status);
if (((octstr_compare(number_type, smsc_conn_list->smsc_type) == 0) ||
			(octstr_compare(smsc_conn_list->smsc_type, octstr_imm("all")) == 0))&&
		((smsc_conn_list->conn_mode == 1) || (smsc_conn_list->conn_mode == 3))
   ) {
	allowed_smsc[index_allowed_smsc++] = i;
	debug("test.smpp", 0, "Allowed SMSC :%s :%d", octstr_get_cstr(smsc_conn_list->smsc_id), i);
}
}
debug("test.smpp", 0, "Preferred SMSC :%d, Allowed SMSC :%d", index_preferred_smsc, index_allowed_smsc);

#if 0 
for (i = 0; i < MAX_NO_SMSC / 2; i++) {
	debug("test.smpp", 0, "Prefer[%d] :%d ", i, preferred_smsc[i]);
}
for (i = 0; i < MAX_NO_SMSC / 2; i++) {
	debug("test.smpp", 0, "Allowed[%d] :%d", i, allowed_smsc[i]);
}

#endif 

*ospreferred_smsc = octstr_create("");
*osallowed_smsc = octstr_create("");


for (i = 0; i < index_preferred_smsc; i++)
octstr_append_char(*ospreferred_smsc, preferred_smsc[i]);

for (i = 0; i < index_allowed_smsc; i++)
octstr_append_char(*osallowed_smsc, allowed_smsc[i]);


debug("test.smpp", 0, "Preferred SMSC [%d]LEN[%d], Allowed SMSC [%d][%d]",
		index_preferred_smsc,
		octstr_len(*ospreferred_smsc),
		index_allowed_smsc,
		octstr_len(*osallowed_smsc));

if (index_preferred_smsc > 0 || index_allowed_smsc > 0) {
	octstr_destroy(number_type);
	return 0;
} else {
	error(0, "None of the SMSC could be found pre:%d all:%d dest:%s type:%s SMSClen :%d",
			index_preferred_smsc,
			index_allowed_smsc,
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(number_type));
	octstr_destroy(number_type),
		gwlist_len(smsc_list);
	return -1;
}
}
*/

/**************************************************************************************/
/* Purpose : This functions is called when any data is received on the server.          */
/**************************************************************************************/
void handle_request(Conn_Msg *msg,PROCESS_CLIENT *process_client)
{
	Octstr *tmp;	
	SMPP_PDU *pdu;
	if(smpp_client_status != CLIENT_RUNNING)
	{
		warning(0,"Messge received when SMSC process is shutting down :%d",conn_msg_type(msg));
		if(conn_msg_type(msg) == submit)
		{
			error(0,"SMS discrading : shutdown :%s :%s",
					octstr_get_cstr(msg->submit.account_name),
					octstr_get_cstr(msg->submit.account_msg_id));
			tmp = octstr_from_position(msg->submit.pdu,4);
			pdu = smpp_pdu_unpack(tmp);
			octstr_destroy(tmp);
			mis_db_insert(	msg->submit.account_name,
					msg->submit.account_msg_id,
					msg->submit.req_time,
					NULL,
					NULL,
					pdu,
					0x0000040B, msg->submit.IP);
		}
		conn_msg_destroy(msg);
		return;
	}

	if(conn_msg_type(msg) == admin)
	{
		if(msg->admin.command == 1)
		{
			if(process_client->conn_id != NULL)
				octstr_destroy(process_client->conn_id);
			process_client->conn_id = octstr_duplicate(msg->admin.conn_id);
			info(0,"[CID:%s] Registered from [IP%s][PORT%d][CLIENTNUM%d]",
					octstr_get_cstr(process_client->conn_id),
					octstr_get_cstr(process_client->client_ip),
					process_client->client_port,
					process_client->client_num
			    );
		}
	}

	if(conn_msg_type(msg)==submit)
	{
		Msg *submit_msg;
		int i,j;
		int allowed_smsc,preferred_smsc;
		int ch;
		allowed_smsc = preferred_smsc = 0;
		if(dndcheck_flag==1)
		{
			msg->submit.preferred_smsc = octstr_create("0");
			msg->submit.allowed_smsc = octstr_create("");
		}	


		/*read first 2 bytes for length*/
		if(octstr_len(msg->submit.preferred_smsc) == 0 && octstr_len(msg->submit.allowed_smsc) == 0)
		{
			error(0,"ROUTING INFORMATION NOT ENOUGH Prefer:%d Allowed:%d",octstr_len(msg->submit.preferred_smsc),octstr_len(msg->submit.allowed_smsc));
			goto ret;
		}

		if(dndcheck_flag==0){
			if(msg->submit.pdu == NULL)
			{
				error(0,"PDU found NULL Discarding message");
				goto ret;
			}
		}
		submit_msg = msg_create();
		if(dndcheck_flag==0){	
			submit_msg->sm_info->pdu = octstr_duplicate(msg->submit.pdu);
			if(submit_msg->sm_info->pdu == NULL) 
			{
				goto ret;
			}
		}

		strcpy(submit_msg->routing_info->req_time,octstr_get_cstr(msg->submit.req_time));
		strcpy(submit_msg->routing_info->account_name,octstr_get_cstr(msg->submit.account_name));
		strcpy(submit_msg->routing_info->account_msg_id,octstr_get_cstr(msg->submit.account_msg_id));
		strcpy(submit_msg->routing_info->destination_addr,octstr_get_cstr(msg->submit.destination_addr));
		strcpy(submit_msg->routing_info->source_addr,octstr_get_cstr(msg->submit.source_addr));
		submit_msg->routing_info->data_coding = msg->submit.data_coding;
		submit_msg->routing_info->esm_class = msg->submit.esm_class;
		
		submit_msg->routing_info->more_messages_to_send = msg->submit.more_messages_to_send;
		submit_msg->sm_info->client_ip = octstr_duplicate(msg->submit.IP);
		debug("smpp_client",0,"1. smpp_smsc *******IP rama ******:%s",octstr_get_cstr(submit_msg->sm_info->client_ip));


		for(i = 0,j=2; i < octstr_len(msg->submit.preferred_smsc);i++,j+=2)
		{
			if(dndcheck_flag==0)
				ch = octstr_get_char(msg->submit.preferred_smsc,i);
			else
				ch=0;

			submit_msg->routing_info->preferred_smsc[i] = ch;
			//	debug("rout",0,"Received Prefer :%d put :%d",ch,submit_msg->routing_info->preferred_smsc[i]);
		}
		submit_msg->routing_info->preferred_smsc[i] = -1;

		for(i = 0; i < octstr_len(msg->submit.allowed_smsc);i++)
		{

			ch = octstr_get_char(msg->submit.allowed_smsc,i);
			submit_msg->routing_info->allowed_smsc[i] = ch;
			//	debug("rout",0,"Received Allowed :%d put :%d",ch,submit_msg->routing_info->allowed_smsc[i]);
		}
		submit_msg->routing_info->allowed_smsc[i] = -1;
		for(i = octstr_len(msg->submit.allowed_smsc);i < MAX_NO_SMSC/2 ; i++)
			submit_msg->routing_info->allowed_smsc[i] = -1;
		for(i = octstr_len(msg->submit.preferred_smsc);i < MAX_NO_SMSC/2 ; i++)
			submit_msg->routing_info->preferred_smsc[i] = -1;


		submit_msg->sm_info->account_name  = octstr_duplicate(msg->submit.account_name);
		submit_msg->sm_info->account_msg_id = octstr_duplicate(msg->submit.account_msg_id);
		submit_msg->sm_info->req_time = octstr_duplicate(msg->submit.req_time);
		submit_msg->routing_info->content = octstr_duplicate(msg->submit.content);
		submit_msg->routing_info->udhdata = octstr_duplicate(msg->submit.udh);
		debug("smpp-client",0,"Read Msg Acc name :%s message ID:%s",
				octstr_get_cstr(submit_msg->sm_info->account_name),
				octstr_get_cstr(submit_msg->sm_info->account_msg_id));	


		smsc2_rout(submit_msg);		  
	}
ret:
	conn_msg_destroy(msg);
}


/*void handle_request(Conn_Msg *msg,PROCESS_CLIENT *process_client)
  {
  Octstr *tmp;	
  SMPP_PDU *pdu;
  if(smpp_client_status != CLIENT_RUNNING)
  {
  warning(0,"Messge received when SMSC process is shutting down :%d",conn_msg_type(msg));
  if(conn_msg_type(msg) == submit)
  {
  error(0,"SMS discrading : shutdown :%s :%s",
  octstr_get_cstr(msg->submit.account_name),
  octstr_get_cstr(msg->submit.account_msg_id));
  tmp = octstr_from_position(msg->submit.pdu,4);
  pdu = smpp_pdu_unpack(tmp);
  octstr_destroy(tmp);

  debug("test.smpp", 0, "----------BEFORE SENDING MESSAGE TO CLIENT---------3");
  mis_db_insert(	msg->submit.account_name,
  msg->submit.account_msg_id,
  msg->submit.req_time,
  NULL,
  NULL,
  pdu,
  0x0000040B, msg->submit.IP);
  }
  conn_msg_destroy(msg);
  return;
  }
  if(conn_msg_type(msg) == admin)
  {
  if(msg->admin.command == 1)
  {
  if(process_client->conn_id != NULL)
  octstr_destroy(process_client->conn_id);
  process_client->conn_id = octstr_duplicate(msg->admin.conn_id);
  info(0,"[CID:%s] Registered from [IP%s][PORT%d][CLIENTNUM%d]",
  octstr_get_cstr(process_client->conn_id),
  octstr_get_cstr(process_client->client_ip),
  process_client->client_port,
  process_client->client_num
  );
  }
  }
  if(conn_msg_type(msg)==submit)
  {
  info(0,"yoooooooooooooooo ==============================================");	

  Msg *submit_msg;
  int i,j;
  int allowed_smsc,preferred_smsc;
  int ch;
  allowed_smsc = preferred_smsc = 0;

  msg->submit.preferred_smsc = octstr_create("0");
  msg->submit.allowed_smsc = octstr_create("");
  info (0, "length is ================= %d %d \n",octstr_len(msg->submit.preferred_smsc), octstr_len(msg->submit.allowed_smsc));
  if(octstr_len(msg->submit.preferred_smsc) == 0 && octstr_len(msg->submit.allowed_smsc) == 0)
  {
  error(0,"ROUTING INFORMATION NOT ENOUGH Prefer:%d Allowed:%d",octstr_len(msg->submit.preferred_smsc),octstr_len(msg->submit.allowed_smsc));
  goto ret;
  }
  submit_msg = msg_create();
  strcpy(submit_msg->routing_info->req_time,octstr_get_cstr(msg->submit.req_time));
  strcpy(submit_msg->routing_info->account_name,octstr_get_cstr(msg->submit.account_name));
  strcpy(submit_msg->routing_info->account_msg_id,octstr_get_cstr(msg->submit.account_msg_id));
  strcpy(submit_msg->routing_info->destination_addr,octstr_get_cstr(msg->submit.destination_addr));
  strcpy(submit_msg->routing_info->source_addr,octstr_get_cstr(msg->submit.source_addr));
  submit_msg->sm_info->client_ip = octstr_duplicate(msg->submit.IP);
  debug("smpp_client",0,"1. smpp_smsc *******IP rama ******:%s",octstr_get_cstr(submit_msg->sm_info->client_ip));

  for(i = 0,j=2; i < 1;i++,j+=2)
{
	ch = octstr_get_char(msg->submit.preferred_smsc,i);
	submit_msg->routing_info->preferred_smsc[i] = 0;
	debug("rout",0,"Received Prefer :%d put :%d",ch,submit_msg->routing_info->preferred_smsc[i]);
}
submit_msg->routing_info->preferred_smsc[i] = -1;

for(i = 0; i < 0;i++)
{
	ch = octstr_get_char(msg->submit.allowed_smsc,i);
	submit_msg->routing_info->allowed_smsc[i] = msg->submit.allowed_smsc;
	debug("rout",0,"Received Allowed :%d put :%d",ch,submit_msg->routing_info->allowed_smsc[i]);
}
submit_msg->routing_info->allowed_smsc[i] = -1;
for(i = octstr_len(msg->submit.allowed_smsc);i < MAX_NO_SMSC/2 ; i++)
submit_msg->routing_info->allowed_smsc[i] = -1;
for(i = octstr_len(msg->submit.preferred_smsc);i < MAX_NO_SMSC/2 ; i++)
submit_msg->routing_info->preferred_smsc[i] = -1;


submit_msg->sm_info->account_name  = octstr_duplicate(msg->submit.account_name);
submit_msg->sm_info->account_msg_id = octstr_duplicate(msg->submit.account_msg_id);
submit_msg->sm_info->req_time = octstr_duplicate(msg->submit.req_time);

debug("smpp-client",0,"Read Msg Acc name :%s message ID:%s",
		octstr_get_cstr(submit_msg->sm_info->account_name),
		octstr_get_cstr(submit_msg->sm_info->account_msg_id));	


smsc2_rout(submit_msg);		  
}
ret:
conn_msg_destroy(msg);
}*/
int mis_db_insert(  Octstr *account_name,
		Octstr *account_msg_id,
		Octstr *req_time,
		Octstr *smsc_id,
		Octstr *smsc_msg_id,
		SMPP_PDU *pdu,
		long submit_status, Octstr *IP)
{
	int ret;
	Conn_Msg *msg;
	msg = conn_msg_create(insert_mis);

	msg->insert_mis.account_name = octstr_duplicate(account_name);
	msg->insert_mis.account_msg_id = octstr_duplicate(account_msg_id);
	if(smsc_id != NULL)
		msg->insert_mis.smsc_id = octstr_duplicate(smsc_id);
	else	
		msg->insert_mis.smsc_id = octstr_create("");
	if(smsc_msg_id != NULL)
		msg->insert_mis.smsc_msg_id= octstr_duplicate(smsc_msg_id);
	else
		msg->insert_mis.smsc_msg_id = octstr_create("");
	msg->insert_mis.req_time = octstr_duplicate(req_time);

	msg->insert_mis.source_addr = octstr_duplicate(pdu->u.submit_sm.source_addr);
	msg->insert_mis.dest_addr = octstr_duplicate(pdu->u.submit_sm.destination_addr);
	msg->insert_mis.submit_status = submit_status;
	msg->insert_mis.esm_class = pdu->u.submit_sm.esm_class;
	msg->insert_mis.message_len = octstr_len(pdu->u.submit_sm.short_message);
	msg->insert_mis.msg_content = octstr_duplicate(pdu->u.submit_sm.short_message);
	//printf("\n\n-------------------data is :%d :%lx\n\n",octstr_len(msg->insert_mis.msg_content),octstr_get_char(msg->insert_mis.msg_content,0));
	if(pdu->u.submit_sm.user_response_code != -1)
		msg->insert_mis.retry_value = pdu->u.submit_sm.user_response_code;
	else
		msg->insert_mis.retry_value = 0;
	msg->insert_mis.IP = octstr_duplicate(IP);
	debug("smpp",0,"msgid received on inserting : %s IP[%s]",octstr_get_cstr(msg->insert_mis.smsc_msg_id),octstr_get_cstr(msg->insert_mis.IP));

	send_msg_dbbox_server(msg,0);

	return 0;
}



/*************************************************************************************
Purpose : This functions sends the data to dbbox server.If client gets disconnected message is destroyed.
This prevents piling up of data in the list when dbbox is not connected.
This is fine as long as we make sure that if dbbox gets disconnected to client,message is sent to server which disconnects all esmes.
 *************************************************************************************/

int send_msg_dbbox_server(Conn_Msg *msg,int mode)
{

	/*
	   if(smpp_client_status != CLIENT_RUNNING)
	   return -1;
	   */
	if(msg == NULL)
	{
		return 0;
	}
	if(dbbox_client == NULL)
	{
		error(0,"dbbox client not found");
		return 0;
	}

	/*This is cos even if the dbbox is not connected we should sent it for later processing*/
	if(	(mode == 1)										||
			((dbbox_client->status != PROCESS_CONNECTED)	&&	
			 (dbbox_q_limit != -1)&&(gwlist_len(dbbox_client->send_list) >= dbbox_q_limit))
	  )
	{
		error(0,"Received dbbox data when dbbox is disconnected/terminating :%d",conn_msg_type(msg));
		/*write data in access log */

		if(conn_msg_type(msg)==insert_mis)
		{
			if(msg->insert_mis.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
				binary_msg_to_ascii(msg->insert_mis.msg_content,&(msg->insert_mis.udh),&(msg->insert_mis.msg_content));
			warning(0,	"SHUT/DIS MIS INSERT MSGS [ESMEAccName:%s][ESMEAccMsgId:%s]"
					"[SMSCId:%s][SMSCMsgId:%s][SourceAddr:%s][DestinationAddr:%s]"
					"[RecvTime:%s][SubmittedStat:%d][RetryValue:%d]"
					"[UDH:%s][Content:%s]",
					octstr_get_cstr(msg->insert_mis.account_name),
					octstr_get_cstr(msg->insert_mis.account_msg_id),
					msg->insert_mis.smsc_id ? octstr_get_cstr(msg->insert_mis.smsc_id):"",
					msg->insert_mis.smsc_msg_id ? octstr_get_cstr(msg->insert_mis.smsc_msg_id):"",
					octstr_get_cstr(msg->insert_mis.source_addr),
					octstr_get_cstr(msg->insert_mis.dest_addr),
					octstr_get_cstr(msg->insert_mis.req_time),
					msg->insert_mis.submit_status,
					msg->insert_mis.retry_value,
					msg->insert_mis.udh ? octstr_get_cstr(msg->insert_mis.udh):"",
					octstr_get_cstr(msg->insert_mis.msg_content)
			       );
		}
		else if(conn_msg_type(msg)==insert_status)
		{
			alog("SHUT/DIS STATUS INSERT[SMSCId:%s][SMSCMsgId:%s][DestinationAddr:%s][DeliveredStat:%d]",
					octstr_get_cstr(msg->insert_status.smsc_id),
					octstr_get_cstr(msg->insert_status.smsc_msg_id),
					octstr_get_cstr(msg->insert_status.dest_addr),
					msg->insert_status.deliver_status
			    );
		}

		conn_msg_destroy(msg);
		return -1;
	}
	if(conn_msg_type(msg) == insert_mis || conn_msg_type(msg) == insert_status)
	{
		send_msg_process_client(dbbox_client,msg);	
		return 1;
	}
	return 0;
}
/****************************************************************************************/
/* Purpose : Sends message to the cleint connected on the reverse port.currently only single client is supported
   more than one client cannnot connect on reverse port.
   If client is not connected/in case of any other error destroys the msg memory
 ****************************************************************************************/
int send_msg_reverse_server(Conn_Msg *msg)
{
	PROCESS_CLIENT *process_conn;
	if(smpp_client_status != CLIENT_RUNNING)
		return -1;
	if(	(msg == NULL) 				||
			(reverse_server == NULL)	||
			(gwlist_len(reverse_server->client_list) == 0)
	  )
	{
		error(0,"reverse server not connected : discarding msg :%d",conn_msg_type(msg));
		conn_msg_destroy(msg);
		return -1;
	}
	gw_rwlock_rdlock(reverse_server->list_lock);
	if(gwlist_len(reverse_server->client_list) == 0)
	{
		gw_rwlock_unlock(reverse_server->list_lock);
		conn_msg_destroy(msg);
		return -1;		
	}
	process_conn = gwlist_get(reverse_server->client_list,0);
	send_msg_process_client(process_conn,msg);	
	gw_rwlock_unlock(reverse_server->list_lock);
	return 0;
}

int send_msg_dbdata_server(Conn_Msg *msg)
{
	PROCESS_CLIENT *process_conn;
	if(smpp_client_status != CLIENT_RUNNING)
		return -1;
	if(     (msg == NULL)                           ||
			(dbdata_server == NULL)        ||
			(gwlist_len(dbdata_server->client_list) == 0)
	  )
	{
		error(0,"dbdata server not connected : discarding msg :%d",conn_msg_type(msg));
		conn_msg_destroy(msg);
		return -1;
	}
	gw_rwlock_rdlock(dbdata_server->list_lock);
	if(gwlist_len(dbdata_server->client_list) == 0)
	{
		gw_rwlock_unlock(dbdata_server->list_lock);
		conn_msg_destroy(msg);
		return -1;
	}
	process_conn = gwlist_get(dbdata_server->client_list,0);
	send_msg_process_client(process_conn,msg);
	gw_rwlock_unlock(dbdata_server->list_lock);
	return 0;
}

/**************************************************************************************/
/* 		Main Function : Program starts execution from here.                   */
/**************************************************************************************/

int main(int argc, char **argv)
{
	int cf_index;
	Cfg *cfg;

	key_t key;
	int msgqid;
	int i;
	int len;
	smpp_client_status = CLIENT_RUNNING;
	gwlib_init();
	start_time = time(NULL);
	suspended = gwlist_create();
	flow_threads = gwlist_create();
	gwlist_add_producer(suspended);

	report_smpp_client_versions("SMPP-Client");

	cf_index = get_and_set_debugs(argc, argv, check_args);

	if (argv[cf_index] == NULL)
		filename = octstr_create("smpp-client.conf");
	else
		filename = octstr_create(argv[cf_index]);

	cfg = cfg_create(filename); 
	if (cfg_read(cfg) == -1)
		panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));

	info(0, "MAIN: Start-up done, entering mainloop");
	if(check_config(cfg) == -1)
		panic(0, "Cannot start with corrupted configuration");

	init_smpp_client(cfg);
	info(0, "-----------------------------------------------------");
	info(0, GW_NAME " SMPP-SMSC Version %s Starting", GW_VERSION);
	info(0, "-----------------------------------------------------");
	/*
	   init_forward_q(); 
	   init_reverse_q();
	   init_system_q();
	   init_mis_q();
	   init_database_q();
	   */
	client_httpadmin_start(cfg);

	initiate_smsc(cfg);
	/*gwthread_create(log_thread, NULL);*/

	if (smpp_client_status == CLIENT_SUSPENDED) {
		info(0, "SMPP Client is now SUSPENDED by startup arguments");
	}else {
		smsc2_resume();
		gwlist_remove_producer(suspended);	
	}

	while (smpp_client_status != CLIENT_SHUTDOWN && smpp_client_status != CLIENT_DEAD && 
			gwlist_producer_count(flow_threads) > 0) {

		gwthread_sleep(10.0);

		if (smpp_client_todo == 0) {
			continue;
		}
		if (smpp_client_todo & CLIENT_LOGREOPEN) {
			warning(0, "SIGHUP received, catching and re-opening logs");
			log_reopen();
			alog_reopen();
			smpp_client_todo = smpp_client_todo & ~CLIENT_LOGREOPEN;
		}

		if (smpp_client_todo & CLIENT_CHECKLEAKS) {
			warning(0, "SIGQUIT received, reporting memory usage.");
			gw_check_leaks();
			smpp_client_todo = smpp_client_todo & ~CLIENT_CHECKLEAKS;
		}
	}

	if (smpp_client_status == CLIENT_SHUTDOWN || smpp_client_status == CLIENT_DEAD)
		warning(0, "Killing Signal or HTTP admin Command Received, Shutting Down...");

	/*Wake up various client and server threads*/
	smpp_client_shutdown();
	//printf("\n\n--------------smpp_client_shutdown-----1\n\n");
	while (gwlist_consume(flow_threads) != NULL)
		;

	info(0, "All flow threads have died, killing SMPP-Client");
	smpp_client_status = CLIENT_DEAD;

	len = gwlist_len(dbbox_client->send_list);
	//printf("\n\n--------------smpp_client_shutdown------2\n\n");
	if(len > 0)
		sleep(20);
	process_program_status = shutting_down;
	int forward_listening_thread = forward_server->listening_thread_id;
	int reverse_listening_thread = reverse_server->listening_thread_id;
	int dbdata_listening_thread = dbdata_server->listening_thread_id;
	gwthread_wakeup(forward_listening_thread);
	gwthread_wakeup(reverse_listening_thread);
	gwthread_wakeup(dbdata_listening_thread);
	gwthread_join(dbdata_listening_thread);
	gwthread_join(forward_listening_thread);
	gwthread_join(reverse_listening_thread);
	/*Kill dbbox at the end*/	
	if(dbbox_client != NULL && gwlist_len(dbbox_client->send_list) > 0)
	{
		Conn_Msg *msg;
		len = gwlist_len(dbbox_client->send_list);
		error(0,"Shutdown msgs in dbbox :%d",len);
		for(i = 0 ;i < len; i++)
		{
			msg = gwlist_extract_first(dbbox_client->send_list);
			send_msg_dbbox_server(msg,1);
		}
	}
	if(dbbox_client != NULL && dbbox_client->thread_id > 0)
		gwthread_wakeup(dbbox_client->thread_id);


	client_httpadmin_stop();
	smsc2_cleanup();
	gwlist_destroy(flow_threads, NULL);
	gwlist_destroy(suspended, NULL);
	mutex_destroy(status_mutex);
	empty_msg_lists();
	//[sanchal][29072009]
	alog ("alog_ends");
	alog_close();		
	cfg_destroy(cfg);
	gwlib_shutdown();
	octstr_destroy(filename);
	octstr_destroy(logfile);
	//delete_msgq(DELETE_REVERSE_QUEUE);
	octstr_destroy(access_file_name);

	if (restart == 1)
		execvp(argv[0],argv);
	return 0;
}

/**************************************************************************************/
/* Purpose : function used to shutdown the smpp-client.                               */
/* Output  : return 0 if successfully shut down the smpp-client.                      */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

int smpp_client_shutdown(void)
{
	static int called = 0;

	wake_sys_thread();	
	mutex_lock(status_mutex);

	if (called) {
		mutex_unlock(status_mutex);
		return -1;
	}

	info( 0, "Shutting down " GW_NAME "...");
	called = 1;
	set_shutdown_status();
	mutex_unlock(status_mutex);
	//printf("\n\nSHUTTING DOWN SMSC\n\n");	
	info( 0, "shutting down smsc");
	smsc2_shutdown();
	//printf("\n\nSMSC SHUT DOWN\n\n");	
	gwthread_wakeup(log_file_thread_id);
	//printf("\n\nWAKING LOG THREAD\n\n");	
	wake_router_thread();
	//printf("\n\nWAKING ROUTER THREAD\n\n");	
	return 0;
}


/**************************************************************************************/
/* Purpose : function used to suspend the smpp-client.                                */
/* Output  : return 0 if successfully started at suspend state.                       */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

static int smpp_client_suspend(void)
{
	mutex_lock(status_mutex);
	if (smpp_client_status != CLIENT_RUNNING) {
		mutex_unlock(status_mutex);
		return -1;
	}
	smpp_client_status = CLIENT_SUSPENDED;
	gwlist_add_producer(suspended);
	mutex_unlock(status_mutex);
	return 0;
}


/**************************************************************************************/
/* Purpose : function used to resume the working of smpp-client.                      */
/* Output  : return 0 if successfully resume the operation of smpp-client.            */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

static int smpp_client_resume(void)
{
	mutex_lock(status_mutex);
	if (smpp_client_status != CLIENT_SUSPENDED) {
		mutex_unlock(status_mutex);
		return -1;
	}
	if (smpp_client_status == CLIENT_SUSPENDED)
		gwlist_remove_producer(suspended);

	smsc2_resume();
	gwthread_wakeup(queue_thread);
	smpp_client_status = CLIENT_RUNNING;
	mutex_unlock(status_mutex);
	return 0;
}

/**************************************************************************************/
/* Purpose : function used to stop the smsc.                                          */
/* Input   : identifier of smsc which one you want to stop.                           */
/* Output  : return 0 if successfully stopped that smsc.                              */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

static int smpp_client_stop_smsc(Octstr *id)
{
	return smsc2_stop_smsc(id);
}

/**************************************************************************************/
/* Purpose : function used to restart the smsc connection.                            */
/* Input   : identifier of smsc which you want to restart.                            */
/* Output  : return 0 if successfully restarted that smsc.                            */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

static int smpp_client_restart_smsc(Octstr *id)
{
	return smsc2_restart_smsc(id);
}

/**************************************************************************************/
/* Purpose : function used to restart the smpp-client.                                */
/* Output  : return 0 if successfully restart the smpp-client.                        */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

static int smpp_client_restart(void)
{
	restart = 1;
	return smpp_client_shutdown();
}


#define append_status(r, s, f, x) { s = f(x); octstr_append(r, s); \
	octstr_destroy(s); }

/**************************************************************************************/
/* Purpose : function used to print the status of smpp-client.                        */
/* Output  : return 0 if successfully restart the smpp-client.                        */
/*         : return -1 if there is an error.                                          */
/**************************************************************************************/

Octstr *smpp_client_print_status(int status_type)
{
	char *s, *lb;
	char *frmt, *footer;
	char buf[1024];
	Octstr *ret, *str, *version;
	time_t t;

	if ((lb = smpp_client_status_linebreak(status_type))==NULL)
		return octstr_create("Un-supported format");

	t = time(NULL) - start_time;

	if (smpp_client_status == CLIENT_RUNNING)
		s = "running";
	else if (smpp_client_status == CLIENT_SUSPENDED)
		s = "suspended";
	else if (smpp_client_status == CLIENT_FULL)
		s = "filled";
	else
		s = "going down";

	//version = version_report_string("smpp-client");

	if (status_type == SMPPCLIENTSTATUS_HTML) {


		frmt = "%s</p>\n\n"
			" <p>Status: %s, uptime %ldd %ldh %ldm %lds</p>\n\n"
			"(%ld queued)</p>\n\n"
			" <p>SMS: received %ld (%ld queued), sent %ld "
			"(%ld queued), store size %ld</p>\n"
			" <p>SMS: inbound %.2f msg/sec, outbound %.2f msg/sec</p>\n\n"
			" <p>DLR: Master %ld queued, Status %ld,using %s storage</p>\n\n";
		footer = "<p>";
	} else if (status_type == SMPPCLIENTSTATUS_WML) {

		frmt = "%s</p>\n\n"
			"   <p>Status: %s, uptime %ldd %ldh %ldm %lds</p>\n\n"
			"   <p>SMS: received %ld (%ld queued)<br/>\n"
			"      SMS: sent %ld (%ld queued)<br/>\n"
			"      SMS: store size %ld<br/>\n"
			"      SMS: inbound %.2f msg/sec<br/>\n"
			"      SMS: outbound %.2f msg/sec</p>\n\n"
			"   <p>DLR: Master %ld queued  Status %ld<br/>\n"
			"      DLR: using %s storage</p>\n\n";
		footer = "<p>";
	} else if (status_type == SMPPCLIENTSTATUS_XML) {

		frmt = "<version>%s</version>\n"
			"<status>%s, uptime %ldd %ldh %ldm %lds</status>\n"
			"</total><queued>%ld</queued></sent>\n\t</wdp>\n"
			"\t<sms>\n\t\t<received><total>%ld</total><queued>%ld</queued></received>\n\t\t<sent><total>%ld"
			"</total><queued>%ld</queued></sent>\n\t\t<storesize>%ld</storesize>\n\t\t"
			"<inbound>%.2f</inbound>\n\t\t<outbound>%.2f</outbound>\n\t</sms>\n"
			"\t<dlr>\n\t\t<queued>%ld</queued>\n\t\t<storage>%s</storage>\n\t</dlr>\n";
		footer = "";
	} else {

		frmt = "%s\n\nStatus: %s, uptime %ldd %ldh %ldm %lds\n\n"
			"SMS: received %ld (%ld queued), sent %ld (%ld queued), store size %ld\n"
			"SMS: inbound %.2f msg/sec, outbound %.2f msg/sec\n\n"
			"DLR:  Master %ld queued, Status %ld queued,using %s storage\n\n";
		footer = "";
	}

	sprintf(buf, frmt,
			octstr_get_cstr(version),
			s, t/3600/24, t/3600%24, t/60%60, t%60,
			counter_value(incoming_msg_counter),
			counter_value(outgoing_msg_counter),
			gwlist_len(outgoing_msg),
			(float)counter_value(incoming_msg_counter)/t,
			(float)counter_value(outgoing_msg_counter)/t);

	octstr_destroy(version);
	octstr_append_cstr(ret, footer);

	return ret;
}

/**************************************************************************************/
/* Purpose : function used to break the line.                                         */
/* Input   : status type of smpp client.                                              */
/* Output  : return line break string for particular type of status.                  */
/**************************************************************************************/

char *smpp_client_status_linebreak(int status_type)
{
	switch(status_type) {
		case SMPPCLIENTSTATUS_HTML:
			return "<br>\n";
		case SMPPCLIENTSTATUS_WML:
			return "<br/>\n";
		case SMPPCLIENTSTATUS_TEXT:
			return "\n";
		case SMPPCLIENTSTATUS_XML:
			return "\n";
		default:
			return NULL;
	}
}

/**************************************************************************************/     
/*		       	End of file smpp_client.c                                     */
/**************************************************************************************/
void system_shutdown(void)
{
	pid_t process_thread_id;
	info( 0, "SYSTEM SHOULD BE SHUTDOWN BY HTTP ADMIN COMMAND");
	process_thread_id = getpid();
	kill(process_thread_id,SIGINT);
}
Octstr *print_client_status(int status_type)
{
	Octstr *tmp;
	Octstr *log_name;
	time_t running_time;
	running_time = time(NULL) - start_time;

	Octstr *dbbox_connected,*dbbox_ip,*dbbox_name;
	time_t dbbox_online=0;
	long dbbox_port=0,dbbox_msg_sent=0,dbbox_queue = 0;
	PROCESS_CLIENT *process_client;

	Octstr *forward_client_ip,*forward_name;
	long forward_port = 0,forward_client=0,forward_msg_recv=0,forward_online=0,forward_queue = 0;

	Octstr *dbdata_client_ip,*dbdata_name;
	long dbdata_port = 0,dbdata_client=0,dbdata_msg_recv=0,dbdata_online=0,dbdata_queue = 0;

	Octstr *reverse_client_ip,*reverse_name;
	long reverse_port = 0,reverse_client=0,reverse_msg_recv=0,reverse_msg_sent=0,reverse_online=0,reverse_queue = 0;

	if(dbbox_client != NULL && dbbox_client->status == PROCESS_CONNECTED)
		dbbox_connected = octstr_create("Connected");
	else
		dbbox_connected = octstr_create("Disconnected");
	if(dbbox_client != NULL && dbbox_client->status == PROCESS_CONNECTED)
	{
		dbbox_ip = octstr_duplicate(dbbox_client->client_ip);
		dbbox_port = dbbox_client->client_port;
		dbbox_online = time(NULL) - dbbox_client->online_time;
		dbbox_msg_sent = dbbox_client->req_sent;
		dbbox_queue = gwlist_len(dbbox_client->send_list);
		dbbox_name = dbbox_client->conn_id ? octstr_duplicate(dbbox_client->conn_id):octstr_create("");
	}
	else 
	{
		dbbox_ip = octstr_create("");
		dbbox_name = octstr_create("");
		dbbox_queue = gwlist_len(dbbox_client->send_list);
		dbbox_name = dbbox_client->conn_id ? octstr_duplicate(dbbox_client->conn_id):octstr_create("");

	}
	if(forward_server != NULL )
	{
		forward_port = forward_server->listening_port;

		gw_rwlock_rdlock(forward_server->list_lock);
		forward_client = gwlist_len(forward_server->client_list);
		if(forward_client > 0)
		{
			process_client = gwlist_get(forward_server->client_list,0);
			forward_client_ip = octstr_duplicate(process_client->client_ip);
			forward_msg_recv = process_client->req_recv;
			forward_online = time(NULL)-process_client->online_time;
			forward_queue = gwlist_len(process_client->send_list);
			forward_name = process_client->conn_id ? octstr_duplicate(process_client->conn_id):octstr_create("");
		}
		gw_rwlock_unlock(forward_server->list_lock);
	}
	if(forward_server== NULL || forward_client	== 0 )
	{
		forward_client_ip=octstr_create("");
		forward_name = octstr_create("");
		forward_client_ip = octstr_create("");
	}
	if(dbdata_server != NULL )
	{
		dbdata_port = dbdata_server->listening_port;

		gw_rwlock_rdlock(dbdata_server->list_lock);
		dbdata_client = gwlist_len(dbdata_server->client_list);
		if(dbdata_client > 0)
		{
			process_client = gwlist_get(dbdata_server->client_list,0);
			dbdata_client_ip = octstr_duplicate(process_client->client_ip);
			dbdata_msg_recv = process_client->req_recv;
			dbdata_online = time(NULL)-process_client->online_time;
			dbdata_queue = gwlist_len(process_client->send_list);
			dbdata_name = process_client->conn_id ? octstr_duplicate(process_client->conn_id):octstr_create("");
		}
		gw_rwlock_unlock(dbdata_server->list_lock);
	}
	if(dbdata_server== NULL || dbdata_client      == 0 )
	{
		dbdata_client_ip=octstr_create("");
		dbdata_name = octstr_create("");
		dbdata_client_ip = octstr_create("");
	}

	if(reverse_server != NULL)
	{
		reverse_port = reverse_server->listening_port;

		gw_rwlock_rdlock(reverse_server->list_lock);
		reverse_client = gwlist_len(reverse_server->client_list);
		if(reverse_client > 0)
		{
			process_client = gwlist_get(reverse_server->client_list,0);
			reverse_client_ip = octstr_duplicate(process_client->client_ip);
			reverse_msg_recv = process_client->req_recv;
			reverse_online = time(NULL)-process_client->online_time;
			reverse_msg_sent = process_client->req_sent;
			reverse_queue = gwlist_len(process_client->send_list);
			reverse_name = process_client->conn_id ? octstr_duplicate(process_client->conn_id):octstr_create("");
		}
		gw_rwlock_unlock(reverse_server->list_lock);
	}
	if(reverse_server == NULL || reverse_client == 0 )
	{
		reverse_client_ip=octstr_create("");
		reverse_name = octstr_create("");
		reverse_client_ip = octstr_create("");
	}
	log_name = log_file_name(log_file_index);
	tmp = octstr_format("Client Process ID: %d.										"
			"%sClient Process Status: %s								"	
			"%sNo of connected SMSC: %d									"	
			"%sOnline Time: Days:%ld,Hour:%ld,Minute:%ld,Seconds:%ld	"
			"%sTotal SMS Received:%ld									"
			"%sTotal SMS Sent:%ld										"
			"%sTotal SMS Queued:%ld										"
			"%sConfiguration File path : %s								"
			"%sLog File path : %s										"
			"%sDbbox server[IP:%s][Port:%d]:%s,ID:%s,Msgsent:%ld,MsgQ:%ld,Online Days:%ld,Hour:%ld,Min:%ld,Sec:%ld"
			"%sFoward Server:%ld,Clinet num:%d,IP:%s,ID:%s,MsgRcvd:%ld,MsgQ:%ld,Online Days:%ld,Hour:%ld,Min:%ld,Sec:%ld"
			"%sReverse Server:%ld,Clinet num:%d,IP:%s,ID:%s,MsgSent:%ld,MsgRcvd:%ld,MsgQ:%ld ,Online Days:%ld,Hour:%ld,Min:%ld,Sec:%ld"
			,
			(int)getpid(),
			bb_status_linebreak(status_type),client_get_status_name(smpp_client_status),
			bb_status_linebreak(status_type),smsc_length(),		
			bb_status_linebreak(status_type),running_time/3600/24, running_time/3600%24, running_time/60%60, running_time%60,
			bb_status_linebreak(status_type),counter_value(incoming_msg_counter),
			bb_status_linebreak(status_type),counter_value(outgoing_msg_counter),
			bb_status_linebreak(status_type),gwlist_len(outgoing_msg),
			bb_status_linebreak(status_type),octstr_get_cstr(filename),
			bb_status_linebreak(status_type),octstr_get_cstr(log_name),
			bb_status_linebreak(status_type),octstr_get_cstr(dbbox_ip),dbbox_port,octstr_get_cstr(dbbox_connected),octstr_get_cstr(dbbox_name),dbbox_msg_sent,dbbox_queue,dbbox_online/3600/24,dbbox_online/3600%24,dbbox_online/60%60,dbbox_online%60,
			bb_status_linebreak(status_type),forward_port,forward_client,octstr_get_cstr(forward_client_ip),octstr_get_cstr(forward_name),forward_msg_recv,forward_queue,forward_online/3600/24,forward_online/3600%24,forward_online/60%60,forward_online%60,
			bb_status_linebreak(status_type),reverse_port,reverse_client,octstr_get_cstr(reverse_client_ip),octstr_get_cstr(reverse_name),reverse_msg_sent,reverse_msg_recv,reverse_queue,reverse_online/3600/24,reverse_online/3600%24,reverse_online/60%60,reverse_online%60	
				);

	octstr_destroy(log_name);
	octstr_destroy(dbbox_connected);
	octstr_destroy(dbbox_ip);
	octstr_destroy(forward_client_ip);
	octstr_destroy(reverse_client_ip);
	octstr_destroy(dbbox_name);
	octstr_destroy(forward_name);
	octstr_destroy(reverse_name);
	return tmp;

}
