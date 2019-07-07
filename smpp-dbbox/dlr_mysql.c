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
 * dlr_mysql.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for MySql database
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
 */
#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"
//#include "dlr.h"
#include "gwlib/process_sock.h"
#ifdef HAVE_MYSQL
#include <mysql/mysql.h>
#include "dlr.h"
#include <stdlib.h>

/*
 * Our connection pool to mysql.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */

extern PROCESS_CLIENT *esme_process_client;
static struct dlr_db_fields *fields = NULL;
static struct dlr_db_mis_fields *fields_mis = NULL;
static struct dlr_db_status_fields *fields_status = NULL;
static struct dlr_db_smpp_esme_fields *fields_smpp_esme = NULL;
//static Semaphore *sem_update_allowed;
extern Counter *dnd_msg_counter;
extern PROCESS_CLIENT *dbdata_client;
static void mysql_update(const Octstr *sql)
{
	int	state;
	DBPoolConn *pc;


#if defined(DLR_TRACE)
	debug("dlr.mysql", 0, "--------QUERY RECEIVED IS sql: %s", octstr_get_cstr(sql));
#endif

	pc = dbpool_conn_consume(pool);
	if (pc == NULL) {
		error(0, "MYSQL: Database pool got no connection! DB update failed!");
		return;
	}
	state = mysql_query(pc->conn, octstr_get_cstr(sql));
	if (state != 0)
		error(0, "MYSQL: %s", mysql_error(pc->conn));

	dbpool_conn_produce(pc);
}

static MYSQL_RES* mysql_select(const Octstr *sql)
{
	int	state;
	MYSQL_RES *result = NULL;
	DBPoolConn *pc;

#if defined(DLR_TRACE)
	debug("dlr.mysql", 0, "--------QUERY RECEIVED IS sql: %s", octstr_get_cstr(sql));
#endif

	pc = dbpool_conn_consume(pool);
	if (pc == NULL) {
		error(0, "MYSQL: Database pool got no connection! DB update failed!");
		return NULL;
	}
	state = mysql_query(pc->conn, octstr_get_cstr(sql));
	if (state != 0) {
		error(0, "MYSQL: %s", mysql_error(pc->conn));
	} else {
		result = mysql_store_result(pc->conn);
	}

	dbpool_conn_produce(pc);

	return result;
}


long mysql_update_counter(const Octstr *sql)
{
	int	state;

	DBPoolConn *pc;

#if defined(DLR_TRACE)
	debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

	pc = dbpool_conn_consume(pool);
	if (pc == NULL) {
		error(0, "MYSQL: Database pool got no connection! DB update failed!");
		return -1;
	}

	state = mysql_query(pc->conn, octstr_get_cstr(sql));
	if (state != 0) 
	{
		error(0, "MYSQL: %s", mysql_error(pc->conn));
	} 

	dbpool_conn_produce(pc);

	return mysql_affected_rows(pc->conn);
}
static void dlr_mysql_shutdown()
{

	dbpool_destroy(pool);
	dlr_db_fields_destroy(fields);
	dlr_db_fields_destroy_mis(fields_mis);
	dlr_db_fields_destroy_status(fields_status);

}
char* mysql_escape(const char *from_str)
{

	char *csstring;
	unsigned int uilen;
	//printf("\nInside mysql_escape : %s\n",octstr_get_cstr(from_str));
	uilen = strlen(from_str) * 2 +1;
	csstring = gw_malloc(uilen);
	gw_assert(csstring != NULL);


	DBPoolConn *pc;

	pc = dbpool_conn_consume(pool);
	if (pc == NULL) {
		error(0, "MYSQL: mysql_escape : Database pool got no connection! DB update failed!");
		return;
	}


	uilen = mysql_real_escape_string(pc->conn,csstring,from_str,strlen(from_str));

	dbpool_conn_produce(pc);

	return csstring;
}

static void dlr_mysql_remove(const Octstr *smsc_id,const Octstr *smscmsgid)
{
	Octstr *sql;


	debug("dlr.mysql", 0, "removing DLR from database");
	sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' LIMIT 1;",
			octstr_get_cstr(fields->table), 
			octstr_get_cstr(fields->field_smsc_id),octstr_get_cstr(smsc_id),
			octstr_get_cstr(fields->field_smsc_msg_id), octstr_get_cstr(smscmsgid));


	mysql_update(sql);
	octstr_destroy(sql);
}

static long dlr_mysql_messages(void)
{
	Octstr *sql;
	long res;
	MYSQL_RES *result;
	MYSQL_ROW row;

	sql = octstr_format("SELECT count(*) FROM %s;", octstr_get_cstr(fields->table));

	result = mysql_select(sql);
	octstr_destroy(sql);

	if (result == NULL) {
		return -1;
	}
	if (mysql_num_rows(result) < 1) {
		debug("dlr.mysql", 0, "Could not get count of DLR table");
		mysql_free_result(result);
		return 0;
	}
	row = mysql_fetch_row(result);
	if (row == NULL) {
		debug("dlr.mysql", 0, "rows found but could not load them");
		mysql_free_result(result);
		return 0;
	}
	res = atol(row[0]);
	mysql_free_result(result);

	return res;
}

static void dlr_mysql_flush(void)
{
	Octstr *sql;


	sql = octstr_format("DELETE FROM %s;", octstr_get_cstr(fields->table));

	mysql_update(sql);
	octstr_destroy(sql);
}



void dlr_mysql_add(struct dlr_entry *entry)
{


	Octstr *sql;
	char table_name[50];
	char number_string[10];

	printf("\ndlr mysql add TABLE NAME IS :%s\n",octstr_get_cstr(fields->table));

	sql = octstr_format("INSERT INTO %s" 
			"(%s, %s, %s, %s, %s) VALUES "
			"('%s','%s','%s','%s',now());",

			octstr_get_cstr(fields->table), 
			octstr_get_cstr(fields->field_account_name),
			octstr_get_cstr(fields->field_account_msg_id),
			octstr_get_cstr(fields->field_smsc_id),
			octstr_get_cstr(fields->field_smsc_msg_id),
			octstr_get_cstr(fields->field_msg_submit_time),

			octstr_get_cstr(entry->account_name),
			octstr_get_cstr(entry->account_msg_id), 
			octstr_get_cstr(entry->smsc_id),
			octstr_get_cstr(entry->smsc_msg_id),
			octstr_get_cstr(entry->msg_submit_time)
			);


	debug("dlr.mysql", 0, "DLR MYSQL ADD %s", octstr_get_cstr(sql));
	mysql_update(sql);

	octstr_destroy(sql);
	dlr_entry_destroy(entry);

}

void thrput_get( void )
{
	Octstr *sql;
	char IP[2022]="";
	MYSQL_RES *result;
	MYSQL_ROW row;
	struct tm ptr;
	time_t tm;
	char buff[20];
	char buff1[20];

	FILE *fptr;
	FILE *fptr1;
	char *file_name="/tmp/TPS/esme_tps.txt"; 



	while(1)
	{
		tm = time(NULL);
		ptr = gw_localtime(tm);

		gw_strftime(buff, 20, "_%Y_%m_%d", &ptr);
		gw_strftime(buff1, 20, "%Y-%m-%d %H:%M:%S", &ptr);
		sql = octstr_format("select ESMEAccName,count(*) from  smpp_esme_table where TIMESTAMPDIFF(minute,RecvTime,now())<5  group by ESMEAccName;");
		result = mysql_select(sql);
		//printf("\n\n---RESULT FOUND :%d\n\n\n",mysql_num_rows(result));
		if (result == NULL) 
		{
			sleep(302);
			continue;
		}

		if (mysql_num_rows(result) > 0)
		{ 
			char file_name1[100]="/tmp/TPS/esme_tps"; 
			strcat(file_name1,buff);
			fptr = fopen(file_name, "w+");
			fptr1 = fopen(file_name1, "a+");
			if (fptr == NULL)
			{
				warning( 0, "writing File does not exists [%s]\n  ",octstr_get_cstr(file_name));
				sleep(300);
				continue;
			}
			while(1)
			{
				row = mysql_fetch_row(result);
				if (!row) 
				{
					debug("dlr.mysql", 0, "No rows found for esme clients acc\n");
					//return NULL;
					break ;
				}
				fprintf(fptr, "%s  = %d\n",  row[0],atoi(row[1])/300);
				fprintf(fptr1, "%s | %s  = %d\n",  buff1,row[0],atoi(row[1])/300);

			}
			fclose(fptr);
			fclose(fptr1);
		}
		else
		{
			warning( 0, "no rows found in for file [%s] \n",file_name);
		}
		sleep(302);	
	}
	mysql_free_result(result);
	octstr_destroy(sql);
}

void dnd_deliver_sm_generate(MYSQL_ROW row){
	Octstr *os1;
	SMPP_PDU *pdu2;
	unsigned long mess_id = 0;
	char id[30];
	char sub[24];
	char dlvrd[24];
	char submit_date[24];
	char done_date[22];
	char stat[24];
	char err[24];
	char text[50];
	/*---for short_message*/
	struct timeval tp;
	struct tm ptr;
	time_t tm;
	char string[8];

	gettimeofday(&tp, NULL);
	sprintf(string, "%d", tp.tv_usec);
	tm = time(NULL);
	ptr = gw_localtime(tm);

	debug("test.smpp", 0, "Generating Deliver_sm PDU");
	pdu2 = smpp_pdu_create(deliver_sm, counter_increase(dnd_msg_counter));
	if (pdu2 != NULL)
		debug("test.smpp", 0, "RECEIVED CORRECT PDU");

	strcpy(stat, "NDNC_Fail");
	strcpy(err, "003");
	strcpy(id, row[1]);
	strcpy(submit_date, row[16]);
	strcpy(done_date, row[17]);
	strcpy(dlvrd, "000");
	strcpy(sub, "001");
	Octstr *tempData = octstr_create(row[13]);
	//	strncpy(text, octstr_get_cstr(tempData),50);
	//if(atoi(row[18]) != 0)
	//	octstr_hex_to_binary(text);
	strncpy(text, octstr_get_cstr(tempData),50);

	//if(atoi(row[18]) != 0)
	//	octstr_hex_to_binary(text);

	pdu2->u.deliver_sm.esm_class = 0x00000004;


	pdu2->u.deliver_sm.source_addr = octstr_create(row[5]);
	pdu2->u.deliver_sm.destination_addr = octstr_create(row[4]);
	pdu2->u.deliver_sm.short_message = octstr_format(
			"id:%s sub:%s dlvrd:%s submit date:%s done date:%s stat:%s err:%s text:%s",
			id, sub, dlvrd, submit_date, done_date, stat, err, text);

	//smpp_pdu_dump(pdu2);

	os1 = smpp_pdu_pack(pdu2);

	Conn_Msg *msg;
	msg = conn_msg_create(insert_status);
	msg->insert_status.smsc_id = octstr_create("MAX");
	msg->insert_status.smsc_msg_id = octstr_create(row[1]);

	msg->insert_status.dest_addr = octstr_create(row[5]);

	msg->insert_status.pdu = os1;
	msg->insert_status.deliver_status = 101;

	dlr_status_add(msg);

	if(msg->insert_status.smsc_msg_id == NULL || msg->insert_status.dest_addr == NULL )
		return;

	Conn_Msg *result_msg=NULL;
	//result_msg = conn_msg_create(values_got);
	result_msg = dlr_find(msg->insert_status.dest_addr,msg->insert_status.smsc_msg_id );
	if(result_msg == NULL || conn_msg_type(result_msg) != values_got)
	{
		info( 0,"No entries Found for Msgid %s and dest_addr %s",
				octstr_get_cstr(msg->insert_status.smsc_msg_id),
				octstr_get_cstr(msg->insert_status.dest_addr));
		conn_msg_destroy(result_msg);
		result_msg = conn_msg_create(values_got);
		result_msg->values_got.result = 0;
	}
	else
	{

		if(result_msg->values_got.result == 1)
			debug("dbbox", 0,"FOUND ENTRY:ACCNAME:%s:ACCMSGID:%s",\
					octstr_get_cstr(result_msg->values_got.account_name),
					octstr_get_cstr(result_msg->values_got.account_msg_id));

	}

	if(esme_process_client != NULL)
	{
		if(esme_process_client->conn_id != NULL)
		{
			result_msg->values_got.pdu = octstr_duplicate(msg->insert_status.pdu);
		}
		gwlist_append(esme_process_client->send_list,result_msg);
		gwthread_wakeup(esme_process_client->thread_id);
	}
	/*  conn_write(esme->conn, os1);*/
	smpp_pdu_destroy(pdu2);
	octstr_destroy(os1);
}

void msg_process ( void ){
	while(1)
	{
		if(esme_process_client != NULL)
		{
			Octstr *sql;
			Octstr *update_sql;
			Octstr *insert_sql;
			MYSQL_RES *result;
			MYSQL_ROW row;


			sql = octstr_create("SELECT ESMEAccName, ESMEAccMsgId, SMSCId, SMSCMsgId, SourceAddr, DestinationAddr, RecvTime, SubmittedStat, SubmitTime, DeliveredStat, DeliverTime, RetryValue, UDH, Content,Client_IP,dnd,DATE_FORMAT(RecvTime, '%y%m%d%H%i%s'),DATE_FORMAT(now(), '%y%m%d%H%i%s'),data_coding, esm_class, more_messages_to_send from process_table where read_flag=0 order by ESMEAccMsgId asc");
			result = mysql_select(sql);
			if (result == NULL) 
			{
				octstr_destroy(sql);
				return NULL;
			}
			if (mysql_num_rows(result) > 0)
			{ 
				while (1) 
				{
					row = mysql_fetch_row(result);
					if (!row) 
					{
						debug("dlr.mysql", 0, "No rows for numbers\n");

						break ;
					}
					else
					{

						if(atoi(row[15])==0){
							Conn_Msg *msg;
							Octstr *current_time;
							msg = conn_msg_create(submit);
							current_time = get_current_datetime();
							msg->submit.req_time = current_time;
							msg->submit.account_name = octstr_create(row[0]);
							msg->submit.account_msg_id = octstr_create(row[1]);
							msg->submit.destination_addr = octstr_create(row[5]);
							msg->submit.source_addr = octstr_create(row[4]);
							msg->submit.data_coding = atoi(row[18]);
							msg->submit.esm_class = atoi(row[19]);
							msg->submit.more_messages_to_send = atoi(row[20]);
							msg->submit.content = octstr_create(row[13]);
							msg->submit.udh=octstr_create(row[12]);
							if (atoi(row[18]) != 0)
								octstr_hex_to_binary(msg->submit.content); 
							if (octstr_len(msg->submit.udh)){
								Octstr *message , *message1;
								char *a=octstr_get_cstr(msg->submit.udh);
								int j=0,i=0;
								int k=strlen(a);
								int udhLen = 0;
								udhLen = ceil(k/2);
								char dest[udhLen][3];
								for( i=0;i<k;i+=2 )
								{
									strncpy(dest[j], a+i, 2);
									dest[j][2]='\0';
									printf("%s \n",dest[j]);
									j++;
								}

								message = octstr_format("%c%c%c%c%c%c",atoi(dest[0]),atoi(dest[1]),atoi(dest[2]),atoi(dest[3]),atoi(dest[4]),atoi(dest[5]));
								octstr_insert(msg->submit.content,message,0);
							}
							//		msg->submit.IP = octstr_create(row[14]);
							send_msg_process_client(dbdata_client,msg);
						}else
						{
							char *csescape_string = NULL;
							csescape_string = mysql_escape(row[13]);
							insert_sql = octstr_format("insert into mis_table(ESMEAccName, ESMEAccMsgId, SMSCId, SMSCMsgId, SourceAddr, DestinationAddr, RecvTime, SubmittedStat,SubmitTime,UDH, Content) values ('%s','%s','MAX','%s','%s','%s','%s','%s',now(),'%s','%s')",row[0],row[1], row[1], row[4], row[5], row[6], row[7], row[12],csescape_string);
							mysql_update(insert_sql);							
							dnd_deliver_sm_generate(row);
						}
						update_sql = octstr_format("update process_table set read_flag = 1 where ESMEAccMsgId = '%s'",row[1]);
						mysql_update(update_sql);		
					}

				}	
			}


			mysql_free_result(result);

			octstr_destroy(sql);
			//		octstr_destroy(update_sql);
		}	
		sleep(10);
	}
}

void esme_file_update(const Octstr *file_name )
{
	Octstr *sql;
	char IP[2022]="";
	MYSQL_RES *result;
	MYSQL_ROW row;
	FILE *fptr;


	sql = octstr_format("SELECT IP_List FROM  esme_client_ip_info");

	result = mysql_select(sql);
	if (result == NULL) 
	{
		octstr_destroy(sql);
		return NULL;
	}
	if (mysql_num_rows(result) > 0)
	{ 
		while(1)
		{
			row = mysql_fetch_row(result);
			if (!row) 
			{
				debug("dlr.mysql", 0, "No rows found for IP info\n");
				//return NULL;
				break ;
			}
			else
			{
				strcat(IP,row[0]);
				strcat(IP,";");
			}
		}


	}	

	sql = octstr_format("SELECT mode,esme_acc,esme_pass,esme_valid_from,ems_valid_to,esme_log_file,esme_log_level,max_submits_uncon,max_submits,max_tx_sessions,max_rx_sessions,allow_source,max_tps,dnd_check,dedicate_smsc FROM esme_client_info");


	result = mysql_select(sql);
	//printf("\n\n---RESULT FOUND :%d\n\n\n",mysql_num_rows(result));
	if (result == NULL) 
	{
		octstr_destroy(sql);
		return NULL;
	}

	if (mysql_num_rows(result) > 0)
	{ 
#if 1
		fptr = fopen(octstr_get_cstr(file_name), "w+");
		if (fptr == NULL)
		{
			warning( 0, "writing File does not exists [%s]\n  ",octstr_get_cstr(file_name));
			return ;
		}
		fprintf(fptr, "group  = core\n");
		fprintf(fptr, "log-file  = /tmp/server_debug/esme/smpp_esme.log\n");
		fprintf(fptr, "access-log  = /tmp/server_debug/esme/access.log\n");
		fprintf(fptr, "log-level  = 0\n");
		fprintf(fptr, "trans-mode-port  = 3000\n");
		fprintf(fptr, "recv-mode-port  = 4000\n");
		fprintf(fptr, "transrecv-mode-port  = 5000\n");
		fprintf(fptr, "admin-port  = 34000\n");
		fprintf(fptr, "admin-password =foobar\n");
		fprintf(fptr, "connect-allow-ip  =\"%s\"\n",IP);
		//fprintf(fptr, "connect-allow-ip  = \"10.157.205.234;192.168.1.249\"\n");
		fprintf(fptr, "gsm-series  = \"98;99\"\n");
		fprintf(fptr, "cdma-series  = \"92\"\n");
		fprintf(fptr, "dbbox-host  = \"localhost\"\n");
		fprintf(fptr, "dbbox-port  = 20000\n");
		fprintf(fptr, "dbbox-reconnect-delay=5\n");
		fprintf(fptr, "smsc-host=localhost\n");
		fprintf(fptr, "smsc-reverse-port=22000\n");
		fprintf(fptr, "smsc-forward-port=21000\n");
		fprintf(fptr, "smsc-reconnect-delay=5\n");
		fprintf(fptr, "smsc-unconnect-q-limit = -1\n");
		fprintf(fptr, "dbbox-unconnect-q-limit=6\n");
		fprintf(fptr, "dbbox-ping-delay = 10\n");
		fprintf(fptr, "dbbox-pingack-timeout = 30\n");
		fprintf(fptr, "smsc-ping-delay = 10\n");
		fprintf(fptr, "smsc-pingack-timeout = 20\n");
		fprintf(fptr, "log-file-size = 10\n");
		fprintf(fptr, "log-interval = 60\n");
		//fclose(fptr);
#endif
		while(1)
		{
			row = mysql_fetch_row(result);
			if (!row) 
			{
				debug("dlr.mysql", 0, "No rows found for esme clients acc\n");
				//return NULL;
				break ;
			}

			debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, ",
					row[0], row[1]);

			//fptr = fopen(octstr_get_cstr(file_name), "a+");
			//	if (fptr == NULL)
			//	{
			//	warning( 0, "File does not exists [%s]\n  ",octstr_get_cstr(file_name));
			//mysql_free_result(result);
			//octstr_destroy(sql);
			//return;
			//}
			fprintf(fptr, "\ngroup  = smpp-client\n");
			fprintf(fptr, "mode  = %s\n",  row[0]);

			fprintf(fptr, "account-name  = %s\n",  row[1]);

			fprintf(fptr, "password  = %s\n", row[2]);
			fprintf(fptr, "validfrom  = %s\n", row[3]);
			fprintf(fptr, "validto  = %s\n", row[4]);
			fprintf(fptr, "log-file  = %s\n", row[5]);
			fprintf(fptr, "log-level  = %d\n", atoi(row[6]));
			fprintf(fptr, "max-submits-unconnect  = %d\n", atoi(row[7]));
			fprintf(fptr, "max-submits  = %d\n", atoi(row[8]));
			fprintf(fptr, "max-tx-sessions  = %d\n", atoi(row[9]));
			fprintf(fptr, "max-rx-sessions  = %d\n", atoi(row[10]));
			fprintf(fptr, "max-rx-sessions  = %d\n", atoi(row[10]));
			if(row[11]!=NULL)
			{
				fprintf(fptr, "account-allow-prefix  = %s\n", row[11]);
			}
			fprintf(fptr, "max-tps = %d\n", atoi(row[12]));
			fprintf(fptr, "dnd-check = %d\n", atoi(row[13]));
			fprintf(fptr, "dedicated-smsc  = %s\n", row[14]);
			//account-allow-prefix = "919810"


			//printf("\n\n----------------DUMPING MESSAGE \n\n\n");
			//conn_msg_dump(msg,0);

		}
		fclose(fptr);
	}
	else
	{
		warning( 0, "no rows found in for file [%s] \n",octstr_get_cstr(file_name));
	}
	mysql_free_result(result);

	octstr_destroy(sql);
}


Conn_Msg* dlr_mysql_get(const Octstr *dest_addr, const Octstr *smsc_msg_id)
{
	Octstr *sql;
	MYSQL_RES *result;
	MYSQL_ROW row;
	char p[20]; 
	char tablename[50];
	//time_t tm1,tm3,tm4;
	//struct tm tm2;
	Conn_Msg *msg;
	msg =  conn_msg_create(values_got);

	/*Try to find entries in the current table, if not nout found find entries in previous two days table*/
	//tm1=time(NULL);
	//tm2=gw_localtime(tm1);

	strcpy(tablename,octstr_get_cstr(fields_mis->table));
	//gw_strftime(p,20,"_%m_%d",&tm2);
	//strcat(tablename,p);
	msg->values_got.result = 0;
	msg->values_got.smsc_msg_id = octstr_duplicate(smsc_msg_id);
	msg->values_got.dest_addr = octstr_duplicate(dest_addr);
	//semaphore_down(sem_update_allowed);
	/*

	   sql = octstr_format("SELECT %s, %s FROM %s WHERE(%s='%s') AND (%s = '%s' OR %s='00%s' OR %s='91%s' OR %s='0%s');",
	   octstr_get_cstr(fields_mis->field_account_name),
	   octstr_get_cstr(fields_mis->field_account_msg_id),
	   tablename,
	   octstr_get_cstr(fields_mis->field_smsc_msg_id),
	   octstr_get_cstr(smsc_msg_id),
	   octstr_get_cstr(fields_mis->field_dest_addr),
	   octstr_get_cstr(dest_addr),
	   octstr_get_cstr(fields_mis->field_dest_addr),
	   octstr_get_cstr(dest_addr),
	   octstr_get_cstr(fields_mis->field_dest_addr),
	   octstr_get_cstr(dest_addr),
	   octstr_get_cstr(fields_mis->field_dest_addr),
	   octstr_get_cstr(dest_addr)
	   );


*/
	sql = octstr_format("SELECT %s, %s FROM %s WHERE %s='%s';",
			octstr_get_cstr(fields_mis->field_account_name),
			octstr_get_cstr(fields_mis->field_account_msg_id),
			tablename,
			octstr_get_cstr(fields_mis->field_smsc_msg_id),
			octstr_get_cstr(smsc_msg_id)
			);



	result = mysql_select(sql);
	//octstr_destroy(sql);
	//printf("\n\n---RESULT FOUND :%d\n\n\n",mysql_num_rows(result));
	if (result == NULL) 
	{
		debug("dlr.mysql", 0, "Not found Found entry, for account=%s, field_account_msg_id=%s, ",octstr_get_cstr(fields_mis->field_account_name),octstr_get_cstr(fields_mis->field_account_msg_id));

		return NULL;
	}

	if (mysql_num_rows(result) > 0)
	{ 
		row = mysql_fetch_row(result);
		if (!row) 
		{
			debug("dlr.mysql", 0, "rows found but could not load them");
			mysql_free_result(result);
			return NULL;
		}

		debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, ",
				row[0], row[1]);

		msg->values_got.result = 1;
		msg->values_got.account_name = octstr_create(row[0]);
		msg->values_got.account_msg_id = octstr_create(row[1]);

		//printf("\n\n----------------DUMPING MESSAGE \n\n\n");
		//conn_msg_dump(msg,0);
		mysql_free_result(result);
		//  sql = octstr_format("DELETE FROM %s  WHERE %s='%s';", tablename, octstr_get_cstr(fields_mis->field_smsc_msg_id),octstr_get_cstr(smsc_msg_id));
		// Rama 02-June	   // mysql_update(sql);
		octstr_destroy(sql);
		return msg;
	}
	else
	{
		info( 0, "no rows found in %s SMSC MSG ID :%s dest addr :%s",tablename,octstr_get_cstr(smsc_msg_id),octstr_get_cstr(dest_addr));
		mysql_free_result(result);		
		return NULL;
	}

#if 0
	/*find  entries in previous day table*/
	tm1=time(NULL);
	tm1 = tm1 - (24*60*60);
	tm2=gw_localtime(tm1);

	strcpy(tablename,octstr_get_cstr(fields_mis->table));
	gw_strftime(p,20,"_%m_%d",&tm2);
	strcat(tablename,p);

	/*
	   sql = octstr_format("SELECT %s, %s FROM %s WHERE %s LIKE '%%%s' AND %s='%s';",
	   octstr_get_cstr(fields_mis->field_account_name),
	   octstr_get_cstr(fields_mis->field_account_msg_id),
	   tablename,
	   octstr_get_cstr(fields_mis->field_dest_addr),
	   octstr_get_cstr(dest_addr),
	   octstr_get_cstr(fields_mis->field_smsc_msg_id),
	   octstr_get_cstr(smsc_msg_id)
	   );
	   */
	sql = octstr_format("SELECT  %s, %s FROM %s WHERE(%s='%s') AND (%s = '%s' OR %s='00%s' OR %s='91%s' OR %s='0%s');",
			octstr_get_cstr(fields_mis->field_account_name),
			octstr_get_cstr(fields_mis->field_account_msg_id),
			tablename,
			octstr_get_cstr(fields_mis->field_smsc_msg_id),
			octstr_get_cstr(smsc_msg_id),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr)
			);
	result = mysql_select(sql);
	octstr_destroy(sql);

	if (result == NULL) 
	{
		return NULL;
	}

	if (mysql_num_rows(result) > 0)
	{ 
		row = mysql_fetch_row(result);
		if (!row) 
		{
			debug("dlr.mysql", 0, "rows found but could not load them");
			mysql_free_result(result);
			return NULL;
		}

		debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, ",
				row[0], row[1]);

		msg->values_got.result = 1;
		msg->values_got.account_name = octstr_create(row[0]);
		msg->values_got.account_msg_id = octstr_create(row[1]);

		mysql_free_result(result);

		return msg;
	}
	else
	{
		info( 0, "no rows found in %s",tablename);
		mysql_free_result(result);		
	}

	/*find entries in table one more older day*/

	tm1=time(NULL);
	tm1 = tm1 - (24*60*60*2);
	tm2=gw_localtime(tm1);

	strcpy(tablename,octstr_get_cstr(fields_mis->table));
	gw_strftime(p,20,"_%m_%d",&tm2);
	strcat(tablename,p);

	/*
	   sql = octstr_format("SELECT %s, %s FROM %s WHERE %s LIKE '%%%s' AND %s='%s';",
	   octstr_get_cstr(fields_mis->field_account_name),
	   octstr_get_cstr(fields_mis->field_account_msg_id),
	   tablename,
	   octstr_get_cstr(fields_mis->field_dest_addr),
	   octstr_get_cstr(dest_addr),
	   octstr_get_cstr(fields_mis->field_smsc_msg_id),
	   octstr_get_cstr(smsc_msg_id)
	   );
	   */

	sql = octstr_format("SELECT %s, %s FROM %s WHERE(%s='%s') AND (%s = '%s' OR %s='00%s' OR %s='91%s' OR %s='0%s');",
			octstr_get_cstr(fields_mis->field_account_name),
			octstr_get_cstr(fields_mis->field_account_msg_id),
			tablename,
			octstr_get_cstr(fields_mis->field_smsc_msg_id),
			octstr_get_cstr(smsc_msg_id),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(dest_addr)
			);
	result = mysql_select(sql);
	octstr_destroy(sql);

	if (result == NULL) 
	{
		return NULL;
	}

	if (mysql_num_rows(result) > 0)
	{ 
		row = mysql_fetch_row(result);
		if (!row) 
		{
			debug("dlr.mysql", 0, "rows found but could not load them");
			mysql_free_result(result);
			return NULL;
		}

		debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, ",
				row[0], row[1]);

		msg->values_got.result = 1;
		msg->values_got.account_name = octstr_create(row[0]);
		msg->values_got.account_msg_id = octstr_create(row[1]);
		mysql_free_result(result);

		return msg;
	}
	else
	{
		info(0, "no rows found in %s",tablename);
		mysql_free_result(result);		
		return NULL;
	}	
#endif
}

void dlr_mysql_mis_add(Conn_Msg *msg)
{
	Octstr *sql;


	char p[20]; 
	char tablename[50];
	char *csescape_string = NULL;

	//time_t tm1;
	//struct tm tm2;

	//tm1=time(NULL);
	//tm2=gw_localtime(tm1);

	strcpy(tablename,octstr_get_cstr(fields_mis->table));
	//gw_strftime(p,20,"_%m_%d",&tm2);
	//strcat(tablename,p);

	csescape_string = mysql_escape(octstr_get_cstr(msg->insert_mis.msg_content));
	if(csescape_string == NULL)
	{
		error(0, "MYSQL: dlr_mysql_master_add : Database pool got no connection! DB update failed!");
		return;
	}
	//sql = octstr_format("INSERT HIGH_PRIORITY INTO %s" 
	sql = octstr_format("INSERT  INTO %s" 
			"(%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s,%s,%s) VALUES "
			"('%s','%s','%s','%s','%d', now(),'%s','%s','%s', '%d','%s','%s','%s') ;",

			tablename, 
			octstr_get_cstr(fields_mis->field_account_name),
			octstr_get_cstr(fields_mis->field_account_msg_id),
			octstr_get_cstr(fields_mis->field_smsc_id),
			octstr_get_cstr(fields_mis->field_smsc_msg_id),
			octstr_get_cstr(fields_mis->field_submit_status), 
			octstr_get_cstr(fields_mis->field_submit_time),
			octstr_get_cstr(fields_mis->field_source_addr),
			octstr_get_cstr(fields_mis->field_dest_addr),
			octstr_get_cstr(fields_mis->field_msg_content),
			octstr_get_cstr(fields_mis->field_retry_value),
			octstr_get_cstr(fields_mis->field_udh),
			octstr_get_cstr(fields_mis->field_recv_time),
			octstr_get_cstr(fields_mis->field_Client_IP),

			octstr_get_cstr(msg->insert_mis.account_name),
			octstr_get_cstr(msg->insert_mis.account_msg_id), 
			octstr_get_cstr(msg->insert_mis.smsc_id),
			octstr_get_cstr(msg->insert_mis.smsc_msg_id),
			msg->insert_mis.submit_status,
			octstr_get_cstr(msg->insert_mis.source_addr),
			octstr_get_cstr(msg->insert_mis.dest_addr),
			//mis_insert_ptr->deliver_status,
			csescape_string,
			msg->insert_mis.retry_value,
			octstr_get_cstr(msg->insert_mis.udh),
			octstr_get_cstr(msg->insert_mis.req_time),
			octstr_get_cstr(msg->insert_mis.IP)
				);

	//debug("dlr.mysql", 0, "DLR MYSQL MIS ADD %s", octstr_get_cstr(sql));
	mysql_update(sql);
	//semaphore_up(sem_update_allowed);
	octstr_destroy(sql);
	gw_free(csescape_string);
}
/*changed from update mis table to insert mis status table for performance enhancements - 1-11-2008*/
void dlr_mysql_status_add(Conn_Msg *msg)
{
	Octstr *sql, *tmp_sql;
	long res;


	char p[20]; 
	char tablename[50];
	//time_t tm1,tm3,tm4;
	//struct tm tm2;


	/*

	   debug("dlr.mysql", 0, "updating DLR status in MIS table");

	   tmp_sql = octstr_format(" SET %s=%d,%s=now() WHERE %s='%s' AND %s='%s' AND `SubmitTime`>=DATE_SUB(CURDATE(),INTERVAL 3 DAY)LIMIT 1;",
	   octstr_get_cstr(fields_mis->field_deliver_status),  
	   mis_update_ptr->deliver_status,
	   octstr_get_cstr(fields_mis->field_deliver_time),
	   octstr_get_cstr(fields_mis->field_smsc_id),
	   mis_update_ptr->smsc_id,
	   octstr_get_cstr(fields_mis->field_smsc_msg_id),
	   mis_update_ptr->smsc_msg_id
	   );


	   tm1=time(NULL);
	   tm2=gw_localtime(tm1);

	   strcpy(tablename,octstr_get_cstr(fields_mis->table));
	   gw_strftime(p,20,"_%m_%d",&tm2);
	   strcat(tablename,p);

	   sql = octstr_format("UPDATE %s",tablename);
	   octstr_append(sql,tmp_sql);

	   res = mysql_update_counter(sql);
	   octstr_destroy(sql);
	   if(res > 0)
	   {
	   octstr_destroy(tmp_sql);
	   return;
	   }

	   tm3 = tm1 - (24*60*60);
	   tm2=gw_localtime(tm3);

	   strcpy(tablename,octstr_get_cstr(fields_mis->table));
	   gw_strftime(p,20,"_%m_%d",&tm2);
	   strcat(tablename,p);

	   sql = octstr_format("UPDATE %s",tablename);
	   octstr_append(sql,tmp_sql);

	   res = mysql_update_counter(sql);
	   octstr_destroy(sql);
	   if(res > 0)
	   {
	   octstr_destroy(tmp_sql);
	   return;
	   }

	   tm4 = tm3 - (24*60*60);
	   tm2=gw_localtime(tm4);

	   strcpy(tablename,octstr_get_cstr(fields_mis->table));
	   gw_strftime(p,20,"_%m_%d",&tm2);
	   strcat(tablename,p);

	   sql = octstr_format("UPDATE %s",tablename);
	   octstr_append(sql,tmp_sql);

	   res = mysql_update_counter(sql);
	   octstr_destroy(sql);

	   if(res > 0)
	   {
	   octstr_destroy(tmp_sql);
	   return;
	   }
	   */
	//tm1=time(NULL);
	//tm2=gw_localtime(tm1);

	strcpy(tablename,octstr_get_cstr(fields_status->table));
	//gw_strftime(p,20,"_%m_%d",&tm2);
	//strcat(tablename,p);



	sql = octstr_format("INSERT INTO %s(%s,%s,%s,%s,%s) VALUES "
			"('%s','%s','%s','%d',now());",
			tablename,

			octstr_get_cstr(fields_status->field_smsc_id),
			octstr_get_cstr(fields_status->field_smsc_msg_id),
			octstr_get_cstr(fields_status->field_dest_addr),
			octstr_get_cstr(fields_status->field_deliver_status),
			octstr_get_cstr(fields_status->field_deliver_time),

			octstr_get_cstr(msg->insert_status.smsc_id),
			octstr_get_cstr(msg->insert_status.smsc_msg_id),
			octstr_get_cstr(msg->insert_status.dest_addr),
			msg->insert_status.deliver_status					
			);

	mysql_update(sql);
	octstr_destroy(sql);
}
void dlr_smpp_esme_add(Conn_Msg *msg)
{
	Octstr *sql;
	char p[20]; 
	char tablename[50];
	char *csescape_string = NULL;

	//time_t tm1;
	//struct tm tm2;

	//tm1=time(NULL);
	//tm2=gw_localtime(tm1);

	strcpy(tablename,octstr_get_cstr(fields_smpp_esme->table));
	//	gw_strftime(p,20,"_%m_%d",&tm2);
	//	strcat(tablename,p);

	csescape_string = mysql_escape(octstr_get_cstr(msg->smpp_esme.msg_content));
	if(csescape_string == NULL)
	{
		error(0, "MYSQL: dlr_smpp_esme_add : Database pool got no connection! DB update failed!");
		return;
	}
	sql = octstr_format("INSERT  INTO %s" 
			"(%s, %s, %s, %s, %s, %s, %s, %s,%s,%s,%s,%s,%s) VALUES "
			"('%s','%s','%d','%s','%s','%s','%d','%s', '%s','%d','%d','%d','%d') ;",

			tablename, 
			octstr_get_cstr(fields_smpp_esme->field_account_name),
			octstr_get_cstr(fields_smpp_esme->field_account_msg_id),
			octstr_get_cstr(fields_smpp_esme->field_submit_status), 
			octstr_get_cstr(fields_smpp_esme->field_source_addr),
			octstr_get_cstr(fields_smpp_esme->field_dest_addr),
			octstr_get_cstr(fields_smpp_esme->field_msg_content),
			octstr_get_cstr(fields_smpp_esme->field_retry_value),
			octstr_get_cstr(fields_smpp_esme->field_udh),
			octstr_get_cstr(fields_smpp_esme->field_recv_time),
			octstr_get_cstr(fields_smpp_esme->field_dnd_flag),
			octstr_get_cstr(fields_smpp_esme->field_data_coding),
			octstr_get_cstr(fields_smpp_esme->field_esm_class),
			octstr_get_cstr(fields_smpp_esme->field_more_messages_to_send),
			octstr_get_cstr(msg->smpp_esme.account_name),
			octstr_get_cstr(msg->smpp_esme.account_msg_id), 
			msg->smpp_esme.submit_status,
			octstr_get_cstr(msg->smpp_esme.source_addr),
			octstr_get_cstr(msg->smpp_esme.dest_addr),
			//mis_insert_ptr->deliver_status,
			csescape_string,
			msg->smpp_esme.retry_value,
			octstr_get_cstr(msg->smpp_esme.udh),
			octstr_get_cstr(msg->smpp_esme.req_time),
			msg->smpp_esme.dnd_flag,
			msg->smpp_esme.data_coding,
			msg->smpp_esme.esm_class,
			msg->smpp_esme.more_messages_to_send
				);

	debug("dlr.mysql", 0, "DLR MYSQL MIS ADD ********[%s]", octstr_get_cstr(sql));
	//Rama 16/08 insert into esme table
	mysql_update(sql);

	//semaphore_up(sem_update_allowed);
	octstr_destroy(sql);
	gw_free(csescape_string);
}
void dlr_mysql_check_conn(void)
{
	Octstr *sql;
	MYSQL_RES *result;
	sql = octstr_create("SELECT NOW();");
	result = mysql_select(sql);
	octstr_destroy(sql);
	mysql_free_result(result);
}
static struct dlr_storage handles = 
{
	.type = "mysql"						,
	.dlr_add = dlr_mysql_add			,
	/*.dlr_update = dlr_mysql_update		,*/
	.dlr_get = dlr_mysql_get			,
	.dlr_remove = dlr_mysql_remove		,
	.dlr_mis_add = dlr_mysql_mis_add	,
	.dlr_status_add = dlr_mysql_status_add,
	.dlr_smpp_esme	= dlr_smpp_esme_add,
	.dlr_messages = dlr_mysql_messages	,
	.dlr_shutdown = dlr_mysql_shutdown	,
	.dlr_flush = dlr_mysql_flush			,
	.dlr_check_conn = dlr_mysql_check_conn 
};

struct dlr_storage *dlr_init_mysql(Cfg *cfg)
{

	CfgGroup *grp;
	List *grplist;
	Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db, *mysql_sock = NULL;
	long mysql_port = 0;
	Octstr *p = NULL;
	long pool_size;
	DBConf *db_conf = NULL;
	Octstr *mysql_id=NULL;
	/*
	 * check for all mandatory directives that specify the field names
	 * of the used MySQL table
	 */
	//sem_update_allowed = semaphore_create(0);
	if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
		panic(0, "DLR: MySQL: group 'dlr-db' is not specified!");

	if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
		panic(0, "DLR: MySQL: directive 'id' is not specified!");

	fields = dlr_db_fields_create(grp);
	gw_assert(fields != NULL);

	octstr_destroy(mysql_id);


	if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db-mis"))))
		panic(0, "DLR: MySQL: group 'dlr-db-mis' is not specified!");

	if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
		panic(0, "DLR: MySQL: directive 'id' is not specified for mis table!");

	fields_mis = dlr_db_fields_create_mis(grp);
	gw_assert(fields_mis != NULL);

	octstr_destroy(mysql_id);

	if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db-status"))))
		panic(0, "DLR: MySQL: group 'dlr-db-status' is not specified!");

	if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
		panic(0, "DLR: MySQL: directive 'id' is not specified for mis status!");

	fields_status = dlr_db_fields_create_status(grp);
	gw_assert(fields_status != NULL);


	octstr_destroy(mysql_id);

	if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db-smpp-esme"))))
		panic(0, "DLR: MySQL: group 'dlr-db-smpp-esme' is not specified!");

	if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
		panic(0, "DLR: MySQL: directive 'id' is not specified for mis status!");

	fields_smpp_esme = dlr_db_fields_create_smpp_esme(grp);
	gw_assert(fields_smpp_esme != NULL);
	/*
	 * now grap the required information from the 'mysql-connection' group
	 * with the mysql-id we just obtained
	 *
	 * we have to loop through all available MySQL connection definitions
	 * and search for the one we are looking for
	 */




	grplist = cfg_get_multi_group(cfg, octstr_imm("mysql-connection"));
	while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
		p = cfg_get(grp, octstr_imm("id"));
		if (p != NULL && octstr_compare(p, mysql_id) == 0) {
			goto found;
		}
		if (p != NULL) octstr_destroy(p);
	}
	panic(0, "DLR: MySQL: connection settings for id '%s' are not specified!",
			octstr_get_cstr(mysql_id));

found:
	octstr_destroy(p);
	gwlist_destroy(grplist, NULL);

	if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
		pool_size = 1;

	if (!(mysql_host = cfg_get(grp, octstr_imm("host"))))
		panic(0, "DLR: MySQL: directive 'host' is not specified!");
	if (!(mysql_user = cfg_get(grp, octstr_imm("username"))))
		panic(0, "DLR: MySQL: directive 'username' is not specified!");
	if (!(mysql_pass = cfg_get(grp, octstr_imm("password"))))
		panic(0, "DLR: MySQL: directive 'password' is not specified!");
	if (!(mysql_db = cfg_get(grp, octstr_imm("database"))))
		panic(0, "DLR: MySQL: directive 'database' is not specified!");
	if((mysql_sock = cfg_get(grp, octstr_imm("socket"))))
		info(0, "MYSQL : Connecting using socket %s.",octstr_get_cstr(mysql_sock));
	else
		info(0, "MYSQL : Connecting using default socket");

	cfg_get_integer(&mysql_port, grp, octstr_imm("port"));  /* optional */

	/*
	 * ok, ready to connect to MySQL
	 */
	db_conf = gw_malloc(sizeof(DBConf));
	gw_assert(db_conf != NULL);

	db_conf->mysql = gw_malloc(sizeof(MySQLConf));
	gw_assert(db_conf->mysql != NULL);

	db_conf->mysql->host = mysql_host;
	db_conf->mysql->port = mysql_port;
	db_conf->mysql->username = mysql_user;
	db_conf->mysql->password = mysql_pass;
	db_conf->mysql->database = mysql_db;
	db_conf->mysql->socket = mysql_sock;

	pool = dbpool_create(DBPOOL_MYSQL, db_conf, pool_size);
	gw_assert(pool != NULL);

	/*
	 * XXX should a failing connect throw panic?!
	 */
	if (dbpool_conn_count(pool) == 0)
		panic(0,"DLR: MySQL: database pool has no connections!");

	octstr_destroy(mysql_id);   

	return &handles;
}



#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_mysql(Cfg* cfg)
{
	return NULL;
}
#endif /* HAVE_MYSQL */


