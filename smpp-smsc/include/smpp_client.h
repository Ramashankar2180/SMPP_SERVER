/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smpp_client.h                                                    */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains enum declaration of variables used in in      */
/*		: other files.                                                     */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#ifndef SMPP_CLIENT_H
#define SMPP_CLIENT_H

#include "gwlib/gwlib.h"
#include "gwlib/process_sock.h"
#include "gwlib/smpp_pdu.h"
/* general SMPP-Client state */

enum 
{
    CLIENT_RUNNING = 0,
    CLIENT_SUSPENDED = 1,	
    CLIENT_SHUTDOWN = 2,
    CLIENT_DEAD = 3,
    CLIENT_FULL = 4        
};


/* type of output given by various status functions */
enum 
{
    SMPPCLIENTSTATUS_HTML = 0,
    SMPPCLIENTSTATUS_TEXT = 1,
    SMPPCLIENTSTATUS_WML = 2,
    SMPPCLIENTSTATUS_XML = 3
};


/**************************************************************************************/
/* Prototype declaration of functions used in "smpp_client.h" file                    */
/**************************************************************************************/

Octstr *smpp_client_print_status(int status_type);
char *smpp_client_status_linebreak(int status_type);
int smpp_client_shutdown(void);
int send_msg_dbbox_server(Conn_Msg *msg,int mode);
void handle_request(Conn_Msg *msg,PROCESS_CLIENT *process_client);
int mis_db_insert(  Octstr *account_name,
                    Octstr *account_msg_id,
                    Octstr *req_time,
                    Octstr *smsc_id,
                    Octstr *smsc_msg_id,
                    SMPP_PDU *pdu,
                    long submit_status,
                    Octstr *IP);

#endif

/*****************************************************************************************/     
/*		       	End of file smpp_client.h                                        */
/*****************************************************************************************/

