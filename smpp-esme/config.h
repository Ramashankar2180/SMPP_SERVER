#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gwlib/gwlib.h"

typedef struct Smsc_Client_List Smsc_Client_List;
typedef struct Smsc_Client	Smsc_Client;

int verify_config_para(Smsc_Client_List *trans);

Smsc_Client_List *smpp_client_create(void);
void smpp_client_destroy(Smsc_Client_List *list);
int smpp_client_add_one(Smsc_Client_List *trans, CfgGroup *grp);
int smsc_client_add_cfg(Smsc_Client_List *trans, Cfg *cfg);
int smpp_client_add_cfg(Smsc_Client_List *trans, Cfg *cfg);


/*returns various parameters of config file*/
Smsc_Client *smpp_client_find_username(Smsc_Client_List *trans,Octstr *name);
Octstr *smpp_client_username(Smsc_Client *ot);
Octstr *smpp_client_password(Smsc_Client *ot);
int smpp_client_mode(Smsc_Client *ot);
Octstr *smpp_client_validfrom(Smsc_Client *ot);
Octstr *smpp_client_validto(Smsc_Client *ot);
Octstr *smpp_client_log_file(Smsc_Client *ot);
Octstr *smpp_client_rx_log_file(Smsc_Client *ot);
Octstr *smpp_client_allowed_prefix(Smsc_Client *ot);
Octstr *smpp_client_denied_prefix(Smsc_Client *ot);
long smpp_client_enquire_link_interval(Smsc_Client *ot);
Octstr *smpp_client_dedicated_smsc(Smsc_Client *ot);
List *smpp_client_preferred_smsc(Smsc_Client *ot);


int smpp_client_log_level(Smsc_Client *ot);
int smpp_client_log_idx(Smsc_Client *ot);
int smpp_client_rx_log_idx(Smsc_Client *ot);


/*returns the total number of smpp_client group in config file*/
long smpp_client_len(Smsc_Client_List *trans);

/*return the translation in the list at position pos*/
Smsc_Client *get_smpp_client(Smsc_Client_List *trans,long pos);


/*http admin related functions*/
Octstr *print_status_configured_esme(int status_type,Smsc_Client_List *trans);
void change_esme_logfile(Smsc_Client_List *trans);

/*Returns rx-allowed parameter of the config file*/
int smpp_client_get_rxmode(Smsc_Client *ot);


long smpp_client_get_max_submits_unconnect(Smsc_Client *ot);
long smpp_client_get_max_submits(Smsc_Client *ot);
int smpp_client_get_max_rx_sessions(Smsc_Client *ot);
int smpp_client_get_max_tx_sessions(Smsc_Client *ot);
int update_msg_dbbox_server(Smsc_Client *ot);
