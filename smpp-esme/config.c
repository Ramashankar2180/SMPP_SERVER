#include "config.h"
#include "server.h"
#include "httpadmin.h"

struct Smsc_Client_List
{
	List *list;
	Dict *dict;		
	Dict *names;
};

struct Smsc_Client
{
	Octstr *account_name;
	Octstr *password;
	Octstr *validfrom;
	Octstr *validto;
	Octstr *log_file;
	Octstr *rx_log_file;
	Octstr *allowed_prefix;
	Octstr *denied_prefix;
	Octstr *dedicated_smsc;
	List *preferred_smsc;
	Octstr *preferred_smsc_tmp;	
	long log_level;
	int log_idx;
	int rx_log_idx;
	long enquire_link_interval;
	long max_submits_unconnect;
	long max_submits;
	long max_tps;

	//[sanchal][121108][deliver_sm flag]
	int rx_allowed;
	int allowed_mode;/*1for Tx,2 for Rx, 3 for TRx 4 - TxRx*/
	int dnds_check ;
	long max_tx_sessions;
	long max_rx_sessions;
	int valid_time_from;
	int valid_time_to;
};

static Smsc_Client *create_onetrans(CfgGroup *grp)
{
	Smsc_Client *ot;
	Octstr *grpname;
	Octstr *tmp;
	Octstr *mode;

	Octstr *smpp_client_grp;
	int is_smpp_client = 0;

	grpname = cfg_get_group_name(grp);
	if (grpname == NULL)
		return NULL;

	smpp_client_grp = octstr_imm("smpp-client");
	if (octstr_compare(grpname, smpp_client_grp) == 0)
	{
		is_smpp_client = 1;
	}
	else
	{
		octstr_destroy(grpname);
		return NULL;
	}

	octstr_destroy(grpname);

	ot = gw_malloc(sizeof(Smsc_Client));


	ot->account_name = NULL;
	ot->password = NULL;
	ot->validfrom = NULL;
	ot->validto = NULL;
	ot->dedicated_smsc = NULL;
	ot->preferred_smsc = NULL;
	ot->preferred_smsc_tmp = NULL;
	ot->log_file = NULL;
	ot->rx_log_file = NULL;
	ot->allowed_prefix = NULL;
	ot->denied_prefix = NULL;

	ot->enquire_link_interval = 0;
	ot->dnds_check = 0;
	ot->log_level = 0;

	//[sanchal][121108][deliver_sm flag]
	ot->rx_allowed = 0;
	ot->log_idx = -1;
	ot->rx_log_idx = -1;
	ot->max_submits_unconnect = -1;
	ot->max_tx_sessions = -1;
	ot->max_rx_sessions = -1;
	if(is_smpp_client)
	{
		mode = cfg_get(grp, octstr_imm("mode"));
		ot->account_name = cfg_get(grp, octstr_imm("account-name"));
		ot->password = cfg_get(grp, octstr_imm("password"));
		ot->validfrom = cfg_get(grp, octstr_imm("validfrom"));
		ot->validto = cfg_get(grp, octstr_imm("validto"));

		ot->log_file = cfg_get(grp, octstr_imm("log-file"));
		ot->allowed_prefix = cfg_get(grp, octstr_imm("account-allow-prefix"));
		ot->denied_prefix = cfg_get(grp, octstr_imm("account-deny-prefix"));
		ot->dedicated_smsc = cfg_get(grp, octstr_imm("dedicated-smsc"));
		ot->preferred_smsc_tmp = cfg_get(grp, octstr_imm("preferred-smsc"));
		tmp = cfg_get(grp, octstr_imm("preferred-smsc"));
		if(tmp != NULL)
		{
			ot->preferred_smsc = octstr_split(tmp, octstr_imm(";")); 
		}
		octstr_destroy(tmp);

		if (cfg_get_integer(&(ot->log_level), grp, octstr_imm("log-level")) == -1)
			ot->log_level = 0;

		if(ot->log_file != NULL)
		{
			/*
			   Octstr *log_file;
			   log_file = logfile_append_time(ot->log_file);
			   ot->log_idx = log_open(octstr_get_cstr(log_file),ot->log_level,GW_EXCL);
			   octstr_destroy(log_file);
			   */
			Octstr *logfile;
			Octstr *logfile1;
			Octstr *templogfile;
			int index;
			logfile = logfile_append_time(ot->log_file);

			//printf("\n inside Opening Loop of smsconn_open log_file logfile: %s \n",octstr_get_cstr(logfile));
			debug("config",0, "create_onetrans : logfile opened %s ",octstr_get_cstr(logfile));

			templogfile = get_latst_logfile(logfile);
			if( templogfile != 0 )
			{
				info(0, "Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(templogfile));
				//printf("Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(templogfile));
				ot->log_idx = log_open(octstr_get_cstr(templogfile),ot->log_level, GW_EXCL);
			}
			else
			{
				info(0, "Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(logfile));
				//printf("Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(logfile));
				ot->log_idx = log_open(octstr_get_cstr(logfile),ot->log_level, GW_EXCL);
			}

			octstr_destroy(logfile);
			logfile1 = log_file_name(ot->log_idx);
			//printf("\n\nORIGINAL FILE NAME :%d :%s\n",ot->log_idx,octstr_get_cstr(logfile1));
			debug("config",0, " create_onetrans: reverse logfile read :%d :%s ",ot->rx_log_idx,octstr_get_cstr(logfile1));
			octstr_destroy(logfile1);
			octstr_destroy(templogfile);

		}
		if (cfg_get_integer(&(ot->enquire_link_interval),grp, octstr_imm("enquire-link-interval")) == -1)
			ot->enquire_link_interval = SMPP_ENQUIRE_LINK_INTERVAL;

		if (cfg_get_integer(&(ot->dnds_check),grp, octstr_imm("dnd-check")) == -1)
			ot->dnds_check = 0;

		cfg_get_bool(&(ot->rx_allowed), grp, octstr_imm("rx-allowed"));
		if(cfg_get_integer(&(ot->max_submits_unconnect), grp, octstr_imm("max-submits-unconnect")) == -1)
			ot->max_submits_unconnect = -1;
		if(cfg_get_integer(&(ot->max_submits), grp, octstr_imm("max-submits")) == -1)
			ot->max_submits = -1;
		if(cfg_get_integer(&(ot->max_tx_sessions), grp, octstr_imm("max-tx-sessions")) == -1)
			ot->max_tx_sessions = -1;
		if(cfg_get_integer(&(ot->max_rx_sessions), grp, octstr_imm("max-rx-sessions")) == -1)
			ot->max_rx_sessions = -1;
		if(cfg_get_integer(&(ot->max_tps), grp, octstr_imm("max-tps")) == -1)
			ot->max_tps = -1;
		if(cfg_get_integer(&(ot->valid_time_from), grp, octstr_imm("valid_time_from")) == -1)
			ot->valid_time_from = -1;
		if(cfg_get_integer(&(ot->valid_time_to), grp, octstr_imm("valid_time_to")) == -1)
			ot->valid_time_to = -1;
	}

	if(ot->account_name == NULL)
	{
		info(0, "Pls Specify name for the account");
		return NULL;
	}
	if(ot->password == NULL)
	{
		info(0, "Pls Specify password for the account : %s",octstr_get_cstr(ot->account_name));
		return NULL;
	}
	if(mode == NULL)
	{
		info(0, "Pls Specify mode for the account : %s",octstr_get_cstr(ot->account_name));
		return NULL;
	}
	if(ot->validfrom == NULL)
	{
		info(0, "Pls Specify account start date for the account : %s",octstr_get_cstr(ot->account_name));
		return NULL;
	}

	if(ot->validto == NULL)
	{
		info(0, "Pls Specify account expiry date for the account : %s",octstr_get_cstr(ot->account_name));
		return NULL;
	}

	if((ot->rx_allowed == 1)&&(octstr_compare(mode,octstr_imm("tx")) != 0))
	{
		info(0, "rx-allowed can be used only when tx is set : %s",octstr_get_cstr(ot->account_name));
		return NULL;
	}

	if(ot->rx_allowed == 1)
	{
		/*
		   ot->rx_log_file = cfg_get(grp, octstr_imm("rx-log-file"));
		   if(ot->rx_log_file != NULL)
		   {
		   Octstr *log_file;
		   log_file = logfile_append_time(ot->rx_log_file);
		   ot->rx_log_idx = log_open(octstr_get_cstr(log_file),ot->log_level,GW_EXCL);
		   octstr_destroy(log_file);
		   }
		   */

		ot->rx_log_file = cfg_get(grp, octstr_imm("rx-log-file"));
		if(ot->rx_log_file != NULL)
		{
			//[sanchal][060409]
			Octstr *logfile = NULL;
			Octstr *logfile1 = NULL;
			Octstr *templogfile = NULL;

			logfile = logfile_append_time(ot->rx_log_file);
			debug("config",0, "create_onetrans : logfile opened %s ",octstr_get_cstr(logfile));

			templogfile = get_latst_logfile(logfile);
			if( templogfile != 0 )
			{
				info(0, " Latest Rev Log File Read successfully is : %s ",octstr_get_cstr(templogfile));
				//printf("===>>> Latest Rev Log File Read successfully is : %s ",octstr_get_cstr(templogfile));
				ot->rx_log_idx = log_open(octstr_get_cstr(templogfile),ot->log_level, GW_EXCL);
			}
			else
			{
				info(0, "Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(logfile));
				//printf("Latest SMSCFile Read successfully is : %s ",octstr_get_cstr(logfile));
				ot->rx_log_idx = log_open(octstr_get_cstr(logfile),ot->log_level, GW_EXCL);
			}

			octstr_destroy(logfile);
			logfile1 = log_file_name(ot->rx_log_idx);
			//printf("\n\n ===>> Reverse LOG FILE NAME :%d :%s\n",ot->rx_log_idx,octstr_get_cstr(logfile1));
			debug("config",0, " create_onetrans: reverse logfile read :%d :%s ",ot->rx_log_idx,octstr_get_cstr(logfile1));
			octstr_destroy(logfile1);
			octstr_destroy(templogfile);
		}
	}	

	/*Now selecting modes*/
	if((octstr_compare(mode,octstr_imm("tx")) == 0)&&(ot->rx_allowed == 1))
		ot->allowed_mode = 4;
	else if(octstr_compare(mode,octstr_imm("tx")) == 0)
		ot->allowed_mode = 1;
	else if(octstr_compare(mode,octstr_imm("rx")) == 0)
		ot->allowed_mode = 2;
	else if(octstr_compare(mode,octstr_imm("txrx")) == 0)
		ot->allowed_mode = 3;
	else
	{
		info(0, "Mode parameter cannot be recognised : %s",octstr_get_cstr(ot->account_name));
		return NULL;
	}


	if(ot->preferred_smsc)
	{
		int i;
		Octstr *smsc_id;
		for(i = 0 ; i < gwlist_len(ot->preferred_smsc);i++)
		{
			smsc_id = gwlist_get(ot->preferred_smsc,i);
			printf("10. preferred-smsc :%s\n",octstr_get_cstr(smsc_id));	
		}
	}

	octstr_destroy(mode);

	return ot;
}


static void destroy_keyword_list(void *list)
{
	gwlist_destroy(list, NULL);
}

static void destroy_onetrans(void *p) 
{
	Smsc_Client *ot;

	ot = p;
	if (ot != NULL) 
	{
		octstr_destroy(ot->validfrom);
		octstr_destroy(ot->validto);
		octstr_destroy(ot->account_name);
		octstr_destroy(ot->password);
		octstr_destroy(ot->log_file);
		octstr_destroy(ot->allowed_prefix);
		octstr_destroy(ot->denied_prefix);
		octstr_destroy(ot->dedicated_smsc);
		octstr_destroy(ot->rx_log_file);
		octstr_destroy(ot->preferred_smsc_tmp);
		gwlist_destroy(ot->preferred_smsc,octstr_destroy_item);
		gw_free(ot);
	}	 
}



Smsc_Client_List *smpp_client_create(void)
{
	Smsc_Client_List *trans;

	trans = gw_malloc(sizeof(Smsc_Client_List));
	trans->list = gwlist_create();
	trans->dict = dict_create(1024, destroy_keyword_list);
	trans->names = dict_create(1024, destroy_keyword_list);
	return trans;
}
void smpp_client_destroy(Smsc_Client_List *trans) 
{

	gwlist_destroy(trans->list, destroy_onetrans);
	dict_destroy(trans->names);
	dict_destroy(trans->dict);
	gw_free(trans);

}
int smpp_client_add_one(Smsc_Client_List *trans, CfgGroup *grp)
{
	Smsc_Client *ot;
	/*
	   long i;
	   List *list, *list2;
	   Octstr *alias;
	   */

	ot = create_onetrans(grp);
	if (ot == NULL)
		return -1;

	//update_msg_dbbox_server(ot);

	// Rama here need to implemen logic for client Allowd IP ,Submit limit , Authentication 

	gwlist_append(trans->list, ot);

	/*
	   list2 = dict_get(trans->names, ot->name);
	   if (list2 == NULL) 
	   {
	   list2 = gwlist_create();
	   dict_put(trans->names, ot->name, list2);
	   }
	   gwlist_append(list2, ot);

	   if (ot->keyword == NULL || ot->type == TRANSTYPE_SENDSMS)
	   return 0;

	   list = dict_get(trans->dict, ot->keyword);
	   if (list == NULL) 
	   {
	   list = gwlist_create();
	   dict_put(trans->dict, ot->keyword, list);
	   }
	   gwlist_append(list, ot);

	   for (i = 0; i < gwlist_len(ot->aliases); ++i) {
	   alias = gwlist_get(ot->aliases, i);
	   list = dict_get(trans->dict, alias);
	   if (list == NULL) {
	   list = gwlist_create();
	   dict_put(trans->dict, alias, list);
	   }
	   gwlist_append(list, ot);
	   }
	   */

	return 0;
}

int smpp_client_add_cfg(Smsc_Client_List *trans, Cfg *cfg)
{
	CfgGroup *grp;
	List *list;
	List *dedicated_list = NULL;
	Smsc_Client *ot1;
	int i,j;
	Octstr *tmp;

	list = cfg_get_multi_group(cfg, octstr_imm("smpp-client"));


	while (list && (grp = gwlist_extract_first(list)) != NULL) 
	{
		if (smpp_client_add_one(trans, grp) == -1) 
		{
			gwlist_destroy(list, NULL);
			return -1;
		}
	}
	gwlist_destroy(list, NULL);

	return 0;
}

int verify_config_para(Smsc_Client_List *trans)
{

	List *dedicated_list = NULL;
	Smsc_Client *ot1;
	int i,j;
	Octstr *tmp;


	if(gwlist_len(trans->list) == 0)
		error(0, "Not even a single client group has been specified");

	else
	{
		dedicated_list = gwlist_create();
		/*first see whether all account names are unique or not*/
		for(i = 0 ; i < gwlist_len(trans->list) ; i++)
		{
			ot1 = gwlist_get(trans->list,i);
			if(ot1->account_name != NULL)
				gwlist_append(dedicated_list,ot1->account_name);
		}


		for(i = 0 ; i < gwlist_len(dedicated_list); i++)
		{
			tmp = gwlist_get(dedicated_list,i);
			for(j = 0; j < gwlist_len(dedicated_list); j++)
			{
				/*make sure we dont compare same location*/
				if((octstr_compare(tmp,gwlist_get(dedicated_list,j)) == 0)&&(i!=j))
				{
					error(0, "Account names should be unique:%s",octstr_get_cstr(ot1->account_name));
					gwlist_destroy(dedicated_list,octstr_destroy_item);
					return -1;
				}
			}
		}

		gwlist_destroy(dedicated_list,NULL);


		dedicated_list = gwlist_create();

		/*prepare a list of all dedicated smscs*/
		for(i = 0 ; i < gwlist_len(trans->list) ; i++)
		{
			ot1 = gwlist_get(trans->list,i);
			if(ot1->dedicated_smsc != NULL)
				gwlist_append(dedicated_list,ot1->dedicated_smsc);
		}

		/*debug statements
		  for(i = 0 ; i < gwlist_len(dedicated_list); i++)
		  {
		  debug("1.c",0, "Item :%d  - :%s",i,octstr_get_cstr(gwlist_get(dedicated_list,i)));
		  }
		  debug statements*/

		/*first verify all dedicated smscs are unique.*/
#if 0
		// Rama Changes 2005 //
		for(i = 0 ; i < gwlist_len(dedicated_list); i++)
		{
			tmp = gwlist_get(dedicated_list,i);
			for(j = 0; j < gwlist_len(dedicated_list); j++)
			{
				/*make sure we dont compare same location*/
				if((octstr_compare(tmp,gwlist_get(dedicated_list,j)) == 0)&&(i!=j))
				{
					error(0, "Dedicated smsc should be unique:%s",octstr_get_cstr(ot1->dedicated_smsc));
					gwlist_destroy(dedicated_list,octstr_destroy_item);
					return -1;
				}
			}
		}
#endif 		

		/*then verify any of the dedicated smsc should not be present in any preferred smsc*/	
		for(i = 0 ; i < gwlist_len(trans->list) ; i++)
		{
			ot1 = gwlist_get(trans->list,i);
			if(ot1->preferred_smsc != NULL)
			{
				for(j = 0; j < gwlist_len(ot1->preferred_smsc); j++)
				{
					if(gwlist_search(dedicated_list,gwlist_get(ot1->preferred_smsc,j) , octstr_item_match) != NULL)
					{
						error(0, "Dedicated smsc cannot be shared with preferred smscs :%s",octstr_get_cstr(ot1->account_name));
						gwlist_destroy(dedicated_list,octstr_destroy_item);
						return -1;
					}
				}
			}
		}

	}//else len > 0
	gwlist_destroy(dedicated_list,NULL);
	return 0;
}

Smsc_Client *smpp_client_find_username(Smsc_Client_List *trans,Octstr *name)
{
	Smsc_Client *t;
	int i;

	gw_assert(name != NULL);
	for (i = 0; i < gwlist_len(trans->list); ++i) 
	{
		t = gwlist_get(trans->list, i);
		if (octstr_compare(name, t->account_name) == 0)
			return t;

	}
	return NULL;
}

Smsc_Client *get_smpp_client(Smsc_Client_List *trans,long pos)
{
	if(pos >= gwlist_len(trans->list))
		return NULL;
	return gwlist_get(trans->list, pos);
}

long smpp_client_len(Smsc_Client_List *trans)
{
	return gwlist_len(trans->list);
}

Octstr *smpp_client_username(Smsc_Client *ot)
{
	return ot->account_name;
}

Octstr *smpp_client_password(Smsc_Client *ot)
{
	return ot->password;
}
int smpp_client_mode(Smsc_Client *ot)
{
	return ot->allowed_mode;
}
Octstr *smpp_client_validfrom(Smsc_Client *ot)
{
	return ot->validfrom;
}
Octstr *smpp_client_validto(Smsc_Client *ot)
{
	return ot->validto;
}
long smpp_client_enquire_link_interval(Smsc_Client *ot)
{
	return ot->enquire_link_interval;
}

int smpp_client_get_dnd(Smsc_Client *ot)
{

	return ot->dnds_check;
}
/*[sanchal][121108][Added for rxmode flag]*/
int smpp_client_get_rxmode(Smsc_Client *ot)
{
	//printf("\n -------------- rx_allowed : %ld \n",ot->rx_allowed);
	return ot->rx_allowed;
}

Octstr *smpp_client_log_file(Smsc_Client *ot)
{
	return ot->log_file;	
}
Octstr *smpp_client_rx_log_file(Smsc_Client *ot)
{
	return ot->rx_log_file;	
}
int smpp_client_log_level(Smsc_Client *ot)
{
	return ot->log_level;
}

Octstr *smpp_client_allowed_prefix(Smsc_Client *ot)
{
	return ot->allowed_prefix;
}
Octstr *smpp_client_denied_prefix(Smsc_Client *ot)
{
	return ot->denied_prefix;
}

Octstr *smpp_client_dedicated_smsc(Smsc_Client *ot)
{
	return ot->dedicated_smsc;
}

Octstr *smpp_client_preferred_smsc_tmp(Smsc_Client *ot)
{
	return ot->preferred_smsc_tmp;
}

List *smpp_client_preferred_smsc(Smsc_Client *ot)
{
	return ot->preferred_smsc;
}

int smpp_client_log_idx(Smsc_Client *ot)
{
	return ot->log_idx;
}

int smpp_client_rx_log_idx(Smsc_Client *ot)
{
	return ot->rx_log_idx;
}

int smpp_client_get_max_tps(Smsc_Client *ot)
{
	return ot->max_tps;
}
int smpp_client_get_valid_from(Smsc_Client *ot)
{
	return ot->valid_time_from;
}
long smpp_client_get_valid_to(Smsc_Client *ot)
{
	return ot->valid_time_to;
}
long smpp_client_get_max_submits(Smsc_Client *ot)
{
	return ot->max_submits;
}
long smpp_client_get_max_submits_unconnect(Smsc_Client *ot)
{
	return ot->max_submits_unconnect;
}
int smpp_client_get_max_rx_sessions(Smsc_Client *ot)
{
	return ot->max_rx_sessions;
}
int smpp_client_get_max_tx_sessions(Smsc_Client *ot)
{
	return ot->max_tx_sessions;
}

void change_esme_logfile(Smsc_Client_List *trans)
{
	Smsc_Client *ot;
	int i,res;
	for(i = 0 ; i < gwlist_len(trans->list) ; i++)
	{
		ot = gwlist_get(trans->list,i);
		if(ot->log_file != NULL)
		{
			res  = change_logfile_name(ot->log_idx);
			if( res == 0)
			{
				Octstr *log_name = NULL;
				log_name = log_file_name(ot->log_idx);
				//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
				octstr_destroy(log_name);
			}
		}	

		if(ot->rx_log_file != NULL)
		{
			res  = change_logfile_name(ot->rx_log_idx);
			if( res == 0)
			{
				Octstr *log_name = NULL;
				log_name = log_file_name(ot->rx_log_idx);
				//info(0, "Reverse Quesue log file changed successfully:%s",octstr_get_cstr(log_name));
				octstr_destroy(log_name);
			}
		}
	}
}

/*HTTP admin related functions*/

Octstr *print_status_configured_esme(int status_type,Smsc_Client_List *trans)
{
	/*
	   char *lb;
	   int para = 0;
	   int i = 0;
	   Octstr *tmp;
	   int details = 1;
	   char no[10];

	   Smsc_Client *ot;
	   lb = (char *)bb_status_linebreak(status_type);
	   if (lb  == NULL)
	   return octstr_create("Un-supported format");

	   if(status_type == BBSTATUS_HTML || status_type == BBSTATUS_WML)
	   para = 1;

	   if (status_type != BBSTATUS_XML)
	   tmp = octstr_format("%sConfigured ESME :%s", para ? "<p>" : "", lb);
	   else
	   tmp = octstr_format("<smscs><count>%d</count>\n\t", gwlist_len(trans->list));

	   for(i = 0 ; i < gwlist_len(trans->list); i ++)
	   {
	   details = 1;
	   ot = gwlist_get(trans->list,i);
	   if(status_type == BBSTATUS_HTML) 
	   {
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
	   octstr_append(tmp, ot->account_name);
	   octstr_append_cstr(tmp, "</b>&nbsp;&nbsp;&nbsp;&nbsp;");


	   snprintf(no,sizeof(no),"%d",details);
	   octstr_append_cstr(tmp,no);
	   octstr_append_cstr(tmp,".Account:");
	   octstr_append(tmp, ot->account_name);
	   details++;

	   octstr_append_cstr(tmp,"<br>");


	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   snprintf(no,sizeof(no),"%d",details);
	   octstr_append_cstr(tmp,no);			
	   octstr_append_cstr(tmp,".Mode:");
	   octstr_append(tmp, ot->mode);
	   details++;

	   octstr_append_cstr(tmp,"<br>");



	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   snprintf(no,sizeof(no),"%d",details);
	   octstr_append_cstr(tmp,no);			
	   octstr_append_cstr(tmp,".Valid From:");
	   octstr_append(tmp, ot->validfrom);
	   details++;

	   octstr_append_cstr(tmp,"<br>");

	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	   octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	snprintf(no,sizeof(no),"%d",details);
	octstr_append_cstr(tmp,no);				
	octstr_append_cstr(tmp,".Valid To:");
	octstr_append(tmp, ot->validto);
	details++;

	octstr_append_cstr(tmp,"<br>");


	if(ot->dedicated_smsc_routing)
	{
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		snprintf(no,sizeof(no),"%d",details);
		octstr_append_cstr(tmp,no);	

		octstr_append_cstr(tmp,".Forced SMSC:");
		octstr_append(tmp, ot->forced_smsc);

		details++;

		octstr_append_cstr(tmp,"<br>");

		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		snprintf(no,sizeof(no),"%d",details);
		octstr_append_cstr(tmp,no);
		octstr_append_cstr(tmp,".First Alternate:");
		octstr_append(tmp, ot->first_alternate);
		details++;

		octstr_append_cstr(tmp,"<br>");

		if(ot->second_alternate != NULL)
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);

			octstr_append_cstr(tmp,".Second Alternate:");
			octstr_append(tmp, ot->second_alternate);

		}
	}
	else
	{

	}



} 
else if (status_type == BBSTATUS_TEXT) 
{
	octstr_append_cstr(tmp, "    ");
	octstr_append(tmp, ot->account_name);
	octstr_append_cstr(tmp, "    ");
} 
if (status_type == BBSTATUS_XML) 
{
	octstr_append_cstr(tmp, "<smsc>\n\t\t<name>");
	octstr_append(tmp, ot->account_name);

	octstr_append_cstr(tmp, "</id>\n\t\t");
}
if (para)
	octstr_append_cstr(tmp, "</p>");
if (status_type == BBSTATUS_XML)
	octstr_append_cstr(tmp, "</smscs>\n");
	else
	octstr_append_cstr(tmp, "\n\n");

	}

return tmp;
*/	
}
//[sanchal][240309][Code Added for size wise implementation]
void change_esme_logfile_onsize(Smsc_Client_List *trans, long iLogSize)
{
	Smsc_Client *ot;
	int i,res;
	for(i = 0 ; i < gwlist_len(trans->list) ; i++)
	{
		ot = gwlist_get(trans->list,i);
		if(ot->log_file != NULL)
		{
			//res  = change_logfile_name(ot->log_idx);
			res  = change_logfile_name_onsize(ot->log_idx, iLogSize);
			if( res == 0)
			{
				Octstr *log_name = NULL;
				log_name = log_file_name(ot->log_idx);
				//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
				octstr_destroy(log_name);
			}
		}

		//[sanchal][060409]
		if(ot->rx_log_file != NULL)
		{
			//res  = change_logfile_name(ot->log_idx);
			res  = change_logfile_name_onsize(ot->rx_log_idx, iLogSize);
			if( res == 0)
			{
				Octstr *log_name = NULL;
				log_name = log_file_name(ot->rx_log_idx);
				//info(0, "Log file changed successfully:%s",octstr_get_cstr(log_name));
				octstr_destroy(log_name);
			}
		}
	}
}

