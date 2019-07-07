/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2005 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * gw/dlr_p.h
 *
 * Implementation of handling delivery reports (DLRs)
 * These are private header.
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <amalysh@centrium.de>
 */
#include "gwlib/common.h"
#ifndef	DLR_P_H
#define	DLR_P_H 1

#include "gwlib/process_sock.h"

#define DLR_TRACE 1


struct dlr_entry
{
	Octstr *account_name;
	Octstr *account_msg_id;
	Octstr *smsc_id;
	Octstr *smsc_msg_id;
	Octstr *msg_submit_time;	

};

struct dlr_get_deliver_info
{
	Octstr *account_name;
	Octstr *account_msg_id;
};

struct dlr_db_fields
{
	Octstr *table;
	Octstr *field_account_name;
	Octstr *field_account_msg_id;
	Octstr *field_smsc_id;
	Octstr *field_smsc_msg_id;
	Octstr *field_msg_submit_time; 

};

struct dlr_db_mis_fields
{
	Octstr *table;
	Octstr *field_account_name;
	Octstr *field_account_msg_id;
	Octstr *field_smsc_id;
	Octstr *field_smsc_msg_id;
	Octstr *field_submit_status;
	Octstr *field_submit_time; 
	Octstr *field_source_addr;
	Octstr *field_dest_addr;
	Octstr *field_recv_time;
	Octstr *field_msg_content;	
	Octstr *field_udh;	
	Octstr *field_retry_value;
	Octstr *field_Client_IP;
};
struct dlr_db_smpp_esme_fields
{
	Octstr *table;
	Octstr *field_account_name;
	Octstr *field_account_msg_id;
	Octstr *field_source_addr;
	Octstr *field_dest_addr;
	Octstr *field_submit_status;
	Octstr *field_recv_time;
	Octstr *field_msg_content;	
	Octstr *field_udh;	
	Octstr *field_retry_value;
	Octstr *field_dnd_flag;	
	Octstr *allowed_smsc;	
	Octstr *preferred_smsc;
	Octstr *field_data_coding;
	Octstr *field_more_messages_to_send;
	Octstr *field_esm_class;
};
struct dlr_db_status_fields
{
	Octstr *table;
	Octstr *field_smsc_id;
	Octstr *field_smsc_msg_id;
	Octstr *field_dest_addr;
	Octstr *field_deliver_status;
	Octstr *field_deliver_time;
};
/*
 * Create struct dlr_entry and initialize it to zero
 */
struct dlr_entry *dlr_entry_create(void);

/*
 * Destroy struct dlr_entry
 */
void dlr_entry_destroy(struct dlr_entry *dlr);

/*
 * Duplicate dlr entry
 */
struct dlr_entry *dlr_entry_duplicate(const struct dlr_entry *dlr);

/* 
 * Callback functions to hanlde specifical dlr storage type 
 */
struct dlr_storage 
{

	const char* type;/*Type of storage. Used for status reguest.*/

	void (*dlr_add) (struct dlr_entry *entry);

	//void (*dlr_update)(const Octstr *smsc_id,int seqnum,const Octstr *smscmsgid);

	Conn_Msg* (*dlr_get)(const Octstr *smsc_id,const Octstr *smscmsgid);

	void (*dlr_remove)(const Octstr *smsc_id,const Octstr *smscmsgid);

	void (*dlr_mis_add)(Conn_Msg *msg);

	void (*dlr_status_add)(Conn_Msg *msg);

	void (*dlr_smpp_esme)(Conn_Msg *msg);

	long (*dlr_messages) (void);/*Return count dlr entries in storage.*/

	void (*dlr_flush) (void);/*Flush storage*/

	void (*dlr_shutdown) (void);/*Shutdown storage*/    
	void (*dlr_check_conn) (void);    
};



struct dlr_storage *dlr_init_mem(Cfg *cfg);
struct dlr_storage *dlr_init_mysql(Cfg *cfg);
struct dlr_storage *dlr_init_sdb(Cfg *cfg);
struct dlr_storage *dlr_init_oracle(Cfg *cfg);
struct dlr_storage *dlr_init_pgsql(Cfg *cfg);



struct dlr_db_fields *dlr_db_fields_create(CfgGroup *grp);
void dlr_db_fields_destroy(struct dlr_db_fields *fields);

struct dlr_db_mis_fields *dlr_db_fields_create_mis(CfgGroup *grp);
void dlr_db_fields_destroy_mis(struct dlr_db_mis_fields *fields);

struct dlr_db_status_fields *dlr_db_fields_create_status(CfgGroup *grp);
void dlr_db_fields_destroy_status(struct dlr_db_status_fields *fields);


#endif /* DLR_P_H */
