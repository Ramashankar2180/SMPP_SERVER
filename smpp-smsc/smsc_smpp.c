/***********************************************************************************/
/* Copyright (c) 2008 Times Internet Limited.                                      */
/* All rights reserved.                                                            */
/*                                                                                 */
/* File Name    : smsc_smpp.c                                                      */
/* Module Name  : SMPP Client                                                      */
/* Project Name : Proxy SMPP Server                                                */
/*                                                                                 */
/* Description  : This file contains smpp structure and working of smpp protocol   */
/*                                                                                 */
/* Author       : Anil Kumar Singh,Jigisha Raje,Silky Sachdeva                     */
/*                                                                                 */
/* Start date   : 04-04-2008                                                       */
/*                                                                                 */
/***********************************************************************************/
#include "gwlib/gwlib.h"
#include "gwlib/smpp_pdu.h"
#include "include/smscconn_p.h"
#include "include/smpp_client.h"
#include "include/msg.h"
#include "include/smscconn.h"
#include "gwlib/process_sock.h"

#define DEBUG 1

#ifndef DEBUG
#define dump_pdu(msg, id, pdu) do{}while(0)
#else
/** This version does dump. */
#define dump_pdu(msg, id, pdu)                  \
	do {                                        \
		debug("bb.sms.smpp", 0, "SMPP[%s]: %s", \
				octstr_get_cstr(id), msg);          \
		smpp_pdu_dump(pdu);                     \
	} while(0)
#endif


/*
 * Some defaults.
 */

#define SMPP_ENQUIRE_LINK_INTERVAL  30.0
#define SMPP_MAX_PENDING_SUBMITS    10 //10 was default changed for testing with max
#define SMPP_DEFAULT_VERSION        0x34
#define SMPP_DEFAULT_PRIORITY       0
#define SMPP_THROTTLING_SLEEP_TIME  15
#define SMPP_DEFAULT_CONNECTION_TIMEOUT  10 * SMPP_ENQUIRE_LINK_INTERVAL
#define SMPP_DEFAULT_WAITACK        60
#define SMPP_DEFAULT_SHUTDOWN_TIMEOUT 30


/* 
 * Some defines
 */
#define SMPP_WAITACK_RECONNECT      0x00
#define SMPP_WAITACK_REQUEUE        0x01
#define SMPP_WAITACK_NEVER_EXPIRE   0x02

/**********************************************************************************************/
/* Implementation of the actual SMPP protocol: reading and writing PDUs in the correct order. */
/**********************************************************************************************/

/**************************************************************************************/
/*        Declaration of smpp structure                                               */
/**************************************************************************************/
extern sys_thread;
extern Counter *incoming_msg_counter;
extern Counter *outgoing_msg_counter;
extern dndcheck_flag;

typedef struct {
	long transmitter;
	long receiver;
	List *msgs_to_send;
	Dict *sent_msgs; 
	List *received_msgs; 
	Counter *message_id_counter; 
	Octstr *host; 
	Octstr *system_type; 
	Octstr *username; 
	Octstr *password; 
	Octstr *address_range; 
	Octstr *service_type;
	Octstr *masking_prefix;
	int source_addr_ton; 
	int source_addr_npi; 
	int dest_addr_ton; 
	int dest_addr_npi;
	long bind_addr_ton;
	long bind_addr_npi;
	int transmit_port; 
	int receive_port; 
	int quitting; 
	long enquire_link_interval;
	long max_pending_submits;
	int version;
	int priority;       /* set default priority for messages */    
	int validityperiod;    
	time_t throttling_err_time;
	int smpp_msg_id_type;  /* msg id in C string, hex or decimal */
	int autodetect_addr;
	Octstr *alt_charset;
	Octstr *alt_addr_charset;
	long connection_timeout;
	long wait_ack;
	int wait_ack_action;
	int deliver_sm_source;
	SMSCConn *conn; 
} SMPP; 


struct smpp_msg 
{
	time_t sent_time;
	Msg *msg;
};


/**************************************************************************************/
/* Forward declaration of local functions used in this file                           */
/**************************************************************************************/

static long decode_integer_value(Octstr *os, long pos, int octets);
static SMPP *smpp_create(SMSCConn *conn, Octstr *host, int transmit_port,  
		int receive_port, Octstr *system_type, 
		Octstr *username, Octstr *password, 
		Octstr *address_range,
		int source_addr_ton, int source_addr_npi,  
		int dest_addr_ton, int dest_addr_npi, 
		int enquire_link_interval, int max_pending_submits, 
		int version, int priority, int validity,
		int smpp_msg_id_type, 
		int autodetect_addr, Octstr *alt_charset, Octstr *alt_addr_charset,
		Octstr *service_type, long connection_timeout,
		long wait_ack, int wait_ack_action,Octstr *masking_prefix,int deliver_sm_source);

static void send_messages(SMPP *smpp, Connection *conn, long *pending_submits);
static void smpp_destroy(SMPP *smpp);
static int read_pdu(SMPP *smpp, Connection *conn, long *len, SMPP_PDU **pdu);
static long smpp_status_to_smscconn_failure_reason(long status);
static void send_enquire_link(SMPP *smpp, Connection *conn, long *last_sent);
static void send_unbind(SMPP *smpp, Connection *conn);
static int send_pdu(Connection *conn, Octstr *id, SMPP_PDU *pdu);
static Connection *open_transmitter(SMPP *smpp);
static Connection *open_receiver(SMPP *smpp);
static Connection *open_transceiver(SMPP *smpp);
static struct io_arg *io_arg_create(SMPP *smpp, int transmitter);
static void io_thread(void *arg);
static long smscconn_failure_reason_to_smpp_status(long reason);
static void handle_pdu(SMPP *smpp, Connection *conn, SMPP_PDU *pdu,long *pending_submits);
static long queued_cb(SMSCConn *conn);
static int shutdown_cb(SMSCConn *conn, int finish_sending);
static int send_msg_cb(SMSCConn *conn, Msg *msg);
static void handle_deliver_sm_msgid(SMPP *smpp,Octstr **msgid);

/**************************************************************************************/
/* Purpose : function used to create smpp msg.                                        */
/* Input   : octstr message what we got from recv queue.                              */
/* Output  : pointer to SMPP structure.                                               */ 
/**************************************************************************************/

static struct smpp_msg* smpp_msg_create(Msg *msg)
{
	struct smpp_msg *result = gw_malloc(sizeof(struct smpp_msg));
	gw_assert(result != NULL);

	/*
	   result->msg = gw_malloc(sizeof(Msg));
	   gw_assert(result->msg != NULL);
	   */
	result->msg = msg;

	//result->msg->sm_info = msg->sm_info;
	result->sent_time = time(NULL);//corrected on 2_2_09
	/*
	   result->msg->routing_info = NULL;
	   result->msg->retries = 0;
	   */
	return result;
}

void printdataval(SMPP *smpp)
{
	printf("\n\n username is  = %s apssword is = %s host = %s",octstr_get_cstr(smpp->username),octstr_get_cstr(smpp->password),octstr_get_cstr(smpp->host));
}


/**************************************************************************************/
/* Purpose : function used to destroy smpp message.                                   */
/* Input   : smpp msessage and indicator to destroy the message.                      */
/**************************************************************************************/

static void smpp_msg_destroy(struct smpp_msg *msg, int destroy_msg)
{
	if (msg == NULL)
		return;

	if (destroy_msg && msg->msg != NULL)
		msg_destroy(msg->msg);

	gw_free(msg);
}

/**************************************************************************************/
/* Purpose : function used to initialise smpp structure.                              */
/* Input   : parameters related to configuration of smpp structure.                   */
/* Output  : pointer to SMPP structure.                                               */ 
/**************************************************************************************/

static SMPP *smpp_create(SMSCConn *conn, Octstr *host, int transmit_port,  
		int receive_port, Octstr *system_type,  
		Octstr *username, Octstr *password, 
		Octstr *address_range,
		int source_addr_ton, int source_addr_npi,  
		int dest_addr_ton, int dest_addr_npi, 
		int enquire_link_interval, int max_pending_submits, 
		int version, int priority, int validity,
		int smpp_msg_id_type, 
		int autodetect_addr, Octstr *alt_charset, Octstr *alt_addr_charset,
		Octstr *service_type, long connection_timeout,
		long wait_ack, int wait_ack_action,Octstr *masking_prefix,int deliver_sm_source) 
{ 
	SMPP *smpp;      
	smpp = gw_malloc(sizeof(*smpp)); 
	smpp->transmitter = -1; 
	smpp->receiver = -1; 
	smpp->msgs_to_send = gwlist_create(); 
	smpp->sent_msgs = dict_create(max_pending_submits, NULL); 
	gwlist_add_producer(smpp->msgs_to_send); 
	smpp->received_msgs = gwlist_create(); 
	smpp->message_id_counter = counter_create(); 
	counter_increase(smpp->message_id_counter);
	smpp->host = octstr_duplicate(host); 
	smpp->system_type = octstr_duplicate(system_type); 
	smpp->username = octstr_duplicate(username); 
	smpp->password = octstr_duplicate(password); 
	smpp->address_range = octstr_duplicate(address_range); 
	smpp->source_addr_ton = source_addr_ton; 
	smpp->source_addr_npi = source_addr_npi; 
	smpp->dest_addr_ton = dest_addr_ton; 
	smpp->dest_addr_npi = dest_addr_npi; 
	smpp->service_type = octstr_duplicate(service_type);
	smpp->transmit_port = transmit_port; 
	smpp->receive_port = receive_port; 
	smpp->enquire_link_interval = enquire_link_interval;
	smpp->max_pending_submits = max_pending_submits; 
	smpp->quitting = 0; 
	smpp->version = version;
	smpp->priority = priority;
	smpp->validityperiod = validity;
	smpp->conn = conn; 
	smpp->throttling_err_time = 0;
	smpp->smpp_msg_id_type = smpp_msg_id_type;    
	smpp->autodetect_addr = autodetect_addr;
	smpp->alt_charset = octstr_duplicate(alt_charset);
	smpp->alt_addr_charset = octstr_duplicate(alt_addr_charset);
	smpp->connection_timeout = connection_timeout;
	smpp->wait_ack = wait_ack;
	smpp->wait_ack_action = wait_ack_action;
	smpp->bind_addr_ton = 0;
	smpp->bind_addr_npi = 0;
	smpp->masking_prefix = octstr_duplicate(masking_prefix);
	smpp->deliver_sm_source = deliver_sm_source;
	debug("smsc_smpp.c",0,"DELIVER-SM-SOURCE :%d-------",smpp->deliver_sm_source);
	return smpp; 
} 

/**************************************************************************************/
/* Purpose : function used to destroy the SMPP structure.                             */
/* Input   : SMPP Structure.                                                          */
/**************************************************************************************/

static void smpp_destroy(SMPP *smpp) 
{ 
	if (smpp != NULL) { 
		gwlist_destroy(smpp->msgs_to_send, msg_destroy); 
		dict_destroy(smpp->sent_msgs); 
		gwlist_destroy(smpp->received_msgs, msg_destroy); 
		counter_destroy(smpp->message_id_counter); 
		octstr_destroy(smpp->host); 
		octstr_destroy(smpp->username); 
		octstr_destroy(smpp->password); 
		octstr_destroy(smpp->system_type); 
		octstr_destroy(smpp->service_type); 
		octstr_destroy(smpp->address_range); 
		octstr_destroy(smpp->alt_charset);
		octstr_destroy(smpp->alt_addr_charset);
		octstr_destroy(smpp->masking_prefix);
		gw_free(smpp); 
	} 
} 


/**************************************************************************************/
/* Purpose : function used to Try to read an SMPP PDU from a Connection.              */
/* Input   : SMPP structure Connection structure and smpp pdu with lenght.            */
/* Output  : Return -1 for error.                                                     */ 
/*         : Return 0 for no PDU to ready yet.                                        */
/*         : Return  1 for PDU read and unpacked.                                     */
/**************************************************************************************/

static int read_pdu(SMPP *smpp, Connection *conn, long *len, SMPP_PDU **pdu) 
{ 
	Octstr *os; 
	/*
	   debug("bb.sms.smpp", 0, "READING PDUs--------:%ld",gwlist_len(smpp->msgs_to_send));
	   printf("READING PDUs--------:%ld\n",gwlist_len(smpp->msgs_to_send));
	   */
	if (*len == 0) { 
		*len = smpp_pdu_read_len(conn); 
		if (*len == -1) { 
			error(0, "SMPP[%s]: Server sent garbage, ignored.",
					octstr_get_cstr(smpp->conn->id)); 
			return -1; 
		} else if (*len == 0) { 
			if (conn_eof(conn) || conn_error(conn)) 
				return -1; 
			return 0; 
		} 
	} 

	os = smpp_pdu_read_data(conn, *len); 
	if (os == NULL) { 
		if (conn_eof(conn) || conn_error(conn)) 
			return -1; 
		return 0; 
	} 
	*len = 0; 

	*pdu = smpp_pdu_unpack(os); 
	if (*pdu == NULL) { 
		error(0, "SMPP[%s]: PDU unpacking failed.",
				octstr_get_cstr(smpp->conn->id)); 
		debug("bb.sms.smpp", 0, "SMPP[%s]: Failed PDU follows.",
				octstr_get_cstr(smpp->conn->id)); 
		octstr_dump(os, 0); 
		octstr_destroy(os);
		return -1;
	}

	octstr_destroy(os);
	return 1;
}

/**************************************************************************************/
/* Purpose : function used to return the status of smpp connection.                   */
/* Input   : status of smpp.                                                          */
/* Output  : return corresponding status of smscconnetion.                            */ 
/**************************************************************************************/


static long smpp_status_to_smscconn_failure_reason(long status)
{
	switch(status) {
		case SMPP_ESME_RMSGQFUL:
		case SMPP_ESME_RTHROTTLED:
		case SMPP_ESME_RX_T_APPN:
		case SMPP_ESME_RSYSERR:
			return SMSCCONN_FAILED_TEMPORARILY;
			break;

		default:
			return SMSCCONN_FAILED_REJECTED;
	}
}



/**************************************************************************************/
/* Purpose : function used to send the enquire link pdu to smsc.                      */
/* Input   : smpp structure with connection structre and time of last sent enquire    */
/*         : pdu.                                                                     */ 
/**************************************************************************************/

static void send_enquire_link(SMPP *smpp, Connection *conn, long *last_sent)
{
	SMPP_PDU *pdu;
	Octstr *os;

	if (date_universal_now() - *last_sent < smpp->enquire_link_interval)
		return;
	*last_sent = date_universal_now();

	pdu = smpp_pdu_create(enquire_link, counter_increase(smpp->message_id_counter));
	dump_pdu("Sending enquire link:", smpp->conn->id, pdu);
	os = smpp_pdu_pack(pdu);
	if (os)
		conn_write(conn, os); 
	octstr_destroy(os);
	smpp_pdu_destroy(pdu);
}


/**************************************************************************************/
/* Purpose : function used to send unbind pdu to smsc.                                */
/* Input   : smpp structure and connection structure.                                 */
/**************************************************************************************/

static void send_unbind(SMPP *smpp, Connection *conn)
{
	SMPP_PDU *pdu;
	Octstr *os;
	pdu = smpp_pdu_create(unbind, counter_increase(smpp->message_id_counter));
	dump_pdu("Sending unbind:", smpp->conn->id, pdu);
	os = smpp_pdu_pack(pdu);
	conn_write(conn, os);
	octstr_destroy(os);
	smpp_pdu_destroy(pdu);
}


/**************************************************************************************/
/* Purpose : function used to send the pdu to smsc.                                   */
/* Input   : smsc id with connection structure and smpp pdu.                          */
/* Output  : 0 if successfully written to socket.                                     */ 
/*         : -1 if an error occurs.                                                   */
/**************************************************************************************/


static int send_pdu(Connection *conn, Octstr *id, SMPP_PDU *pdu)
{
	Octstr *os;
	int ret;

	dump_pdu("Sending PDU:", id, pdu);
	os = smpp_pdu_pack(pdu);

	if (os)
		ret = conn_write(conn, os);   
	else
		ret = -1;
	octstr_destroy(os);
	return ret;
}

/**************************************************************************************/
/* Purpose : function used to send the messages to smsc.                              */
/* Input   : smpp structure and connection structure and original message.            */
/* Output  : 0 if successfully written in to socket.                                  */
/*         : -1 if an error occurs.                                                   */ 
/**************************************************************************************/


static void send_messages(SMPP *smpp, Connection *conn, long *pending_submits)
{
	Msg *msg;
	SMPP_PDU *pdu;
	Octstr *os;
	double delay = 0;
	int result;
	Octstr *msg1;
	Octstr *source;
	Octstr *new_source;
	Octstr *new_os;

	if (*pending_submits == -1){
		return;
	}
	while (*pending_submits < smpp->max_pending_submits) 
	{
		msg = gwlist_extract_first(smpp->msgs_to_send);
		if (msg == NULL)
			break;
		if(dndcheck_flag)
		{
			pdu = smpp_pdu_create(submit_sm,counter_increase(smpp->message_id_counter));

			pdu->u.submit_sm.service_type = octstr_duplicate(smpp->service_type);

			if(smpp->source_addr_ton != -1)
				pdu->u.submit_sm.source_addr_ton = smpp->source_addr_ton;
			else
				pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_NATIONAL; // national 


			if(smpp->source_addr_npi != -1)
				pdu->u.submit_sm.source_addr_npi = smpp->source_addr_npi;
			else
				pdu->u.submit_sm.source_addr_npi = GSM_ADDR_TON_NATIONAL; // national

			if(smpp->dest_addr_ton != -1)
				pdu->u.submit_sm.dest_addr_ton = smpp->dest_addr_ton;
			else
				pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_NATIONAL; // national 

			if(smpp->dest_addr_npi != -1)
				pdu->u.submit_sm.dest_addr_npi = smpp->dest_addr_npi;
			else
				pdu->u.submit_sm.dest_addr_npi = GSM_ADDR_NPI_E164; // ISDN number plan 
			pdu->u.submit_sm.sm_length = octstr_len(msg->routing_info->content);
			pdu->u.submit_sm.registered_delivery = 1;
			pdu->u.submit_sm.priority_flag = smpp->priority;
			pdu->u.submit_sm.data_coding = msg->routing_info->data_coding;
			pdu->u.submit_sm.esm_class = msg->routing_info->esm_class;
			pdu->u.submit_sm.more_messages_to_send = msg->routing_info->more_messages_to_send;
			pdu->u.submit_sm.short_message = octstr_duplicate(msg->routing_info->content);
	/*		if (octstr_len(msg->routing_info->udhdata)){
				Octstr *message , *message1;
				char *a=octstr_get_cstr(msg->routing_info->udhdata);
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
				octstr_insert(pdu->u.submit_sm.short_message,message,0);
			}*/
			pdu->u.submit_sm.source_addr=octstr_create(msg->routing_info->source_addr);			
			pdu->u.submit_sm.destination_addr=octstr_create(msg->routing_info->destination_addr);
			//smpp_pdu_dump(pdu);		
			Octstr *os = smpp_pdu_pack(pdu);	
			msg->sm_info->pdu = os;

		}
		if(msg->sm_info == NULL || msg->sm_info->pdu == NULL)
			continue;
		gw_assert(msg->sm_info->pdu != NULL);
		msg1 = octstr_from_position(msg->sm_info->pdu,4);
		pdu = smpp_pdu_unpack(msg1);
		octstr_destroy(msg1);
		if (pdu == NULL) 
		{
			panic(0, "SMPP: SMPP PDU Unpacking Failed.");
			break;
		}
		result = strcmp(pdu->type_name,"submit_sm");
		if(result == 0)
		{
			//printf("------------  BEFORE CHARSET LATIN1 TO GSM    -----------------------------");	

			//Check if UDH Flag is present then
#if 0
			//if(pdu->u.submit_sm.esm_class & 0x40 )	
			if(1)
			{
				long v1,v2 = 0;
				Octstr *udh;
				Octstr *value;
				Octstr *new_text;
				Octstr *temp = NULL;

				/*copy the message into the octstr:octstr memcpy is used cos it has binary data*/
				value = octstr_memcpy(octstr_get_cstr(pdu->u.submit_sm.short_message),octstr_len(pdu->u.submit_sm.short_message));

				/*get the length of UDH : First byte in the short message field*/
				v1 = decode_integer_value(value,0,1);
				/*read 3rd byte in the UDH:if UDH length is less than 3
				  message will be treated as binary message*/
				if(v1 >= 3)
					v2 = decode_integer_value(value,2,1);

				printf(" ====>>>>> SMS len :%d,UDH Length:%ld",octstr_len(value),v1);

				/*get UDH from 0th position to UDH len + 1 as UDHs first byte is its length */
				udh = get_octstr(value,0,v1+1);

				//octstr_destroy(udh);
				//octstr_destroy(short_msg);
				new_text = octstr_from_position(value,v1+1);
				printf("\n ====>>>> new_text: %s	\n",octstr_get_cstr(new_text));

				exit(0);			  

				//octstr_destroy(pdu->u.submit_sm.short_message);

				charset_latin1_to_gsm(new_text);

				//concatenate two messages

				pdu->u.submit_sm.short_message=octstr_duplicate(new_text);
				temp=octstr_duplicate(new_text);


				//charset_latin1_to_gsm(pdu->u.submit_sm.short_message);
			}		   
			else
#endif

				if(smpp->alt_charset)
				{
					if(pdu->u.submit_sm.esm_class & 0x40 )	
					{
						//printf("\n Containing UDH part (LONG MESSAGE) \n");	
						charset_latin1_to_gsm_new(pdu->u.submit_sm.short_message);
					}
					else
					{
						//printf("\n Doesnot contain UDH (SHORT MESSAGE)	\n");
						charset_latin1_to_gsm(pdu->u.submit_sm.short_message);
					}	   
				}   

			/*1. Change the sequence number*/
			pdu->u.submit_sm.sequence_number = counter_increase(smpp->message_id_counter);
			/*2. Change ton/npi values*/
			if(smpp->source_addr_ton != -1)
				pdu->u.submit_sm.source_addr_ton = smpp->source_addr_ton;
			if(smpp->source_addr_npi != -1)
				pdu->u.submit_sm.source_addr_npi = smpp->source_addr_npi;
			if(smpp->dest_addr_ton != -1)
				pdu->u.submit_sm.dest_addr_ton = smpp->dest_addr_ton;
			if(smpp->dest_addr_npi != -1)
				pdu->u.submit_sm.dest_addr_npi = smpp->dest_addr_npi;


			/*3. check if UDH indicator is set for the message submit*/
			if( (pdu->u.submit_sm.esm_class & ESM_CLASS_DELIVER_UDH_INDICATOR	)&&
					(compare_change_string(smpp->conn->id) == 0)					)
			{
				debug("bb.sms.smpp", 0, "UDH value will be changed from :%lx to %lx for %s.",\
						pdu->u.submit_sm.esm_class,ESM_CLASS_DELIVER_UDH_INDICATOR,octstr_get_cstr(smpp->conn->id)); 
				pdu->u.submit_sm.esm_class = ESM_CLASS_DELIVER_UDH_INDICATOR ;

			}

			source = octstr_create(msg->routing_info->source_addr);	

			/*
			   printf("\n------INSIDE SENDING PDU SOURCE ADDR IS:%s :%d:%s:%s:%d------\n",\
			   msg->routing_info->source_addr,pdu->u.submit_sm.sequence_number,octstr_get_cstr(smpp->masking_prefix),
			   octstr_get_cstr(source),
			   octstr_isnum(source));
			   */
			/*4. Add masking prefix*/
			if  (   (smpp->masking_prefix != NULL ) &&
					((octstr_isnum(source) == 0) || (octstr_len(source)   <   10) )
			    )
			{

				new_source = octstr_duplicate(smpp->masking_prefix);
				if(octstr_len(source) > 8)
					octstr_append_data(new_source,octstr_get_cstr(source),8);
				else
					octstr_append(new_source,source);
				octstr_destroy(pdu->u.submit_sm.source_addr);
				pdu->u.submit_sm.source_addr = new_source;
				new_source = NULL;	
				//info(0, "Changing source from :%s to %s.",octstr_get_cstr(source),octstr_get_cstr(pdu->u.submit_sm.source_addr));
			}
			octstr_destroy(source);
			octstr_destroy(msg->sm_info->pdu);
			new_os = smpp_pdu_pack(pdu);
			msg->sm_info->pdu = new_os;
		}
		if (send_pdu(conn, smpp->conn->id, pdu) == 0) 
		{
			/*message sent successfully change the message state*/
			msg->msg_state = MSG_SENT_NORESP;
			msg->retries++;
			struct smpp_msg *smpp_msg = smpp_msg_create(msg);
			os = octstr_format("%ld", pdu->u.submit_sm.sequence_number);
			dict_put(smpp->sent_msgs, os, smpp_msg);
			smpp_pdu_destroy(pdu);
			octstr_destroy(os);
			++(*pending_submits);

		}
		else { 
			smpp_pdu_destroy(pdu);
			break;
		}
	}
}


/**************************************************************************************/
/* Purpose : function used to Open transmission connection to SMS center.             */
/* Input   : SMPP structure                                                           */
/* Output  : return Return NULL for error,open Connection for OK.                     */ 
/**************************************************************************************/

static Connection *open_transmitter(SMPP *smpp)
{ 
	SMPP_PDU *bind; 
	Connection *conn; 


	conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->conn->our_host ); 
	if (conn == NULL) {
		error(0, "SMPP[%s]: Couldn't connect to server.",
				octstr_get_cstr(smpp->conn->id));
		return NULL; 
	} 

	bind = smpp_pdu_create(bind_transmitter, 
			counter_increase(smpp->message_id_counter)); 
	bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username); 
	bind->u.bind_transmitter.password = octstr_duplicate(smpp->password); 
	if (smpp->system_type == NULL) 
		bind->u.bind_transmitter.system_type = octstr_create("VMA"); 
	else 
		bind->u.bind_transmitter.system_type =  
			octstr_duplicate(smpp->system_type); 
	bind->u.bind_transmitter.interface_version = smpp->version;
	bind->u.bind_transmitter.address_range =  
		octstr_duplicate(smpp->address_range); 
	bind->u.bind_transmitter.addr_ton = smpp->bind_addr_ton;
	bind->u.bind_transmitter.addr_npi = smpp->bind_addr_npi;
	send_pdu(conn, smpp->conn->id, bind); 
	smpp_pdu_destroy(bind); 
	info(0,"Bind transmitter Sent"); 
	return conn; 
} 


/**************************************************************************************/
/* Purpose : function used to Open transceiver connection to SMS center.              */
/* Input   : SMPP structre                                                            */
/* Output  : return Return NULL for error,open Connection for OK.                     */ 
/**************************************************************************************/

static Connection *open_transceiver(SMPP *smpp) 
{ 
	SMPP_PDU *bind;
	Connection *conn; 

	conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->conn->our_host ); 
	if (conn == NULL) {  
		error(0, "SMPP[%s]: Couldn't connect to server.",
				octstr_get_cstr(smpp->conn->id));
		return NULL; 
	} 

	bind = smpp_pdu_create(bind_transceiver, 
			counter_increase(smpp->message_id_counter)); 
	bind->u.bind_transceiver.system_id = octstr_duplicate(smpp->username); 
	bind->u.bind_transceiver.password = octstr_duplicate(smpp->password); 
	if (smpp->system_type == NULL) 
		bind->u.bind_transceiver.system_type = octstr_create("VMA"); 
	else    
		bind->u.bind_transceiver.system_type = octstr_duplicate(smpp->system_type); 
	bind->u.bind_transceiver.interface_version = smpp->version;
	bind->u.bind_transceiver.address_range = octstr_duplicate(smpp->address_range); 
	bind->u.bind_transceiver.addr_ton = smpp->bind_addr_ton;
	bind->u.bind_transceiver.addr_npi = smpp->bind_addr_npi;
	send_pdu(conn, smpp->conn->id, bind); 
	smpp_pdu_destroy(bind); 
	info(0,"Bind  TransReceiver Sent"); 

	return conn; 
} 


/**************************************************************************************/
/* Purpose : function used to Open reception connection to SMS center.                */
/* Input   : SMPP structre                                                            */
/* Output  : return Return NULL for error,open Connection for OK.                     */ 
/**************************************************************************************/

static Connection *open_receiver(SMPP *smpp)
{
	SMPP_PDU *bind;
	Connection *conn;
	conn = conn_open_tcp(smpp->host, smpp->receive_port, smpp->conn->our_host );
	if (conn == NULL) {
		error(0, "SMPP[%s]: Couldn't connect to server.",
				octstr_get_cstr(smpp->conn->id));
		return NULL;
	}
	bind = smpp_pdu_create(bind_receiver,
			counter_increase(smpp->message_id_counter));
	bind->u.bind_receiver.system_id = octstr_duplicate(smpp->username);
	bind->u.bind_receiver.password = octstr_duplicate(smpp->password);
	if (smpp->system_type == NULL)
		bind->u.bind_receiver.system_type = octstr_create("VMA");
	else
		bind->u.bind_receiver.system_type =
			octstr_duplicate(smpp->system_type);
	bind->u.bind_receiver.interface_version = smpp->version;
	bind->u.bind_receiver.address_range =
		octstr_duplicate(smpp->address_range);
	bind->u.bind_receiver.addr_ton = smpp->bind_addr_ton;
	bind->u.bind_receiver.addr_npi = smpp->bind_addr_npi;
	send_pdu(conn, smpp->conn->id, bind);
	smpp_pdu_destroy(bind);
	info(0,"Bind  Receiver Sent"); 
	return conn;
}


/**************************************************************************************/
/* Purpose : function used to return smpp status.                                      */
/* Input   : status of smsc connection                                                */
/* Output  : return smpp status for particular reason.                                */ 
/**************************************************************************************/

static long smscconn_failure_reason_to_smpp_status(long reason)
{
	switch (reason) {
		case SMSCCONN_FAILED_REJECTED:
			return SMPP_ESME_RX_R_APPN;
		case SMSCCONN_SUCCESS:
			return SMPP_ESME_ROK;
		case SMSCCONN_FAILED_QFULL:
		case SMSCCONN_FAILED_TEMPORARILY:
			return SMPP_ESME_RX_T_APPN;
	}
	return SMPP_ESME_RX_T_APPN;
}

static void get_smscid(Octstr *sm,Octstr **smscmsgid1,int *stat)
{
	char buff[45];


	times_octstr_get_many_alphanumeric(buff,sm, 1);

	*smscmsgid1 = octstr_create(buff);

	memset(buff,0,sizeof(buff));

	times_octstr_get_many_chars(buff,sm,6);

	if(strcmp(buff,"DELIVRD")==0)
		*stat = 1;
	else if(strcmp(buff,"EXPIRED")==0)
		*stat = 2;
	else if(strcmp(buff,"DELETED")==0)
		*stat = 3;
	else if(strcmp(buff,"UNDELIV")==0)
		*stat = 4;
	else if(strcmp(buff,"ACCEPTD")==0)
		*stat = 5;
	else if(strcmp(buff,"UNKNOWN")==0)
		*stat = 6;
	else if(strcmp(buff,"REJECTD")==0)
		*stat = 7;
	else
		*stat = 10; 
}



static void handle_deliver_sm_msgid(SMPP *smpp,Octstr **msgid)
{

	Octstr *temp = NULL;

	debug("smpp",0,"msgid received is %s",octstr_get_cstr(*msgid));

	if (*msgid != NULL) {

		if (smpp->smpp_msg_id_type == -1) {
			return;
		} else {
			if ((smpp->smpp_msg_id_type & 0x02) || 
					(!octstr_check_range(*msgid, 0, octstr_len(*msgid), gw_isdigit))) {
				temp = octstr_format("%lu", strtoll(octstr_get_cstr(*msgid), NULL, 16));
			} else {
				temp = octstr_format("%lu", strtoll(octstr_get_cstr(*msgid), NULL, 10));
			}
		}

		octstr_destroy(*msgid);
		*msgid = temp;
		debug("smpp",0,"After Precess msgid received is %s",octstr_get_cstr(*msgid));
	} 

}

/**************************************************************************************/
/* Purpose : function used to handle the incoming pdu and generate appropriate        */
/*         : response.                                                                */
/* Input   : SMPP structre with connection structure and smpp pdu.                    */
/* Output  : return connection structure.                                             */ 
/**************************************************************************************/

static void handle_pdu(SMPP *smpp, Connection *conn, SMPP_PDU *pdu,
		long *pending_submits)
{
	SMPP_PDU *resp;
	Octstr *os;
	Msg *msg = NULL;
	struct reverse_queue_data *rqd_ptr;
	long reason;
	long cmd_stat;
	struct smpp_msg *smpp_msg = NULL;
	resp = NULL;
	Octstr *smscmsgid = NULL;
	Octstr *dest_addr = NULL;
	Octstr *new_dest_addr = NULL;
	switch (pdu->type) {
		case data_sm:
			os = smpp_pdu_pack(pdu);
			resp = smpp_pdu_create(data_sm_resp, pdu->u.data_sm.sequence_number);
			counter_increase(incoming_msg_counter);
			mutex_lock(smpp->conn->flow_mutex);
			if (smpp->conn->is_stopped) {
				mutex_unlock(smpp->conn->flow_mutex);
				resp->u.data_sm.command_status = SMPP_ESME_RX_T_APPN;
				break;
			}
			mutex_unlock(smpp->conn->flow_mutex);
			break;

		case deliver_sm:
			{	    
				struct mis_update mis_update;
				int status;
				int send_server = 0; 
				counter_increase(incoming_msg_counter);
				/*changed on 13/08/2009 - To handle deliver sm in message payload field*/
				if(pdu->u.deliver_sm.sm_length == 0 && pdu->u.deliver_sm.message_payload)
					get_smscid(pdu->u.deliver_sm.message_payload,&smscmsgid,&status);
				else
					get_smscid(pdu->u.deliver_sm.short_message,&smscmsgid,&status);

				handle_deliver_sm_msgid(smpp,&smscmsgid);
				octstr_destroy(pdu->u.deliver_sm.receipted_message_id);
				pdu->u.deliver_sm.receipted_message_id = octstr_duplicate(smscmsgid);		

				/*finding dest addr*/

				if(	smpp->deliver_sm_source == 1 && pdu->u.deliver_sm.source_addr )
					new_dest_addr = octstr_duplicate(pdu->u.deliver_sm.source_addr);

				else if(smpp->deliver_sm_source == 0 	&& pdu->u.deliver_sm.destination_addr)
					new_dest_addr = octstr_duplicate(pdu->u.deliver_sm.destination_addr);
				/*Insert what ever comes but send only valid number to server*/
				if(new_dest_addr != NULL && (octstr_len(new_dest_addr)) > 9 && (octstr_isnum(new_dest_addr) == 1))
				{
					int len;
					int location;
					len = octstr_len(new_dest_addr);
					location = len - 10;
					dest_addr = octstr_from_position(new_dest_addr,location);
					send_server = 1;
				}	

				else if(new_dest_addr == NULL)	
					error(0, "SMSC[%s]:RECEIVED DEST IN DELIVER SM: IS NULL ",octstr_get_cstr(smpp->conn->id));
				else if(octstr_len(new_dest_addr) < 10)
					error(0, "SMSC[%s]:RECEIVED DEST IN DELIVER SM:LEN INVALID :%s ",octstr_get_cstr(smpp->conn->id),octstr_get_cstr(new_dest_addr));
				else if(octstr_isnum(new_dest_addr) == 0)
					error(0, "SMSC[%s]:RECEIVED DEST IN DELIVER SM:ALPHA-NUMERIC INVALID :%s ",octstr_get_cstr(smpp->conn->id), octstr_get_cstr(new_dest_addr));

				/*
				   strcpy(mis_update.smsc_id,octstr_get_cstr(smpp->conn->id));
				   strcpy(mis_update.smsc_msg_id,octstr_get_cstr(smscmsgid));
				   if(dest_addr != NULL)
				   strcpy(mis_update.dest_addr,octstr_get_cstr(dest_addr));
				   else if(new_dest_addr != NULL)
				   strcpy(mis_update.dest_addr,octstr_get_cstr(new_dest_addr));

				   mis_update.deliver_status = status;

				   send_msg_mis_q(UPDATE_MIS_TABLE,0,0,&mis_update);
				   */
				Conn_Msg *msg;
				msg = conn_msg_create(insert_status);
				msg->insert_status.smsc_id = octstr_duplicate(smpp->conn->id);
				msg->insert_status.smsc_msg_id = octstr_duplicate(smscmsgid);
				if(dest_addr != NULL)
					msg->insert_status.dest_addr = octstr_duplicate(dest_addr);
				else if(new_dest_addr != NULL)
					msg->insert_status.dest_addr = octstr_duplicate(new_dest_addr);
				os = smpp_pdu_pack(pdu);
				msg->insert_status.pdu = os;
				msg->insert_status.deliver_status = status;
				send_msg_dbbox_server(msg,0);

				/* 
				   if(send_server == 1)
				   {
				   send_server = 0;
				   Conn_Msg *msg1;

				   os = smpp_pdu_pack(pdu);
				   msg1 = conn_msg_create(deliver);
				   msg1->deliver.smsc_id = octstr_duplicate(smpp->conn->id);
				   msg1->deliver.smsc_msg_id = octstr_duplicate(smscmsgid);
				   msg1->deliver.dest_addr = octstr_duplicate(dest_addr);
				   msg1->deliver.pdu = os;
				   send_msg_reverse_server(msg1);
				   }
				   */
				octstr_destroy(smscmsgid);
				if(dest_addr != NULL)
					octstr_destroy(dest_addr);
				if(new_dest_addr != NULL)
					octstr_destroy(new_dest_addr);
				dest_addr = NULL;
				new_dest_addr = NULL;

				mutex_lock(smpp->conn->flow_mutex);
				if (smpp->conn->is_stopped) {
					mutex_unlock(smpp->conn->flow_mutex);
					resp = smpp_pdu_create(deliver_sm_resp,
							pdu->u.deliver_sm.sequence_number);
					resp->u.deliver_sm.command_status = SMPP_ESME_RX_T_APPN;
					break;
				}
				mutex_unlock(smpp->conn->flow_mutex);
				if (pdu->u.deliver_sm.esm_class & (0x04|0x08)) {
					resp = smpp_pdu_create(deliver_sm_resp,
							pdu->u.deliver_sm.sequence_number);
					reason = SMSCCONN_SUCCESS;
					resp->u.deliver_sm_resp.command_status = smscconn_failure_reason_to_smpp_status(reason);
				}
				else
				{
					resp = smpp_pdu_create(deliver_sm_resp,
							pdu->u.deliver_sm.sequence_number);
					reason = SMSCCONN_SUCCESS;
					resp->u.deliver_sm_resp.command_status = smscconn_failure_reason_to_smpp_status(reason);
				}  
			}      
			break;

		case enquire_link:
			resp = smpp_pdu_create(enquire_link_resp,
					pdu->u.enquire_link.sequence_number);
			break;

		case enquire_link_resp:
			break;

		case submit_sm_resp:
			{            
				struct db_insert data_db;
				SMPP_PDU *submit_sm_pdu;
				Octstr *new_os = NULL;
				Octstr *tmp = NULL;
				os = octstr_format("%ld", pdu->u.submit_sm_resp.sequence_number);
				smpp_msg = dict_remove(smpp->sent_msgs, os);
				if (smpp_msg == NULL) 
				{
					warning(0, "SMPP[%s]: SMSC sent submit_sm_resp "
							"with wrong sequence number 0x%08lx",
							octstr_get_cstr(smpp->conn->id),
							pdu->u.submit_sm_resp.sequence_number);
					break;
				}
				debug("smsc_smpp.c", 0, "At Submit_sm Resp : Account Name = %s and Account Msg Id = %s Message id is :%s ", \
						octstr_get_cstr(smpp_msg->msg->sm_info->account_name ), 			\
						octstr_get_cstr(smpp_msg->msg->sm_info->account_msg_id),\
						octstr_get_cstr(pdu->u.submit_sm_resp.message_id));

				if(strncmp(octstr_get_cstr(pdu->u.submit_sm_resp.message_id),"S",1) == 0)
				{
					Octstr *new_id = NULL;
					new_id = octstr_from_position(pdu->u.submit_sm_resp.message_id,0);
					octstr_destroy(pdu->u.submit_sm_resp.message_id);
					pdu->u.submit_sm_resp.message_id = new_id;
				}

				if(smpp->smpp_msg_id_type == -1) 
				{
					tmp = octstr_duplicate(pdu->u.submit_sm_resp.message_id);

				} 
				else 
				{
					if ((smpp->smpp_msg_id_type & 0x01) || 
							(!octstr_check_range(pdu->u.submit_sm_resp.message_id, 0, 
									     octstr_len(pdu->u.submit_sm_resp.message_id), gw_isdigit))) {

						tmp = octstr_format("%lu", strtoll(  
									octstr_get_cstr(pdu->u.submit_sm_resp.message_id), NULL, 16));
					} else {

						tmp = octstr_format("%lu", strtoll( 
									octstr_get_cstr(pdu->u.submit_sm_resp.message_id), NULL, 10));
					}					 
				}

				new_os = octstr_from_position(smpp_msg->msg->sm_info->pdu,4);
				submit_sm_pdu = smpp_pdu_unpack(new_os);


				/*resp status of submit sm resp is added*/
				debug("test.smpp", 0, "----------BEFORE SENDING MESSAGE TO CLIENT---------222");	
				mis_db_insert(	smpp_msg->msg->sm_info->account_name,
						smpp_msg->msg->sm_info->account_msg_id,
						smpp_msg->msg->sm_info->req_time,
						smpp->conn->id,
						tmp,
						submit_sm_pdu,
						pdu->u.submit_sm_resp.command_status,smpp_msg->msg->sm_info->client_ip
					     );

				/* if pdu->u.submit_sm_resp.command_status !=0 create deliver_sm if possible */

				octstr_destroy(os);
				octstr_destroy(new_os);


				msg = smpp_msg->msg;
				msg->msg_state = MSG_SENT_RESP;
				smpp_msg_destroy(smpp_msg, 0);

				if (pdu->u.submit_sm_resp.command_status != 0) {
					error(0, "SMPP[%s]: SMSC returned error code 0x%08lx (%s) "
							"in response to submit_sm.",
							octstr_get_cstr(smpp->conn->id),
							pdu->u.submit_sm_resp.command_status,
							smpp_error_to_string(pdu->u.submit_sm_resp.command_status));
					reason = smpp_status_to_smscconn_failure_reason(
							pdu->u.submit_sm_resp.command_status);

					/*
					 * check to see if we got a "throttling error", in which case we'll just
					 * sleep for a while.
					 */
					if (pdu->u.submit_sm_resp.command_status == SMPP_ESME_RTHROTTLED)
						time(&(smpp->throttling_err_time));
					else
						smpp->throttling_err_time = 0;
					octstr_destroy(tmp);
					smscconn_send_failed(smpp->conn, msg, reason,submit_sm_pdu);
					--(*pending_submits);

				} 
				else 
				{

					strcpy(data_db.account_name,octstr_get_cstr(msg->sm_info->account_name));
					strcpy(data_db.account_msg_id,octstr_get_cstr(msg->sm_info->account_msg_id));
					strcpy(data_db.smsc_id,octstr_get_cstr(smpp->conn->id));
					strcpy(data_db.smsc_msg_id,octstr_get_cstr(tmp));
					//send_msg_database_q(DATABASE_MSGQ_TYPE,INSERT_VALUES,0,&data_db);/*mapping insert*/

					smscconn_sent(smpp->conn, msg,tmp,submit_sm_pdu);
					octstr_destroy(tmp);
					--(*pending_submits);
				}
				smpp_pdu_destroy(submit_sm_pdu); 
			}
			break;

		case bind_transmitter_resp:
			if (pdu->u.bind_transmitter_resp.command_status != 0) {
				error(0, "SMPP[%s]: SMSC rejected login to transmit, "
						"code 0x%08lx (%s).",
						octstr_get_cstr(smpp->conn->id),
						pdu->u.bind_transmitter_resp.command_status,
						smpp_error_to_string(pdu->u.bind_transmitter_resp.command_status));
				if (pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVSYSID ||
						pdu->u.bind_transmitter_resp.command_status == SMPP_ESME_RINVPASWD)
					smpp->quitting = 1;
			} else {
				info(0,"SMPP[%s]: Connection Details[ONLINE]-TX mode",octstr_get_cstr(smpp->conn->id));
				*pending_submits = 0;
				mutex_lock(smpp->conn->flow_mutex);
				smpp->conn->status = SMSCCONN_ACTIVE;
				smpp->conn->connect_time = time(NULL);
				mutex_unlock(smpp->conn->flow_mutex);
				smpp_client_smscconn_connected(smpp->conn);
			}
			break;

		case bind_transceiver_resp:
			if (pdu->u.bind_transceiver_resp.command_status != 0) {
				error(0, "SMPP[%s]: SMSC rejected login to transmit, "
						"code 0x%08lx (%s)",
						octstr_get_cstr(smpp->conn->id),
						pdu->u.bind_transceiver_resp.command_status,
						smpp_error_to_string(pdu->u.bind_transceiver_resp.command_status));
				if (pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVSYSID ||
						pdu->u.bind_transceiver_resp.command_status == SMPP_ESME_RINVPASWD)
					smpp->quitting = 1;
			} else {
				info(0,"SMPP[%s]: Connection Details[ONLINE]-TRX mode",octstr_get_cstr(smpp->conn->id));
				*pending_submits = 0;
				mutex_lock(smpp->conn->flow_mutex);
				smpp->conn->status = SMSCCONN_ACTIVE;
				smpp->conn->connect_time = time(NULL);
				mutex_unlock(smpp->conn->flow_mutex);
				smpp_client_smscconn_connected(smpp->conn);
			}
			break;

		case bind_receiver_resp:
			if (pdu->u.bind_receiver_resp.command_status != 0) {
				error(0, "SMPP[%s]: SMSC rejected login to receive, "
						"code 0x%08lx (%s).",
						octstr_get_cstr(smpp->conn->id),
						pdu->u.bind_receiver_resp.command_status,
						smpp_error_to_string(pdu->u.bind_receiver_resp.command_status));
				if (pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVSYSID ||
						pdu->u.bind_receiver_resp.command_status == SMPP_ESME_RINVPASWD)
					smpp->quitting = 1;
			} else {
				info(0,"SMPP[%s]: Connection Details[ONLINE]-RX mode",octstr_get_cstr(smpp->conn->id));

				mutex_lock(smpp->conn->flow_mutex);
				if (smpp->conn->status != SMSCCONN_ACTIVE) {
					smpp->conn->status = SMSCCONN_ACTIVE_RECV;
					smpp->conn->connect_time = time(NULL);
				}
				mutex_unlock(smpp->conn->flow_mutex);
			}
			break;

		case unbind:
			info(0,"SMPP[%s]: Connection Details[OFFLINE]-Received unbind",octstr_get_cstr(smpp->conn->id));
			resp = smpp_pdu_create(unbind_resp, pdu->u.unbind.sequence_number);
			mutex_lock(smpp->conn->flow_mutex);
			smpp->conn->status = SMSCCONN_DISCONNECTED;
			mutex_unlock(smpp->conn->flow_mutex);
			break;

		case unbind_resp:
			mutex_lock(smpp->conn->flow_mutex);
			smpp->conn->status = SMSCCONN_DISCONNECTED;
			mutex_unlock(smpp->conn->flow_mutex);
			break;

		case generic_nack:
			cmd_stat  = pdu->u.generic_nack.command_status;

			os = octstr_format("%ld", pdu->u.generic_nack.sequence_number);
			smpp_msg = dict_remove(smpp->sent_msgs, os);
			octstr_destroy(os);

			if (smpp_msg == NULL) {
				error(0, "SMPP[%s]: SMSC rejected last command"
						"code 0x%08lx (%s).",
						octstr_get_cstr(smpp->conn->id),
						cmd_stat,
						smpp_error_to_string(cmd_stat));
			} else {
				msg = smpp_msg->msg;
				smpp_msg_destroy(smpp_msg, 0);

				error(0, "SMPP[%s]: Generic Nack SMSC returned error code 0x%08lx (%s) "
						"in response to submit_sm.",
						octstr_get_cstr(smpp->conn->id),
						cmd_stat,
						smpp_error_to_string(cmd_stat));

				/*
				 * check to see if we got a "throttling error", in which case we'll just
				 * sleep for a while
				 */
				if (cmd_stat == SMPP_ESME_RTHROTTLED)
					time(&(smpp->throttling_err_time));
				else
					smpp->throttling_err_time = 0;

				reason = smpp_status_to_smscconn_failure_reason(cmd_stat);
				smscconn_send_failed(smpp->conn, msg, reason,NULL);
				--(*pending_submits);
			}
			break;
		default:
			error(0, "SMPP[%s]: Unknown PDU type 0x%08lx, ignored.",
					octstr_get_cstr(smpp->conn->id), pdu->type);
			/*
			   send gnack , see smpp3.4 spec., section 3.3
			   because we doesn't know what kind of pdu received, we assume generick_nack_resp
			   (header always the same)
			   */
			resp = smpp_pdu_create(generic_nack, pdu->u.generic_nack.sequence_number);
			resp->u.generic_nack.command_status = SMPP_ESME_RINVCMDID;
			break;
	}

	if (resp != NULL) {
		send_pdu(conn, smpp->conn->id, resp);
		smpp_pdu_destroy(resp);
	}
}


struct io_arg {
	SMPP *smpp;
	int transmitter;
};


static struct io_arg *io_arg_create(SMPP *smpp, int transmitter)
{
	struct io_arg *io_arg;

	io_arg = gw_malloc(sizeof(*io_arg));
	io_arg->smpp = smpp;
	io_arg->transmitter = transmitter;
	return io_arg;
}


/**************************************************************************************/
/* Purpose : function used to clean up the sent queue                                 */
/* Output  : return 1 if io_thread should reconnect; 0 if not                         */ 
/**************************************************************************************/


static int do_queue_cleanup(SMPP *smpp, long *pending_submits, int action)
{
	List *keys;
	Octstr *key;
	struct smpp_msg *smpp_msg;
	time_t now = time(NULL);

	if (*pending_submits <= 0)
		return 0;

	/* check if action set to wait ack for ever */
	if (action == SMPP_WAITACK_NEVER_EXPIRE)
		return 0;

	keys = dict_keys(smpp->sent_msgs);
	if (keys == NULL)
		return 0;

	while ((key = gwlist_extract_first(keys)) != NULL) {
		smpp_msg = dict_get(smpp->sent_msgs, key);
		if (smpp_msg != NULL && difftime(now, smpp_msg->sent_time) > smpp->wait_ack) {
			switch(action) {
				case SMPP_WAITACK_RECONNECT: /* reconnect */
					/* found at least one not acked msg */
					warning(0, "SMPP[%s]: Not ACKED message found, reconnecting.",
							octstr_get_cstr(smpp->conn->id));
					octstr_destroy(key);
					gwlist_destroy(keys, octstr_destroy_item);
					return 1; /* io_thread will reconnect */
				case SMPP_WAITACK_REQUEUE: /* requeue */
					smpp_msg = dict_remove(smpp->sent_msgs, key);
					if (smpp_msg != NULL) {
						warning(0, "SMPP[%s]: Not ACKED message found, will retransmit."
								" SENT<%ld>sec. ago, SEQ<%s>",
								octstr_get_cstr(smpp->conn->id),
								(long)difftime(now, smpp_msg->sent_time) ,
								octstr_get_cstr(key));
						smscconn_send_failed(smpp->conn, smpp_msg->msg, SMSCCONN_FAILED_TEMPORARILY,NULL);
						smpp_msg_destroy(smpp_msg, 0);
						(*pending_submits)--;
					}
					break;
				default:
					error(0, "SMPP[%s] Unknown clenup action defined %xd.",
							octstr_get_cstr(smpp->conn->id), action);
					octstr_destroy(key);
					gwlist_destroy(keys, octstr_destroy_item);
					return 0;
			}
		}
		octstr_destroy(key);
	}
	gwlist_destroy(keys, octstr_destroy_item);

	return 0;
}


/**************************************************************************************/
/* Purpose : This is the main function for the background thread for doing I/O on     */
/*         : one SMPP connection (the one for transmitting or receiving messages).    */
/*         : It makes the initial connection to the SMPP server and re-connects       */
/*         : if there are I/O errors or other errors that require it.                 */
/* Input   : NULL Pointer.                                                            */
/**************************************************************************************/

static void io_thread(void *arg)
{

	SMPP *smpp;
	struct io_arg *io_arg;
	int transmitter;
	Connection *conn;
	int ret;
	long last_enquire_sent;
	long pending_submits;
	long len;
	SMPP_PDU *pdu;
	double timeout;
	int smsc_status = 0;
	time_t last_response, last_cleanup;
	struct smsc_info *info_smsc;
	io_arg = arg;
	smpp = io_arg->smpp;
	transmitter = io_arg->transmitter;
	gw_free(io_arg);
	int status = 0;
	int test_value;
	conn = NULL;

	log_thread_to(smpp->conn->log_idx);


	while (!smpp->quitting) {

		if (transmitter == 1) 
		{	 
			smpp->conn->bind_sent = 1; 
			smpp->conn->mode = 1;
			conn = open_transmitter(smpp);
		}
		else if (transmitter == 2) 
		{
			smpp->conn->bind_sent = 1;
			smpp->conn->mode = 3;
			conn = open_transceiver(smpp);
		}
		else  
		{
			smpp->conn->bind_sent = 1;
			smpp->conn->mode = 2;
			conn = open_receiver(smpp);
		}

		last_enquire_sent = last_cleanup = last_response = date_universal_now();
		pending_submits = -1;
		len = 0;
		smpp->throttling_err_time = 0;

		for (;conn != NULL;) {
			/*
			   printf("----------IO THREAD WAKE UP----------------\n");
			   */
			timeout = last_enquire_sent + smpp->enquire_link_interval
				- date_universal_now();
			test_value = conn_wait(conn, timeout);
			if ( test_value == -1)
			{
				error(0,"SMPP[%s]: Connection Details[OFFLINE]-Socket Error",octstr_get_cstr(smpp->conn->id));
				break;
			}
			/*
			   printf("----------IO THREAD WAKE UP----------------1---value:%d timeout:%d---\n",test_value,timeout);		
			   */
			if (smpp->quitting) {
				send_unbind(smpp, conn);
				last_response = time(NULL);
				while(conn_wait(conn, 1.00) != -1 &&
						difftime(time(NULL), last_response) < SMPP_DEFAULT_SHUTDOWN_TIMEOUT &&
						smpp->conn->status != SMSCCONN_DISCONNECTED) {
					if (read_pdu(smpp, conn, &len, &pdu) == 1) {
						dump_pdu("Got PDU:", smpp->conn->id, pdu);
						handle_pdu(smpp, conn, pdu, &pending_submits);
						smpp_pdu_destroy(pdu);
					}
				}
				info(0, "SMPP[%s]: Connection Details[OFFLINE]-%s: break and shutting down",
						octstr_get_cstr(smpp->conn->id), __func__);
				break;
			}

			send_enquire_link(smpp, conn, &last_enquire_sent);

			while ((ret = read_pdu(smpp, conn, &len, &pdu)) == 1) {
				last_response = time(NULL);
				dump_pdu("Got PDU:", smpp->conn->id, pdu);
				handle_pdu(smpp, conn, pdu, &pending_submits);
				smpp_pdu_destroy(pdu);

				if(sys_thread != -1 && status != smpp->conn->status)
				{
					status = smpp->conn->status;
					gwthread_wakeup(sys_thread);
				}

				if (smpp->conn->status != SMSCCONN_ACTIVE && smpp->conn->status != SMSCCONN_ACTIVE_RECV) {
					ret = -1;
					break;
				}

				send_enquire_link(smpp, conn, &last_enquire_sent);

				if (transmitter && difftime(time(NULL), smpp->throttling_err_time) > SMPP_THROTTLING_SLEEP_TIME) {
					smpp->throttling_err_time = 0;
					send_messages(smpp, conn, &pending_submits);
				}
			}

			if (ret == -1) {
				error(0, "SMPP[%s]: Connection Details[OFFLINE]-I/O error or recvd unbind.Re-connecting.",
						octstr_get_cstr(smpp->conn->id));
				break;
			}

			if (ret == 0 && smpp->connection_timeout > 0 &&
					difftime(time(NULL), last_response) > smpp->connection_timeout) {
				error(0, "SMPP[%s]: Connection Details[OFFLINE]-No responses from SMSC within %ld sec. Reconnecting.",
						octstr_get_cstr(smpp->conn->id), smpp->connection_timeout);
				break;
			}

			if (transmitter && difftime(time(NULL), last_cleanup) > smpp->wait_ack) {
				if (do_queue_cleanup(smpp, &pending_submits, smpp->wait_ack_action))
					break; 
				last_cleanup = time(NULL);
			}

			if (transmitter && difftime(time(NULL), smpp->throttling_err_time) > SMPP_THROTTLING_SLEEP_TIME) {
				smpp->throttling_err_time = 0;
				send_messages(smpp, conn, &pending_submits);
			}
		}

		if (conn != NULL) 
		{
			conn_destroy(conn);
			conn = NULL;
			reset_smpp_counters(smpp->conn);
		}

		mutex_lock(smpp->conn->flow_mutex);
		smpp->conn->status = SMSCCONN_RECONNECTING;
		mutex_unlock(smpp->conn->flow_mutex);

		if (transmitter) 
		{
			Msg *msg;
			struct smpp_msg *smpp_msg;
			List *noresp;
			Octstr *key;
			int wake_thread = 0;

			long reason = (smpp->quitting?SMSCCONN_FAILED_SHUTDOWN:SMSCCONN_FAILED_TEMPORARILY);
			while((msg = gwlist_extract_first(smpp->msgs_to_send)) != NULL)
			{
				if(msg == NULL || msg->sm_info == NULL || msg->sm_info->pdu == NULL)
					continue;
				smscconn_send_failed(smpp->conn, msg, reason,NULL);
				wake_thread = 1;
			}
			noresp = dict_keys(smpp->sent_msgs);
			while((key = gwlist_extract_first(noresp)) != NULL) 
			{
				smpp_msg = dict_remove(smpp->sent_msgs, key);
				if (smpp_msg != NULL && smpp_msg->msg) 
				{
					smscconn_send_failed(smpp->conn, smpp_msg->msg, reason,NULL);
					smpp_msg_destroy(smpp_msg, 0);
					wake_thread = 1;
				}
				octstr_destroy(key);
			}
			gwlist_destroy(noresp, NULL);
			if(wake_thread)
				wake_router_thread();
		}
		if (!smpp->quitting) {

			if(sys_thread != -1 && status != smpp->conn->status)
			{
				status = smpp->conn->status;
				gwthread_wakeup(sys_thread);
			}	

			error(0, "SMPP[%s]: Couldn't connect to SMS center (retrying in %ld seconds).",
					octstr_get_cstr(smpp->conn->id), smpp->conn->reconnect_delay);
			gwthread_sleep(smpp->conn->reconnect_delay);
		}
	}
	mutex_lock(smpp->conn->flow_mutex);
	smpp->conn->status = SMSCCONN_DEAD;
	mutex_unlock(smpp->conn->flow_mutex);
}


/**************************************************************************************/
/* Functions called by smscconn.c via the SMSCConn function pointers.                 */
/**************************************************************************************/

/**************************************************************************************/
/* Purpose : function used to queue the incoming the message                          */
/* Input   : Connection structure                                                     */
/* Output  : returns load of this smsc conenction                                     */ 
/**************************************************************************************/

/*
   static long queued_cb(SMSCConn *conn)
   {
   SMPP *smpp;

   smpp = conn->data;
   conn->load = (smpp ? (conn->status != SMSCCONN_DEAD ?
   gwlist_len(smpp->msgs_to_send) : 0) : 0);

   return conn->load;
   }
   */


static long queued_cb(SMSCConn *conn)
{
	SMPP *smpp;
	long len = 0;
	smpp = conn->data;
	//conn->load = (smpp ? (conn->status != SMSCCONN_DEAD ?  (gwlist_len(smpp->msgs_to_send)+counter_value(conn->received)) : 0) : 0);
	if( (smpp) && (conn->status != SMSCCONN_DEAD))
	{
		len = gwlist_len(smpp->msgs_to_send);
		//conn->load = len + counter_value(conn->received)+dict_key_count(smpp->sent_msgs);
		conn->load = counter_value(conn->received);
	}
	return len;
}

static int waiting_cb(SMSCConn *conn)
{
	SMPP *smpp;
	int waiting;
	smpp = conn->data;
	waiting = (smpp ? (conn->status != SMSCCONN_DEAD ? dict_key_count(smpp->sent_msgs):0):0);

	return waiting;
}



/**************************************************************************************/
/* Purpose : function used to produce the incoming message to smpp structure          */
/* Input   : Connection structure and incoming msg                                    */
/* Output  : 0 if successfully produced                                               */ 
/**************************************************************************************/


static int send_msg_cb(SMSCConn *conn, Msg *msg1)
{
	SMPP *smpp;
	smpp = conn->data;
	printf("\n\n username is  1111111111111111111222222222222= %s apssword is = %s host = %s",octstr_get_cstr(smpp->username),octstr_get_cstr(smpp->password),octstr_get_cstr(smpp->host));
	debug("smsc-smpp",0,"SMSC Received msg Acc :%s,Acc msgid:%s",octstr_get_cstr(msg1->sm_info->account_name),octstr_get_cstr(msg1->sm_info->account_msg_id));

	/*   printf("send_msg_cb : OCTSTR LEN %d :%lx :%s :%s :%s\n",\

	     octstr_len(msg1->pdu),msg1->pdu,octstr_get_cstr(msg1->account_name),octstr_get_cstr(msg1->account_msg_id),octstr_get_cstr(msg1->req_time));
	     */ 
	gwlist_produce(smpp->msgs_to_send, msg_duplicate(msg1));

	/*gwlist_append(smpp->msgs_to_send, msg1); - Jigisha*/
	gwthread_wakeup(smpp->transmitter);

	debug("bb.sms.smpp", 0, "PRODUCING IN THE QUEUE-------------------(queued):%ld",gwlist_len(smpp->msgs_to_send));
	printf("\n\n username is  1111111111111111111= %s apssword is = %s host = %s",octstr_get_cstr(smpp->username),octstr_get_cstr(smpp->password),octstr_get_cstr(smpp->host));
	counter_increase(conn->received);
	return 0;
}

/**************************************************************************************/
/* Purpose : function used to shutdown the smsc connection                            */
/* Input   : Connection structure and                                                 */
/* Output  : 0 if successfully shutdown the smsc connection                           */ 
/**************************************************************************************/
static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
	SMPP *smpp;

	info(0, "Shutting down SMSCConn %s (%s)",
			octstr_get_cstr(conn->name),
			finish_sending ? "slow" : "instant");

	conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

	/* XXX implement finish_sending */

	smpp = conn->data;
	smpp->quitting = 1;
	if (smpp->transmitter != -1) {
		gwthread_wakeup(smpp->transmitter);
		gwthread_join(smpp->transmitter);
	}
	if (smpp->receiver != -1) {
		gwthread_wakeup(smpp->receiver);
		gwthread_join(smpp->receiver);
	}
	smpp_destroy(smpp);

	debug("bb.smpp", 0, "SMSCConn %s shut down.",
			octstr_get_cstr(conn->name));
	conn->status = SMSCCONN_DEAD;
	smpp_client_smscconn_killed();
	printf("\n\n-----------TRANSMITTER THREAD JOINED\n\n");
	return 0;
}

/*******************************************************************************************/
/* Public interface. This version is suitable for the Kannel bearerbox SMSCConn interface. */
/*******************************************************************************************/

/**************************************************************************************/
/* Purpose : function used to read smpp related configuration info from configuration */
/*         : file                                                                     */
/* Input   : SMPP Structure and configuration group structure                         */
/* Output  : 0 if successfully read and initialised smpp structure                    */ 
/**************************************************************************************/

int smsc_smpp_create(SMSCConn *conn, CfgGroup *grp)
{
	Octstr *host;
	long port ;
	long receive_port;
	Octstr *username;
	Octstr *password;
	Octstr *system_id;
	Octstr *system_type;
	Octstr *address_range;
	long source_addr_ton;
	long source_addr_npi;
	long dest_addr_ton;
	long dest_addr_npi;
	Octstr *service_type;
	SMPP *smpp;
	int ok;
	int transceiver_mode;
	Octstr *smsc_id;
	long enquire_link_interval;
	long max_pending_submits;
	long version;
	long priority;
	long validity;
	long smpp_msg_id_type;
	int autodetect_addr;
	Octstr *alt_charset;
	Octstr *alt_addr_charset;
	long connection_timeout, wait_ack, wait_ack_action;
	Octstr *masking_prefix;
	int deliver_sm_source;

	alt_addr_charset = alt_charset = NULL;
	transceiver_mode = 0;
	autodetect_addr = 1;
	deliver_sm_source = 1;

	host = cfg_get(grp, octstr_imm("host")); 
	if (cfg_get_integer(&port, grp, octstr_imm("port")) == -1) 
		port = 0; 
	if (cfg_get_integer(&receive_port, grp, octstr_imm("receive-port")) == -1) 
		receive_port = 0; 
	cfg_get_bool(&transceiver_mode, grp, octstr_imm("transceiver-mode")); 

	username = cfg_get(grp, octstr_imm("smsc-username")); 
	password = cfg_get(grp, octstr_imm("smsc-password")); 
	system_type = cfg_get(grp, octstr_imm("system-type")); 
	address_range = cfg_get(grp, octstr_imm("address-range")); 
	service_type = cfg_get(grp, octstr_imm("service-type")); 

	system_id = cfg_get(grp, octstr_imm("system-id")); 
	if (system_id != NULL) { 
		warning(0, "SMPP: obsolete system-id variable is set, " 
				"use smsc-username instead."); 
		if (username == NULL) { 
			warning(0, "SMPP: smsc-username not set, using system-id instead"); 
			username = system_id; 
		} else 
			octstr_destroy(system_id); 
	} 


	/* 
	 * check if timing values have been configured, otherwise
	 * use the predefined default values.
	 */
	if (cfg_get_integer(&enquire_link_interval, grp, 
				octstr_imm("enquire-link-interval")) == -1)
		enquire_link_interval = SMPP_ENQUIRE_LINK_INTERVAL;
	if (cfg_get_integer(&max_pending_submits, grp, 
				octstr_imm("max-pending-submits")) == -1)
		max_pending_submits = SMPP_MAX_PENDING_SUBMITS;

	/* Check that config is OK */ 
	ok = 1; 
	if (host == NULL) { 
		error(0,"SMPP: Configuration file doesn't specify host"); 
		ok = 0; 
	}     
	if (username == NULL) { 
		error(0, "SMPP: Configuration file doesn't specify username."); 
		ok = 0; 
	} 
	if (password == NULL) { 
		error(0, "SMPP: Configuration file doesn't specify password."); 
		ok = 0; 
	} 
	if (system_type == NULL) { 
		error(0, "SMPP: Configuration file doesn't specify system-type."); 
		ok = 0; 
	} 
	if (octstr_len(service_type) > 6) {
		error(0, "SMPP: Service type must be 6 characters or less.");
		ok = 0;
	}

	if (port == 0 && receive_port == 0) {
		error(0,"SMPP: Port or Receiver Port is not set for SMSC-Connection");
		ok = 0;
	}

	if(transceiver_mode && port == 0) {
		error(0,"SMPP: port is not set for transceiver mode connection with SMSC");
		ok = 0;
	}

	if (!ok) 
		return -1; 

	/* if the ton and npi values are forced, set them, else set them to -1 */ 
	if (cfg_get_integer(&source_addr_ton, grp, 
				octstr_imm("source-addr-ton")) == -1) 
		source_addr_ton = -1; 
	if (cfg_get_integer(&source_addr_npi, grp, 
				octstr_imm("source-addr-npi")) == -1) 
		source_addr_npi = -1; 
	if (cfg_get_integer(&dest_addr_ton, grp, 
				octstr_imm("dest-addr-ton")) == -1) 
		dest_addr_ton = -1; 
	if (cfg_get_integer(&dest_addr_npi, grp, 
				octstr_imm("dest-addr-npi")) == -1) 
		dest_addr_npi = -1; 

	/* if source addr autodetection should be used set this to 1 */
	if (cfg_get_bool(&autodetect_addr, grp, octstr_imm("source-addr-autodetect")) == -1)
		autodetect_addr = 1; /* default is autodetect if no option defined */

	/* check for any specified interface version */
	if (cfg_get_integer(&version, grp, octstr_imm("interface-version")) == -1)
		version = SMPP_DEFAULT_VERSION;
	else
		/* convert decimal to BCD */
		version = ((version / 10) << 4) + (version % 10);

	/* check for any specified priority value in range [0-5] */
	if (cfg_get_integer(&priority, grp, octstr_imm("priority")) == -1)
		priority = SMPP_DEFAULT_PRIORITY;
	else if (priority < 0 || priority > 3)
		panic(0, "SMPP: Invalid value for priority directive in configuraton (allowed range 0-3).");

	/* check for message validity period */
	if (cfg_get_integer(&validity, grp, octstr_imm("validityperiod")) == -1)
		validity = -1;
	else if (validity < 0)
		panic(0, "SMPP: Invalid value for validity period (allowed value >= 0).");

	/* set the msg_id type variable for this SMSC */
	if (cfg_get_integer(&smpp_msg_id_type, grp, octstr_imm("msg-id-type")) == -1) {
		/* 
		 * defaults to C string "as-is" style 
		 */
		smpp_msg_id_type = -1; 
	} else {
		if (smpp_msg_id_type < 0 || smpp_msg_id_type > 3)
			panic(0,"SMPP: Invalid value for msg-id-type directive in configuraton"); 
	}

	/* check for an alternative charset */
	alt_charset = cfg_get(grp, octstr_imm("alt-charset"));
	alt_addr_charset = cfg_get(grp, octstr_imm("alt-addr-charset"));

	/* check for connection timeout */
	if (cfg_get_integer(&connection_timeout, grp, octstr_imm("connection-timeout")) == -1)
		connection_timeout = SMPP_DEFAULT_CONNECTION_TIMEOUT;

	/* check if wait-ack timeout set */
	if (cfg_get_integer(&wait_ack, grp, octstr_imm("wait-ack")) == -1)
		wait_ack = SMPP_DEFAULT_WAITACK;

	if (cfg_get_integer(&wait_ack_action, grp, octstr_imm("wait-ack-expire")) == -1)
		wait_ack_action = SMPP_WAITACK_REQUEUE;

	if (wait_ack_action > 0x03 || wait_ack_action < 0)
		panic(0, "SMPP: Invalid wait-ack-expire directive in configuration.");

	masking_prefix = cfg_get(grp, octstr_imm("masking-prefix"));
	if((masking_prefix != NULL)&&(octstr_len(masking_prefix) != 3))
		panic(0, "SMPP: Invalid masking-prefix for SMSC ");
	if(cfg_get_bool(&deliver_sm_source, grp, octstr_imm("deliver-sm-source")) == -1)	
		deliver_sm_source = 1;
	//printf("\ndeliver_sm_source :%d\n",deliver_sm_source);

	smpp = smpp_create(conn, host, port, receive_port, system_type,  
			username, password, address_range,
			source_addr_ton, source_addr_npi, dest_addr_ton,  
			dest_addr_npi, enquire_link_interval, 
			max_pending_submits, version, priority, validity, 
			smpp_msg_id_type, autodetect_addr, alt_charset, alt_addr_charset,
			service_type, connection_timeout, wait_ack, wait_ack_action,masking_prefix,deliver_sm_source); 

	cfg_get_integer(&smpp->bind_addr_ton, grp, octstr_imm("bind-addr-ton"));
	cfg_get_integer(&smpp->bind_addr_npi, grp, octstr_imm("bind-addr-npi"));

	conn->data = smpp; 
	conn->name = octstr_format("SMPP:%S:%d/%d:%S:%S",  
			host, port, 
			(receive_port ? receive_port : port),  
			username, system_type); 

	smsc_id = cfg_get(grp, octstr_imm("smsc-id")); 

	if (smsc_id == NULL) 
	{ 
		conn->id = octstr_duplicate(conn->name); 
	} 


	octstr_destroy(host); 
	octstr_destroy(username); 
	octstr_destroy(password); 
	octstr_destroy(system_type); 
	octstr_destroy(address_range); 
	octstr_destroy(smsc_id);
	octstr_destroy(alt_charset); 
	octstr_destroy(alt_addr_charset);
	octstr_destroy(service_type);
	if(system_id != NULL) octstr_destroy(system_id); 
	conn->status = SMSCCONN_CONNECTING; 
	if(masking_prefix != NULL)
		octstr_destroy(masking_prefix);
	/* 
	 * I/O threads are only started if the corresponding ports 
	 * have been configured with positive numbers. Use 0 to  
	 * disable the creation of the corresponding thread. 
	 */ 
	if (port != 0) 
		smpp->transmitter = gwthread_create(io_thread, io_arg_create(smpp,  
					(transceiver_mode ? 2 : 1))); 
	if (receive_port != 0) 
		smpp->receiver = gwthread_create(io_thread, io_arg_create(smpp, 0)); 

	if ((port != 0 && smpp->transmitter == -1) ||  
			(receive_port != 0 && smpp->receiver == -1)) { 
		error(0, "SMPP[%s]: Couldn't start I/O threads.",
				octstr_get_cstr(smpp->conn->id)); 
		smpp->quitting = 1; 
		if (smpp->transmitter != -1) { 
			gwthread_wakeup(smpp->transmitter); 
			gwthread_join(smpp->transmitter); 
		} 
		if (smpp->receiver != -1) { 
			gwthread_wakeup(smpp->receiver); 
			gwthread_join(smpp->receiver); 
		} 
		smpp_destroy(conn->data); 
		return -1; 
	} 

	conn->shutdown = shutdown_cb; 
	conn->queued = queued_cb; 
	conn->waiting = waiting_cb;
	conn->send_msg = send_msg_cb; 
	return 0; 
}

#if 1 
static long decode_integer_value(Octstr *os, long pos, int octets)
{
	unsigned long u;
	int i;

	if (octstr_len(os) < pos + octets)
		return -1;

	u = 0;
	for (i = 0; i < octets; ++i)
		u = (u << 8) | octstr_get_char(os, pos + i);

	return u;
}
#endif										 
