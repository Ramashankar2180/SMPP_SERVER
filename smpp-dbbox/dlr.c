
/*
 * gw/dlr.c
 *
 * Implementation of handling delivery reports (DLRs)
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
 *
 * Changes:
 * 2001-12-17: andreas@fink.org:
 *     implemented use of mutex to avoid two mysql calls to run at the same time
 * 2002-03-22: stolj@wapme.de:
 *     added more abstraction to fit for several other storage types
 * 2002-08-04: stolj@wapme.de:
 *     added simple database library (sdb) support
 * 2002-11-14: stolj@wapme.de:
 *     added re-routing info for DLRs to route via bearerbox to the same smsbox
 *     instance. This is required if you use state conditioned smsboxes or smppboxes
 *     via one bearerbox. Previously bearerbox was simple ignoring to which smsbox
 *     connection a msg is passed. Now we can route the messages inside bearerbox.
 */

#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
//#include "gwlib/common.h"
#include "dlr.h"
#include "dlr_p.h"

/* Our callback functions */
static struct dlr_storage *handles = NULL;

/*
 * Function to allocate a new struct dlr_entry entry
 * and intialize it to zero
 */
struct dlr_entry *dlr_entry_create(void)
{
	struct dlr_entry *dlr;

	dlr = gw_malloc(sizeof(*dlr));
	gw_assert(dlr != NULL);

	/* set all values to NULL */
	memset(dlr, 0, sizeof(*dlr));

	return dlr;
}

/* 
 * Duplicate dlr entry
 */
struct dlr_entry *dlr_entry_duplicate(const struct dlr_entry *dlr)
{
	struct dlr_entry *ret;

	if (dlr == NULL)
		return NULL;

	ret = dlr_entry_create();
	ret->account_name = octstr_duplicate(dlr->account_name);
	ret->account_msg_id = octstr_duplicate(dlr->account_msg_id);
	ret->smsc_id = octstr_duplicate(dlr->smsc_id);
	ret->smsc_msg_id = octstr_duplicate(dlr->smsc_msg_id);
	ret->msg_submit_time = octstr_duplicate(dlr->msg_submit_time);

	return ret;
}

/*
 * Function to destroy the struct dlr_entry entry
 */
void dlr_entry_destroy(struct dlr_entry *dlr)
{
	/* sanity check */
	if (dlr == NULL)
		return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

	O_DELETE(dlr->account_name);
	O_DELETE(dlr->account_msg_id);
	O_DELETE(dlr->smsc_id);
	O_DELETE(dlr->smsc_msg_id);
	O_DELETE(dlr->msg_submit_time);
#undef O_DELETE

	gw_free(dlr);
}

/*
 * Load all configuration directives that are common for all database
 * types that use the 'dlr-db' group to define which attributes are 
 * used in the table
 */
struct dlr_db_fields *dlr_db_fields_create(CfgGroup *grp)
{
	struct dlr_db_fields *ret = NULL;

	ret = gw_malloc(sizeof(*ret));
	gw_assert(ret != NULL);
	memset(ret, 0, sizeof(*ret));

	if (!(ret->table = cfg_get(grp, octstr_imm("table"))))
		panic(0, "DLR: DB: directive 'table' is not specified!");

	if (!(ret->field_account_name = cfg_get(grp, octstr_imm("field-EsmeAccountName"))))
		panic(0, "DLR: DB: directive 'field-EsmeAccountName' is not specified!");

	if (!(ret->field_account_msg_id = cfg_get(grp, octstr_imm("field-EsmeMsgId"))))
		panic(0, "DLR: DB: directive 'field-EsmeMsgId' is not specified!");

	if (!(ret->field_smsc_id = cfg_get(grp, octstr_imm("field-SmscId"))))
		panic(0, "DLR: DB: directive 'field-SmscId' is not specified!");

	if (!(ret->field_smsc_msg_id = cfg_get(grp, octstr_imm("field-SmscMsgId"))))
		panic(0, "DLR: DB: directive 'field-SmscMsgId' is not specified!");

	if (!(ret->field_msg_submit_time = cfg_get(grp, octstr_imm("field-DatetimeSubmit"))))
		panic(0, "DLR: DB: directive 'field-DatetimeSubmit' is not specified!");


	debug("dlr_detail_add : dlr.c",0,"dlr db fields created");
	return ret;
}

void dlr_db_fields_destroy(struct dlr_db_fields *fields)
{
	/* sanity check */
	if (fields == NULL)
		return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

	O_DELETE(fields->table);
	O_DELETE(fields->field_account_name);
	O_DELETE(fields->field_account_msg_id);
	O_DELETE(fields->field_smsc_id);
	O_DELETE(fields->field_smsc_msg_id);
	O_DELETE(fields->field_msg_submit_time);

#undef O_DELETE

	gw_free(fields);
}
struct dlr_db_mis_fields *dlr_db_fields_create_mis(CfgGroup *grp)
{
	struct dlr_db_mis_fields *ret = NULL;

	ret = gw_malloc(sizeof(*ret));
	gw_assert(ret != NULL);
	memset(ret, 0, sizeof(*ret));

	if (!(ret->table = cfg_get(grp, octstr_imm("table"))))
		panic(0, "DLR: DB: directive 'table' is not specified!");

	if (!(ret->field_account_name = cfg_get(grp, octstr_imm("field-EsmeAccountName"))))
		panic(0, "DLR: DB: directive 'field-EsmeAccountName' is not specified!");

	if (!(ret->field_account_msg_id = cfg_get(grp, octstr_imm("field-EsmeMsgId"))))
		panic(0, "DLR: DB: directive 'field-EsmeMsgId' is not specified!");

	if (!(ret->field_smsc_id = cfg_get(grp, octstr_imm("field-SmscId"))))
		panic(0, "DLR: DB: directive 'field-SmscId' is not specified!");

	if (!(ret->field_smsc_msg_id = cfg_get(grp, octstr_imm("field-SmscMsgId"))))
		panic(0, "DLR: DB: directive 'field-SmscMsgId' is not specified!");

	if (!(ret->field_submit_status = cfg_get(grp, octstr_imm("field-SubmitStat"))))
		panic(0, "DLR: DB: directive 'field-SubmitStat' is not specified!");

	if (!(ret->field_source_addr = cfg_get(grp, octstr_imm("field-SourceAddr"))))
		panic(0, "DLR: DB: directive 'field-SourceAddr' is not specified!");

	if (!(ret->field_dest_addr = cfg_get(grp, octstr_imm("field-DestAddr"))))
		panic(0, "DLR: DB: directive 'field-DestAddr' is not specified!");

	if (!(ret->field_submit_time = cfg_get(grp, octstr_imm("field-DatetimeSubmit"))))
		panic(0, "DLR: DB: directive 'field-DatetimeSubmit' is not specified!");

	if (!(ret->field_msg_content = cfg_get(grp, octstr_imm("field-MsgContent"))))
		panic(0, "DLR: DB: directive 'field-MsgContent' is not specified!");

	if (!(ret->field_udh = cfg_get(grp, octstr_imm("field-UDH"))))
		panic(0, "DLR: DB: directive 'field-UDH' is not specified!");


	if (!(ret->field_recv_time = cfg_get(grp, octstr_imm("field-DatetimeRecv"))))
		panic(0, "DLR: DB: directive 'field-DatetimeRecv' is not specified!");

	if (!(ret->field_retry_value = cfg_get(grp, octstr_imm("field-RetryValue"))))
		panic(0, "DLR: DB: directive 'field-RetryValue' is not specified!");

	if (!(ret->field_Client_IP = cfg_get(grp, octstr_imm("field-client-ip"))))
		panic(0, "DLR: DB: directive 'field-RetryValue' is not specified!");


	debug("dlr_detail_add : dlr.c",0,"dlr db MIS fields created");
	return ret;
}
void dlr_db_fields_destroy_mis(struct dlr_db_mis_fields *fields)
{
	/* sanity check */
	if (fields == NULL)
		return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

	O_DELETE(fields->table);
	O_DELETE(fields->field_account_name);
	O_DELETE(fields->field_account_msg_id);
	O_DELETE(fields->field_smsc_id);
	O_DELETE(fields->field_smsc_msg_id);

	O_DELETE(fields->field_submit_status);
	O_DELETE(fields->field_submit_time);
	O_DELETE(fields->field_source_addr);
	O_DELETE(fields->field_dest_addr);
	O_DELETE(fields->field_recv_time);
	O_DELETE(fields->field_msg_content);
	O_DELETE(fields->field_udh);
	O_DELETE(fields->field_retry_value);

#undef O_DELETE

	gw_free(fields);
}

struct dlr_db_status_fields *dlr_db_fields_create_status(CfgGroup *grp)
{
	struct dlr_db_status_fields *ret = NULL;

	ret = gw_malloc(sizeof(*ret));
	gw_assert(ret != NULL);
	memset(ret, 0, sizeof(*ret));

	if (!(ret->table = cfg_get(grp, octstr_imm("table"))))
		panic(0, "DLR: DB: directive 'table' is not specified!");

	if (!(ret->field_smsc_id = cfg_get(grp, octstr_imm("field-SmscId"))))
		panic(0, "DLR: DB: directive 'field-SmscId' is not specified!");

	if (!(ret->field_smsc_msg_id = cfg_get(grp, octstr_imm("field-SmscMsgId"))))
		panic(0, "DLR: DB: directive 'field-SmscMsgId' is not specified!");

	if (!(ret->field_dest_addr = cfg_get(grp, octstr_imm("field-DestAddr"))))
		panic(0, "DLR: DB: directive 'field-DestAddr' is not specified!");

	if (!(ret->field_deliver_status = cfg_get(grp, octstr_imm("field-DelieverStat"))))
		panic(0, "DLR: DB: directive 'field-DelieverStat' is not specified!");

	if (!(ret->field_deliver_time = cfg_get(grp, octstr_imm("field-DatetimeDeliver"))))
		panic(0, "DLR: DB: directive 'field-DatetimeDeliver' is not specified!");

	debug("dlr_detail_add : dlr.c",0,"dlr db STATUS fields created");
	return ret;
}

void dlr_db_fields_destroy_status(struct dlr_db_status_fields *fields)
{
	if (fields == NULL)
		return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

	O_DELETE(fields->table);
	O_DELETE(fields->field_smsc_id);
	O_DELETE(fields->field_smsc_msg_id);
	O_DELETE(fields->field_dest_addr);
	O_DELETE(fields->field_deliver_status);
	O_DELETE(fields->field_deliver_time);
#undef O_DELETE

	gw_free(fields);
}
/*
 * Initialize specifically dlr storage. If defined storage is unknown
 * then panic.
 */
void dlr_init(Cfg* cfg)
{
	CfgGroup *grp;
	Octstr *dlr_type;
	Octstr *log_file;
	int log_level;

	/* check which DLR storage type we are using */
	grp = cfg_get_single_group(cfg, octstr_imm("core"));
	if(grp == NULL)
		panic(0, "DLR: can't find group core");

	dlr_type = cfg_get(grp, octstr_imm("dlr-storage"));
	/* 
	 * assume we are using internal memory in case no directive
	 * has been specified, warn the user anyway
	 */
	if (dlr_type == NULL) 
	{
		dlr_type = octstr_imm("internal");
		warning(0, "DLR: using default 'internal' for storage type.");
	}

	/* call the sub-init routine */
	if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0)
	{

		handles = dlr_init_mysql(cfg);
		if(handles == NULL)
		{
			panic(0, "Handlers not found for mysql storage");
		}

	} 
	else
	{
		panic(0, "dlr type not supported");
	}
	/*
	   else if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
	   handles = dlr_init_sdb(cfg);
	   } else if (octstr_compare(dlr_type, octstr_imm("oracle")) == 0) {
	   handles = dlr_init_oracle(cfg);
	   } else if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
	   handles = dlr_init_mem(cfg);
	   } else if (octstr_compare(dlr_type, octstr_imm("pgsql")) == 0) {
	   handles = dlr_init_pgsql(cfg);
	   }
	   */
	/*
	 * add aditional types here
	 */

	if (handles == NULL) {
		panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
	}

	/* check needed function pointers */
	if (handles->dlr_add == NULL || handles->dlr_get == NULL || handles->dlr_remove == NULL)
		panic(0, "DLR: storage type '%s' don't implement needed functions", octstr_get_cstr(dlr_type));

	/* get info from storage */
	info(0, "DLR using storage type: %s", handles->type);

	/* cleanup */
	octstr_destroy(dlr_type);
}

/*
 * Shutdown dlr storage
 */
void dlr_shutdown()
{
	//printf("\nINSIDE dlr_shutdown()1\n");
	if (handles != NULL && handles->dlr_shutdown != NULL)
		handles->dlr_shutdown();
	//printf("\nINSIDE dlr_shutdown()2\n");
}

/* 
 * Return count waiting delivery entries or -1 if error occurs
 */
long dlr_messages(void)
{
	if (handles != NULL && handles->dlr_messages != NULL)
		return handles->dlr_messages();

	return -1;
}

/*
 * Return type of used dlr storage
 */
const char* dlr_type(void)
{
	if (handles != NULL && handles->type != NULL)
		return handles->type;

	return "unknown";
}

/*
 * Add new dlr entry into dlr storage
 */
void dlr_add(Octstr *account_name,Octstr *account_msg_id, Octstr* smsc_id,Octstr*smsc_msg_id)
{


	struct dlr_entry *dlr = NULL;

	if(octstr_len(smsc_id) == 0) 
	{
		warning(0, "DLR[%s]: Can't add a dlr without smsc-id", dlr_type());
		return;
	}


	if (handles == NULL || handles->dlr_add == NULL)
	{
		return;
	}


	dlr = dlr_entry_create();
	gw_assert(dlr != NULL);


	dlr->account_name = (account_name ? octstr_duplicate(account_name) : octstr_create(""));   
	dlr->account_msg_id = (account_msg_id ? octstr_duplicate(account_msg_id) : octstr_create(""));   
	dlr->smsc_id = (smsc_id ? octstr_duplicate(smsc_id) : octstr_create(""));   
	dlr->smsc_msg_id = (smsc_msg_id ? octstr_duplicate(smsc_msg_id) : octstr_create(""));   


	debug("dlr.dlr", 0, "DLR[%s]: DLR ADD Adding DLR acc name=%s, acc msg id=%s, smsc=%s, smsc msg id=%s",
			dlr_type(), 
			octstr_get_cstr(dlr->account_name), 
			octstr_get_cstr(dlr->account_msg_id),
			octstr_get_cstr(dlr->smsc_id), 
			octstr_get_cstr(dlr->smsc_msg_id)

	     );


	handles->dlr_add(dlr);



}
Conn_Msg *dlr_find(const Octstr *dest_addr,const Octstr *smscmsgid)
{

	Conn_Msg *msg=NULL;
	/* check if we have handler registered */
	if (handles == NULL || handles->dlr_get == NULL)
	{
		msg = conn_msg_create(values_got);
		msg->values_got.result = 0;
		msg->values_got.smsc_msg_id = octstr_duplicate(smscmsgid);
		msg->values_got.dest_addr = octstr_duplicate(dest_addr);
		return msg;
	}
	debug("dlr.dlr", 0, "DLR[%s]: Looking for DLR dest addr=%s, smsc msg id=%s",
			dlr_type(), 
			octstr_get_cstr(dest_addr), 
			octstr_get_cstr(smscmsgid));

	msg = handles->dlr_get(dest_addr, smscmsgid);
	if(msg == NULL )  
	{
		warning(0, "DLR[%s]: values not found for %s and smscmsgid :%snot found.",dlr_type(),octstr_get_cstr(dest_addr) ,octstr_get_cstr(smscmsgid));
		msg = conn_msg_create(values_got);
		msg->values_got.result = 0;
		msg->values_got.smsc_msg_id = octstr_duplicate(smscmsgid);
		msg->values_got.dest_addr = octstr_duplicate(dest_addr);
	}
	else if(msg->values_got.result == 1)	
	{
		//printf("\nDUMPING MESSAGE AT DLR\n\n");
		//conn_msg_dump(msg,0);
		debug("dlr.dlr", 0, "DLR[%s]: FOUND DLR account name=%s, account msg id=%s",
				dlr_type(), 
				octstr_get_cstr(msg->values_got.account_name), 
				octstr_get_cstr(msg->values_got.account_msg_id));

	}
	info(0,"Result of select querry result:%d smscid:%s smscmsgid:%s accname:%s accmsgid:%s",
			msg->values_got.result,
			msg->values_got.smsc_msg_id 	? octstr_get_cstr(msg->values_got.smsc_msg_id):"",
			msg->values_got.dest_addr 		? octstr_get_cstr(msg->values_got.dest_addr):"",
			msg->values_got.account_name 	? octstr_get_cstr(msg->values_got.account_name):"",
			msg->values_got.account_msg_id 	? octstr_get_cstr(msg->values_got.account_msg_id):""
	    );
	return msg;
}

void dlr_remove(const Octstr *smsc_id,const Octstr *smscmsgid)
{
	if (handles == NULL || handles->dlr_remove == NULL)
		return;

	handles->dlr_remove(smsc_id, smscmsgid);
}
void dlr_mis_add(Conn_Msg *msg)
{
	if (handles == NULL || handles->dlr_mis_add == NULL || msg == NULL || conn_msg_type(msg) != insert_mis)
		return;

	handles->dlr_mis_add(msg);
}


void dlr_smpp_esme(Conn_Msg *msg)
{
	if (handles == NULL || handles->dlr_smpp_esme == NULL || msg == NULL)
		return;

	handles->dlr_smpp_esme(msg);
}

void dlr_status_add(Conn_Msg *msg)
{
	if (handles == NULL || handles->dlr_status_add == NULL || msg == NULL)
		return;

	handles->dlr_status_add(msg);
}

void dlr_flush(void)
{
	info(0, "Flushing all %ld queued DLR messages in %s storage", dlr_messages(), 
			dlr_type());

	if (handles != NULL && handles->dlr_flush != NULL)
		handles->dlr_flush();
}




struct dlr_db_smpp_esme_fields *dlr_db_fields_create_smpp_esme(CfgGroup *grp)
{
	struct dlr_db_smpp_esme_fields *ret = NULL;

	ret = gw_malloc(sizeof(*ret));
	gw_assert(ret != NULL);
	memset(ret, 0, sizeof(*ret));

	if (!(ret->table = cfg_get(grp, octstr_imm("table"))))
		panic(0, "DLR: DB: directive 'table' is not specified!");

	if (!(ret->field_account_name = cfg_get(grp, octstr_imm("field-EsmeAccountName"))))
		panic(0, "DLR: DB: directive 'field-EsmeAccountName' is not specified!");

	if (!(ret->field_account_msg_id = cfg_get(grp, octstr_imm("field-EsmeMsgId"))))
		panic(0, "DLR: DB: directive 'field-EsmeMsgId' is not specified!");


	if (!(ret->field_submit_status = cfg_get(grp, octstr_imm("field-SubmitStat"))))
		panic(0, "DLR: DB: directive 'field-SubmitStat' is not specified!");

	if (!(ret->field_source_addr = cfg_get(grp, octstr_imm("field-SourceAddr"))))
		panic(0, "DLR: DB: directive 'field-SourceAddr' is not specified!");

	if (!(ret->field_dest_addr = cfg_get(grp, octstr_imm("field-DestAddr"))))
		panic(0, "DLR: DB: directive 'field-DestAddr' is not specified!");


	if (!(ret->field_msg_content = cfg_get(grp, octstr_imm("field-MsgContent"))))
		panic(0, "DLR: DB: directive 'field-MsgContent' is not specified!");

	if (!(ret->field_udh = cfg_get(grp, octstr_imm("field-UDH"))))
		panic(0, "DLR: DB: directive 'field-UDH' is not specified!");


	if (!(ret->field_recv_time = cfg_get(grp, octstr_imm("field-DatetimeRecv"))))
		panic(0, "DLR: DB: directive 'field-DatetimeRecv' is not specified!");

	if (!(ret->field_retry_value = cfg_get(grp, octstr_imm("field-RetryValue"))))
		panic(0, "DLR: DB: directive 'field-RetryValue' is not specified!");
	if (!(ret->allowed_smsc = cfg_get(grp, octstr_imm("allowed_smsc"))))
		panic(0, "DLR: DB: directive 'allowed_smsc' is not specified!");

	if (!(ret->preferred_smsc = cfg_get(grp, octstr_imm("preferred_smsc"))))
		panic(0, "DLR: DB: directive 'preferred_smsc' is not specified!");

	if (!(ret->field_dnd_flag = cfg_get(grp, octstr_imm("field-dnd-flag"))))
		panic(0, "DLR: DB: directive 'field-dnd-flag' is not specified!");	

if (!(ret->field_data_coding = cfg_get(grp, octstr_imm("field-data-coding"))))
                panic(0, "DLR: DB: directive 'field-data-coding' is not specified!");

	if (!(ret->field_esm_class = cfg_get(grp, octstr_imm("field-esm-class"))))
                panic(0, "DLR: DB: directive 'field-esm-class' is not specified!");

	if (!(ret->field_more_messages_to_send = cfg_get(grp, octstr_imm("field-more-messages-to-send"))))
                panic(0, "DLR: DB: directive 'field-more-messages-to-send' is not specified!");

	debug("dlr_detail_add : dlr.c",0,"dlr db SMPP ESME fields created");
	return ret;
}
void dlr_db_fields_destroy_smpp_esme(struct dlr_db_smpp_esme_fields *fields)
{
	/* sanity check */
	if (fields == NULL)
		return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

	O_DELETE(fields->table);
	O_DELETE(fields->field_account_name);
	O_DELETE(fields->field_account_msg_id);
	O_DELETE(fields->field_submit_status);
	O_DELETE(fields->field_source_addr);
	O_DELETE(fields->field_dest_addr);
	O_DELETE(fields->field_recv_time);
	O_DELETE(fields->field_msg_content);
	O_DELETE(fields->field_udh);
	O_DELETE(fields->field_retry_value);	
#undef O_DELETE

	gw_free(fields);
}
void dlr_check_conn(void)
{
	if (handles != NULL && handles->dlr_check_conn != NULL)
		handles->dlr_check_conn();
}
