/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : msg.c                                                            */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains function used to destroy msg structure        */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#include "include/msg.h"


Msg *msg_create()
{
	Msg *msg;
	
	msg = gw_malloc(sizeof(Msg));
 	gw_assert(msg != NULL);
	
	msg->msg_state = MSG_NOT_ROUTED;/*Not sent*/
	msg->retries = 0;
	msg->msg_retry_time = 0;
	msg->failed_smsc_id = NULL;
	
	msg->sm_info = gw_malloc(sizeof(struct info));
	msg->sm_info->pdu = NULL;
	msg->sm_info->account_name = NULL;
	msg->sm_info->account_msg_id = NULL;
	msg->sm_info->req_time = NULL;
	msg->sm_info->client_ip = NULL;
	
	msg->routing_info = gw_malloc(sizeof(struct forward_queue_data));
  	gw_assert(msg->routing_info != NULL);
	return msg;
}

/**************************************************************************************/
/* Purpose : function used to destroy the message structre.                           */
/* Input   : Message structre which one you want to destroy.                          */
/**************************************************************************************/


void msg_destroy(void  *message)
{
	Msg *os = (Msg*)message;

	if(os == NULL)
		return;
	
	if(os->sm_info != NULL)
	{
		if(os->sm_info->pdu != NULL)
			octstr_destroy(os->sm_info->pdu);
		if(os->sm_info->account_name != NULL)
			octstr_destroy(os->sm_info->account_name);
		if(os->sm_info->account_msg_id != NULL)
			octstr_destroy(os->sm_info->account_msg_id);
		
		if(os->sm_info->req_time != NULL)
			octstr_destroy(os->sm_info->req_time);
		
		gw_free(os->sm_info);
	}
	if(os->routing_info != NULL)
		gw_free(os->routing_info);
	if(os->failed_smsc_id != NULL)
		gw_free(os->failed_smsc_id);
	
	if(os != NULL)
		gw_free(os);
	
}

/**************************************************************************************/
/* Purpose : function used to duplicate the message structre.                         */
/* Input   : Message structre which one you want to duplicate                         */
/**************************************************************************************/

Msg* msg_duplicate(Msg * msg)
{

	Msg *new_msg;
		
	new_msg = msg_create();
	
	new_msg->sm_info->pdu = octstr_duplicate(msg->sm_info->pdu);
	new_msg->sm_info->account_name = octstr_duplicate(msg->sm_info->account_name);
	new_msg->sm_info->account_msg_id = octstr_duplicate(msg->sm_info->account_msg_id);
	new_msg->sm_info->req_time = octstr_duplicate(msg->sm_info->req_time);
	new_msg->sm_info->client_ip = octstr_duplicate(msg->sm_info->client_ip);
	memcpy(new_msg->routing_info,msg->routing_info,sizeof(struct forward_queue_data));
	
	new_msg->msg_state = msg->msg_state;
	new_msg->retries = msg->retries;
	new_msg->msg_retry_time = msg->msg_retry_time;
	if(msg->failed_smsc_id != NULL)
		new_msg->failed_smsc_id = octstr_duplicate(msg->failed_smsc_id);
	
	return new_msg;
}

/**************************************************************************************/
/* Purpose : function used to destroy the message structre.                           */
/* Input   : Message structre which one you want to destroy.                          */
/**************************************************************************************/


void info_destroy(struct info * q)
{
if(q == NULL) return;

if(q->pdu != NULL)
	octstr_destroy(q->pdu);

if(q->account_name != NULL)
	octstr_destroy(q->account_name);

if(q->account_msg_id != NULL)
	octstr_destroy(q->account_msg_id);

gw_free(q);
}


/**************************************************************************************/
/* Purpose : function used to convert the string to lower case.                       */
/* Input   : pointer to character string.                                             */
/**************************************************************************************/

char *lc(char *str)
{
   char *t;

   for (t = str; *t; t++)
   {
      /* Convert to lowercase */
      (*t) = tolower(*t);
   }

   /* Done */
   return (str);
}


/**************************************************************************************/
/* Purpose : function used to convert the string to upper case.                       */
/* Input   : pointer to character string.                                             */
/**************************************************************************************/

char *uc(char *str)
{
   char *t;

   for (t = str; *t; t++)
   {
      /* Convert to uppercase */
      (*t) = toupper(*t);
   }

   /* Done */
   return (str);
}

/********************************************************************/     
/*		       	End of file msg.c                           */
/********************************************************************/
