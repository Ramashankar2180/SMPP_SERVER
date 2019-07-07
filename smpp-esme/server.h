/* ==================================================================== 
 *  
*/
/*
 * server.h - SMPP server for testing purposes
 *
 * 
 */
 
#ifndef SERVER_H
#define SERVER_H 

enum 
{
    PROGRAM_RUNNING = 0,
    PROGRAM_ISOLATED = 1,	/* do not receive new messgaes from UDP/SMSC */
    PROGRAM_SUSPENDED = 2,	/* do not transfer any messages */
    PROGRAM_SHUTDOWN = 3,
    PROGRAM_DEAD = 4,
    PROGRAM_FULL = 5         /* message queue too long, do not accept new messages */
};
/* type of output given by various status functions */


 
#define SMPP_SERVER_VERSION 0x34
#define PORT_TRANSMITTER 	3000
#define PORT_RECEIVER 		4000
#define PORT_TRANRCV 		5000

#define SMPP_ENQUIRE_LINK_INTERVAL  30.0
#define SMPP_MAX_PENDING_SUBMITS    10
#define SMPP_DEFAULT_VERSION        0x34
#define SMPP_DEFAULT_PRIORITY       0
#define SMPP_THROTTLING_SLEEP_TIME  15
#define SMPP_DEFAULT_CONNECTION_TIMEOUT  10 * SMPP_ENQUIRE_LINK_INTERVAL
#define SMPP_DEFAULT_WAITACK        60
#define SMPP_DEFAULT_SHUTDOWN_TIMEOUT 30

void shutdown_smpp_server();

struct msg_deliver
{
	Octstr *smsc_id;
	Octstr *smsc_msg_id;
	Octstr *account_msg_id;
	Octstr *os;
};

#endif
