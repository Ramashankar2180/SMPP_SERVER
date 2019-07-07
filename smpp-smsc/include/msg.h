/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : msg.h                                                            */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains structure of receive message what we receive  */
/*		: through message queue and prototype declaration of function      */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#ifndef MSG_H
#define MSG_H

#include "gwlib/msgq.h"
#include "gwlib/smpp_pdu.h"


/*Not routed, routed but not sent, sent but no resp, sent and recv resp*/
typedef enum
{
	MSG_NOT_ROUTED = 0,
	MSG_NOT_SENT,
	MSG_SENT_NORESP,
	MSG_SENT_RESP
}msg_state;

/***********************************************************************************/
/* structure template for sending user message                                     */
/***********************************************************************************/

struct info
{
	Octstr *pdu;
	Octstr *account_name;
	Octstr *account_msg_id;
	Octstr *req_time;				
	Octstr *client_ip;
	Octstr *content;				
};

typedef struct recv_msg
{
	struct info *sm_info;
	struct forward_queue_data *routing_info;
	int retries;/*no of times we should try to route the message again*/
	int msg_state;/*Not routed, routed but not sent, sent but no resp, sent and recv resp*/
	long msg_retry_time;/*when the message is kept in the queue*/
	Octstr *failed_smsc_id;/*smsc where the sending failed*/
}Msg;

/************************************************************************************/
/* Prototype declaration of functions used in "msg.c" file                          */
/************************************************************************************/
void info_destory(struct info * q);
Msg * msg_duplicate(Msg * msg);
void msg_destroy(void *message);
Msg *msg_create();
char *lc(char *str);
char *uc(char *str);
#endif

/********************************************************************/     
/*		       	End of file msg.h                           */
/********************************************************************/
