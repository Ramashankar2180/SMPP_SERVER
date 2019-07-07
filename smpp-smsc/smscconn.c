/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smscconn.c                                                       */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file includes callback functions called by SMSCConn         */
/*		: implementations                                                          */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#include <signal.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include "include/smscconn.h"
#include "include/smscconn_p.h"


#define SMSCCONN_RECONNECT_DELAY     10.0

extern List *flow_threads;
//extern List *outgoing_msg;
extern Counter *outgoing_msg_counter;

/**************************************************************************************/
/* Purpose : function used to start the smsc connection.                              */
/* Input   : SMSCCONN structure which you want to start.                              */
/**************************************************************************************/

void smscconn_start(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->is_stopped == 0) {
	mutex_unlock(conn->flow_mutex);
	return;
    }
    conn->is_stopped = 0;
    mutex_unlock(conn->flow_mutex);
    
     if (conn->start_conn)
	conn->start_conn(conn);
}


/**************************************************************************************/
/* Purpose : function used to increment the counter for smsc connection.              */
/* Input   : SMSCCONN structure and message.                                          */
/**************************************************************************************/

void smscconn_sent(SMSCConn *conn, Msg *sms,Octstr *smsc_msg_id,SMPP_PDU *pdu)
{
    Octstr *cid;
	counter_increase(outgoing_msg_counter);
	/*
	if (conn && smscconn_id(conn))
    	cid = smscconn_id(conn);
	else if (conn && smscconn_name(conn))
    	cid = smscconn_name(conn);
	else if (msg->sms.smsc_id)
    	cid = msg->sms.smsc_id;
	else
    	cid = octstr_imm("");
	*/
	alog("SMS Sent [SMSC:%s][SMSC_MSGID:%s][ESME:%s][ESME_MSGID:%s][FROM:%s][TO:%s]",
								octstr_get_cstr(conn->id),
								octstr_get_cstr(smsc_msg_id),
								octstr_get_cstr(sms->sm_info->account_name),
								octstr_get_cstr(sms->sm_info->account_msg_id),
								octstr_get_cstr(pdu->u.submit_sm.source_addr),
								octstr_get_cstr(pdu->u.submit_sm.destination_addr)
								);
	msg_destroy(sms);
	if (conn) counter_increase(conn->sent);
}
/**************************************************************************************/
/* Purpose : function used to create smsc connection.                                 */
/* Input   : pointer to cfggroup and variable indicate whether start as a stopped     */
/*	   : or not.                                                                      */
/* Output  : return NULL if smsc field is missing.                                    */
/*	   : return pointer to SMSCCONN if successfully created.                          */	  
/**************************************************************************************/

SMSCConn *smscconn_create(CfgGroup *grp, int start_as_stopped)
{
    SMSCConn *conn;
    Octstr *smsc_type;
	Octstr *allowed_prefix_regex;
    Octstr *denied_prefix_regex;
    Octstr *preferred_prefix_regex;
    int ret;
    Octstr *tmp;
    char data[30];
    if (grp == NULL)
	return NULL;

    conn = gw_malloc(sizeof(*conn));
    memset(conn, 0, sizeof(*conn));
	conn->why_killed = SMSCCONN_ALIVE;
    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = -1;
    conn->is_stopped = start_as_stopped;
    conn->bind_sent = -1;
    conn->received = counter_create();
    conn->sent = counter_create();
    conn->failed = counter_create();
    conn->flow_mutex = mutex_create();

#define GET_OPTIONAL_VAL(x, n) x = cfg_get(grp, octstr_imm(n))
#define SPLIT_OPTIONAL_VAL(x, n) \
        do { \
                Octstr *tmp = cfg_get(grp, octstr_imm(n)); \
                if (tmp) x = octstr_split(tmp, octstr_imm(";")); \
                else x = NULL; \
                octstr_destroy(tmp); \
        }while(0)
	
    GET_OPTIONAL_VAL(conn->id, "smsc-id");
    GET_OPTIONAL_VAL(conn->allowed_prefix, "allowed-prefix");
    GET_OPTIONAL_VAL(conn->denied_prefix, "denied-prefix");
    GET_OPTIONAL_VAL(conn->our_host, "our-host");
    GET_OPTIONAL_VAL(conn->log_file, "log-file");
    cfg_get_bool(&conn->alt_dcs, grp, octstr_imm("alt-dcs"));



	GET_OPTIONAL_VAL(allowed_prefix_regex, "allowed-prefix-regex");
    if (allowed_prefix_regex != NULL) 
        if ((conn->allowed_prefix_regex = gw_regex_comp(allowed_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(allowed_prefix_regex));
    GET_OPTIONAL_VAL(denied_prefix_regex, "denied-prefix-regex");
    if (denied_prefix_regex != NULL) 
        if ((conn->denied_prefix_regex = gw_regex_comp(denied_prefix_regex, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(denied_prefix_regex));
   

	/*if (cfg_get_bool(&conn->deliver_sm_source, grp, octstr_imm("deliver-sm-source")) == -1)
		conn->deliver_sm_source = 1; */
										
    
    if (cfg_get_integer(&conn->log_level, grp, octstr_imm("log-level")) == -1)
        conn->log_level = 0;

    if(conn->log_file != NULL) 
	{
		/*
		Octstr *logfile;
		int index;
		logfile = logfile_append_time(conn->log_file);
		conn->log_idx = log_open(octstr_get_cstr(logfile),conn->log_level, GW_EXCL); 
		octstr_destroy(logfile);
		logfile = log_file_name(conn->log_idx);
		printf("\n\nORIGINAL FILE NAME :%d :%s\n",conn->log_idx,octstr_get_cstr(logfile));
		octstr_destroy(logfile);		
		*/
		
		//printf("\n inside Opening Loop of smsconn_open log_file : %s \n",octstr_get_cstr(conn->log_file));
		debug("smscconn", 0, "smscconn_create: logfilename: %s\n",octstr_get_cstr(conn->log_file));
	
        Octstr *logfile;
		Octstr *logfile1;
        Octstr *templogfile;
        int index;
        logfile = logfile_append_time(conn->log_file);
    
        //[sanchal][230309][Get the latst logfile]
        templogfile = get_latst_logfile(logfile);
        if( templogfile != 0 )
        {
	         //printf("Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(templogfile));
			 info(0, "smscconn_create: Latestlogfilename: %s",octstr_get_cstr(templogfile));
            conn->log_idx = log_open(octstr_get_cstr(templogfile),conn->log_level, GW_EXCL);
        }
        else
        {
            printf("Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(logfile));
			info(0, "smscconn_create: New log filename: %s\n",octstr_get_cstr(logfile));
            conn->log_idx = log_open(octstr_get_cstr(logfile),conn->log_level, GW_EXCL);
        }

        octstr_destroy(logfile);

        logfile1 = log_file_name(conn->log_idx);
        //printf("\n\nORIGINAL FILE NAME :%d :%s\n",conn->log_idx,octstr_get_cstr(logfile1));
		debug("smscconn",0, "smscconn_create: read Log File Name is :%d :%s ",conn->log_idx,octstr_get_cstr(logfile1));
        octstr_destroy(templogfile);
		octstr_destroy(logfile1);
	} 
#undef GET_OPTIONAL_VAL
#undef SPLIT_OPTIONAL_VAL
    
    if (cfg_get_integer(&conn->reconnect_delay, grp,
                        octstr_imm("reconnect-delay")) == -1)
        conn->reconnect_delay = SMSCCONN_RECONNECT_DELAY;

    smsc_type = cfg_get(grp, octstr_imm("smsc"));
    if (smsc_type == NULL) {
        error(0, "Required field 'smsc' missing for smsc group.");
        smscconn_destroy(conn);
        octstr_destroy(smsc_type);
        return NULL;
    }
	
	if (conn->id == NULL) {
        error(0, "Required field 'smsc-id' missing for smsc group.");
        smscconn_destroy(conn);
        octstr_destroy(smsc_type);
        return NULL;
    }

    if (octstr_compare(smsc_type, octstr_imm("smpp")) == 0)
	ret = smsc_smpp_create(conn, grp);
    conn->smsc_type = cfg_get(grp, octstr_imm("smsc-type"));
    if(conn->smsc_type == NULL) {
		error(0, "Required field 'smsc-type' missing for smsc %s.",	
													octstr_get_cstr(conn->id));
        smscconn_destroy(conn);
        octstr_destroy(smsc_type);
        return NULL;
    }

	strcpy(data,octstr_get_cstr(conn->smsc_type));

		
	if((!strcmp(data,"int")||!strcmp(data,"gsm")||
			!strcmp(data,"cdma")||!strcmp(data,"all")) == 0)
	 {
		error(0, "Wrong Value of Field 'smsc-type' for smsc %s.",	
													octstr_get_cstr(conn->id));
        smscconn_destroy(conn);
        octstr_destroy(smsc_type);
        return NULL;
    }
		
    octstr_destroy(smsc_type);
    if (ret == -1) {
        smscconn_destroy(conn);
        return NULL;
    }
    smpp_client_smscconn_ready(conn);

    return conn;
}

/**************************************************************************************/
/* Purpose : function used to find the name of smsc connection.                       */
/* Input   : SMSCCONN structure for which you want to find the name.                  */
/* Output  : return name of smsc-connection.                                          */
/**************************************************************************************/

int smscconn_send(SMSCConn *conn, Msg *msg)
{
    int ret = -1;
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if(conn->status == SMSCCONN_DEAD || conn->why_killed != SMSCCONN_ALIVE) 
	{
        mutex_unlock(conn->flow_mutex);
        return -1;
    }
    
printf("\n\n host is  = %s name is = %s ",octstr_get_cstr(conn->our_host),octstr_get_cstr(conn->name));

	// printf("\n\n\n*************smscconn_send %s\n",octstr_get_cstr(msg->req_time));
	 printf(" \n\n-----------------------INSIDE SMSCONN SEND-------------\n");
	//printf("\n\n host is  = %s name is = %s data = %d ",octstr_get_cstr(conn->our_host),octstr_get_cstr(conn->name),conn_msg_type(msg));



//SMPP *smpp;

  //  smpp = conn->data;
//printf("\n\n username is  = %s apssword is = %s host = %s",octstr_get_cstr(smpp->username),octstr_get_cstr(smpp->password),octstr_get_cstr(smpp->host));

printdataval(conn->data);








	ret = conn->send_msg(conn, msg);
     mutex_unlock(conn->flow_mutex);
	printf("\n\n return value is  = %d",ret);
    return ret;
}

/**************************************************************************************/
/* Purpose : function used to check smscconnection is usable or not.                  */
/* Input   : SMSCCONN structure and pointer to structure of type forward queue data.  */
/* Output  : return 0 if smsc-connection is usable or not.                            */
/*         : return -1 if smsc-connection is not usable.                              */
/**************************************************************************************/

int smscconn_usable(SMSCConn *conn,struct forward_queue_data *msg)
{
	Octstr * receiver = NULL;
    gw_assert(conn != NULL);
    gw_assert(msg != NULL);
	receiver = octstr_create(msg->destination_addr);
    if (conn->status == SMSCCONN_DEAD || conn->why_killed != SMSCCONN_ALIVE)
	return -1;
	
	
	if (conn->allowed_prefix && ! conn->denied_prefix && 
     								  (does_prefix_match(conn->allowed_prefix, receiver) != 1))
		{
			octstr_destroy(receiver);
	   		return -1;
  		}
    if (conn->denied_prefix && ! conn->allowed_prefix &&
       										(does_prefix_match(conn->denied_prefix,receiver) == 1))
		{
		debug("smscconn", 0, "Message with destination = %s, is rejected,due to denied prefix set for \
				Series = %s\n",octstr_get_cstr(receiver),octstr_get_cstr(conn->denied_prefix));
			octstr_destroy(receiver);
	        return -1;
		}
    if (conn->denied_prefix_regex && ! conn->allowed_prefix_regex) {
        if (gw_regex_match_pre(conn->denied_prefix_regex, receiver) == 1) {
			octstr_destroy(receiver);
            return -1;
    		}
	}
    
	 if (conn->denied_prefix && conn->allowed_prefix &&
       			(does_prefix_match(conn->allowed_prefix,receiver) == 1) &&
      							 (does_prefix_match(conn->denied_prefix, receiver) == 1) ) {
	warning(0, "For Destination = %s, both Allowed and Denied Prifix is set,Ignoring Message \
											for SMSC %s",octstr_get_cstr(receiver),octstr_get_cstr(conn->id));
			octstr_destroy(receiver);
			return -1;
	}
	
    if (conn->denied_prefix && conn->allowed_prefix &&
       			(does_prefix_match(conn->allowed_prefix,receiver) != 1) &&
      							 (does_prefix_match(conn->denied_prefix, receiver) == 1) ) {
			octstr_destroy(receiver);
			return -1;
	}
    if (conn->allowed_prefix_regex && conn->denied_prefix_regex) {
        if (gw_regex_match_pre(conn->allowed_prefix_regex, receiver) == 0 &&
            gw_regex_match_pre(conn->denied_prefix_regex, receiver) == 1) {
			octstr_destroy(receiver);
            return -1;
			}
    }
        
  if(receiver != NULL)		
  			octstr_destroy(receiver);   
  return 0;
}

/**************************************************************************************/
/* Purpose : function used to find the info. for particular smsc connection.          */
/* Input   : SMSCCONN structure and address of statusinfo type struct variable.       */
/*         : it stores the smsc related information in to that address.               */
/* Output  : return name of smsc-connection.                                          */
/**************************************************************************************/

int smscconn_info(SMSCConn *conn, StatusInfo *infotable)
{
    if (conn == NULL || infotable == NULL)
	return -1;

    mutex_lock(conn->flow_mutex);

    infotable->status = conn->status;
    infotable->mode = conn->mode;
	infotable->killed = conn->why_killed;
    infotable->is_stopped = conn->is_stopped;
    infotable->online = time(NULL) - conn->connect_time;
    
    infotable->sent = counter_value(conn->sent);
    infotable->received = counter_value(conn->received);
    infotable->failed = counter_value(conn->failed);

    if (conn->queued)
	infotable->queued = conn->queued(conn);
    else
	infotable->queued = -1;
	
    if(conn->waiting)
		infotable->waiting = conn->waiting(conn);
	
	infotable->load = conn->load;
    mutex_unlock(conn->flow_mutex);

    return 0;
}
void reset_smpp_counters(SMSCConn *conn)
{
	if(conn)
	{
		counter_set(conn->sent,0);
		counter_set(conn->received,0);
		counter_set(conn->failed,0);
	}
}
/*****************************************************************************************/
/* Purpose : fucntion used when there is a problem to send the message to smsc.          */
/*	       : checks max. no of retries for this message and if it reaches then finally   */
/*	       : it discards otherwise if connection is alive then it will store the message */
/*         : to outgoing_msg queue.                                                      */
/* Input   : SMSCCONN structure and message and reason.                                  */
/*****************************************************************************************/

void smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason,SMPP_PDU *pdu)
{  
    
	if(pdu != NULL)
	{
		alog("SMS FAILED [SMSC:%s][ERROR:%s][ESME:%s][ESME_MSGID:%s][FROM:%s][TO:%s]",
								octstr_get_cstr(conn->id),
								smpp_error_to_string(reason),
								octstr_get_cstr(sms->sm_info->account_name),
								octstr_get_cstr(sms->sm_info->account_msg_id),
								octstr_get_cstr(pdu->u.submit_sm.source_addr),
								octstr_get_cstr(pdu->u.submit_sm.destination_addr)
								);
	}
	else if(sms->sm_info->account_name != NULL && sms->sm_info->account_msg_id != NULL)
	{
		alog("SMS FAILED [SMSC:%s][REASON:%d][ACCNAME:%s][ACCNTMSGID:%s]",
			octstr_get_cstr(conn->id),
			reason,
			octstr_get_cstr(sms->sm_info->account_name),
			octstr_get_cstr(sms->sm_info->account_msg_id)
			);
	}
	else
	{
		alog("SMS FAILED [SMSC:%s][REASON:%d]", octstr_get_cstr(conn->id), reason);
	}
	switch (reason) {
    case SMSCCONN_FAILED_TEMPORARILY:
          if(conn && smscconn_status(conn) == SMSCCONN_ACTIVE) 
		  {
              /*
			  if (!sms->retries) 
			  {
           	  	warning(0, "Maximum retries for message exceeded, discarding it!");
               	  smscconn_send_failed(NULL, sms, SMSCCONN_FAILED_DISCARDED,NULL);
               	  break;
           	  }
			  sms->retries --;
			 */
		}
		if(sms->msg_retry_time == 0)
			sms->msg_retry_time = time(NULL);
		sms->failed_smsc_id = octstr_duplicate(conn->id);
		//gwlist_produce(outgoing_msg, sms);
		produce_outgoing_msg(sms);
	   
	   /*testing -- open
	   Octstr *tmp;
	   SMPP_PDU *pdu_new;
	   tmp = octstr_from_position(sms->sm_info->pdu,4);
	   pdu_new = smpp_pdu_unpack(tmp);
	   octstr_destroy(tmp);
	   printf("\nPRODUCING IN THE QUEUE---1:%s::%d \n",octstr_get_cstr(pdu_new->u.submit_sm.short_message),sms->retries);
	   smpp_pdu_destroy(pdu_new);
	   testing -- close*/
		
       break;
  
    case SMSCCONN_FAILED_SHUTDOWN:
		//printf("\nPRODUCING IN THE QUEUE----2\n");
		if(sms->msg_retry_time == 0)
			sms->msg_retry_time = time(NULL);
		sms->failed_smsc_id = octstr_duplicate(conn->id);
        //gwlist_produce(outgoing_msg, sms);
		produce_outgoing_msg(sms);
        break;
	
    case SMSCCONN_FAILED_DISCARDED:
	 msg_destroy(sms);
	 if (conn) counter_increase(conn->failed);
         break;
	
    default:
	 msg_destroy(sms);
	 if (conn) counter_increase(conn->failed);
    }
}

/**************************************************************************************/
/* Purpose : function used to find the name of smsc connection.                       */
/* Input   : SMSCCONN structure for which you want to find the name.                  */
/* Output  : return name of smsc-connection.                                          */
/**************************************************************************************/

const Octstr *smscconn_name(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    return conn->name;
}

/**************************************************************************************/
/* Purpose : function used to find the smsc connection id.                            */
/* Input   : SMSCCONN structure for which you want to get the id.                     */
/* Output  : return id of smsc connection.                                            */
/**************************************************************************************/

const Octstr *smscconn_id(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    return conn->id;
}

/**************************************************************************************/
/* Purpose : function used to get the status of smsc-connection.                      */
/* Input   : SMSCCONN structure for which you want to find the status.                */
/* Output  : status of smsc connection as requested.                                  */
/**************************************************************************************/

int smscconn_status(SMSCConn *conn)
{
    gw_assert(conn != NULL);

    return conn->status;
}

/**************************************************************************************/
/* Purpose : function used to shut down the smsc connection.                          */
/* Input   : SMSCCONN structure which you want to destroy and variable                */
/**************************************************************************************/

void smscconn_shutdown(SMSCConn *conn, int finish_sending)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD) {
	mutex_unlock(conn->flow_mutex);
	return;
    }   
    if (conn->shutdown) {
        mutex_unlock(conn->flow_mutex);
	conn->shutdown(conn, finish_sending);
    }
    else {
	conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
        mutex_unlock(conn->flow_mutex);
    }
    return;
}

/**************************************************************************************/
/* Purpose : function used to stop the smsc connection.                               */
/* Input   : SMSCCONN structure which you want to stop.                               */
/* Output  : return 0 if successfully stoped the smsc connection.                     */
/*         : return -1 if smsc connection is dead or already stoped.                  */
/**************************************************************************************/

int smscconn_stop(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->is_stopped != 0
															|| conn->why_killed != SMSCCONN_ALIVE)
    {
	mutex_unlock(conn->flow_mutex);
	return -1;
    }
    conn->is_stopped = 1;
    mutex_unlock(conn->flow_mutex);

    if (conn->stop_conn)
	conn->stop_conn(conn);

    return 0;
}

/**************************************************************************************/
/* Purpose : function used to destroy the smsc connection.                            */
/* Input   : SMSCCONN structure which you want to destroy.                            */
/* Output  : return 0 if successfully destroyed or if conn is NULL.                   */
/*         : return -1 if SMSC Connection is dead.                                    */
/**************************************************************************************/

int smscconn_destroy(SMSCConn *conn)
{
    if (conn == NULL)
	return 0;
    if (conn->status != SMSCCONN_DEAD)
	return -1;
    mutex_lock(conn->flow_mutex);

    counter_destroy(conn->received);
    counter_destroy(conn->sent);
    counter_destroy(conn->failed);
    octstr_destroy(conn->name);
    octstr_destroy(conn->id);
   	octstr_destroy(conn->denied_prefix);
    octstr_destroy(conn->allowed_prefix);
    if (conn->denied_prefix_regex != NULL) 
								 gw_regex_destroy(conn->denied_prefix_regex);
    if (conn->allowed_prefix_regex != NULL)
								 gw_regex_destroy(conn->allowed_prefix_regex);

    octstr_destroy(conn->our_host);
    octstr_destroy(conn->log_file);
	octstr_destroy(conn->smsc_type);
    mutex_unlock(conn->flow_mutex);
    mutex_destroy(conn->flow_mutex);

    gw_free(conn);
    return 0;
}
void smscconn_change_logfile(SMSCConn *conn)
{
	int ret;
	Octstr *log_name;
	gw_assert(conn != NULL);
	ret = change_logfile_name(conn->log_idx);
	if(ret == 0)
	{
		log_name = log_file_name(conn->log_idx);
		//info(0, "Log file changed successfully:%s ",octstr_get_cstr(log_name));
		octstr_destroy(log_name);
	}
	return;
}

//[sanchal][230309][Splitting logs in a single when it reaches configurable limit]
void smscconn_change_logfile_onsize(SMSCConn *conn, long iLogSize)
{
    int ret;
    Octstr *log_name;
    gw_assert(conn != NULL);

    //printf("\nsmscconn.c : smscconn_change_logfile_onsize: begining\n");
	//debug("smscconn",0, " smscconn_change_logfile_onsize: begining");

    ret = change_logfile_name_onsize(conn->log_idx, iLogSize);
    if(ret == 0)
    {
        log_name = log_file_name(conn->log_idx);
        //info(0, "Log file changed successfully:%s ",octstr_get_cstr(log_name));
        octstr_destroy(log_name);
    }

    //printf("\nsmscconn.c : smscconn_change_logfile_onsize: end\n");
	//debug("smscconn",0, " smscconn_change_logfile_onsize: end");

    return;
}


/**************************************************************************************/     
/*		       	End of file smscconn.c                                                */
/**************************************************************************************/
