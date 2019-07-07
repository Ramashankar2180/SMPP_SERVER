/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : shared.h                                                         */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains prototype declarations of functions           */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/

#ifndef SHARED_H
#define SHARED_H
#include "gwlib/gwlib.h"

#define INFINITE_TIME -1

Octstr *version_smpp_client_report_string(const char *boxname);

void report_smpp_client_versions(const char *boxname);

#endif


/*******************************************************************************/     
/*		       	End of file shared.h                                           */
/*******************************************************************************/





