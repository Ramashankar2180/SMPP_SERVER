#include "gwlib/smpp_pdu.h"
/*mis related structures*/
struct mis_insert
{
	long int msg_id;			/*used for message queue type*/
	int msg_type;
	char msg_content[400];
	char udh[40];
	char account_name[30];
	char account_msg_id[30];
	char smsc_id[30];
	char smsc_msg_id[SMPP_MAX_SMSC_MSG_ID];
	char source_addr[30];
	char dest_addr[30];
	char req_time[24];
	int deliver_status;
	int submit_status;
	int esm_class;	
	long message_len;
};
struct mis_update
{
	long int msg_id;
	int msg_type;
	char smsc_id[30];
	char smsc_msg_id[SMPP_MAX_SMSC_MSG_ID];
	char dest_addr[20];
	int deliver_status;
};

//void dlr_mis_add(struct mis_insert *mis_insert);
//void dlr_mis_update(struct mis_update *mis_update_ptr);
