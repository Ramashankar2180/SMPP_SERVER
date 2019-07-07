/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smsc.h                                                           */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains prototype declaration of functions used       */
/*		: in file smsc.c                                                   */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#ifndef SMSC_H
#define SMSC_H

#include "msg.h"


/**************************************************************************************/
/* Prototype declaration of functions used in "smsc.c" file                           */
/**************************************************************************************/


int start2_smsc(Cfg *cfg);

int smsc2_start(Cfg *config);

int smsc2_restart(Cfg *config);

void smsc2_suspend(void);   

void smsc2_resume(void);    

int smsc2_shutdown(void);

void smsc2_cleanup(void); 

Octstr *smsc2_status(int status_type);

int smsc2_stop_smsc(Octstr *id);   

int smsc2_restart_smsc(Octstr *id);  

void send_terminate_info_server(void);

void change_smsc_logfile(void);

void wake_router_thread(void);
void wake_sys_thread(void);

void produce_outgoing_msg(Msg *msg);
#endif


/*****************************************************************************************/     
/*		       	End of file smsc.h                                               */
/*****************************************************************************************/
