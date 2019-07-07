
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <signal.h>


#include "gwlib/gwlib.h"
#include "gwlib/smpp_pdu.h"
#include "dbbox.h"
#include "gwlib/msgq.h"
#include "dlr.h"
#include "dbbox_httpadmin.h"
#include "gwlib/process_sock.h"
#include "gwlib/charset.h"
Semaphore *sem_update_allowed;
Counter *dnd_msg_counter;
static long master_thread_id;
static Octstr *kannel_instance = NULL;
Cfg *cfg;
volatile sig_atomic_t program_status;
pthread_mutex_t count_lock;
void shutdown_self(void);
PROCESS_CLIENT *esme_process_client = NULL;
PROCESS_CLIENT *dbdata_client;
void thrput_get( void );
void msg_process(void);
static time_t process_start_time;


static Octstr *filename;
long log_file_index;
volatile enum process_program_status process_program_status = starting_up;
long port_num = -1;
SERVER_DATA *server_data;

int giManagelogInterval;
//[sanchal][250309][to store the interval of log checking sizewise as well as day wise]
long giLogSize;
//[sanchal][250109][to store size of log read from conffile]

void testdata(){
	while(1){
		info(0, "thread is running =================================");
		sleep(10);
	}
}

void manage_log(Cfg *cfg)
{
	CfgGroup *grp;
	Octstr *log_file = NULL;
	Octstr *new_log_file = NULL;
	long lvl;
	char szTempVar[28];    //[sanchal][250309]
	Octstr *retVallog;	//[sanchal][250309]
	/*read  configuration file related parametes and open log file*/
	grp = cfg_get_single_group(cfg, octstr_imm("core"));

	log_file = cfg_get(grp, octstr_imm("log-file"));

	cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

	if(log_file != NULL)
	{
		retVallog = get_latst_filename(log_file);
		if( retVallog == NULL )
		{
			Octstr *logfile = NULL;
			logfile = logfile_append_time(log_file);
			log_file_index = log_open(octstr_get_cstr(logfile), lvl, GW_NON_EXCL);
			//printf(" \n\n --->> logfile : %s \n",octstr_get_cstr(logfile));
			info(0, "LogFile: NewLogFileName : %s",octstr_get_cstr(logfile));
			octstr_destroy(logfile);
		}
		else
		{
			log_file_index = log_open(octstr_get_cstr(retVallog), lvl, GW_NON_EXCL);
			//printf(" \n\n --->> retVallog : %s \n",octstr_get_cstr(retVallog));
			info(0, "LogFile: Latest LogFileName: %s",octstr_get_cstr(retVallog));
		}

		octstr_destroy(retVallog);
		octstr_destroy(log_file);
	}
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
	Octstr *manage_log_interval = NULL;
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
}

void main_thread(Cfg *cfg)
{
	CfgGroup *grp;
	Octstr *host;
	int i;
	PROCESS_CLIENT *test_client;
	long ping_delay;
	long ping_ack_timeout;
	long client_timeout;
	Octstr *log_name;	
	Octstr *date_string;
	Octstr *compare_string;
	/*read  configuration file related parametes and open log file*/
	grp = cfg_get_single_group(cfg, octstr_imm("core"));


	if(grp == NULL)
		panic(0, "Unable to read Kannel-instance from Configuration");

	host = cfg_get(grp,octstr_imm("dbbox-host"));
	if(host != NULL)
		warning(0,"dbbox-host parameter need not be specfied,Using localhost");
	if(cfg_get_integer((long*)&port_num, grp, octstr_imm("dbbox-port")) == -1)
		panic(0,"dbbox-port not specfied, used as listening port");

	if(cfg_get_integer(&ping_delay,grp,octstr_imm("dbbox-ping-delay")) == -1)
		ping_delay = 10;
	if(cfg_get_integer(&ping_ack_timeout,grp,octstr_imm("dbbox-pingack-timeout")) == -1)
		ping_ack_timeout = 5;

	if(cfg_get_integer(&client_timeout,grp,octstr_imm("dbbox-client-timeout")) == -1)
		client_timeout = 2*(ping_delay + ping_ack_timeout);
	dbbox_httpadmin_start(cfg);

	server_data = init_start_server(&port_num,1,client_timeout);

	////

	long port_num1;
	long reconnect_delay;
	long smsc_q_limit = -1;

	host = cfg_get(grp,octstr_imm("smsc-host"));
	if(host == NULL)
		panic(0,"smsc-host is not specified");
	if(cfg_get_integer(&port_num, grp, octstr_imm("dbdata-port")) == -1)
		panic(0,"smsc-forward-port is not specified");
	if(cfg_get_integer(&reconnect_delay, grp, octstr_imm("smsc-reconnect-delay")) == -1)
	{
		warning(0,"smsc-reconnect-delay is not specfied using 10 sec as default");
		reconnect_delay = 10;
	}
	if(cfg_get_integer(&smsc_q_limit, grp, octstr_imm("smsc-unconnect-q-limit")) == -1)
	{
		smsc_q_limit = -1;
	}
	if(cfg_get_integer(&ping_delay,grp,octstr_imm("smsc-ping-delay")) == -1)
		ping_delay = 10;
	if(cfg_get_integer(&ping_ack_timeout,grp,octstr_imm("smsc-pingack-timeout")) == -1)
		ping_ack_timeout = 5;
	dbdata_client = init_start_client(host,port_num,reconnect_delay,ping_delay,ping_ack_timeout);
	printf("\n\n-------CONNECTING TO FORWARD\n\nn");
	if(dbdata_client != NULL)
	{
		//	if (gwthread_create(msg_process, NULL) == -1)
		//              warning(0, "Failed to start a new thread for test data");
		info(0,"=====================================================connected");
	}
	else
		panic(0,"Could not connect to forward client");

	////
	int ret = 0;

	int start_time = 0;
	int max_time = 6*60;//6 hrs
	date_string = get_log_format_datetime();
	while(program_status == PROGRAM_RUNNING)
	{

		/*
		   gw_rwlock_rdlock(server_data->list_lock);
		   for( i = 0 ; i < gwlist_len(server_data->client_list) ; i++)
		   {
		   test_client = gwlist_get(server_data->client_list,i);
		   info(0,"[CID:%s]CLIENT NUM [%d]IP[%d]PORT[%d]",
		   test_client->conn_id?octstr_get_cstr(test_client->conn_id):"",
		   test_client->client_num,
		   octstr_get_cstr(test_client->client_ip),
		   test_client->client_port);
		   }
		   gw_rwlock_unlock(server_data->list_lock);	
		   */
		start_time++;
		if(start_time == max_time)
		{
			start_time = 0;
			dlr_check_conn();
		}
		compare_string = get_log_format_datetime();
		if(octstr_compare(date_string,compare_string) != 0)
		{
			octstr_destroy(date_string);
			date_string = octstr_duplicate(compare_string);
			if ( log_file_index != -1 )
			{
				ret  = change_logfile_name(log_file_index);
				if(ret == 0)
				{
					log_name = log_file_name(log_file_index);
					//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
					octstr_destroy(log_name);
				}
			}

		}
		else
		{
			if ( log_file_index != -1 )
			{
				ret  = change_logfile_size(log_file_index, giLogSize);
				if(ret == 0)
				{
					log_name = log_file_name(log_file_index);
					//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
					octstr_destroy(log_name);
				}
			}
		}

		octstr_destroy(compare_string);
		//gwthread_sleep(60);	
		gwthread_sleep(giManagelogInterval);
	}
	octstr_destroy(date_string);
	info( 0, "%s terminates.", __func__);
}

void update_file(Cfg *cfg)
{
	CfgGroup *grp;
	Octstr *esme_file;
	int i;
	PROCESS_CLIENT *test_client;
	long ping_delay;
	long ping_ack_timeout;
	long client_timeout;
	Octstr *log_name;	
	Octstr *date_string;
	Octstr *compare_string;
	/*read  configuration file related parametes and open log file*/
	grp = cfg_get_single_group(cfg, octstr_imm("core"));


	if(grp == NULL)
		panic(0, "Unable to read Kannel-instance from Configuration");

	esme_file = cfg_get(grp,octstr_imm("esme-file-path"));


	esme_file_update(esme_file);


	/*
	   gw_rwlock_rdlock(server_data->list_lock);
	   for( i = 0 ; i < gwlist_len(server_data->client_list) ; i++)
	   {
	   test_client = gwlist_get(server_data->client_list,i);
	   info(0,"[CID:%s]CLIENT NUM [%d]IP[%d]PORT[%d]",
	   test_client->conn_id?octstr_get_cstr(test_client->conn_id):"",
	   test_client->client_num,
	   octstr_get_cstr(test_client->client_ip),
	   test_client->client_port);
	   }
	   gw_rwlock_unlock(server_data->list_lock);	
	   */

	octstr_destroy(date_string);
	info( 0, "%s terminates.", __func__);
}

static void signal_handler(int signum)
{
	/* On some implementations (i.e. linuxthreads), signals are delivered
	 * to all threads.  We only want to handle each signal once for the
	 * entire box, and we let the gwthread wrapper take care of choosing
	 * one.
	 */
	if (!gwthread_shouldhandlesignal(signum))
		return;
	printf("\nCTRL - C : TERMINATING\n");
	/*send messages to terminate both the thread*/
	info(0,"--------------SIGNAL RECEIVED--------------- %d",signum);			
	switch (signum) {
		case SIGINT:
			{
				if (program_status != PROGRAM_SHUTDOWN) 
				{
					int listening_thread = server_data->listening_thread_id;
					error(0, "SIGINT received, aborting dbbox program...");
					program_status = PROGRAM_SHUTDOWN;
					process_program_status = shutting_down;
					//send_msg_mis_q(INSERT_MIS_TABLE,TERMINATE_MIS_THREAD,0,&mis_insert);
					//send_msg_mis_q(UPDATE_MIS_TABLE,TERMINATE_MIS_THREAD,0,&mis_update);
					//sleep(1);

					gwthread_wakeup_all();
					//shutdown_process_server(server_data);
					gwthread_join(listening_thread);
					//sleep(1);
				}

				break;
			}
		case SIGTERM:

			break;

		case SIGHUP:
			break;

			/* 
			 * It would be more proper to use SIGUSR1 for this, but on some
			 * platforms that's reserved by the pthread support. 
			 */
		case SIGQUIT:
			break;
		case SIGSEGV:
			{

				/*Printfs are required cos we are not sure whether debug will print information or not.*/
				int j, nptrs;
				void *buffer[50];
				char **strings;
				char funcname[100];
				nptrs = backtrace(buffer, 50);
				debug("bb", 0, "backtrace() returned %d addresses",nptrs);
				printf("backtrace() returned %d addresses\n",nptrs);
				int *p = NULL;
				*p = 10;strings = backtrace_symbols(buffer, nptrs);
				if(strings == NULL)
				{
					perror("backtrace_symbols");
					exit(EXIT_FAILURE);
				}
				for(j = 0; j < nptrs; j++)
				{
					debug("bb", 0, "Function Value is :%s",strings[j]);
					printf("Function Value is :%s\n",strings[j]);
				}
				gw_free(strings);
				gwthread_funcname(gwthread_self(),funcname);
				printf("\nsignal_handler : %s\n",strsignal(signum));
				printf("SIGSEGV Generated by Process:%ld Thread:%ld Name:%s\n",gwthread_self_pid(),gwthread_self(),funcname);
				panic(0, "SIGSEGV Generated by Process:%ld Thread:%ld Name:%s\n",gwthread_self_pid(),gwthread_self(),funcname);

				/*
				   pthread_detach(pthread_self());
				   pthread_exit(pthread_self());
				   */
				exit(0);
				break;
			}
	}
}


void handle_request(Conn_Msg *msg,PROCESS_CLIENT *process_client)
{
	static int total_inserts = 0;
	static int total_selects = 0;
	if(program_status != PROGRAM_RUNNING)
	{
		warning(0,"Received request when system is not running :%d",conn_msg_type(msg));
		conn_msg_destroy(msg);
	}
	debug("dbbox.c",0,"[CID:%s]Msg received from CLIENTNUM[%d]IP[%s]PORT[:%d]ID[:%d]",
			process_client->conn_id ? octstr_get_cstr(process_client->conn_id):"",
			process_client->client_num,
			octstr_get_cstr(process_client->client_ip),
			process_client->client_port,
			conn_msg_type(msg));
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
			Octstr *tmp = octstr_create("smpp-esme");
			if((octstr_compare(process_client->conn_id,tmp) == 0)&&(esme_process_client == NULL))
			{
				esme_process_client = process_client;
				info(0,"ESME Client registered,storing it");
			}
			octstr_destroy(tmp);
		}
		else if(msg->admin.command == 808){
			if (gwthread_create(msg_process, NULL) == -1)
				warning(0, "Failed to start a new thread for test data");

		}
	}
	else if(conn_msg_type(msg) == values_get)
	{
		//semaphore_down(sem_update_allowed);
		debug("dbbox", 0,"READ VALUES GET : %s",octstr_get_cstr(msg->values_get.smsc_msg_id));
		//pthread_mutex_lock(&count_lock); 
		total_selects = total_selects + 1;
		debug("dbbox",0,"decrease semaphore :%d:%d",total_inserts,total_selects);
		//usleep(1000);
		if(total_inserts < total_selects)
		{
			info(0,"INSERTS LESS THAN SELECTS");
			//usleep(1);
		}
		if(total_inserts == total_selects)
		{
			info(0,"INSERTS EQUALS SELECTS");
			//usleep(1);
		}	
		Conn_Msg *result_msg;
		if(msg->values_get.smsc_msg_id == NULL || msg->values_get.dest_addr == NULL)
			return;

		result_msg = dlr_find(msg->values_get.dest_addr,msg->values_get.smsc_msg_id );
		if(result_msg == NULL || conn_msg_type(result_msg) != values_got)
		{
			info( 0,"No entries Found for Msgid %s and dest_addr %s",
					octstr_get_cstr(msg->values_get.smsc_msg_id),
					octstr_get_cstr(msg->values_get.dest_addr));
			conn_msg_destroy(result_msg);
			result_msg = conn_msg_create(values_got);
			result_msg->values_got.result = 0;
			gwlist_append(process_client->send_list,result_msg);
			gwthread_wakeup(process_client->thread_id);
		}
		else
		{

			if(result_msg->values_got.result == 1)
				debug("dbbox", 0,"FOUND ENTRY:ACCNAME:%s:ACCMSGID:%s",\
						octstr_get_cstr(result_msg->values_got.account_name),
						octstr_get_cstr(result_msg->values_got.account_msg_id));
			gwlist_append(process_client->send_list,result_msg);
			gwthread_wakeup(process_client->thread_id);

		}
		//pthread_mutex_unlock(&count_lock); 
	}//values get
	else if(conn_msg_type(msg) == insert_mis)
	{

		debug("dbbox", 0,"READ INSERT MIS : %s",octstr_get_cstr(msg->insert_mis.smsc_msg_id));
		debug("dbbox", 0,"1. Account Name : %s",octstr_get_cstr(msg->insert_mis.account_name));
		debug("dbbox", 0,"2. ESM Class : %d",msg->insert_mis.esm_class);
		debug("dbbox", 0,"3. Message Length : %d",msg->insert_mis.message_len);
		//debug("dbbox", 0,"4. Message : %s",((struct mis_insert*)ptr)->msg_content);

		//pthread_mutex_lock(&count_lock); 
		/*check whether UDHI bit is set*/
		if(msg->insert_mis.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
		{
			int ret;
			ret = binary_msg_to_ascii(msg->insert_mis.msg_content,&(msg->insert_mis.udh),&(msg->insert_mis.msg_content));
			if(ret == 0)
			{

				printf("======>>>>UDH :%s : %d ",octstr_get_cstr(msg->insert_mis.udh),octstr_len(msg->insert_mis.udh));
				debug("dbbox", 0,"UDH :%s : %d ",octstr_get_cstr(msg->insert_mis.udh),octstr_len(msg->insert_mis.udh));
				debug("dbbox", 0,"Binary MESSAGE :%s",octstr_get_cstr(msg->insert_mis.msg_content));

			}
		}//esm class
		else
		{
			//If UDHI bit is reset-no UDH has to be prepared and message content requires no processing.
			msg->insert_mis.udh = octstr_create("");
		}
		//pthread_mutex_unlock(&count_lock); 
		charset_gsm_to_latin1(msg->insert_mis.msg_content);
		dlr_mis_add(msg);
		//semaphore_up(sem_update_allowed);

		total_inserts = total_inserts + 1;
		debug("dbbox",0,"increase semaphore :%d:%d",total_inserts,total_selects);
		//usleep(10);
	}//insert mis
	else if(conn_msg_type(msg) == insert_status)
	{
		debug("dbbox", 0,"-------------MIS STATUS INSERT-------------\n");
		debug("dbbox", 0,"1. msg_id :%s status :%d dest:%s",
				octstr_get_cstr(msg->insert_status.smsc_msg_id),
				msg->insert_status.deliver_status,
				octstr_get_cstr(msg->insert_status.dest_addr));
		dlr_status_add(msg);
		if(msg->insert_status.pdu == NULL)
			error(0,"found null pdu");
		else
			info(0,"found something in pdu");

		if(msg->insert_status.smsc_msg_id == NULL || msg->insert_status.dest_addr == NULL )
			return;

		Conn_Msg *result_msg;
		result_msg = dlr_find(msg->insert_status.dest_addr,msg->insert_status.smsc_msg_id );
		if(result_msg == NULL || conn_msg_type(result_msg) != values_got)
		{
			info( 0,"No entries Found for Msgid %s and dest_addr %s",
					octstr_get_cstr(msg->insert_status.smsc_msg_id),
					octstr_get_cstr(msg->insert_status.dest_addr));
			conn_msg_destroy(result_msg);
			result_msg = conn_msg_create(values_got);
			result_msg->values_got.result = 0;
		}
		else
		{

			if(result_msg->values_got.result == 1)
				debug("dbbox", 0,"FOUND ENTRY:ACCNAME:%s:ACCMSGID:%s",\
						octstr_get_cstr(result_msg->values_got.account_name),
						octstr_get_cstr(result_msg->values_got.account_msg_id));
		}
		if(esme_process_client != NULL)
		{
			if(esme_process_client->conn_id != NULL)
			{
				info(0,"sending result to 1");
				info(0,"sending result to [%s][result%d]",octstr_get_cstr(esme_process_client->conn_id),
						result_msg->values_got.result);
				info(0,"sending result to 2");
				result_msg->values_got.pdu = msg->insert_status.pdu;
			}

			msg->insert_status.pdu = NULL;
			gwlist_append(esme_process_client->send_list,result_msg);
			gwthread_wakeup(esme_process_client->thread_id);
		}

	}//insert status
	else if(conn_msg_type(msg) == smpp_esme)
	{
		int ret;
		debug("dbbox", 0,"-------------SMPP ESME INSERT-------------\n");
		debug("dbbox", 0,"1. msg_id :%s  dest:%s :%d :%d",
				octstr_get_cstr(msg->smpp_esme.account_msg_id),
				octstr_get_cstr(msg->smpp_esme.dest_addr),
				msg->smpp_esme.submit_status,
				msg->smpp_esme.esm_class);
		if(msg->smpp_esme.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
		{
			if(msg->smpp_esme.data_coding != 0)
				ret = binary_msg_to_ascii_hindi(msg->smpp_esme.msg_content,&(msg->smpp_esme.udh),&(msg->smpp_esme.msg_content));
			else
				ret = binary_msg_to_ascii(msg->smpp_esme.msg_content,&(msg->smpp_esme.udh),&(msg->smpp_esme.msg_content));
			if(ret == 0)
			{
				debug("dbbox", 0,"UDH :[%s] : %d ",octstr_get_cstr(msg->smpp_esme.udh),octstr_len(msg->smpp_esme.udh));
				debug("dbbox", 0,"SMPP-ESME LONG TEXT MESSAGE :%s",octstr_get_cstr(msg->smpp_esme.msg_content));
			}
		}//esm class
		else
		{
			/*If UDHI bit is reset-no UDH has to be prepared and message content requires no processing.*/
			msg->smpp_esme.udh = octstr_create("");
		}
		dlr_smpp_esme(msg);
	}//smpp esme insert	
	conn_msg_destroy(msg);
}
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

static int check_args(int i, int argc, char **argv) 
{
	/*
	   if (strcmp(argv[i], "-S")==0 || strcmp(argv[i], "--suspended")==0)
	   bb_status = BB_SUSPENDED;
	   else if (strcmp(argv[i], "-I")==0 || strcmp(argv[i], "--isolated")==0)
	   bb_status = BB_ISOLATED;
	   else
	   return -1;
	   */
	return 0;
}

/*
   This thread is responsible for inserting the data into MIS table.A seperate thread is require as
   message insertion is done independantly by all the SMSC threads.And it is possible that updation takes
   longer time.
   */
/*
   void mis_insert_thread(void *arg)
   {
   int msg_type;
   void *ptr;
   Octstr *udh = NULL;
   Octstr *value = NULL;
   Octstr *try = NULL;
   Octstr *new_text = NULL;
   ptr = NULL;
   for(;;)
   {
   recv_msg_mis_q(INSERT_MIS_TABLE,&msg_type,0,&ptr);
   if(msg_type == TERMINATE_MIS_THREAD)
   {
   gw_free(ptr);	
   break;
   }
   debug("dbbox", 0, "------------MIS THREAD INSERT---------------:%lx",ptr);
   debug("dbbox", 0,"1. Account Name : %s",((struct mis_insert*)ptr)->account_name);
   debug("dbbox", 0,"2. ESM Class : %d",((struct mis_insert*)ptr)->esm_class);
   debug("dbbox", 0,"3. Message Length : %d",((struct mis_insert*)ptr)->message_len);
//debug("dbbox", 0,"4. Message : %s",((struct mis_insert*)ptr)->msg_content);


if(((struct mis_insert*)ptr)->esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
{
long v1,v2 = 0;
value = octstr_memcpy(((struct mis_insert*)ptr)->msg_content,((struct mis_insert*)ptr)->message_len);
debug("dbbox", 0,"4. Octstr_len : %d",octstr_len(value));

if(v1 >= 3)
v2 = decode_integer_value(value,2,1);

debug("dbbox", 0,"5. UDH Length: %ld",v1);
debug("dbbox", 0,"5. Text INT value: %ld",v2);

udh = get_octstr(value,0,v1+1);
try = bin_to_ascii(udh);	
debug("dbbox", 0,"UDH :%s : %d ",octstr_get_cstr(try),octstr_len(try));
strcpy(((struct mis_insert*)ptr)->udh,octstr_get_cstr(try));

octstr_destroy(try);
octstr_destroy(udh);

if(v2 == 3)
{
new_text = octstr_from_position(value,v1+1);
debug("dbbox", 0,"LONG TEXT MESSAGE :%s",octstr_get_cstr(new_text));
strcpy(((struct mis_insert*)ptr)->msg_content,octstr_get_cstr(new_text));				
}
else
{
new_text = octstr_from_position(value,v1+1);
try = bin_to_ascii(new_text);	
debug("dbbox", 0,"BINARY MESSAGE :%s",octstr_get_cstr(try));
strcpy(((struct mis_insert*)ptr)->msg_content,octstr_get_cstr(try));
octstr_destroy(try);				

}

octstr_destroy(new_text);				
octstr_destroy(value);
}//esm class
else
{
strcpy(((struct mis_insert*)ptr)->udh,"");
}

//debug("dbbox", 0, "------------MIS THREAD INSERT---FREE------------:%lx",ptr);
dlr_mis_add((struct mis_insert*)ptr);	

gw_free(ptr);	
}
info( 0, "%s terminates.", __func__);	
}
*/
/*
   This thread is responsible for updating delivery status and deliver time in the mis table.A seperate
   thread is made as different SMSC threads will try to update the data.
   */
/*
   void mis_update_thread(void *arg)
   {
   int msg_type;
   void *ptr;
   ptr = NULL;
   int ret;
   for(;;)
   {


   recv_msg_mis_q(UPDATE_MIS_TABLE,&msg_type,0,&ptr);
   if(msg_type == TERMINATE_MIS_THREAD)
   {
   gw_free(ptr);
   break;
   }
   if(msg_type == SHUTDOWN_DB_SYSTEM)
   {

*//*Debug
    If shutdown message is received from server:send SIGINT signal to the process which 
    executes signal handler which terminated all the thread and does all the required clean-up
    Debug
    */
/*
   pid_t process_thread_id;
   info( 0, "DATABASE SYSTEM SHOULD BE SHUTDOWN");
   process_thread_id = getpid();
   kill(process_thread_id,SIGINT);
   gw_free(ptr);
   break;
   }
   if(msg_type == CHANGE_DB_LOG_FILE)
   {
   debug("system_thread.c", 0, "DBBOX process received change log file command");
   ret  = change_logfile_name(log_file_index);
   if(ret == 0)
   {
   Octstr *log_name;
   log_name = log_file_name(log_file_index);
//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
octstr_destroy(log_name);
}
gw_free(ptr);
continue;
}
*/
/*	
	Octstr *smsc_id;
	Octstr *smsc_msg_id;
	*/
//debug("dbbox", 0,"-------------MIS STATUS INSERT-------------\n");
//debug("dbbox", 0,"1. msg_id :%s status :%d dest:%s",((struct mis_update*)ptr)->smsc_msg_id,((struct mis_update*)ptr)->deliver_status,((struct mis_update*)ptr)->dest_addr);
/*
   semaphore_down(sem_update_allowed);
   smsc_id = octstr_create(((struct mis_update*)ptr)->smsc_id);
   smsc_msg_id = octstr_create(((struct mis_update*)ptr)->smsc_msg_id);
   */
//dlr_mis_update((struct mis_update*)ptr);
//gw_free(ptr);


/*added for testing

  struct get_account_info *get_acc_info_ptr;
  struct db_extracted_values db_extracted;

  debug("dbbox", 0,"1. SMSC Id :%s",octstr_get_cstr(smsc_id));
  debug("dbbox", 0,"2. SMSC Msg Id :%s",octstr_get_cstr(smsc_msg_id));
  get_acc_info_ptr = dlr_find(smsc_id,smsc_msg_id);
  if(get_acc_info_ptr == NULL)
  {
  debug("dbbox", 0,"No entries Found");
  db_extracted.result = 0;	
  send_msg_database_q(MAPPING_MSGQ_TYPE,EXTRACTED_VALUES,0,&db_extracted);
  }
  else
  {

  debug("dbbox", 0,"1. Account Name :%s",octstr_get_cstr(get_acc_info_ptr->account_name));
  debug("dbbox", 0,"2. Account Msg Id :%s",octstr_get_cstr(get_acc_info_ptr->account_msg_id));

  db_extracted.result = 1;
  strcpy(db_extracted.account_name,octstr_get_cstr(get_acc_info_ptr->account_name));
  strcpy(db_extracted.account_msg_id,octstr_get_cstr(get_acc_info_ptr->account_msg_id));
  send_msg_database_q(MAPPING_MSGQ_TYPE,EXTRACTED_VALUES,0,&db_extracted);	
  octstr_destroy(get_acc_info_ptr->account_name);
  octstr_destroy(get_acc_info_ptr->account_msg_id);
  gw_free(get_acc_info_ptr);

  }

  octstr_destroy(smsc_id);
  octstr_destroy(smsc_msg_id);			

/*added for testing*/

//}
//info( 0, "%s terminates.", __func__);	
//}
/*
   This function is responsible for handling all querries related to mapping thread.A single mapping
   thread is enough
   1. table will remain small.
   2. get values is called by a sinlge thread in server process.
   */
/*
   void mapping_thread(void *arg)
   {
   int msg_type;
   void *ptr;
   ptr = NULL;
   int terminate_thread = 0;
   for(;;)
   {
//printf("\nDBBOX BEFORE MESSAGE RECV\n");
recv_msg_database_q(DATABASE_MSGQ_TYPE,&msg_type,0,&ptr);
debug("dbbox", 0,"------------MAPPING THREAD:%d--------------",msg_type);
switch(msg_type)
{
*/
/*
   case INSERT_VALUES:
   {
   struct db_insert *data_db_ptr = (struct db_insert *)ptr;
   Octstr *account_name;
   Octstr *account_msg_id;
   Octstr *smsc_id;
   Octstr *smsc_msg_id;

   debug("dbbox", 0,"1. Account Name :%s",((struct db_insert *)ptr)->account_name);
   debug("dbbox", 0,"2. Account Msg Id :%s",((struct db_insert *)ptr)->account_msg_id);
   debug("dbbox", 0,"3. SMSC Id :%s",((struct db_insert *)ptr)->smsc_id);
   debug("dbbox", 0,"4. SMSC Msg Id :%s",((struct db_insert *)ptr)->smsc_msg_id);


   account_name = octstr_create(((struct db_insert *)ptr)->account_name);
   account_msg_id = octstr_create(((struct db_insert *)ptr)->account_msg_id);
   smsc_id = octstr_create(((struct db_insert *)ptr)->smsc_id);
   smsc_msg_id = octstr_create(((struct db_insert *)ptr)->smsc_msg_id);

   dlr_add(account_name,account_msg_id,smsc_id,smsc_msg_id);
   octstr_destroy(account_name);
   octstr_destroy(account_msg_id);
   octstr_destroy(smsc_id);
   octstr_destroy(smsc_msg_id);

   gw_free(ptr);
   break;
   }//case INSERT VALUES

*/
/*
   case GET_VALUES:
   {

   struct get_account_info *get_acc_info_ptr;
   struct db_extracted_values db_extracted;

   Octstr *dest_addr;
   Octstr *smsc_msg_id;


   debug("dbbox", 0,"FIND ENTRY:SMSC Id :%s:SMSCMSGID:%s:DESTADDR:%s",\
   ((struct db_extract_values *)ptr)->smsc_id,
   ((struct db_extract_values *)ptr)->smsc_msg_id,
   ((struct db_extract_values *)ptr)->dest_addr);

   dest_addr = octstr_create(((struct db_extract_values *)ptr)->dest_addr);
   smsc_msg_id = octstr_create(((struct db_extract_values *)ptr)->smsc_msg_id);

   get_acc_info_ptr = dlr_find(dest_addr,smsc_msg_id);
   if(get_acc_info_ptr == NULL)
   {
   debug("dbbox", 0,"No entries Found");
   db_extracted.result = 0;	
   send_msg_database_q(MAPPING_MSGQ_TYPE,EXTRACTED_VALUES,0,&db_extracted);
   }
   else
   {

   debug("dbbox", 0,"FOUND ENTRY:ACCNAME:%s:ACCMSGID:%s",\
   octstr_get_cstr(get_acc_info_ptr->account_name),
   octstr_get_cstr(get_acc_info_ptr->account_msg_id));

   db_extracted.result = 1;
   strcpy(db_extracted.account_name,octstr_get_cstr(get_acc_info_ptr->account_name));
   strcpy(db_extracted.account_msg_id,octstr_get_cstr(get_acc_info_ptr->account_msg_id));
   send_msg_database_q(MAPPING_MSGQ_TYPE,EXTRACTED_VALUES,0,&db_extracted);	
   octstr_destroy(get_acc_info_ptr->account_name);
   octstr_destroy(get_acc_info_ptr->account_msg_id);
   gw_free(get_acc_info_ptr);

   }

   octstr_destroy(smsc_msg_id);
   octstr_destroy(dest_addr);	
   gw_free(ptr);
   break;
   }//GET VALUES
   */
/*
   case REMOVE_VALUES:
   {
   Octstr *smsc_id;
   Octstr *smsc_msg_id;


   debug("dbbox", 0,"1. SMSC Id :%s",((struct db_extract_values *)ptr)->smsc_id);
   debug("dbbox", 0,"2. SMSC Msg Id :%s",((struct db_extract_values *)ptr)->smsc_msg_id);

   smsc_id = octstr_create(((struct db_extract_values *)ptr)->smsc_id);
   smsc_msg_id = octstr_create(((struct db_extract_values *)ptr)->smsc_msg_id);

   dlr_remove(smsc_id,smsc_msg_id);
   octstr_destroy(smsc_id);
   octstr_destroy(smsc_msg_id);

   gw_free(ptr);
   break;
   }
   */
/*
   case TERMINATE_THREAD:
   {
   terminate_thread = 1;
   debug("dbbox", 0,"Mapping thread terminates");
   break;
   }
   default:
   {
   printf("\nINSIDE DEFAULT\n");
   break;
   }

   }//switch 
   if(terminate_thread)
   break;
   }//for	
   info( 0, "%s terminates.", __func__);	
   }
   */
int main(int argc, char **argv)
{
	int cf_index;
	int res;

	process_start_time = time(NULL);
	program_status = PROGRAM_RUNNING;
	gwlib_init();
	setup_signal_handlers();

	cf_index = get_and_set_debugs(argc, argv, check_args);

	if (argv[cf_index] == NULL)
		filename = octstr_create("kannel.conf");
	else
		filename = octstr_create(argv[cf_index]);

	cfg = cfg_create(filename);
	if (cfg_read(cfg) == -1)
		panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));
	manage_log(cfg);	

	report_versions("dbbox");


	debug("db", 0, "DBBOX.c : Main Thread: STARTED with config file :%s.",octstr_get_cstr(filename));

	info(0, "-----------------------------------------------------");
	info(0, GW_NAME " SMPP-DBBOX Version %s Starting", GW_VERSION);
	info(0, "-----------------------------------------------------");

	dnd_msg_counter= counter_create();
	dlr_init(cfg);

	sem_update_allowed = semaphore_create(0);
	//	if (gwthread_create(msg_process, NULL) == -1)
	//		warning(0, "Failed to start a new thread for test data");

	update_file(cfg);
	main_thread(cfg);

	info(0, GW_NAME " dbbox terminating.....................");

	cfg_destroy(cfg);
	octstr_destroy(filename);
	dlr_shutdown();

	dbbox_httpadmin_stop();

	semaphore_destroy(sem_update_allowed);

	//gwthread_join_all();
	//delete_msgq(DELETE_DATABASE_QUEUE);
	//delete_msgq(DELETE_MIS_QUEUE);

	gwlib_shutdown();

	info( 0, "EXITING.");
}

void dbbox_shutdown()
{
	pid_t process_thread_id;
	info(0, "DATABASE SYSTEM SHOULD BE SHUTDOWN BY HTTP COMMAND");
	process_thread_id = getpid();
	kill(process_thread_id,SIGINT);
}
Octstr *print_dbbox_status(int status_type)
{
	Octstr *tmp;
	Octstr *log_file;
	Octstr *tmp1;
	int i;
	PROCESS_CLIENT *test_client;

	time_t running_time;
	running_time = time(NULL) - process_start_time;
	log_file = log_file_name(log_file_index);

	tmp = octstr_format("Dbbox Process ID: %d.										"
			"%sDbbox Process Status: %s								"
			"%sOnline Time: Days:%ld,Hour:%ld,Minute:%ld,Seconds:%ld	"
			"%sConfiguration File path : %s								"
			"%sLog File path : %s										"
			"%sDBBOX Server :%d"
			,
			(int)getpid(),
			bb_status_linebreak(status_type),dbbox_get_status_name(program_status),
			bb_status_linebreak(status_type),running_time/3600/24, running_time/3600%24, running_time/60%60, running_time%60,
			bb_status_linebreak(status_type),octstr_get_cstr(filename),
			bb_status_linebreak(status_type),octstr_get_cstr(log_file),
			bb_status_linebreak(status_type),server_data->listening_port
			);
	gw_rwlock_rdlock(server_data->list_lock);
	if(gwlist_len(server_data->client_list) == 0)
		octstr_append(tmp,octstr_format("%sNo clients connected",bb_status_linebreak(status_type)));
	else
	{
		for(i = 0; i < gwlist_len(server_data->client_list);i++)
		{
			test_client = gwlist_get(server_data->client_list,i);
			running_time = time(NULL)-test_client->online_time;
			octstr_format_append(tmp,"%sIP:%s,ID:%s,Msgsent:%ld,Msgrecv:%ld,MsgQ:%ld,Online Days:%ld,Hour:%ld,Min:%ld,Sec:%ld",
					bb_status_linebreak(status_type),
					octstr_get_cstr(test_client->client_ip),
					test_client->conn_id ? octstr_get_cstr(test_client->conn_id):"",
					test_client->req_sent,
					test_client->req_recv,
					gwlist_len(test_client->send_list),
					running_time/3600/24, running_time/3600%24, running_time/60%60, running_time%60 );

		}
	}
	gw_rwlock_unlock(server_data->list_lock);


	octstr_destroy(log_file);
	return tmp;

}
