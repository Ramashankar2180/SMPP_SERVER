#ifndef ESME_H
#define ESME_H


#include "gwlib/gwlib.h"
#include "config.h"
#include "gwlib/msgq.h"
#include "gwlib/process_sock.h"

enum 
{
  	ESME_UNCONNECTED = 0,/*i ESME is not binded successfully */
  	ESME_BINDED = 1,	/*when binded successfully*/
	ESME_DISCONNECT = 2,	/*this is when program is terminated from here and esme is still connected - can be removed*/
  	ESME_SEND_UNBIND=3,
	ESME_SMSC_UNCONNECTED = 4 /*when no smsc are connected for esme tx*/
};

//[sanchal][191108] [Adding tempConn structure]
//[ It exists till bind to have socket descriptor info till Tx/rx/TxRx mode information]
//[ is recieved 								       ]	 
typedef struct
{
    
	Connection *conn;
	
	Octstr *client_ip;/*SMPP client IP - different for tx/rx.*/
    int client_port;/*SMPP client port different for tx/rx.*/
    int listen_port;/*port from which bind was received - different for tx/rx.*/
    int bind_successful;/*if bind is unsuccessful*/
    long log_idx;/*This is required cos we want to log even bind responses*/
    
}TEMPCONN;

//[sanchal][121108] [Seperating Tx from ESME structure]
typedef struct
{

	Connection *conn;/*socket descriptor for reading and writing - different for tx/rx*/
	Counter *msg_id_counter;/*counter for smsc msg id for that client - only for tx*/
	Counter *msg_sent;/*messages successfully written on the forward queue - only for tx*/
	Counter *msg_rejected;/*messages rejected by the server - only for tx*/
	Counter *submits_unconnect;/*total number of submits when smsc are disconnected*/

   long max_submits_unconnect; /*max no of submits allowed when smsc are unconnected, to accomodate -1 as well*/
	/*
	stores the sequcne number staring from what is received from bind
	stores the seq number of last packet sent/received
	for new packet this value shouold be incremented by 1 before coying in the pdu.
	*/
	Counter *seq_number_counter;/*different for tx/rx*/

	/*system related information as maintained by the server for each ESME*/
	Octstr *client_ip;/*SMPP client IP - different for tx/rx.*/
	int client_port;/*SMPP client port - not much of significance as we already have connected desc - different for tx/rx.*/
	int listen_port;/*port from which bind was received - different for tx/rx.*/

	time_t start_time;/*time when bind is received from the esme-different for tx/rx*/
	int status;/*current esme status - unconnected,binded,sending unbind - different for tx/rx*/
	int quit;/*when set tells that ESME has fully disconencted and we can shutdown esme now - different for tx/rx*/

	
	List *data_to_write;
}ESME_TX;

//[sanchal][121108] [Seperating RX from ESME structure]
typedef struct
{
	Connection *conn;/*socket descriptor for reading and writing - different for tx/rx*/
	/*system related information as maintained by the server for each ESME*/
	Octstr *client_ip;/*SMPP client IP - different for tx/rx.*/

	int client_port;/*SMPP client port - not much of significance as we already have connected desc - different for tx/rx.*/

	int listen_port;/*port from which bind was received - different for tx/rx.*/
	List *data_to_write;/*data to be written on the connection pointer - only for tx*/

	/*to be added for rx
	long int deliver_sm sent; - number of deliver sm sent to the account
	*/

	/*
	stores the sequcne number staring from what is received from bind
	stores the seq number of last packet sent/received
	for new packet this value shouold be incremented by 1 before coying in the pdu.
	*/

	Counter *seq_number_counter;/*different for tx/rx*/
	time_t start_time;/*time when bind is received from the esme-different for tx/rx*/

	long thread_id;/*thread id of the receive smpp threas used for waking up the thread-different for tx/rx*/

	int status;/*current esme status - unconnected,binded,sending unbind - different for tx/rx*/
	int quit;/*when set tells that ESME has fully disconencted and we can shutdown esme now - different for tx/rx*/

	

}ESME_RX;

/*generic structure for storing esme connection*/
typedef struct 
{
	/*parameters as coming from ESME*/
	Octstr *system_id;/*username with which esme logs in - same for tx/rx*/
	Octstr *password;/*password for the account - - same for tx/rx*/
	Octstr *system_type;/*system type - optional parameter in bind - - same for tx/rx*/
	Octstr *address_range;/*optional parameter in bind - same for tx/rx*/
	int interface_version;/*smpp version which clinet follows - same for tx/rx*/
	int addr_ton;/*addr ton and npi from bind - same for tx/rx */
	int addr_npi ;/* - same for tx/rx*/

	/*Parameters as read from config file related to routing*/
	long enquire_link_interval; /* same for tx/rx*/
	/*account allowed and denied prefix*/
	Octstr *allowed_prefix;/* same for tx/rx*/
	Octstr *denied_prefix;/* same for tx/rx*/

	/*logfile related details*/
	Octstr *log_file;/*same for tx/rx*/
	long log_level;/*same for tx/rx*/
	int log_idx;    /* index position within the global logfiles[] array in gwlib/log.c - same for tx/rx*/

	int mode;/*In which mode this ESME account is binded 1-TX,2-RX,3-TRx*/
	/*Mode has following values*/
	/*1 - Tx Only
	2 - Rx only
	3 - Trx only
	4 - Tx of the account is already binded, now rx
	5 - Rx of the account is already binded now tx.*/
	int tx_thread_id;/*Thread id for tx thread*/
	int rx_thread_id;/*Thread id for rx thread*/
	
	/* Parameters for TX Mode */
	ESME_TX *esmetx;
	/* Parameters for RX Mode */
	ESME_RX *esmerx;
}ESME;

//[201108] [It supports Tx/Rx/TRx/TxRx mode ]
typedef struct 
{
	ESME *esme;
	SMPP_PDU *resp;      //Compulsory field	
}MSG_BIND_INFO;

typedef struct 
{
	Octstr *smsc_id;
	int conn_status;
}SMSC;

/*allocates temporary memeory for accepting an esme connection*/

TEMPCONN *tempconn_create(	Connection *conn, 
			const char *client_ip,
			int client_port,
			int listen_port
		);

/*destroys the temp_conn*/
void tempconn_destroy(TEMPCONN *temp_conn);

/*destroys memory for an esme connection*/
void esme_destroy(ESME *esme,int mode);

/*initialises mutex and esme list*/
void esme_init(void);

/*add a populated emse to the esme list*/
void add_esme(ESME *esme);

/*destoys an esme : matches esme system id in the list and makes sure that connection is dead before
destroying the esme*/
void destroy_esme(ESME *esme);

/*returns the length of esme list len*/
int esme_list_len();

/*destoys list and mutex*/
void esme_shutdown(int shutdown);

/*finds whether as esme as already looged in whith system_id*/
ESME* is_present_esme(Octstr *system_id);

/*validates a user's name password,mode account details in the translations read from config file*/
//int authorise_user(Octstr *username,Octstr *password,Octstr *mode,Smsc_Client_List *translations,ESME *esme);
int authorise_user(SMPP_PDU *pdu,int mode,Smsc_Client_List *translations,TEMPCONN *temp_conn,ESME **esme);

/*return the connection corresponding to an esme with esme id specified as an argument*/
Connection* extract_connection(Octstr* esme_id);

/*returns the current value of an esme with esme id specified as an argument*/
unsigned long int extract_seqno_counter_value(Octstr* esme_id);

/*returns the esme object whose id is specified as an agrument*/
ESME *extract_esme(Octstr* esme_id);

/*updates the esme connections as soon as smsc list are updated*/
void update_emse_conn(Smsc_Client_List *translations);

/*
*		
*
*			SMSC list related functions
*
*
*/
/*initializes smsc list and mutexes*/
void smsc_init(void);

/*called to destroy each item of smsc_conn_list*/
void smsc_destroy(void *item);

/*delets all the list entry and list itself*/
void smsc_shutdown(void);

/*called on power on when client sends connection information status of all the smscs
The list will contain as many entries as configured in client config file*/
void prepare_smsc_list(Conn_Msg *msg);

/*updates the connection status of an smsc whose id is specfied in the argument*/
void update_smsc_list(Conn_Msg *msg);

/*diplays the content of the smsc list for testing only*/
void display_smsc_info(void);

/*checks in the smsc list where any smsc has connection status of 1*/
int is_smsc_present(void);

/*returns connection status of an smsc*/
int smsc_conn_status(Octstr *smsc_id);


/*returns the length of SMSC list len*/
int smsc_list_len();
int connected_smsc_list_len();

/*checks whether a particular user has enough smsc so that it can submit_sm*/
int check_smsc_conn(Octstr *username,Smsc_Client_List *translations);

/*return an ESME at position pos in the list*/
ESME *get_esme(long pos);

long init_log_file(Octstr *username, Smsc_Client_List *translations);

void init_connect_ip(Cfg *cfg);

//int rout_pdu(Smsc_Client_List *translations,Octstr *dest_addr,Octstr *username, Octstr **ospreferred_smsc,Octstr
//**osallowed_smsc);


long convert_addr_from_pdu(Octstr *esme_name, Octstr *addr, long ton, long npi);
long verify_pdu_para(ESME *esme, SMPP_PDU *pdu, Octstr **dest_addr,Octstr **source_addr);



void log_smsc_info(void);/*logs current smsc's info into the log file*/



Octstr *print_esme_status(int status_type);/*Prints HTTP Status*/
int extract_log_idx(Octstr *account_name);
#endif
