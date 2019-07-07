/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smscconn_p.h                                                     */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : SMSC                                                             */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#ifndef SMSCCONN_P_H
#define SMSCCONN_P_H

#include <signal.h>
#include "gwlib/gwlib.h"
#include "gwlib/regex.h"
#include "smscconn.h"
#include "msg.h"

struct smscconn {
    
    smscconn_status_t status;		
    int 	load;	
	int bind_sent;     
    smscconn_killed_t why_killed;				
    time_t 	connect_time;	
    Mutex 	*flow_mutex;	
    Counter *received;
    Counter *sent;
    Counter *failed;
    volatile sig_atomic_t 	is_stopped;
    Octstr *name;		
    Octstr *id;			
    Octstr *our_host;   /* local device IP to bind for TCP communication */
    /* Our smsc specific log-file data */
    Octstr *log_file;
    long log_level;
    int log_idx;    /* index position within the global logfiles[] array in gwlib/log.c */
	Octstr *allowed_prefix;
    regex_t *allowed_prefix_regex;
    Octstr *denied_prefix;
    regex_t *denied_prefix_regex;
    long reconnect_delay; /* delay in seconds while re-connect attempts */

    int alt_dcs; /* use alternate DCS 0xFX */
    int (*shutdown) (SMSCConn *conn, int finish_sending);
    int (*send_msg) (SMSCConn *conn, Msg *msg);
    long (*queued) (SMSCConn *conn);
    int (*waiting)(SMSCConn *conn);
	void (*start_conn) (SMSCConn *conn);
    void (*stop_conn) (SMSCConn *conn);
	Octstr *smsc_type;
    void *data;			/* SMSC specific stuff */
	int mode;/*mode of SMSC 1-TX only 2-RX 3-TRX*/
	long max_q_len;
};


int smsc_smpp_create(SMSCConn *conn, CfgGroup *cfg);
#endif
