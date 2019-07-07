/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smsc.c                                                           */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains routing, writing actual access logs,          */
/*		        : handling failed messages etc.                                    */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#include "gw-config.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include "include/smpp_client.h"
#include "include/smscconn_p.h"
#include "gwlib/gwlib.h"
#include "include/smscconn.h"

#include "include/msg.h"
#include "gwlib/smpp_pdu.h"
#include "gwlib/process_sock.h"

long sys_thread = -1;
long global_q_min = -1;
long global_q_max = -1;
/**************************************************************************************/
/* Declaration of Extern Variables.                                                   */
/**************************************************************************************/

extern List *outgoing_msg;
extern List *flow_threads;
extern List *suspended;
extern List *isolated;
extern Counter *incoming_msg_counter;
extern Counter *outgoing_msg_counter;
extern volatile sig_atomic_t smpp_client_status;

/**************************************************************************************/
/* Declaration of Static Variables.                                                   */
/**************************************************************************************/

static volatile sig_atomic_t smsc_running;
static List *smsc_list;
static RWLock smsc_list_lock;
static List *smsc_groups;
static long msg_resend_frequency;
static long system_info_resend_frequency;
static long router_thread = -1;
//static long sys_thread = -1;

static List *compare_smsc_string = NULL;


static long max_msg_duration = -1;
static long max_retry_limit = 1;
static long log_file_idx = -1;


void produce_outgoing_msg(Msg *sms)
{

	gwlist_produce(outgoing_msg, sms);	
	if((global_q_max != -1) && (gwlist_len(outgoing_msg)) >= global_q_max)
	{
		Conn_Msg *msg;
		msg = conn_msg_create(global_q_msg);
		msg->global_q_msg.q_limit = 1;
		send_msg_reverse_server(msg);
		info(0,"SENIND GLOBAL-Q-MAX :%d :%d",global_q_max,gwlist_len(outgoing_msg));
	}
}
void wake_router_thread(void)
{
	if(router_thread != -1)
	{
		gwthread_wakeup(router_thread);		
	}
	if((smpp_client_status != CLIENT_RUNNING)&&(router_thread != -1))
		gwthread_join(router_thread);
}


int compare_change_string(Octstr *smsc_id)
{

	if((compare_smsc_string == NULL) || smsc_id == NULL)
		return 1;

	if(gwlist_search(compare_smsc_string,smsc_id,octstr_item_match) != NULL)
		return 0;
	else
		return 1;
}

/**************************************************************************************/
/* Forward declaration of local functions used in this file .                         */
/**************************************************************************************/

static void msg_router(void *arg);
static long smsc2_find(Octstr *id, long start);
static void send_system_info(void *arg);

/**************************************************************************************/
/* Purpose : function used to destroy the smsc connection.                            */
/* Input   : SMSCCONN structure.                                                      */ 
/**************************************************************************************/

void smpp_client_smscconn_ready(SMSCConn *conn)
{
	gwlist_add_producer(flow_threads);

}

/**************************************************************************************/
/* Purpose : function used to find the smsc.                                          */
/* Input   : pointer to octstr that identify the particular smsc.                     */ 
/**************************************************************************************/

void smpp_client_smscconn_connected(SMSCConn *conn)
{
	if (router_thread >= 0)
		gwthread_wakeup(router_thread);
}

/**************************************************************************************/
/* Purpose : function used to kill the smscconnection.                                */
/*         : after status has been set to SMSCCONN_DEAD, smpp-client                  */
/*         : is free to release/delete 'conn.                                         */ 
/**************************************************************************************/

void smpp_client_smscconn_killed(void)
{
	if (sys_thread >= 0)
		gwthread_wakeup(sys_thread);

	gwlist_remove_producer(flow_threads);

}
void wake_sys_thread(void)
{
	if(sys_thread == -1)
		return;
	gwthread_wakeup(sys_thread);
}
/**************************************************************************************/
/* Purpose : Thread used to consume the incoming message and call msg_router function */
/*         : to route those messages to correct smsc.                                 */
/* Input   : NULL Pointer.                                                            */ 
/**************************************************************************************/

static void msg_router(void *arg)
{
	Msg *msg;
	Msg *newmsg = NULL;
	Msg *startmsg = NULL;

	Octstr *tmp;

	SMPP_PDU *pdu;
	SMSCConn *conn = NULL;

	int list_len = 0,list_len_index = 0;
	int i,j;
	int preferred_nos = 0;
	int allowed_nos = 0;
	int ret;

	ret = 0;
	gwlist_add_producer(flow_threads);
	//gwlist_add_producer(outgoing_msg);
	//gwthread_wakeup(MAIN_THREAD_ID);


	if(log_file_idx	!= - 1)	
		log_thread_to(log_file_idx);
	else
		info(0,"No log file specified for failed sms");

	info( 0, "THREAD STARTED :%s:%d", __func__,gwthread_self());

	while(smpp_client_status == CLIENT_RUNNING) 
	{
		if(gwlist_len(outgoing_msg) > 0)
		{	
			list_len = gwlist_len(outgoing_msg);
			for(list_len_index = 0; list_len_index < list_len;list_len_index++)
			{

				msg = NULL;
				msg = gwlist_extract_first(outgoing_msg);
				if(msg == NULL)
					continue;
				if(msg->sm_info == NULL)
				{
					error(0,"MSG NOT BUT SM INFO FOUND NULL :%p",msg);
					continue;
				}
				if(msg->sm_info->pdu == NULL)
				{
					error(0,"MSG NOT BUT SM INFO PDU FOUND NULL :%p",msg);
					continue;
				}
				if(msg->routing_info == NULL)
				{
					error(0,"MSG NOT BUT ROUTING INFO FOUND NULL :%p",msg);
					continue;
				}

				/*
				   debug("sms_router",0,"SMS FAILED [FAILEDSMSC:%s][MSG_STATE:%d][ESME:%s][ESME_MSGID:%s][FROM:%s][TO:%s][MSG-RETRY:%d]",
				   octstr_get_cstr(msg->failed_smsc_id),
				   msg->msg_state,
				   octstr_get_cstr(msg->sm_info->account_name),
				   octstr_get_cstr(msg->sm_info->account_msg_id),
				   octstr_get_cstr(pdu->u.submit_sm.source_addr),
				   octstr_get_cstr(pdu->u.submit_sm.destination_addr),
				   msg->retries
				   );
				   */	
				/*msg sent to smsc but no resp is recivevd so destroy the msg */

				if(msg->msg_state == MSG_SENT_NORESP)
				{
					tmp = octstr_from_position(msg->sm_info->pdu,4);
					pdu = smpp_pdu_unpack(tmp);
					octstr_destroy(tmp);
					error(0, "MSG SENT:NO RESP RECEIVED:[SMSCID:%s][MSG_STATE:%d][ESME:%s][ESME_MSGID:%s][FROM:%s][TO:%s][MSG-RETRY:%d]",
							octstr_get_cstr(msg->failed_smsc_id),
							msg->msg_state,
							octstr_get_cstr(msg->sm_info->account_name),
							octstr_get_cstr(msg->sm_info->account_msg_id),
							octstr_get_cstr(pdu->u.submit_sm.source_addr),
							octstr_get_cstr(pdu->u.submit_sm.destination_addr),
							msg->retries);
					debug("test.smpp", 0, "----------BEFORE SENDING MESSAGE TO CLIENT---------4");					
					mis_db_insert(	msg->sm_info->account_name,
							msg->sm_info->account_msg_id,
							msg->sm_info->req_time,
							msg->failed_smsc_id,
							NULL,
							pdu,
							0x00000405, msg->sm_info->client_ip);
					smpp_pdu_destroy(pdu);
					msg_destroy(msg);
					continue;
				}
				/*retry limit of the message has been exceeded.destroy the message*/

				if(msg->retries == max_retry_limit)
				{

					tmp = octstr_from_position(msg->sm_info->pdu,4);
					pdu = smpp_pdu_unpack(tmp);
					octstr_destroy(tmp);
					error(0, "MSG RETRY LIMIT EXCEEDED:[SMSCID:%s][MSG_STATE:%d][ESME:%s][ESME_MSGID:%s][FROM:%s][TO:%s][MSG-RETRY:%d]",
							octstr_get_cstr(msg->failed_smsc_id),
							msg->msg_state,
							octstr_get_cstr(msg->sm_info->account_name),
							octstr_get_cstr(msg->sm_info->account_msg_id),
							octstr_get_cstr(pdu->u.submit_sm.source_addr),
							octstr_get_cstr(pdu->u.submit_sm.destination_addr),
							msg->retries
					     );
					debug("test.smpp", 0, "----------BEFORE SENDING MESSAGE TO CLIENT---------888");					
					mis_db_insert(	msg->sm_info->account_name,
							msg->sm_info->account_msg_id,
							msg->sm_info->req_time,
							msg->failed_smsc_id,
							NULL,
							pdu,
							0x00000404, msg->sm_info->client_ip);
					smpp_pdu_destroy(pdu);
					msg_destroy(msg);
					continue;
				}

				/*If message has expired in the memory*/
				if((max_msg_duration != -1)&&(difftime(time(NULL),msg->msg_retry_time) > max_msg_duration))
				{
					tmp = octstr_from_position(msg->sm_info->pdu,4);
					pdu = smpp_pdu_unpack(tmp);
					octstr_destroy(tmp);
					error(0, "MSG EXPIRED IN MEMORY[SMSCID:%s][MSG_STATE:%d][ESME:%s][ESME_MSGID:%s][FROM:%s][TO:%s][MSG-RETRY:%d]",
							octstr_get_cstr(msg->failed_smsc_id),
							msg->msg_state,
							octstr_get_cstr(msg->sm_info->account_name),
							octstr_get_cstr(msg->sm_info->account_msg_id),
							octstr_get_cstr(pdu->u.submit_sm.source_addr),
							octstr_get_cstr(pdu->u.submit_sm.destination_addr),
							msg->retries								
					     );
					debug("test.smpp", 0, "----------BEFORE SENDING MESSAGE TO CLIENT---------44");
					mis_db_insert(	msg->sm_info->account_name,
							msg->sm_info->account_msg_id,
							msg->sm_info->req_time,
							msg->failed_smsc_id,
							NULL,
							pdu,
							0x00000404 ,msg->sm_info->client_ip);

					smpp_pdu_destroy(pdu);
					msg_destroy(msg);
					continue;
				}

				if(smsc_length() > 0)
				{

					debug("sms_router", 0, "FOUND ONE SMSC ROUTING MSG[MSG_STATE:%d][ESME:%s][ESME_MSGID:%s][MSG-RETRY:%d]",
							msg->msg_state,
							(msg->routing_info->account_name),
							(msg->routing_info->account_msg_id),
							msg->retries
					     );
					/*
					   for(i = 0; i < MAX_NO_SMSC/2; i++)
					   {
					   debug("test.smpp", 0, "Prefer GOT[%d] :%d ",i,msg->routing_info->preferred_smsc[i]);		
					   }
					   for(i = 0; i < MAX_NO_SMSC/2; i++)
					   {
					   debug("test.smpp", 0, "Allowed GOT[%d] :%d",i,msg->routing_info->allowed_smsc[i]);		
					   }
					   */
					for(j = 0; msg->routing_info->preferred_smsc[j]	!= -1 ; j++)
						preferred_nos = j+1;
					for(j = 0; msg->routing_info->allowed_smsc[j] != -1 ; j++)
						allowed_nos = j+1;
					//debug("test.smpp", 0, "Preferred SMSC :%d, Allowed SMSC :%d",preferred_nos,allowed_nos);

					//If that is not the case copy all preferred smsc into allowed smsc and discard preferred smsc

					for(j = 0,ret = allowed_nos; j < preferred_nos ;j++,ret++)
					{
						msg->routing_info->allowed_smsc[ret] = msg->routing_info->preferred_smsc[j];
						msg->routing_info->preferred_smsc[j] = -1;
					}
					msg->routing_info->allowed_smsc[ret] = -1;
					/*
					   for(i = 0; i < MAX_NO_SMSC/2; i++)
					   {
					   debug("test.smpp", 0, "Prefer NEW[%d] :%d ",i,msg->routing_info->preferred_smsc[i]);		
					   }
					   for(i = 0; i < MAX_NO_SMSC/2; i++)
					   {
					   debug("test.smpp", 0, "Allowed NEW[%d] :%d",i,msg->routing_info->allowed_smsc[i]);		
					   }
					   */
					smsc2_rout(msg);						
					continue;
				}//smsc len greater than one
				if(smsc_length() == 0)
				{
					gwlist_produce(outgoing_msg,msg);
					//produce_outgoing_msg(msg);
				}

			}//for - len of msg in q			
			/*NO SMSCs are present*/
			if(smsc_length() == 0)
			{
				debug("sms_router", 0, "NO SMSCs ARE PRESENT FOR ROUTING :%d MSGS",gwlist_len(outgoing_msg));
			}
			if((global_q_min != -1) &&( gwlist_len(outgoing_msg) < global_q_min))
			{
				Conn_Msg *conn_msg;
				conn_msg = conn_msg_create(global_q_msg);
				conn_msg->global_q_msg.q_limit = 2;
				send_msg_reverse_server(conn_msg);
				info(0,"Sending GL0BAL-Q-MIN :%d :%d",global_q_min,gwlist_len(outgoing_msg));
			}
		}//if q len > 1		
		gwthread_sleep(msg_resend_frequency);	
	}//while
	while(smsc_length() > 0)
		sleep(1);
	/*In case thread is terminating and outgoing sms is not zero - put them into a file*/
	printf("SYSTEM TERMINATING WITH  :%d MSGS IN RAM\n",gwlist_len(outgoing_msg));
	error( 0, "SYSTEM TERMINATING WITH  :%d MSGS IN RAM",gwlist_len(outgoing_msg));
	j = gwlist_len(outgoing_msg);
	for(i = 0 ; i < j;i++)
	{
		msg = gwlist_extract_first(outgoing_msg);
		if((msg == NULL) || (msg->sm_info == NULL) || (msg->sm_info->pdu == NULL))
		{
			continue;
		}
		tmp = octstr_from_position(msg->sm_info->pdu,4);
		pdu = smpp_pdu_unpack(tmp);
		octstr_destroy(tmp);
		debug("sms_router",0,"FAILED MESS[ACC_NAME:%s][ACC_MSGID:%s]",octstr_get_cstr(msg->sm_info->account_name),
				octstr_get_cstr(msg->sm_info->account_msg_id));
		if(msg->msg_state == MSG_SENT_NORESP)
			ret = 0x00000405;
		else 
			ret = 0x00000406;
		debug("test.smpp", 0, "----------BEFORE SENDING MESSAGE TO CLIENT---------4444");		
		mis_db_insert(	msg->sm_info->account_name,
				msg->sm_info->account_msg_id,
				msg->sm_info->req_time,
				msg->failed_smsc_id,
				NULL,
				pdu,
				ret, msg->sm_info->client_ip);
		smpp_pdu_destroy(pdu);
		msg_destroy(msg);
	}
	//printf("+++++++++++MSG ROUETER TERMINATE\n");
	//gwlist_destroy(outgoing_msg,msg_destroy);
	smsc_running = 0;
	gwlist_remove_producer(flow_threads);
	//gwlist_remove_producer(outgoing_msg);
	gwlist_destroy(outgoing_msg, NULL);
	//printf("+++++++++++MSG ROUETER TERMINATE++++++++++11\n");
	router_thread = -1;
	info( 0, "%s terminates.", __func__);
}
/**************************************************************************************/
/* Purpose : function used to start the smsc and check all smsc's have unique smsc    */
/*	   : id or not.                                                                   */
/* Input   : pointer to cfg structure.                                                */
/* Output  : return 0 if successfully started.                                        */
/*         : return -1 if SMSC is already running.                                    */
/**************************************************************************************/

int start2_smsc(Cfg *cfg)
{
	CfgGroup *grp;
	SMSCConn *conn;
	Octstr *os;
	Octstr *tmp;
	Octstr *log;
	int i,j;
	if (smsc_running) return -1;

	smsc_list = gwlist_create();

	grp = cfg_get_single_group(cfg, octstr_imm("SMPP-Client"));

	if (cfg_get_integer(&msg_resend_frequency, grp,octstr_imm("msg-resend-freq")) == -1 || msg_resend_frequency <= 0) 
		msg_resend_frequency = 60;

	if (cfg_get_integer(&max_msg_duration, grp,octstr_imm("max-msg-duration")) == -1)  
		max_msg_duration = -1;

	if (cfg_get_integer(&max_retry_limit, grp,octstr_imm("msg-retry-limit")) == -1)  
		max_retry_limit = 1;

	if (cfg_get_integer(&global_q_min, grp,octstr_imm("global-q-min")) == -1)  
		global_q_min = -1;
	if (cfg_get_integer(&global_q_max, grp,octstr_imm("global-q-max")) == -1)  
		global_q_max = -1;

	if(global_q_max != -1 && global_q_min == -1)
		panic(0,"'global-q-max' value is set but 'global-q-min' value is not set");
	if(global_q_min != -1 && global_q_max == -1)
		panic(0,"'global-q-min' value is set but 'global-q-max' value is not set");
	if(global_q_min != -1 && global_q_max != -1)
	{
		if(global_q_min >= global_q_max)
		{
			panic(0,"'global-q-min' value is >= 'global-q-max'");	
		}
	}
	info(0, "Set MSG resend frequency to %ld seconds.", msg_resend_frequency);

	tmp = cfg_get(grp, octstr_imm("compare-esm-smscid"));
	if(tmp != NULL)
	{
		compare_smsc_string = octstr_split(tmp, octstr_imm(";")); 
	}
	octstr_destroy(tmp);

	/*	
		log = cfg_get(grp, octstr_imm("msg-failed-log"));
		if(log != NULL)
		{

		Octstr *log_file;
		log_file = logfile_append_time(log); 
		log_file_idx = log_open(octstr_get_cstr(log_file), 0, GW_EXCL);
		octstr_destroy(log_file);       
		}
		octstr_destroy(log);
		*/


	gw_rwlock_init_static(&smsc_list_lock);
	smsc_groups = cfg_get_multi_group(cfg, octstr_imm("smsc"));
	gwlist_add_producer(smsc_list);

	for (i = 0; i < gwlist_len(smsc_groups) && 
			(grp = gwlist_get(smsc_groups, i)) != NULL; i++) {
		conn = smscconn_create(grp, 1); 
		if (conn == NULL)
			panic(0, "Cannot start with SMSC connection failing");
		gwlist_append(smsc_list, conn);
	}

	gwlist_remove_producer(smsc_list);

	if(gwlist_len(smsc_list) > 1)
	{
		for(i=0;i<gwlist_len(smsc_list);i++)
		{
			for(j = 1+i; j < gwlist_len(smsc_list); j++)
			{			
				if(octstr_compare(((SMSCConn *)(gwlist_get(smsc_list,i)))->id,
							((SMSCConn *)(gwlist_get(smsc_list,j)))->id) == 0)
					panic(0, "For more than one SMSC's, There can't be same SMSC-id");

			}
		}
	}

	if ((sys_thread = gwthread_create(send_system_info,NULL)) == -1)
		panic(0, "Failed to start a new thread for SYSTEM related info.");


	if ((router_thread = gwthread_create(msg_router, NULL)) == -1)
		panic(0, "Failed to start a new thread for SMS routing");

	smsc_running = 1;
	return 0;
} 

/**************************************************************************************/
/* Purpose : This function is responsible for sending all SMSC disconnected message   */
/*			to the server when lient program is terminated         					  */
/* Input   : NULL pointer.                                                            */
/**************************************************************************************/

void send_terminate_info_server(void)
{
	struct smsc_info info_smsc[gwlist_len(smsc_list)];
	StatusInfo info; 
	SMSCConn *conn;
	int i;

	gw_rwlock_rdlock(&smsc_list_lock);
	if(gwlist_len(smsc_list) == 0) 
	{
		warning(0, "No SMSC's Connected");
		gw_rwlock_unlock(&smsc_list_lock);		
	}
	else
	{
		for(i = 0; i < gwlist_len(smsc_list); i++)
		{
			conn = gwlist_get(smsc_list, i);
			strcpy(info_smsc[i].smsc_id,octstr_get_cstr(conn->id)); 
			info_smsc[i].conn_status = 0;   	
		}

		send_msg_system_q(SYSTEM_Q_FIRST_INFO,SMSC_INFORMATION,((sizeof(struct smsc_info))*gwlist_len(smsc_list)),NULL);
		send_msg_system_q(SMSC_INFORMATION,gwlist_len(smsc_list),0,info_smsc);
		gw_rwlock_unlock(&smsc_list_lock);
	}
}
/**************************************************************************************/
/* Purpose : Thread function used to send all smsc's status info with smsc id         */
/* Input   : NULL pointer.                                                            */
/**************************************************************************************/
void send_system_info_init(void)
{
	StatusInfo status_info; 
	SMSCConn *conn;
	int i,len;
	struct smsc_info info_smsc1[gwlist_len(smsc_list)];
	Conn_Msg *msg,*msg1;

	gw_rwlock_rdlock(&smsc_list_lock);
	if(gwlist_len(smsc_list) == 0) 
	{
		warning(0, "No SMSC's to receive message");
		gw_rwlock_unlock(&smsc_list_lock);
		return;
	}
	msg1 = conn_msg_create(no_of_smsc);
	info(0,"Sending system information : %d",gwlist_len(smsc_list));
	msg1->no_of_smsc.no_of_smsc = gwlist_len(smsc_list);
	send_msg_reverse_server(msg1);
	for(i = 0; i < gwlist_len(smsc_list); i++)
	{
		conn = gwlist_get(smsc_list, i);
		if(conn->bind_sent != 1) 
		{
			i--;
			printf("\nInside bind not sent ;%d\n",conn->bind_sent);
			continue;
		}
		smscconn_info(conn, &status_info);
		msg = conn_msg_create(info_smsc);

		/*
		   strcpy(info_smsc[i].smsc_id,octstr_get_cstr(conn->id)); 
		   strcpy(info_smsc[i].smsc_type,octstr_get_cstr(conn->smsc_type));
		   */	
		msg->info_smsc.smsc_id = octstr_duplicate(conn->id);
		msg->info_smsc.smsc_type = octstr_duplicate(conn->smsc_type);

		if(status_info.status == SMSCCONN_ACTIVE)
			msg->info_smsc.conn_status = 1;
		else
			msg->info_smsc.conn_status = 0;  	

		msg->info_smsc.conn_mode = status_info.mode;/*connection status is not important routing is done on the basis of connection mode*/

		info(0, "Sending System Info: Smsc-ID is=%s,Mode=%d,Status=%d,Smsc-type is %s",\
				octstr_get_cstr(msg->info_smsc.smsc_id),
				msg->info_smsc.conn_mode,
				msg->info_smsc.conn_status,
				octstr_get_cstr(msg->info_smsc.smsc_type));
		send_msg_reverse_server(msg);
	}
	/*
	   send_msg_system_q(SYSTEM_Q_FIRST_INFO,SMSC_INFORMATION,((sizeof(struct smsc_info))*gwlist_len(smsc_list)),NULL);
	   send_msg_system_q(SMSC_INFORMATION,gwlist_len(smsc_list),0,info_smsc);
	   */

}

static void send_system_info(void *arg)
{
	StatusInfo status_info; 
	SMSCConn *conn;
	int i,len;
	int status = 1;
	gwlist_add_producer(flow_threads);
	struct smsc_info info_smsc1[gwlist_len(smsc_list)];
	Conn_Msg *msg,*msg1;

	while(smpp_client_status != CLIENT_SHUTDOWN)
	{
		gw_rwlock_rdlock(&smsc_list_lock);
		//printf("\n\n++++++++++SYSTEM THREAD WAKEUP\n");  
		if(gwlist_len(smsc_list) == 0) 
		{
			warning(0, "No SMSC's to receive message");
			gw_rwlock_unlock(&smsc_list_lock);
			break;
		}
		if(status == 1)
			status = 0;
		else
		{

			for(i = 0; i < gwlist_len(smsc_list); i++)
			{		
				conn = gwlist_get(smsc_list, i);
				smscconn_info(conn, &status_info);
				msg = conn_msg_create(info_smsc);

				msg->info_smsc.smsc_id = octstr_duplicate(conn->id);
				msg->info_smsc.smsc_type = octstr_duplicate(conn->smsc_type);

				if(status_info.status == SMSCCONN_ACTIVE)
					msg->info_smsc.conn_status = 1;
				else
					msg->info_smsc.conn_status = 0;  	

				msg->info_smsc.conn_mode = status_info.mode;
				info(0, "Sending System Info: Smsc-ID=%s,Mode=%d,Status=%d,Smsc-type is %s",\
						octstr_get_cstr(msg->info_smsc.smsc_id),
						msg->info_smsc.conn_mode,
						msg->info_smsc.conn_status,
						octstr_get_cstr(msg->info_smsc.smsc_type));
				send_msg_reverse_server(msg);

			}
			/*
			   send_msg_system_q(SYSTEM_Q_FIRST_INFO,SMSC_INFORMATION,((sizeof(struct smsc_info))*gwlist_len(smsc_list)),NULL);
			   send_msg_system_q(SMSC_INFORMATION,gwlist_len(smsc_list),0,info_smsc);
			   */
		}


		gw_rwlock_unlock(&smsc_list_lock);	
		//printf("\n\n++++++++++SYSTEM THREAD SLEEPING\n");  
		gwthread_sleep(2000);
	}

	gwlist_remove_producer(flow_threads);
	sys_thread = -1;
	//printf("\n\n+++++++++SYSTEM THREAD TERMINATE\n\n");
	info( 0, "%s terminates.", __func__);
}

/**************************************************************************************/
/* Purpose : function used to route the message to requested smsc.                    */
/*	   : check whether dedicated routing is set or random routing is to be used.  */
/* Input   : Message structre which one you want to send to smsc.                     */
/* Output  : return 1 if finds a good one.                                            */
/*         : return -1 if if cannot find nothing at all.                              */ 
/*         : return 0 if all acceptable currently disconnected.                       */
/**************************************************************************************/
/*
   Routing can be done in 2 modes
   1. Dedicated routing-All the SMSCs are specified in preferred smsc according to priority.Once an SMSC is submitted successfully function returns.
   2. Random routing - Load factor(no of sms received by SMSC) of all SMSCs in allowed smsc is seen and one with lowest load factor is choosen.Routing is not random but is load based routing.
   */
int smsc2_rout(Msg *message)
{
	StatusInfo info;
	SMSCConn *conn = NULL, *best_ok = NULL;
	int bo_sent =0;
	int i, ret, bad_found;
	int s=0;
	int allowed_smsc_len;
	int selected_position;
	gw_rwlock_rdlock(&smsc_list_lock);
	if(gwlist_len(smsc_list) == 0) 
	{
		warning(0, "No SMSC's to receive message");
		gw_rwlock_unlock(&smsc_list_lock);
		return -1;
	}

	/*
	   debug("smsc.c",0,"Routing message Acc name :%s ID :%s",
	   octstr_get_cstr(message->sm_info->account_name),octstr_get_cstr(message->sm_info->account_msg_id));	
	   */
	for(i=0;message->routing_info->preferred_smsc[i] != -1;i++)
	{
		conn = NULL;
		debug("rout",0, "smsc list length = %d %d",gwlist_len(smsc_list), message->routing_info->preferred_smsc[i]);
		debug("rout",0,"Received Prefer :%d",message->routing_info->preferred_smsc[i]);
		conn = gwlist_get1(smsc_list, message->routing_info->preferred_smsc[i]);
		smscconn_info(conn, &info);
		/*If this paricular SMSC is not active then rout the msg only if system is not getting shutdown,else destroy the messa		ge*/
		if (info.status != SMSCCONN_ACTIVE)
		{
			if(smpp_client_status != CLIENT_SHUTDOWN)
				continue;
			else 
			{
				gw_rwlock_unlock(&smsc_list_lock);
				msg_destroy(message);
				return -1;
			}
		}
		ret = smscconn_usable(conn,message->routing_info);
		if(ret == -1) 
			continue;
		message->msg_state = MSG_NOT_SENT;
		ret = smscconn_send(conn,message);
		if (ret == -1)
			continue;
		else 
		{
			gw_rwlock_unlock(&smsc_list_lock);
			msg_destroy(message);
			return 1;
		}					
	}//for
	for(i=0;message->routing_info->allowed_smsc[i] != -1;i++)
		allowed_smsc_len = i+1;

	best_ok=NULL;
	bad_found = 0;
	/*
	   s = gw_rand() % len_allowed;
	   debug("smsc",0,"RANDOM FOUND ---RANDOM:%d,LENGTH:%d",s,len_allowed);
	   for(i=0;message->routing_info->allowed_smsc[i] != -1;i++)
	   */
	for(i=0;i < allowed_smsc_len;i++)
	{	
		bad_found = 0;
		selected_position = message->routing_info->allowed_smsc[i];
		conn = gwlist_get(smsc_list, selected_position);
		smscconn_info(conn, &info);

		if(info.status != SMSCCONN_ACTIVE)
		{
			bad_found = 1;
			continue;
		}

		ret = smscconn_usable(conn,message->routing_info);

		if (ret == -1)
			continue;
		/*
		   debug("smsc",0,"FIRST SELECTED ---POSITION:%d,I:%d",(s+i)%len_allowed,i);
		   debug("smsc",0,"current sent factor of SMSC ID:%s is SENT:%d", octstr_get_cstr(conn->id),info.sent);

		   debug("smsc.c",0,"--------------RANDOM ROUTING:%d,%d:%d:%d",len_allowed,(i+s) % len_allowed,info.load,bo_sent);*/
		debug("smsc.c",0,"POSITION :%d,SMSC :%s LOAD :%d",i,octstr_get_cstr(conn->id),info.load);
		if(best_ok == NULL || info.load < bo_sent) 
		{
			//if(best_ok != NULL)
			//printf("Value of info.sent is %d and bo_sent is %d\n",info.load,bo_sent);
			debug("smsc",0,"CHOOSING-----:%d",selected_position);
			best_ok = conn;
			bo_sent = info.load;
		} 
		else
			continue;
	}//for
	if (best_ok) 
	{
		message->msg_state = MSG_NOT_SENT;
		ret = smscconn_send(best_ok, message);
	}
	else if (bad_found) 
	{
		if (smpp_client_status != CLIENT_SHUTDOWN)
			produce_outgoing_msg(message);
		gw_rwlock_unlock(&smsc_list_lock);
		return 0;
	}	
	else 
	{
		gw_rwlock_unlock(&smsc_list_lock);
		if (smpp_client_status == CLIENT_SHUTDOWN)
			return 0;
		return -1;
	}
	gw_rwlock_unlock(&smsc_list_lock);
	if(ret == -1)
		return(smsc2_rout(message));

	msg_destroy(message);	
	return 1;
}

/**************************************************************************************/
/* Purpose : function used to Find a matching smsc-id in the smsc list starting       */
/*	   : at position start.                                                       */
/* Input   : identifier of smsc which one you want to find and variable start.        */
/* Output  : return 0 if successfully destroyed or if conn is NULL.                   */
/*           return -1 if SMSC Connection is dead.                                    */
/**************************************************************************************/

static long smsc2_find(Octstr *id, long start)
{
	SMSCConn *conn = NULL;
	long i;

	if (start > gwlist_len(smsc_list) || start < 0)
		return -1;

	for (i = start; i < gwlist_len(smsc_list); i++) {
		conn = gwlist_get(smsc_list, i);
		if (conn != NULL && octstr_compare(conn->id, id) == 0) {
			break;
		}
	}
	if (i >= gwlist_len(smsc_list))
		i = -1;
	return i;
}
/**************************************************************************************/
/* Purpose : function used to stop the particular smsc                                */
/* Input   : identifier of smsc which one you want to stop.                           */
/* Output  : return 0 if successfully stoped smsc.                                    */
/*           return -1 if SMSC is not working (already stoped state).                 */
/**************************************************************************************/
int smsc2_stop_smsc(Octstr *id)
{
	SMSCConn *conn;
	long i = -1;

	if (!smsc_running)
		return -1;

	gw_rwlock_rdlock(&smsc_list_lock);
	while((i = smsc2_find(id, ++i)) != -1) {
		conn = gwlist_get(smsc_list, i);
		if (conn != NULL && smscconn_status(conn) == SMSCCONN_DEAD) {
			info(0, "HTTP: Could not shutdown already dead smsc-id `%s'",
					octstr_get_cstr(id));
		} else {
			info(0,"HTTP: Shutting down smsc-id `%s'", octstr_get_cstr(id));
			smscconn_shutdown(conn, 1);   
		}
	}
	gw_rwlock_unlock(&smsc_list_lock);
	return 0;
}
/**************************************************************************************/
/* Purpose : function used to restart the the particular smsc                         */
/* Input   : identifier of smsc which one you want to restart.                        */
/* Output  : return 0 if successfully restarted smsc.                                 */
/*           return -1 if SMSC is not working (already stoped state).                 */
/**************************************************************************************/
int smsc2_restart_smsc(Octstr *id)
{
	CfgGroup *grp;
	SMSCConn *conn, *new_conn;
	Octstr *smscid = NULL;
	long i = -1;
	int num = 0;

	if (!smsc_running)
		return -1;

	gw_rwlock_wrlock(&smsc_list_lock);

	while((i = smsc2_find(id, ++i)) != -1) {
		int hit;
		long group_index;

		conn = gwlist_get(smsc_list, i);
		if (conn != NULL && smscconn_status(conn) != SMSCCONN_DEAD) {
			warning(0, "HTTP: Could not re-start already running smsc-id `%s'",
					octstr_get_cstr(id));
			continue;
		}

		hit = 0;
		grp = NULL;
		for (group_index = 0; group_index < gwlist_len(smsc_groups) && 
				(grp = gwlist_get(smsc_groups, group_index)) != NULL; group_index++) {
			smscid = cfg_get(grp, octstr_imm("smsc-id"));
			if (smscid != NULL && octstr_compare(smscid, id) == 0) {
				if (hit == num)
					break;
				else
					hit++;
			}
			octstr_destroy(smscid);
			smscid = NULL;
		}
		octstr_destroy(smscid);
		if (hit != num) {
			error(0, "HTTP: Could not find config for smsc-id `%s'", octstr_get_cstr(id));
			break;
		}

		info(0,"HTTP: Re-starting smsc-id `%s'", octstr_get_cstr(id));

		new_conn = smscconn_create(grp, 1);
		if (new_conn == NULL) {
			error(0, "Start of SMSC connection failed, smsc-id `%s'", octstr_get_cstr(id));
			continue; 
		}


		gwlist_delete(smsc_list, i, 1);

		smscconn_destroy(conn);
		gwlist_insert(smsc_list, i, new_conn);
		smscconn_start(new_conn);
		num++;
	}  

	gw_rwlock_unlock(&smsc_list_lock);

	if (router_thread >= 0)
		gwthread_wakeup(router_thread);

	return 0;
}
/**************************************************************************************/
/* Purpose : function used to resume the opeartion of all smscs configured            */ 
/**************************************************************************************/
void smsc2_resume(void)
{
	SMSCConn *conn;
	long i;

	if (!smsc_running)
		return;

	gw_rwlock_rdlock(&smsc_list_lock);
	for (i = 0; i < gwlist_len(smsc_list); i++) {
		conn = gwlist_get(smsc_list, i);
		smscconn_start(conn);
	}
	gw_rwlock_unlock(&smsc_list_lock);

	if (router_thread >= 0)
		gwthread_wakeup(router_thread);

}
/**************************************************************************************/
/* Purpose : function used to suspend all smsc configured                             */ 
/**************************************************************************************/
void smsc2_suspend(void)
{
	SMSCConn *conn;
	long i;

	if (!smsc_running)
		return;

	gw_rwlock_rdlock(&smsc_list_lock);
	for (i = 0; i < gwlist_len(smsc_list); i++) {
		conn = gwlist_get(smsc_list, i);
		smscconn_stop(conn);
	}
	gw_rwlock_unlock(&smsc_list_lock);
}
/**************************************************************************************/
/* Purpose : function used to shutdown the all smsc connection                        */
/**************************************************************************************/
int smsc2_shutdown(void)
{
	SMSCConn *conn;
	long i;

	if (!smsc_running)
		return -1;

	gw_rwlock_rdlock(&smsc_list_lock);
	for(i=0; i < gwlist_len(smsc_list); i++) {
		conn = gwlist_get(smsc_list, i);
		smscconn_shutdown(conn, 1);
	}
	gw_rwlock_unlock(&smsc_list_lock);
	/*
	   if (router_thread >= 0)
	   {
	   gwthread_wakeup(router_thread);
	   }
	   */
	//gwlist_remove_producer(incoming_msg);
	return 0;
}
/**************************************************************************************/
/* Purpose : function used to cleanup all smsc configured                             */ 
/**************************************************************************************/
void smsc2_cleanup(void)
{
	SMSCConn *conn;
	long i;

	if (!smsc_running)
		return;

	debug("smscconn", 0, "final clean-up for SMSCConn");

	gw_rwlock_wrlock(&smsc_list_lock);
	for (i = 0; i < gwlist_len(smsc_list); i++) {
		conn = gwlist_get(smsc_list, i);
		smscconn_destroy(conn);
	}
	gwlist_destroy(smsc_list, NULL);
	gwlist_destroy(compare_smsc_string,octstr_destroy_item);
	compare_smsc_string = NULL;
	smsc_list = NULL;
	gw_rwlock_unlock(&smsc_list_lock);
	gwlist_destroy(smsc_groups, NULL);
	gw_rwlock_destroy(&smsc_list_lock);
}
/**************************************************************************************/
/* Purpose : function used to find the status of smsc                                 */
/* Input   : status type of smpp-client                                               */
/* Output  : return Octstr that is the information about the status of smsc           */
/**************************************************************************************/
Octstr *smsc2_status(int status_type)
{
	Octstr *tmp;
	char tmp3[64];
	char *lb;
	long i;
	int para = 0;
	SMSCConn *conn;
	StatusInfo info;
	const Octstr *conn_id = NULL;
	const Octstr *conn_name = NULL;

	if ((lb = smpp_client_status_linebreak(status_type)) == NULL)
		return octstr_create("Un-supported format");

	if (status_type == SMPPCLIENTSTATUS_HTML || status_type == SMPPCLIENTSTATUS_WML)
		para = 1;

	if (!smsc_running) {
		if (status_type == SMPPCLIENTSTATUS_XML)
			return octstr_create ("<smscs>\n\t<count>0</count>\n</smscs>");
		else
			return octstr_format("%sNo SMSC connections%s\n\n", para ? "<p>" : "",
					para ? "</p>" : "");
	}

	if (status_type != SMPPCLIENTSTATUS_XML)
		tmp = octstr_format("%sTotal Mesages Queued:%ld %s",para ? "<p>" : "", gwlist_len(outgoing_msg), lb);
	else
		tmp = octstr_format("<smscs><count>%d</count>\n\t", gwlist_len(smsc_list));

	if (status_type != SMPPCLIENTSTATUS_XML)
		octstr_format_append(tmp,"%sSMSC connections:%s", para ? "<p>" : "", lb);
	else
		octstr_format_append(tmp,"<smscs><count>%d</count>\n\t", gwlist_len(smsc_list));

	gw_rwlock_rdlock(&smsc_list_lock);
	for (i = 0; i < gwlist_len(smsc_list); i++) {
		conn = gwlist_get(smsc_list, i);

		if ((smscconn_info(conn, &info) == -1)) {
			continue;
		}

		conn_id = conn ? smscconn_id(conn) : octstr_imm("unknown");
		conn_id = conn_id ? conn_id : octstr_imm("unknown");
		conn_name = conn ? smscconn_name(conn) : octstr_imm("unknown");

		if (status_type == SMPPCLIENTSTATUS_HTML) {
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
			octstr_append(tmp, conn_id);
			octstr_append_cstr(tmp, "</b>&nbsp;&nbsp;&nbsp;&nbsp;");
		} else if (status_type == SMPPCLIENTSTATUS_TEXT) {
			octstr_append_cstr(tmp, "    ");
			octstr_append(tmp, conn_id);
			octstr_append_cstr(tmp, "    ");
		} 
		if (status_type == SMPPCLIENTSTATUS_XML) {
			octstr_append_cstr(tmp, "<smsc>\n\t\t<name>");
			octstr_append(tmp, conn_name);
			octstr_append_cstr(tmp, "</name>\n\t\t");
			octstr_append_cstr(tmp, "<id>");
			octstr_append(tmp, conn_id);
			octstr_append_cstr(tmp, "</id>\n\t\t");
		} else
			octstr_append(tmp, conn_name);

		switch (info.status) {
			case SMSCCONN_ACTIVE:
			case SMSCCONN_ACTIVE_RECV:
				sprintf(tmp3, "online %lds", info.online);
				break;
			case SMSCCONN_DISCONNECTED:
				sprintf(tmp3, "disconnected");
				break;
			case SMSCCONN_CONNECTING:
				sprintf(tmp3, "connecting");
				break;
			case SMSCCONN_RECONNECTING:
				sprintf(tmp3, "re-connecting");
				break;
			case SMSCCONN_DEAD:
				sprintf(tmp3, "dead");
				break;
			default:
				sprintf(tmp3, "unknown");
		}
		float msg_sec;
		if(info.online == 0)
			msg_sec = 0.0;
		else
			msg_sec = (info.sent/info.online);


		float incoming_msg_sec;
		if(info.online == 0)
			incoming_msg_sec = 0.0;
		else
			incoming_msg_sec = (info.received/info.online);

		if (status_type == SMPPCLIENTSTATUS_XML)
			octstr_format_append(tmp, "<status>%s</status>\n\t\t<received>%ld</received>"
					"\n\t\t<sent>%ld</sent>\n\t\t<failed>%ld</failed>\n\t\t"
					"<queued>%ld</queued>\n\t</smsc>\n", tmp3,
					info.received, info.sent, info.failed,
					info.queued);
		else
			octstr_format_append(tmp, "(%s,rcvd %ld,sent %ld,failed %ld,queued %ld,waiting %d,"
					"incoming SMS/sec %.2f,outgoing SMS/sec %.2f )%s", tmp3,
					info.received, info.sent, info.failed,info.queued,info.waiting,
					incoming_msg_sec,msg_sec, lb);
	}
	gw_rwlock_unlock(&smsc_list_lock);

	if (para)
		octstr_append_cstr(tmp, "</p>");
	if (status_type == SMPPCLIENTSTATUS_XML)
		octstr_append_cstr(tmp, "</smscs>\n");
	else
		octstr_append_cstr(tmp, "\n\n");
	return tmp;
}
/****************************************************************************************/     
/*		       	End of file smsc.c                                             */
/****************************************************************************************/
int smsc_length(void)
{
	long i;
	int connected_smsc = 0;
	SMSCConn *conn;
	StatusInfo info;

	gw_rwlock_rdlock(&smsc_list_lock);
	for (i = 0; i < gwlist_len(smsc_list); i++) 
	{
		conn = gwlist_get(smsc_list, i);
		if ((smscconn_info(conn, &info) == -1)) 
		{
			continue;
		}
		if(info.status == SMSCCONN_ACTIVE)
			connected_smsc++;

	}
	gw_rwlock_unlock(&smsc_list_lock);
	return connected_smsc;
}
void change_smsc_logfile(void)
{
	SMSCConn *conn;
	int ret;
	long i;
	if (!smsc_running)
		return;
	gw_rwlock_rdlock(&smsc_list_lock);
	for (i = 0; i < gwlist_len(smsc_list); i++)
	{
		conn = gwlist_get(smsc_list, i);
		smscconn_change_logfile(conn);
	}
	gw_rwlock_unlock(&smsc_list_lock);

	/*change this log file as well*/
	if(log_file_idx == -1)
		return;
	ret = change_logfile_name(log_file_idx);
	if(ret == 0)
	{
		Octstr *log_name;
		log_name = log_file_name(log_file_idx);
		//info(0, "Log file changed successfully:%s ",octstr_get_cstr(log_name));
		octstr_destroy(log_name);
	}

}


//[sanchal][230309][Code for breaking logfiles in same day when size increase]
void change_smsc_logfile_onsize(long iLogSize)
{
	SMSCConn *conn;
	long i;

	//printf("\nsmscconn.c : change_smsc_logfile_onsize: begining\n");
	debug("smsc",0,"change_smsc_logfile_onsize : begining");

	//if (!smsc_running)
	//return;

	gw_rwlock_rdlock(&smsc_list_lock);
	for(i = 0; i < gwlist_len(smsc_list); i++)
	{
		conn = gwlist_get(smsc_list, i);
		smscconn_change_logfile_onsize( conn, iLogSize);
	}
	gw_rwlock_unlock(&smsc_list_lock);
	//printf("\nsmscconn.c : change_smsc_logfile_onsize: end \n");
	debug("smsc",0,"change_smsc_logfile_onsize : end");
}

