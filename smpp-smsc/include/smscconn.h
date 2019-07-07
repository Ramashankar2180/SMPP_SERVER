/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smscconn.h                                                       */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains enum declaration and prototype declaration    */
/*		: of the functions                                                 */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#ifndef SMSCCONN_H
#define SMSCCONN_H

#include "gwlib/gwlib.h"
#include "msg.h"
#include "gwlib/smpp_pdu.h"

/********************************************************************************/
/* structure template for smsc_state and enum declarations of variables         */
/********************************************************************************/

typedef struct smscconn SMSCConn;

enum {
    SMSCCONN_SUCCESS = 0,
    SMSCCONN_FAILED_SHUTDOWN,
    SMSCCONN_FAILED_REJECTED,
    SMSCCONN_FAILED_MALFORMED,
    SMSCCONN_FAILED_TEMPORARILY,
    SMSCCONN_FAILED_DISCARDED,
    SMSCCONN_FAILED_QFULL	
};

typedef enum {
    SMSCCONN_CONNECTING,
    SMSCCONN_ACTIVE,
    SMSCCONN_ACTIVE_RECV,
    SMSCCONN_RECONNECTING,
    SMSCCONN_DISCONNECTED,
    SMSCCONN_DEAD	
} smscconn_status_t;

typedef enum {
    SMSCCONN_ALIVE = 0,
    SMSCCONN_KILLED_WRONG_PASSWORD = 1,
    SMSCCONN_KILLED_CANNOT_CONNECT = 2,
    SMSCCONN_KILLED_SHUTDOWN = 3
} smscconn_killed_t;




typedef struct smsc_state {
    smscconn_status_t status;	
    smscconn_killed_t killed;	
    int is_stopped;	
    unsigned long received;	
    unsigned long sent;		
    unsigned long failed;	
    long queued;	
	int waiting;
    long online;	
    int load;		
	int mode;
} StatusInfo;


/**************************************************************************************/
/* Prototype declaration of functions used in "smscconn.c" file                       */
/**************************************************************************************/

SMSCConn *smscconn_create(CfgGroup *cfg, int start_as_stopped);

void smscconn_shutdown(SMSCConn *smscconn, int finish_sending);

int smscconn_destroy(SMSCConn *smscconn);

int smscconn_stop(SMSCConn *smscconn);

void smscconn_start(SMSCConn *smscconn);

const Octstr *smscconn_name(SMSCConn *smscconn);

const Octstr *smscconn_id(SMSCConn *conn);

int smscconn_usable(SMSCConn *conn,struct forward_queue_data *msg);

int smscconn_send(SMSCConn *conn, Msg *msg);

int smscconn_status(SMSCConn *smscconn);

int smscconn_info(SMSCConn *smscconn, StatusInfo *infotable);

void smpp_client_smscconn_ready(SMSCConn *conn);

void smpp_client_smscconn_connected(SMSCConn *conn);

void smpp_client_smscconn_killed(void);

void smscconn_sent(SMSCConn *conn, Msg *sms,Octstr *smsc_msg_id,SMPP_PDU *pdu);

void smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason,SMPP_PDU *pdu);


#endif

/*****************************************************************************************/     
/*		       	End of file smscconn.h                                        */
/*****************************************************************************************/
