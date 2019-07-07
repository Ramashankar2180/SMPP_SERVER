#include "gwlib/smpp_pdu.h"
#include "esme.h"
#include "server.h"

static RWLock esme_list_lock;
static List *esme_list;

static RWLock smsc_list_lock;
static List *smsc_list;

struct smsc_conn_list
{
	Octstr *smsc_id;
	Octstr *smsc_type;
	int conn_status;
	int conn_mode;
};
typedef struct smsc_conn_list SMSC_Conn_List;

/*allowed and denied IP list as read from Config file*/
Octstr *allowed_ip = NULL;
Octstr *denied_ip = NULL;
Octstr *allowed_prefix = NULL;
Octstr *denied_prefix = NULL;
Octstr *gsm_series = NULL;
Octstr *cdma_series = NULL;
Octstr *int_series = NULL;
Octstr *default_series = NULL;


void init_connect_ip(Cfg *cfg)
{
	CfgGroup *grp;
	int i;
	
	
	grp = cfg_get_single_group(cfg, octstr_imm("core"));
	if(grp == NULL)
	{
		 info(0, "Allowed/Denied IP could not be fetched");
		return;
	}
	
	/*read allowed IP*/ 
	allowed_ip = cfg_get(grp, octstr_imm("connect-allow-ip"));
    if (allowed_ip == NULL)
    	allowed_ip = octstr_create(" ");
	/*read denied IP*/
    denied_ip = cfg_get(grp, octstr_imm("connect-deny-ip"));
    if (denied_ip == NULL)
    	denied_ip = octstr_create(" ");
    if (allowed_ip != NULL && denied_ip == NULL)
	    info(0, "Server connection allowed IPs defined without any denied...");

	/*read allowed prefixes*/
	allowed_prefix = cfg_get(grp, octstr_imm("global-allow-prefix"));
    
	if (allowed_prefix == NULL)
    	allowed_prefix = octstr_create("");
    
	/*read denied prefixes*/
	denied_prefix = cfg_get(grp, octstr_imm("global-deny-prefix"));
   /* 
	if (denied_prefix == NULL)
    	denied_prefix = octstr_create("");
	*/
	/*read gsm/cdma series prefixes*/
	gsm_series = cfg_get(grp, octstr_imm("gsm-series"));
    if (gsm_series == NULL)
    	gsm_series = octstr_create("");
	
	
	cdma_series = cfg_get(grp, octstr_imm("cdma-series"));
    if (cdma_series == NULL)
    	cdma_series = octstr_create("");
	
	
	int_series = cfg_get(grp, octstr_imm("int-series"));
    if (int_series == NULL)
    	int_series = octstr_create("");	
	
	
	default_series = cfg_get(grp, octstr_imm("default-series"));
	if(default_series == NULL)
	{
		info(0, "No default series is specified :Using gsm",octstr_get_cstr(default_series));
		default_series = octstr_create("gsm");
	}
	else if(!((octstr_compare(default_series, octstr_imm("gsm")) == 0)		||
		(octstr_compare(default_series, octstr_imm("cdma")) == 0)		||
	  	(octstr_compare(default_series, octstr_imm("int")) == 0)		||
		(octstr_compare(default_series, octstr_imm("all")) == 0))
	  )
	{
		info(0, "Specfied default series %s cannot be used:Using gsm",octstr_get_cstr(default_series));
		octstr_destroy(default_series);
		default_series = octstr_create("gsm");
	}
	
	debug("test.smpp", 0, "Allow IP :%s: Deny IP :%s:",octstr_get_cstr(allowed_ip),octstr_get_cstr(denied_ip));
	debug("test.smpp", 0, "Allow Prefix :%s: Deny Prefix :%s:",
	allowed_prefix ? octstr_get_cstr(allowed_prefix):"",
	denied_prefix?octstr_get_cstr(denied_prefix):"");
	debug("test.smpp", 0, "GSM Series:%s ",octstr_get_cstr(gsm_series));
	debug("test.smpp", 0, "CDMA Series:%s ",octstr_get_cstr(cdma_series));
	debug("test.smpp", 0, "INT Series:%s ",octstr_get_cstr(int_series));
	debug("test.smpp", 0, "Default Series :%s",octstr_get_cstr(default_series));
	
}

TEMPCONN *tempconn_create(Connection *conn, const char *client_ip,int client_port,int listen_port,int mode)
{
    TEMPCONN *temp_conn;
    
    temp_conn = gw_malloc(sizeof(TEMPCONN));
    memset(temp_conn, 0, sizeof(TEMPCONN));

    temp_conn->bind_successful = 0;
	
	temp_conn->conn = conn;
    temp_conn->client_ip = octstr_create(client_ip);
	temp_conn->client_port = client_port;
    temp_conn->listen_port = listen_port;
	temp_conn->mode = mode;
	temp_conn->session_id = 0;
    return temp_conn;
}

void tempconn_destroy(TEMPCONN *temp_conn)
{
	if(temp_conn == NULL)
		return;
	if(temp_conn->bind_successful == 0)
	{
		/*DEstroying temp conn is not required cos if bind is successful both the pointers are copied into 
		esme structure*/
		conn_destroy(temp_conn->conn); //pointer is copied to tx,rx structure
		octstr_destroy(temp_conn->client_ip);	
	}
	gw_free(temp_conn);
}
void esme_destroy(ESME *esme,int mode,int session_id)
{
    
	int i;
	int found_esme = -1;
	ESME *esme1 = NULL;
	ESME_TX *esmetx = NULL;
	ESME_RX *esmerx = NULL;
	if(esme == NULL)
	{
		return;
	}
	
	/*This is done because emse is created as soon as the request comes from the clinet but 
	system id like parameters are loaded only when bind is accepted and esme is added in the list.
	When bind is not successful esme will not be deleted from the list but still memory
	allocated when request is accepted should be released.*/
	gw_rwlock_wrlock(&esme_list_lock);
	if(esme->system_id != NULL)
	{
		for (i = 0; i < gwlist_len(esme_list); i++) 
		{
        	esme1 = gwlist_get(esme_list, i);
			if(esme1->system_id == NULL)
				continue;
        	if((octstr_compare(esme->system_id,esme1->system_id)) == 0)
			{
				info(0, "ESME destroyed with id :%s	len:%d",octstr_get_cstr(esme1->system_id),(int)gwlist_len(esme_list));
				found_esme = i;
				break;
			}		
    	}
	}
	
	#define O_DELETE(a)  { if (a != NULL) octstr_destroy(a); a = NULL; }	
	if((esme != NULL)&&(found_esme != -1))
	{
		if((mode == 1 )&&(session_id > 0)&&(esme->tx_esme != NULL))
		{
			for( i = 0; i < gwlist_len(esme->tx_esme);i++)
			{
				esmetx = gwlist_get(esme->tx_esme,i);
				if(esmetx->session_id == session_id)
				{
					gwlist_delete(esme->tx_esme,i,1);
					info(0,"Deleting TX from position:%d ",i);
					break;
				}
				esmetx = NULL;
			}
			info( 0, "Destroying TX str :%s SESSION ID;%d",octstr_get_cstr(esme->system_id),esmetx->session_id);	
			conn_destroy(esmetx->conn);	
			O_DELETE(esmetx->client_ip);
			counter_destroy(esmetx->seq_number_counter);
			counter_destroy(esmetx->msg_received);
			counter_destroy(esmetx->msg_sent);
			counter_destroy(esmetx->msg_rejected);
			counter_destroy(esmetx->submits_unconnect);
			gwlist_destroy(esmetx->data_to_write,octstr_destroy_item);
			esmetx->data_to_write = NULL;
			gw_free(esmetx);
			esmetx = NULL;
			if(gwlist_len(esme->tx_esme) == 0)
			{
				info(0,"All TX of account :%s HAS BEEN DESTROYED",octstr_get_cstr(esme->system_id));
				//gwlist_destroy(esme->tx_esme,NULL);
				//esme->tx_esme = NULL;
			}
		}//destroying tx
		else if((mode == 2 )&&(session_id > 0)&&(esme->rx_esme != NULL))
		{
			for( i = 0; i < gwlist_len(esme->rx_esme);i++)
			{
				esmerx = gwlist_get(esme->rx_esme,i);
				if(esmerx->session_id == session_id)
				{
					gwlist_delete(esme->rx_esme,i,1);
					info(0,"Deleting RX from position:%d ",i);
					break;
				}
				esmerx = NULL;
			}
			info( 0, "Destroying RX str :%s",octstr_get_cstr(esme->system_id));			
			conn_destroy(esmerx->conn);	
			O_DELETE(esmerx->client_ip);
			counter_destroy(esmerx->seq_number_counter);
			counter_destroy(esmerx->msg_sent);
			gwlist_destroy(esmerx->data_to_write,octstr_destroy_item);
			esmerx->data_to_write = NULL;
			gw_free(esmerx);
			esmerx = NULL;
			if(gwlist_len(esme->rx_esme) == 0)
			{
				info(0,"All RX of account :%s HAS BEEN DESTROYED",octstr_get_cstr(esme->system_id));
				//gwlist_destroy(esme->rx_esme,NULL);
				//esme->rx_esme = NULL;
			}
		}
		if((gwlist_len(esme->tx_esme) == 0) && (gwlist_len(esme->rx_esme) == 0))
		{
			info( 0, "Destroying COMMON and deleteing from list id:%s pos:%d",octstr_get_cstr(esme->system_id),found_esme);			
			gwlist_delete(esme_list,found_esme,1);	
			O_DELETE(esme->system_id);
			O_DELETE(esme->password);
			O_DELETE(esme->system_type);
			O_DELETE(esme->address_range);
			O_DELETE(esme->log_file);
			O_DELETE(esme->rx_log_file);
			O_DELETE(esme->allowed_prefix);
			O_DELETE(esme->denied_prefix);
			gwlist_destroy(esme->tx_esme,NULL);
			esme->tx_esme = NULL;/*It is imporatant to make it NULL esme shutdown will not break*/
			gwlist_destroy(esme->rx_esme,NULL);
			esme->rx_esme = NULL;
			gw_free(esme);
			esme = NULL;
		}
    }
	#undef O_DELETE	
	gw_rwlock_unlock(&esme_list_lock);	
}

void esme_init()
{
	esme_list = gwlist_create();
    gw_rwlock_init_static(&esme_list_lock);
}
void add_esme(ESME *esme)
{
	gw_rwlock_wrlock(&esme_list_lock);
	gwlist_append(esme_list,esme);
	gw_rwlock_unlock(&esme_list_lock);
	info( 0, "ESME added with id : %s len :%d",octstr_get_cstr(esme->system_id),(int)gwlist_len(esme_list));
}

int esme_list_len()
{
	int len;
	gw_rwlock_rdlock(&esme_list_lock);
	len = gwlist_len(esme_list);
	gw_rwlock_unlock(&esme_list_lock);
	//printf("\nESME LIST is %d\n",len);
	return len;
}

void esme_shutdown(int shutdown)
{
	
	/* esme shutdown is not protected by the mutex because of following reasons-
	1. Requirement of the function was which when returns suggets that all client thread 
		have been completed successfully.
	2. Client thread to complete requires emse_destroy function to be called wicch requires mutex.
	3. esme destroy performs major changes in the list
	4. emse destroy can also be called even when the system shutdown is not done
	5. esme shutdown performs only minor changes and is called when the system is shut down.
	6. join inside this function also makes sure that esme are detroyed sequentially.Tis prevents
		all threads to call esme destroy(which requires mutex) thus deadlock.
	7. if shutdown is 1 means esme is disconnected cos server process is going down.
	*/
	int i,j;
	int list_size;
	int esme_size;
	ESME *esme;
	ESME_TX *esme_tx;
	ESME_RX *esme_rx;
	if(esme_list != NULL)
	{
		esme_size = gwlist_len(esme_list);
		for(i = 0; i < esme_size;i++)
		{
			esme = gwlist_get(esme_list, 0);
			if(esme == NULL)
				continue;
			list_size = gwlist_len(esme->tx_esme);
			for(j = 0 ; j < list_size;j++)
			{
				/*Always read for position 0, cos as as thread terminated it is deleted from the list as well*/
				esme_tx = gwlist_get(esme->tx_esme,0);
				if(esme_tx  == NULL)
					continue;
				esme_tx->status = ESME_DISCONNECT;
				debug("esme.c",0,"WAKING UP thread Name:%s Mode:%d Sessionid:%d,Threadid:%d",octstr_get_cstr(esme->system_id),esme_tx->mode,esme_tx->session_id,esme_tx->thread_id);
				gwthread_wakeup(esme_tx->thread_id);
				gwthread_join(esme_tx->thread_id);
				/*
				printf("\nAFTER THREAD JOIN :%d\n\n",gwlist_len(esme->tx_esme));
				printf("After thread join :%d\n",gwlist_len(esme->tx_esme));
				*/
				if(esme == NULL) 
				{
					break;
				}
				if(esme->tx_esme == NULL)
				{
					break;
				}
				if(gwlist_len(esme->tx_esme) == 0)
				{
					break;
				}
			}
			info(0,"All TX deleted\n");
			
			if(esme == NULL)
				break;
			list_size = gwlist_len(esme->rx_esme);
			for(j = 0 ; j < list_size;j++)
			{
				/*Always read for position 0, cos as as thread terminated it is deleted from the list as well*/
				esme_rx = gwlist_get(esme->rx_esme,0);
				if(esme_rx  == NULL)
					continue;
				esme_rx->status = ESME_DISCONNECT;
				debug("esme.c",0,"WAKING UP thread Name:%s Mode:%d Sessionid:%dThreadid:%d",octstr_get_cstr(esme->system_id),esme_rx->mode,esme_rx->session_id,esme_rx->thread_id);
				gwthread_wakeup(esme_rx->thread_id);
				gwthread_join(esme_rx->thread_id);
				/*
				printf("\nAFTER THREAD JOIN :%d\n\n",gwlist_len(esme->rx_esme));
				printf("After thread join :%d\n",gwlist_len(esme->rx_esme));
				*/
				if(esme == NULL) 
				{
					break;
				}
				if(esme->rx_esme == NULL)
				{
					break;
				}
				if(gwlist_len(esme->rx_esme) == 0)
				{
					break;
				}
			}
			info(0,"All RX deleted\n");
		} 
	}
	if(shutdown == 1)
	{
		gwlist_destroy(esme_list,NULL);
		esme_list = NULL;
		gw_rwlock_destroy(&esme_list_lock);
	#define O_DELETE(a)  { if (a != NULL) octstr_destroy(a); a = NULL; }	
		O_DELETE(allowed_ip);
		O_DELETE(denied_ip);
		O_DELETE(allowed_prefix);
		O_DELETE(denied_prefix);
		O_DELETE(default_series);
		O_DELETE(gsm_series);
		O_DELETE(cdma_series);
		O_DELETE(int_series);
	#undef O_DELETE
	}
}
void *get_session_str(ESME *esme,int mode,int session_id)
{
	int i,j;
	void *return_str = NULL;
	ESME_TX *esme_tx;
	ESME_RX *esme_rx;
	if(esme == NULL || session_id < 1 || mode < 1 || mode > 3)
		return NULL;
	gw_rwlock_rdlock(&esme_list_lock);
	if((mode == 1)||(mode == 3))
	{
		for( j = 0 ; j < gwlist_len(esme->tx_esme);j++)
		{
			esme_tx = gwlist_get(esme->tx_esme,j);
			if(esme_tx->session_id == session_id)
			{
				return_str = esme_tx;
				break;
			}
		}
	}
	else if(mode == 2)
	{
		for(j = 0 ; j < gwlist_len(esme->rx_esme);j++)
		{
			esme_rx = gwlist_get(esme->rx_esme,j);
			if(esme_rx->session_id == session_id)
			{
				return_str = esme_rx;
				break;
			}
		}
	}
	gw_rwlock_unlock(&esme_list_lock);
	return return_str;
}
Connection* extract_connection(Octstr* esme_id)
{
	int i;
	ESME *esme;
	Connection *return_conn = NULL;
	gw_rwlock_rdlock(&esme_list_lock);
	
	for (i = 0; i < gwlist_len(esme_list); i++) 
	{
        esme = gwlist_get(esme_list, i);
        if((octstr_compare(esme->system_id,esme_id)) == 0)
		{
			if((esme->mode == 2 || esme->mode > 3)&&(esme->esmerx != NULL))
				return_conn = esme->esmerx->conn;
			else if(esme->esmetx != NULL)
				return_conn = esme->esmetx->conn;
			debug("test.smpp", 0, "Returning connection with id :%s",octstr_get_cstr(esme->system_id));		
			break;
		}
    }
	gw_rwlock_unlock(&esme_list_lock);
	return return_conn;	
}
/*
unsigned long int extract_seqno_counter_value(Octstr* esme_id)
{
	int i;
	ESME *esme;
	unsigned long int return_value = 0;
	gw_rwlock_rdlock(&esme_list_lock);
	
	for (i = 0; i < gwlist_len(esme_list); i++) 
	{
        esme = gwlist_get(esme_list, i);
        if((octstr_compare(esme->system_id,esme_id)) == 0)
		{
			//debug("test.smpp", 0, "Returning connection with id :%s",octstr_get_cstr(esme->system_id));
			return_value = counter_value(esme->seq_number_counter);
			break;
		}
    }
	gw_rwlock_unlock(&esme_list_lock);
	return return_value;
}
*/
ESME *get_esme(long pos)
{
	ESME *esme = NULL;
	
	if(pos >= esme_list_len())
		return NULL;
	gw_rwlock_rdlock(&esme_list_lock);
	esme = gwlist_get(esme_list,pos);
	gw_rwlock_unlock(&esme_list_lock);
	return esme;
}
void lock_esme_list()
{
	gw_rwlock_rdlock(&esme_list_lock);

}

void unlock_esme_list()
{
	gw_rwlock_unlock(&esme_list_lock);

}

void* extract_esme(Octstr* esme_id,int *mode)
{
	
	int i,j;
	ESME *esme = NULL;
	void *rx_str = NULL;
	ESME_TX *esme_tx;
	ESME_RX *esme_rx;
	
	int s = 0;
	*mode = 0;
	debug("test.smpp",0,"---------1\n");
	/*locking unlocking for this function is done by application*/	
	for (i = 0; i < gwlist_len(esme_list); i++) 
	{
        esme = gwlist_get(esme_list, i);
debug("test.smpp",0,"---------1 %s\n",octstr_get_cstr(esme_id));
debug("test.smpp",0,"---------1 %s\n",octstr_get_cstr(esme->system_id));
        if((octstr_compare(esme->system_id,esme_id)) == 0)
		{
			break;
		}
		esme = NULL;
	}
	debug("test.smpp",0,"---------2\n");
	if(esme == NULL)
	{
		return NULL;
	}
	/*
	if((gwlist_len(esme->rx_esme) == 0 )&& (gwlist_len(esme->tx_esme) == 0))
		return NULL	;
	*/
	debug("test.smpp",0,"---------3 ;%d\n",gwlist_len(esme->rx_esme));
	if(gwlist_len(esme->rx_esme) > 0)
	{
		s = gw_rand() % gwlist_len(esme->rx_esme);
		rx_str = gwlist_get(esme->rx_esme,s);
		*mode = 2;
		debug("test.smpp",0,"Returning RX account at pos :%d",s);
	}
	else if(gwlist_len(esme->tx_esme) > 0)
	{
		for(j = 0 ; j < gwlist_len(esme->tx_esme);j++)
		{
			s = gw_rand() % gwlist_len(esme->tx_esme);
			rx_str = gwlist_get(esme->tx_esme,s);
			if(((ESME_TX*)rx_str)->mode == 3)
			{
				*mode = 3;
				debug("test.smpp",0,"Returning TRX at pos :%d",s);
				break;
			}
			rx_str = NULL;
		}
	}
	debug("test.smpp",0,"---------4\n");
    
	return rx_str;
}
int extract_log_idx(Octstr *account_name)
{
	int i;
	ESME *esme = NULL;
	
	gw_rwlock_rdlock(&esme_list_lock);
	
	for (i = 0; i < gwlist_len(esme_list); i++) 
	{
        esme = gwlist_get(esme_list, i);
        if((octstr_compare(esme->system_id,account_name)) == 0)
		{
			break;
			
		}
		esme = NULL;
    }
	gw_rwlock_unlock(&esme_list_lock);
	
	if(esme == NULL)
		return -1;
	return esme->log_idx;
}

ESME* is_present_esme(Octstr *system_id)
{
	int i;
	int return_value = 0;
	ESME *esme;
	ESME *return_esme = NULL;
	gw_rwlock_rdlock(&esme_list_lock);
	
	for (i = 0; i < gwlist_len(esme_list); i++) 
	{
        esme = gwlist_get(esme_list, i);
        if((octstr_compare(esme->system_id,system_id)) == 0)
		{
			info( 0, "ESME already loged in with id :%s",octstr_get_cstr(esme->system_id));
			return_esme = esme;
			break;
		}
    }
	gw_rwlock_unlock(&esme_list_lock);
	return return_esme;
}

long init_log_file(Octstr *username, Smsc_Client_List *translations,int mode)
{
	Smsc_Client *t = NULL;
	if(username == NULL)
		return;
		
	if((t = smpp_client_find_username(translations, username))==NULL)
		return -1;
	if(mode == 1 && smpp_client_log_file(t) != NULL)
		return smpp_client_log_idx(t);
	else if(mode == 2 && smpp_client_rx_log_file(t) != NULL)
		return smpp_client_rx_log_idx(t);
	else
		return -1;

}
/*
*This function returns the lowest possible,unique session id in the list provided.
*This function assumes that list has all items of type int,Caller must ensure that.
*/
int allocate_session_id(List *search_list,int mode)
{
	int lowest = 0;/*session id will start from 1*/
	int *found_lowest = NULL;
	int total_sessions = 0;
	List *session_list;
	int *session_id;
	int new_no;
	int i;
	ESME_TX *esme_tx;
	ESME_RX *esme_rx;
	if(search_list == NULL)
		return 0;
	
	session_list = gwlist_create();
	if(mode == 1)
	{
		for(i = 0 ; i < gwlist_len(search_list);i++)
		{
			esme_tx = (ESME_TX*)gwlist_get(search_list,i);
			gwlist_append(session_list,&(esme_tx->session_id));
		}
	}
	else if(mode == 2)
	{
		for(i = 0 ; i < gwlist_len(search_list);i++)
		{
			esme_rx = (ESME_RX*)gwlist_get(search_list,i);
			gwlist_append(session_list,&(esme_rx->session_id));
		}
		
	}
	
	/*
	Function first find the lowest in the string.If lowest is not 1, 1 is assigned as session id.
	if lowest is 1 search in the string for lowest + 1, for complete list.
	*/
	
	total_sessions = gwlist_len(session_list);/*find total number of sessions in the list*/
	/*find lowest */
	for(i = 0 ; i < total_sessions;i++)
	{
		session_id = gwlist_get(session_list,i);
		if(i == 0)
		{
			lowest = *session_id;
			continue;
		}
		if(*session_id < lowest)
			lowest = *session_id;
	}
	/*1 is not found in the list */
	if(lowest != 1)
	{
		gwlist_destroy(session_list,NULL);
		return  1;
	}
	else
	{
		for(i = 0 ; i < total_sessions-1; i++)
		{
			new_no = lowest+i+1;
			found_lowest = gwlist_search(session_list,(&new_no),int_item_match);
			if(found_lowest == NULL)
			{
				gwlist_destroy(session_list,NULL);
				return ( lowest + i + 1);
				break;
			}
		}
		gwlist_destroy(session_list,NULL);
		return (lowest + total_sessions);
	}		
}
int authorise_user(SMPP_PDU *pdu,int mode,Smsc_Client_List *translations,TEMPCONN *temp_conn,ESME **esme_ptr)
{	
	Smsc_Client *t = NULL;
	Date_Result date_result = Your_Account_Is_Not_Valid;
	Octstr *client_ip;
	ESME *esme_present = NULL;
	int init_substr = 0;
	int allowed_mode = 0;
	ESME *esme= NULL;
	int new_session_id;
	long *thread_id;

#if 0	
	if((is_allowed_ip(allowed_ip,denied_ip,temp_conn->client_ip)) == 0)
		return 8;
#endif
	
	/*parameters to function are not complete*/
	if(mode > 4 || mode < 1)
		return -1;
		
	if(pdu == NULL || translations == NULL || temp_conn == NULL)
		return -1;	
	
	/*username does not exists in config file*/
	if((t = smpp_client_find_username(translations, pdu->u.bind_transmitter.system_id))==NULL)
		return 1;
	
	/*verifying password*/
	if(pdu->u.bind_transmitter.password	!= NULL)
	{
		if(octstr_compare(pdu->u.bind_transmitter.password, smpp_client_password(t))!=0)
			return 2;
	}
	/*verify if the mode is allowed*/
	allowed_mode = smpp_client_mode(t);
	//printf("STEP 2 MODE :%d allowed mode :%d\n",mode,allowed_mode);
	if((allowed_mode < 4) && (mode != allowed_mode))
		return 3;
	/*added on 18/11/2009 */
	if (mode == 3 && allowed_mode == 4)
		return 3;
	/*verifying dates*/	
	date_result = compare_date(smpp_client_validfrom(t),smpp_client_validto(t)); 
   	if(date_result == Your_Account_Will_Valid_After_Sometime)
	{
		return  4;
	}
	else if(date_result == Your_Account_Is_Not_Valid)
	{
		return  5;
	}
	
	
	
	esme_present = is_present_esme(pdu->u.bind_transmitter.system_id);
	/*debug statements
	if(esme_present == NULL)
		printf("\n\nESME NULL IN AUTHORISE USER\n\n");	
	printf("\n\n:%d:%d\n\n",allowed_mode,mode);	
	/*debug statements*/
	
	/*bind trans when only bind tx is allowed and one esme is already binded*/
	/*
	if((allowed_mode < 4) && (mode == allowed_mode) && (esme_present != NULL))
		return 6;
	*/
	/*If TX comes again and it has alreadt logged in*/	
	/*
	if((allowed_mode == 4)&&(mode == 1)&&(esme_present != NULL) &&(esme_present->tx_thread_id != -1))
		return 9;
	if((allowed_mode == 4)&&(mode == 2)&&(esme_present != NULL) &&(esme_present->rx_thread_id != -1))
		return 9;
	*/
	if(mode == 1 || mode == 3)
	{
		if((esme_present == NULL)&&(smpp_client_get_max_tx_sessions(t) == 0))
			return 10;
		else if((esme_present != NULL)&&(gwlist_len(esme_present->tx_esme) == smpp_client_get_max_tx_sessions(t)))
			return 10;
	}
	if(mode == 2 )
	{
		if((esme_present == NULL)&&(smpp_client_get_max_rx_sessions(t) == 0))
			return 10;
		else if((esme_present != NULL)&&(gwlist_len(esme_present->rx_esme) == smpp_client_get_max_rx_sessions(t)))
			return 10;
	}
	
	gw_rwlock_wrlock(&esme_list_lock);
	if(esme_present == NULL)
	{
		
		esme = gw_malloc(sizeof(ESME));
		
		//esme->mode = mode;/*esme is first time added into the list*/		
   		/*
		esme->tx_total_sessions = 0;
		esme->rx_total_sessions = 0;
		esme->tx_session_ids = gwlist_create();
		esme->rx_session_ids = gwlist_create();
		*/
		esme->tx_esme = gwlist_create();
		esme->rx_esme = gwlist_create();
		/*
		esme->tx_session_thread_id = gwlist_create();
		esme->rx_session_thread_id = gwlist_create();
		*/
		/*
		esme->tx_thread_id = -1;
		esme->rx_thread_id = -1;
		esme->esmetx = NULL;
		esme->esmerx = NULL;
		*/
		esme->system_id = octstr_duplicate(pdu->u.bind_transmitter.system_id);
		esme->password = octstr_duplicate(pdu->u.bind_transmitter.password);
		esme->system_type = octstr_duplicate(pdu->u.bind_transmitter.system_type);
		esme->interface_version = pdu->u.bind_transmitter.interface_version;
		esme->addr_ton = pdu->u.bind_transmitter.addr_ton;
		esme->addr_npi = pdu->u.bind_transmitter.addr_npi;
		esme->address_range = octstr_duplicate(pdu->u.bind_transmitter.address_range);
		esme->enquire_link_interval = smpp_client_enquire_link_interval(t);
		esme->allowed_prefix = octstr_duplicate(smpp_client_allowed_prefix(t));
		esme->denied_prefix = octstr_duplicate(smpp_client_denied_prefix(t));
		esme->max_submits_unconnect = smpp_client_get_max_submits_unconnect(t);
		esme->max_submits = smpp_client_get_max_submits(t);
		esme->max_tps = smpp_client_get_max_tps(t);
		esme->valid_time_from = smpp_client_get_valid_from(t);
		esme->valid_time_to = smpp_client_get_valid_to(t);
		esme->dnd_check = smpp_client_get_dnd(t);
		esme->msg_count = 0;
		time(&esme->esm_time);
		esme->log_file = NULL;
		esme->rx_log_file = NULL;
		if(smpp_client_log_file(t))
		{
			esme->log_file = octstr_duplicate(smpp_client_log_file(t));
			esme->log_level = smpp_client_log_level(t);
			esme->log_idx = smpp_client_log_idx(t);
		}
		else
		{
			esme->log_level = -1;
			esme->log_idx = -1;
		}
		if(smpp_client_rx_log_file(t))
		{
			esme->rx_log_file = octstr_duplicate(smpp_client_rx_log_file(t));
			esme->log_level = smpp_client_log_level(t);
			esme->rx_log_idx = smpp_client_rx_log_idx(t);
		}
		if(mode == 1 || mode == 3)
			init_substr = 1;
			
		else if(mode == 2)
			init_substr = 2;
		
		//add_esme(esme);		/*add will be done only here*/
		gwlist_append(esme_list,esme);
		info( 0, "ESME added with id : %s len :%d",octstr_get_cstr(esme->system_id),(int)gwlist_len(esme_list));
		info( 0, "Initialising common structure : %s",octstr_get_cstr(esme->system_id));
	}
	else
	{
		esme = esme_present;
		if(mode == 1 || mode == 3)
			init_substr = 1;
		else if(mode == 2)
			init_substr = 2;
		/*
		if((esme->mode == 2) && (mode == 1 || mode == 3 ))
		{
			//If rx of the mode is already binded and now tx has come
			esme->mode = 5;
			init_substr = 1;
		}
		else if((esme->mode == 1) && (mode == 2))
		{
			//If tx of the mode is already binded and now rx has come
			esme->mode = 4;
			init_substr = 2;
		}
		*/
	}
	
	if(init_substr == 1)
	{
		ESME_TX *esmetx;
		/*
		esme->tx_thread_id = gwthread_self();
		*/
		esmetx = gw_malloc(sizeof(ESME_TX));
		esmetx->conn = temp_conn->conn;
		esmetx->client_ip = temp_conn->client_ip;
		esmetx->listen_port = temp_conn->listen_port;
		esmetx->client_port =temp_conn-> client_port;			
		time(&(esmetx->start_time));
		esmetx->msg_received = counter_create();
		esmetx->seq_number_counter = counter_create();
		counter_set(esmetx->seq_number_counter,pdu->u.bind_transmitter.sequence_number);
		esmetx->status = ESME_BINDED;
		esmetx->msg_sent = counter_create();
		esmetx->msg_rejected = counter_create();
		esmetx->submits_unconnect = counter_create();
		esmetx->quit = 0;
		esmetx->data_to_write = gwlist_create();
		esmetx->mode = mode;
		if(check_smsc_conn(pdu->u.bind_transmitter.system_id,translations) == 0 || check_smsc_conn(pdu->u.bind_transmitter.system_id,translations) == 1 )
            esmetx->status = ESME_SMSC_UNCONNECTED;
		gwlist_add_producer(esmetx->data_to_write);
		
		/*	Now session related handling*/
		new_session_id = allocate_session_id(esme->tx_esme,1);	
		esmetx->session_id = new_session_id;
		esmetx->thread_id = gwthread_self();
		info( 0, "Initialisiing TX structure : %s ADDED SESSION ID :%d Mode:%d",
		octstr_get_cstr(esme->system_id),esmetx->session_id,mode);
		temp_conn->mode = mode;
		temp_conn->session_id = new_session_id;
		/*append strcutures into esme list*/
		gwlist_append(esme->tx_esme,esmetx);
	}
	else if(init_substr == 2)
	{
				
		/*
		esme->rx_thread_id = gwthread_self();
		*/
		ESME_RX *esmerx;
		esmerx = gw_malloc(sizeof(ESME_RX));
		esmerx->conn = temp_conn->conn;
		//if(temp_conn->client_ip == NULL)
		esmerx->client_ip = temp_conn->client_ip;
		esmerx->listen_port = temp_conn->listen_port;
		esmerx->client_port = temp_conn->client_port;			
		time(&(esmerx->start_time));
		esmerx->seq_number_counter = counter_create();
		counter_set(esmerx->seq_number_counter,pdu->u.bind_receiver.sequence_number);
		esmerx->status = ESME_BINDED;
		esmerx->quit = 0;
		esmerx->mode = 2;
		esmerx->msg_sent = counter_create();
		esmerx->data_to_write = gwlist_create();
		gwlist_add_producer(esmerx->data_to_write);
		
		/*	Now session related handling*/
		new_session_id = allocate_session_id(esme->rx_esme,2);	
		esmerx->session_id = new_session_id;
		esmerx->thread_id = gwthread_self();
		info( 0, "Initialisiing RX structure : %s ADDED SESSION ID :%d Mode:%d",
		octstr_get_cstr(esme->system_id),esmerx->session_id,mode);
		temp_conn->mode = mode;
		temp_conn->session_id = new_session_id;
		/*append strcutures into esme list*/
		gwlist_append(esme->rx_esme,esmerx);
	}
	*esme_ptr = esme;
	
	gw_rwlock_unlock(&esme_list_lock);
	return 0;
	
}
SMSC_Conn_List* smsc_conn_create(Octstr *smsc_id,int conn_status,Octstr *smsc_type,int conn_mode)
{
	SMSC_Conn_List *smsc_conn_list;
	smsc_conn_list = gw_malloc(sizeof(*smsc_conn_list));
	smsc_conn_list->smsc_id = octstr_duplicate(smsc_id);
	smsc_conn_list->conn_status = conn_status;/*smsc not connected*/
	smsc_conn_list->conn_mode = conn_mode;
	smsc_conn_list->smsc_type = octstr_duplicate(smsc_type);
	return smsc_conn_list;
}


void smsc_init(void)
{
	smsc_list = gwlist_create();
    gw_rwlock_init_static(&smsc_list_lock);
}

void smsc_destroy(void *item)
{
	octstr_destroy(((SMSC_Conn_List *)item)->smsc_id);
	octstr_destroy(((SMSC_Conn_List *)item)->smsc_type);
	((SMSC_Conn_List *)item)->conn_status = 0;
	gw_free(item);
}

void smsc_shutdown(void)
{
	gw_rwlock_wrlock(&smsc_list_lock);
	gwlist_destroy(smsc_list,smsc_destroy);
	smsc_list = NULL;
    gw_rwlock_unlock(&smsc_list_lock);
	gw_rwlock_destroy(&smsc_list_lock);
}


void prepare_smsc_list(Conn_Msg *msg)
{
	int i;
	SMSC_Conn_List *smsc_conn_list;
	gw_rwlock_wrlock(&smsc_list_lock);
	smsc_conn_list = smsc_conn_create(msg->info_smsc.smsc_id,msg->info_smsc.conn_status,msg->info_smsc.smsc_type,msg->info_smsc.conn_mode);
	gwlist_append(smsc_list,smsc_conn_list);
	
	gw_rwlock_unlock(&smsc_list_lock);
	
}
void update_smsc_list(Conn_Msg *msg)
{
	int i;
	SMSC_Conn_List *smsc_conn_list;
	gw_rwlock_wrlock(&smsc_list_lock);
	
	for(i = 0; i < gwlist_len(smsc_list);i++)
	{
		smsc_conn_list = gwlist_get(smsc_list,i);
		if((octstr_compare(msg->info_smsc.smsc_id,smsc_conn_list->smsc_id)) == 0)
		{
			debug("test.smpp", 0, "Updating SMSC with id :%s",octstr_get_cstr(smsc_conn_list->smsc_id));
			smsc_conn_list->conn_status = msg->info_smsc.conn_status;
			break;
		}
	}
	
	gw_rwlock_unlock(&smsc_list_lock);
}
void display_smsc_info(void)
{
	int i;
	SMSC_Conn_List *smsc_conn_list;
	gw_rwlock_rdlock(&smsc_list_lock);
	for(i = 0; i <gwlist_len(smsc_list);i++)
	{
		smsc_conn_list = gwlist_get(smsc_list,i);
		debug("test.smpp", 0, "SMSC INFO ID:%s,Mode:%d,Status;%d",
					octstr_get_cstr(smsc_conn_list->smsc_id),
					smsc_conn_list->conn_mode,
					smsc_conn_list->conn_status);
	}	
	
	gw_rwlock_unlock(&smsc_list_lock);
}

int is_smsc_present(void)
{
	/*returns - 0 when no smsc in tx/trx mode,
				1 when smsc in tx/trx mode is there but none is conencted
				2 when smsc in tx/trx mode is there and is connecected
	*/
	int i;
	int return_result = 0;
	SMSC_Conn_List *smsc_conn_list;
	gw_rwlock_rdlock(&smsc_list_lock);
	for(i = 0; i <gwlist_len(smsc_list);i++)
	{
		smsc_conn_list = gwlist_get(smsc_list,i);
		if((smsc_conn_list->conn_mode == 1)||(smsc_conn_list->conn_mode == 3))
		{
			return_result = 1;
			debug("test.smpp", 0, "Found connected SMSC ID:%s,Mode:%d,Status:%d ",
			octstr_get_cstr(smsc_conn_list->smsc_id),
			smsc_conn_list->conn_mode,
			smsc_conn_list->conn_status);
			if(smsc_conn_list->conn_status == 1)
			{
				return_result = 2;
				break;
			}			
		}
	}
	gw_rwlock_unlock(&smsc_list_lock);
	debug("test.smpp", 0, "RETURN IS SMSC PRESENT :%d ",return_result);
	
	return return_result;	
}
int smsc_conn_status(Octstr *smsc_id)
{
	int i;
	int return_result = 0;
	SMSC_Conn_List *smsc_conn_list;
	gw_rwlock_rdlock(&smsc_list_lock);
	for(i = 0; i <gwlist_len(smsc_list);i++)
	{
		smsc_conn_list = gwlist_get(smsc_list,i);
		if(octstr_compare(smsc_id,smsc_conn_list->smsc_id) == 0)
		{
			debug("test.smpp", 0, "REturning connection status SMSC :%s-:%d ",
					octstr_get_cstr(smsc_conn_list->smsc_id),smsc_conn_list->conn_status);
			return_result = smsc_conn_list->conn_status;
			break;
		}		
	}
	gw_rwlock_unlock(&smsc_list_lock);
	return return_result;	
}
int check_smsc_conn(Octstr *username,Smsc_Client_List *translations)
{

	Smsc_Client *t = NULL;
	
	if(username == NULL)
		return 0;
		
	if ((t = smpp_client_find_username(translations, username))==NULL)
		return 0;
	
	return is_smsc_present();

}

void update_esme_conn(Smsc_Client_List *translations)
{
	int i,j;
	int esme_len;
	Smsc_Client *t = NULL;
	ESME *esme = NULL;
	ESME_TX *esme_tx = NULL;
	/*This thread changes the status of TX connected sessions of an account according to SMSC status*/	
	
	debug("test.smpp", 0, "Inside update emse connection ");
	
	gw_rwlock_wrlock(&esme_list_lock);
	for(i = 0; i < gwlist_len(esme_list); i++)
	{
		
		esme = gwlist_get(esme_list,i);
		if((check_smsc_conn(esme->system_id,translations) == 0) || (check_smsc_conn(esme->system_id,translations) == 1))
		{
			error(0,"SMSCs for account are either not present/unconnected :%s",octstr_get_cstr(esme->system_id));
			for(j = 0 ; j < gwlist_len(esme->tx_esme);j++)
			{
				esme_tx = gwlist_get(esme->tx_esme,j);
				if(esme_tx != NULL &&  esme_tx->status != ESME_SMSC_UNCONNECTED)
				{
					esme_tx->status = ESME_SMSC_UNCONNECTED;
					info(0, "Changing status of session :%d",esme_tx->session_id);
				}
			}
		}
		else if(check_smsc_conn(esme->system_id,translations) == 2)
		{
			for(j = 0 ; j < gwlist_len(esme->tx_esme);j++)
			{
				esme_tx = gwlist_get(esme->tx_esme,j);
				if(esme_tx != NULL && esme_tx->status == ESME_SMSC_UNCONNECTED)
				{
					esme_tx->status = ESME_BINDED;
					counter_set(esme_tx->submits_unconnect,0);
					info( 0, "SMSC PRESENT FOR ESME CHANGING STATUS : %s Session :%d",
					octstr_get_cstr(esme->system_id),esme_tx->session_id);
				}
				
			}//esme tx
			
		}//else
	}//for esme list	
	gw_rwlock_unlock(&esme_list_lock);

}
long convert_addr_from_pdu(Octstr *esme_name, Octstr *addr, long ton, long npi)
{
    long reason = 0;
    /*
    1- Invalid ton
    2 - Invalid npi
    3 - Invalid addr
    */

   /*This will not happen cos we are already checking for address for NULL before calling thing function*/
    if (addr == NULL)
        return 1;

    //printf("\nInside convert_addr_from_pdu : %s : %d :%d\n",octstr_get_cstr(addr),ton,npi);
    /* first verify ranges on npi and ton */

    if(     (ton > 6)   ||
            (ton < 0)
       )
    {
        error(0, "ESME[%s]: Mallformed addr `%s', Invalid TON/NPI value. ",
                     octstr_get_cstr(esme_name),
                     octstr_get_cstr(addr)
              );
        return 1;
    }
	if(!    ((npi == 0) ||
             (npi == 1) ||
             (npi == 3) ||
             (npi == 4) ||
             (npi == 6) ||
             (npi == 8) ||
             (npi == 9) ||
             (npi == 10)||
             (npi == 14)
            )
       )
    {
        error(0, "ESME[%s]: Mallformed addr `%s', Invalid NPI value. ",
                     octstr_get_cstr(esme_name),
                     octstr_get_cstr(addr)
              );

        return 2;
    }

	switch(ton)
    {
        case GSM_ADDR_TON_INTERNATIONAL:
        {
            /*
             * Checks to perform:
             *   1) assume international number has at least 7 chars
             *   2) the whole source addr consist of digits, exception '+' in front
             */
             if(octstr_len(addr) < 7)
             {
                error(0, "SMPP[%s]: Mallformed addr `%s', expected at least 7 digits. ",
                     octstr_get_cstr(esme_name),
                     octstr_get_cstr(addr));
                return 3;
            }
            else if (octstr_get_char(addr, 0) == '+' &&
                   !octstr_check_range(addr, 1, 256, gw_isdigit))
            {
                error(0, "ESME[%s]: Mallformed addr `%s', expected all digits with +. ",
                         octstr_get_cstr(esme_name),
                         octstr_get_cstr(addr));
                return 3;
            }
            else if(octstr_get_char(addr, 0) != '+' &&
                   !octstr_check_range(addr, 0, 256, gw_isdigit))
            {
                error(0, "ESME[%s]: Mallformed addr `%s', expected all digits without +. ",
                     octstr_get_cstr(esme_name),
                     octstr_get_cstr(addr));
                return 3;
            }
			/*DONT DO ANY DELETION OR ADDITIONS*/

            /* check if we received leading '00', then remove it
            if (octstr_search(addr, octstr_imm("00"), 0) == 0)
                octstr_delete(addr, 0, 2);
            */
            /* international, insert '+' if not already here
            if(octstr_get_char(addr, 0) != '+')
                octstr_insert_char(addr, 0, '+');
            */
            break;
        }//case GSM ADDR TON INTERNATIONAL


        case GSM_ADDR_TON_NATIONAL:
        {
            if(octstr_len(addr) < 10)
            {
                error(0, "ESME[%s]: Mallformed addr `%s', National length less than 10 chars. ",
                     octstr_get_cstr(esme_name),
                     octstr_get_cstr(addr));
                return 3;
            }
            break;
        }//GSM ADDR TON NATIONAL

case GSM_ADDR_TON_ALPHANUMERIC:
        {
            if(octstr_len(addr) > 11)
            {
            /* alphanum sender, max. allowed length is 11 (according to GSM specs) */
                error(0, "ESME[%s]: Mallformed addr `%s', alphanum length greater 11 chars. ",
                     octstr_get_cstr(esme_name),
                     octstr_get_cstr(addr));
                return 3;
            }
            break;
        }//GSM ADDR TON ALPHANUMERIC


        default: /* otherwise don't touch addr, user should handle it */
        {
            break;
        }
    }//switch ton
    return 0;
}



void log_smsc_info(void)
{
	int i = 0;
	SMSC_Conn_List *smsc_conn_list;
	debug("test.smpp", 0, "SMSC Info are as follows");	
	gw_rwlock_rdlock(&smsc_list_lock);
	for(i = 0; i <gwlist_len(smsc_list);i++)
	{
		smsc_conn_list = gwlist_get(smsc_list,i);
		debug("test.smpp", 0,"Loc:%d,ID:%s,Type:%s,Status:%d",i,octstr_get_cstr(smsc_conn_list->smsc_id),octstr_get_cstr(smsc_conn_list->smsc_type),smsc_conn_list->conn_status);
	}
	gw_rwlock_unlock(&smsc_list_lock);
}

int smsc_list_len()
{
	int i;
	gw_rwlock_rdlock(&smsc_list_lock);
	i = gwlist_len(smsc_list);
	gw_rwlock_unlock(&smsc_list_lock);
	return i;
}
int connected_smsc_list_len()
{
	int i = 0;
	int con_smsc = 0;
	SMSC_Conn_List *smsc_conn_list;
	gw_rwlock_rdlock(&smsc_list_lock);
	for(i = 0; i <gwlist_len(smsc_list);i++)
	{
		smsc_conn_list = gwlist_get(smsc_list,i);
		if(smsc_conn_list->conn_status == 1)
			con_smsc++;
	}
	gw_rwlock_unlock(&smsc_list_lock);
	return con_smsc;
}


int rout_pdu(Smsc_Client_List *translations,Octstr *dest_addr,Octstr *username, Octstr **ospreferred_smsc,Octstr **osallowed_smsc)
{
	
	Octstr *number_type = NULL;
	Octstr *dedicated_smsc = NULL;
	Octstr *prefer_smsc = NULL;
	SMSC_Conn_List *smsc_conn_list;
	Smsc_Client *t = NULL;
	List *list_prefer_smsc;
	
	
	int i,j,k=0;
	int index_not_allowed = 0;
	int index_preferred_smsc = 0;
	int index_allowed_smsc = 0;
	char preferred_smsc[256];
	char allowed_smsc[256];
	/*initilise all memory with -1: 
	-1 index suggests no smsc in the category
	*/
	/*conn->status is used as the mode of smsc 1-TX,2-RX,3-TRX*/	
	if(translations == NULL || dest_addr == NULL || username == NULL )
	{
		return -1;
	}	
		
	for(i = 0 ; i < 256;i++)
	{
		preferred_smsc[i] = -1;
		allowed_smsc[i] = -1;
	}
	
	
	/*username is not found in the file:find translation unit for that emse account*/
	if((t = smpp_client_find_username(translations, username))==NULL)
		return -1;
	
	/*detemine type of number*/
	/*If none of the series are specified assume default series*/
	if((!gsm_series) && (!cdma_series) && (!int_series))
		number_type = octstr_duplicate(default_series);
		
	if((gsm_series)&&(number_type == NULL))
	{
		if(does_prefix_match(gsm_series,dest_addr) == 1)
			number_type = octstr_create("gsm");
	}	
	
	else if((cdma_series)&&(number_type == NULL))
	{
		if(does_prefix_match(cdma_series,dest_addr) == 1)
			number_type = octstr_create("cdma");
	}
	
	else if((int_series)&&(number_type == NULL))
	{
		if(does_prefix_match(int_series,dest_addr) == 1)
			number_type = octstr_create("int");
	}
	
	/*if still number does not match to any series*/
	if(number_type == NULL)
		number_type = octstr_duplicate(default_series);
	
	debug("test.smpp", 0, "Dest Number :%s Type:%s",octstr_get_cstr(dest_addr),octstr_get_cstr(number_type));			
	/*
	if dedicated smsc is configured, supports that number type and is connected : 
	*/
	gw_rwlock_rdlock(&smsc_list_lock);
//	data2=smpp_client_dedicated_smsc(t);
	dedicated_smsc = smpp_client_dedicated_smsc(t);
	if(dedicated_smsc != NULL)
	{
		for(i = 0; i <gwlist_len(smsc_list);i++)
		{
			smsc_conn_list = gwlist_get(smsc_list,i);
			/*Make sure it supoorts the type and is connected*/
			if(	(octstr_compare(dedicated_smsc,smsc_conn_list->smsc_id) == 0) 	&&
				((octstr_compare(number_type,smsc_conn_list->smsc_type) == 0)	||
				octstr_compare(smsc_conn_list->smsc_type,octstr_imm("all"))==0) &&				
				((smsc_conn_list->conn_mode == 1) || (smsc_conn_list->conn_mode == 3))
			  )
			 {
			 	preferred_smsc[index_preferred_smsc++] = i;
				break;
			 }
		}
	}//dediacted_smsc
//	data1=smpp_client_preferred_smsc_tmp(t);
//info(0,"data222222 ========================= %s\n",octstr_get_cstr(data2));

//info(0,"data11111 ========================= %s\n",octstr_get_cstr(data1));
	list_prefer_smsc = smpp_client_preferred_smsc(t);
	if(list_prefer_smsc != NULL)
	{
		for(i = 0 ; i < gwlist_len(list_prefer_smsc);i++)
		{
			prefer_smsc = gwlist_get(list_prefer_smsc,i);
			
			/*search prefer smsc in the smsc list*/
			for(j = 0; j < gwlist_len(smsc_list);j++)
			{
				
				/*
				config file already makes sure that dedicated smsc ids are 
				not specified in preferred list: So no need to check for those smsc indexes which are
				already present
				*/
				smsc_conn_list = gwlist_get(smsc_list,j);
				
				/*Make sure it supoorts the type and is connected*/
				if(	(octstr_compare(prefer_smsc,smsc_conn_list->smsc_id) == 0) 			&&
					((octstr_compare(number_type,smsc_conn_list->smsc_type) == 0)		||
					 octstr_compare(smsc_conn_list->smsc_type,octstr_imm("all")) == 0)	&&
					((smsc_conn_list->conn_mode == 1) || (smsc_conn_list->conn_mode == 3))
			  	 )
				 {
			 		preferred_smsc[index_preferred_smsc++] = j;
					break;
				 }				
			}//smsc for loop
		}//prefer smsc for loop
	}//prefer smsc exists
	
	debug("test.smpp", 0, "Preferred SMSCs :%d ",index_preferred_smsc);				
	
	/*now only allowed smscs are left*/
	for(i = 0; i < gwlist_len(smsc_list);i++)
	{
		
		/*Dont set those SMSC which have already been listed in preferred SMSC*/
		if(index_preferred_smsc > 0)
		{
			for(j = 0 ; j <= index_preferred_smsc; j++)
			{
				if(preferred_smsc[j] == i)
				{
					debug("test.smpp", 0, "SMSC at index :%d is already in preferred list",j);		
					index_not_allowed = 1;
					break;
				}							
			}
			if(index_not_allowed)
			{
				index_not_allowed = 0;
				continue;
			}					
		}
		/*
		initial logic was only those SMSCs where allowed which support the number type and were connected
		but this was changed cos msg is stored in RAM and any SMSC can get connected at any time
		*/
		smsc_conn_list = gwlist_get(smsc_list,i);
		debug("test.smpp", 0, "Checking Allowed SMSC :%s   :%s :%d",octstr_get_cstr(smsc_conn_list->smsc_id),octstr_get_cstr(smsc_conn_list->smsc_type),smsc_conn_list->conn_status);
		if(	((octstr_compare(number_type,smsc_conn_list->smsc_type) == 0) 				|| 
			 (octstr_compare(smsc_conn_list->smsc_type,octstr_imm("all")) == 0)			)&&	
			((smsc_conn_list->conn_mode == 1) || (smsc_conn_list->conn_mode ==3))
		  )
		 {
			allowed_smsc[index_allowed_smsc++] = i;	
			debug("test.smpp", 0, "Allowed SMSC :%s :%d",octstr_get_cstr(smsc_conn_list->smsc_id),i);	
		 }	
	}
	gw_rwlock_unlock(&smsc_list_lock);
	
	debug("test.smpp", 0, "Preferred SMSC :%d, Allowed SMSC :%d",index_preferred_smsc,index_allowed_smsc);				
	
#if 0 //  Rama 22/07
	for(i = 0; i < MAX_NO_SMSC/2; i++)
	{
		debug("test.smpp", 0, "Prefer[%d] :%d ",i,preferred_smsc[i]);		
	}
	for(i = 0; i < MAX_NO_SMSC/2; i++)
	{
		debug("test.smpp", 0, "Allowed[%d] :%d",i,allowed_smsc[i]);		
	}
	
#endif	
	
	*ospreferred_smsc = octstr_create("");
	*osallowed_smsc = octstr_create("");
	/*
	octstr_format_append(*ospreferred_smsc,"%d",index_preferred_smsc);	
	octstr_append_char(*ospreferred_smsc,index_preferred_smsc);
	*/
	
	for( i = 0; i < index_preferred_smsc ; i++)
		octstr_append_char(*ospreferred_smsc,preferred_smsc[i]);
		/*
		octstr_format_append(*ospreferred_smsc,"%d",i);
		octstr_append_decimal(*ospreferred_smsc,i);
		*/
	/*
	octstr_format_append(*osallowed_smsc,"%d",index_allowed_smsc);
	octstr_append_char(*osallowed_smsc,index_allowed_smsc);
	*/
	for( i = 0; i < index_allowed_smsc ; i++)
		octstr_append_char(*osallowed_smsc,allowed_smsc[i]);
		/*
		octstr_append_decimal(*osallowed_smsc,i);
		octstr_format_append(*osallowed_smsc,"%d",i);
		*/
	
	debug("test.smpp", 0, "Preferred SMSC [%d]LEN[%d], Allowed SMSC [%d][%d]",
	index_preferred_smsc,
	octstr_len(*ospreferred_smsc),
	index_allowed_smsc,
	octstr_len(*osallowed_smsc));				
debug("test.smpp", 0, "ospreferred_smsc :%s Type:%s",octstr_get_cstr(*osallowed_smsc),octstr_get_cstr(*ospreferred_smsc));	
	if( index_preferred_smsc > 0 || index_allowed_smsc > 0 )
    {
        octstr_destroy(number_type);
        return 0;
    }
    else
    {
        error(0,"None of the SMSC could be found pre:%d all:%d dest:%s type:%s SMSClen :%d",
        index_preferred_smsc,
        index_allowed_smsc,
        octstr_get_cstr(dest_addr),
        octstr_get_cstr(number_type));
        octstr_destroy(number_type),
        gwlist_len(smsc_list);
        return -1;
    }
}

int  check_sender_api(Octstr **account, Octstr *source_addr)
{
  FILE *fp;
  char path[512];
  char URL[1035];
  int ret=0;

  /* Open the command for reading. */

	path[0]='\0';
  sprintf(URL,"curl -s \"http://192.168.1.27:8081/fonadaSmpp/admin/sndridsts?userid=%s&senderid=%s\"", octstr_get_cstr(*account),octstr_get_cstr(source_addr));
  fp = popen(URL, "r");
  if (fp == NULL) {
    error(0,"Failed to run command[%s]\n",URL);
  }

  /* Read the output a line at a time - output it. */
  while (fgets(path, sizeof(path), fp) != NULL) {
  }

  ret = strcmp(path,"True");	
  /* close */
  pclose(fp);

  if(!ret)
  {
	  return 1;
  }
  else
  {
	  return 0;
  }
}


int  check_dnd_api(Octstr **dest_addr)
{
  FILE *fp;
  char path[512];
  char URL[1035];
  int ret=0;

  /* Open the command for reading. */
  sprintf(URL,"curl -s \"http://192.168.1.95:8080/dndFilter/dnd/filterbyno?number=%s\"", octstr_get_cstr(*dest_addr));
  fp = popen(URL, "r");
  if (fp == NULL) {
    error(0,"Failed to run command[%s]\n",URL);
  }

  /* Read the output a line at a time - output it. */
  while (fgets(path, sizeof(path), fp) != NULL) {
  }

  ret = strcmp(path,"nondnd");	
  /* close */
  pclose(fp);

  if(!ret)
  {
	  return 1;
  }
  else
  {
	  return 0;
  }
}

long verify_pdu_para(ESME *esme, SMPP_PDU *pdu, Octstr **dest_addr,Octstr **source_addr)
{
    long reason = SMPP_ESME_ROK;
    long ret = 0;
    Octstr  *temp_addr ,   *allow_dest_addr ;

    if( (pdu->u.submit_sm.source_addr == NULL)                                  ||
        (pdu->u.submit_sm.destination_addr == NULL)                             ||
        ((pdu->u.submit_sm.source_addr != NULL) && (octstr_len(pdu->u.submit_sm.source_addr) > SMPP_MAX_SOURCE_ADDR_LEN))   ||
        ((pdu->u.submit_sm.destination_addr != NULL) && (octstr_len(pdu->u.submit_sm.destination_addr) > SMPP_MAX_DEST_ADDR_LEN))
        )
    {
        warning(0,"ERROR in either source/destination [CLIENT:%s][SOURCE:%s][SOURCE LEN:%d][DEST:%s][DEST LEN:%d][CONTENT:%s]",
                    octstr_get_cstr(esme->system_id),
                    (pdu->u.submit_sm.source_addr == NULL) ? "<Empty>":octstr_get_cstr(pdu->u.submit_sm.source_addr),
                    (pdu->u.submit_sm.source_addr == NULL) ? 0:octstr_len(pdu->u.submit_sm.source_addr),
                    (pdu->u.submit_sm.destination_addr == NULL) ? "<Empty>":octstr_get_cstr(pdu->u.submit_sm.destination_addr),
                    (pdu->u.submit_sm.destination_addr == NULL) ? 0:octstr_len(pdu->u.submit_sm.destination_addr),
                    (pdu->u.submit_sm.short_message== NULL) ? "<Empty>":octstr_get_cstr(pdu->u.submit_sm.short_message)
                    );
    if( pdu->u.submit_sm.source_addr == NULL ||
        ((pdu->u.submit_sm.source_addr != NULL) && (octstr_len(pdu->u.submit_sm.source_addr) > SMPP_MAX_SOURCE_ADDR_LEN))
      )
        return SMPP_ESME_RINVSRCADR;
    else if(    pdu->u.submit_sm.destination_addr == NULL ||
    ((pdu->u.submit_sm.destination_addr != NULL) && (octstr_len(pdu->u.submit_sm.destination_addr) > SMPP_MAX_DEST_ADDR_LEN))    )
        {
            return SMPP_ESME_RINVDSTADR;
        }
    }
	/*verify source address first*/


    ret = convert_addr_from_pdu(    esme->system_id,
                                    pdu->u.submit_sm.source_addr,
                                    pdu->u.submit_sm.source_addr_ton,
                                    pdu->u.submit_sm.source_addr_npi
                                );

    /*invalid ton*/
    if(ret == 1)
    {
        return SMPP_ESME_RINVSRCTON;
    }

    /*invalid npi*/
    if(ret == 2)
        return SMPP_ESME_RINVSRCNPI;

    /*invalid address*/
    if(ret == 3)
        return SMPP_ESME_RINVSRCADR;

    /*verify destination address*/
    ret = convert_addr_from_pdu(    esme->system_id,
                                    pdu->u.submit_sm.destination_addr,
                                    pdu->u.submit_sm.dest_addr_ton,
                                    pdu->u.submit_sm.dest_addr_npi
                                );

    /*invalid ton*/
    if(ret == 1)
        return SMPP_ESME_RINVDSTTON;

    /*invalid npi*/
    if(ret == 2)
        return SMPP_ESME_RINVDSTNPI;
	/*invalid address*/
    if(ret == 3)
        return SMPP_ESME_RINVDSTADR;

    /*
    verify whether destination number is allowed or not
    */

    if(octstr_len(pdu->u.submit_sm.destination_addr) == 10)/*10 digit no*/
	{
          temp_addr = octstr_duplicate(pdu->u.submit_sm.destination_addr);
	}
    else if(octstr_len(pdu->u.submit_sm.destination_addr) == 11)/*0no*/
	{
        temp_addr = octstr_from_position(pdu->u.submit_sm.destination_addr,1);
	}
    else if(octstr_len(pdu->u.submit_sm.destination_addr) == 12)/*91no*/
	{
    	temp_addr = octstr_from_position(pdu->u.submit_sm.destination_addr,2);
	}
    else if(octstr_len(pdu->u.submit_sm.destination_addr) == 13)/*+91no*/
	{
    	temp_addr = octstr_from_position(pdu->u.submit_sm.destination_addr,3);
	}
    else                            /*number type is not known*/
	{
     	temp_addr = octstr_duplicate(pdu->u.submit_sm.destination_addr);
	}

    allow_dest_addr =   octstr_format("91%s",octstr_get_cstr(temp_addr));
    *dest_addr          =   temp_addr;
    *source_addr        =   octstr_duplicate(pdu->u.submit_sm.source_addr);

	
		
#if 1 // This is main code chane by Rama 22/07
	//if(allowed_prefix)
	if(!pattern_list_matches_ip(esme->allowed_prefix, *source_addr))
	{
	
	debug("server_test.c",0, "Inside check_sender_api for sender- [%s] esme exist with sender id[%s]",octstr_get_cstr(pdu->u.submit_sm.source_addr),octstr_get_cstr(allowed_prefix), octstr_get_cstr(esme->allowed_prefix));
	ret = check_sender_api(&esme->system_id,*source_addr);
	if(!ret)
		// if (esme->allowed_prefix && !pattern_list_matches_ip(esme->allowed_prefix, *source_addr))
	{

		error(0, "ESME[%s]:  rejected by global allowed prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*source_addr));
		octstr_destroy(allow_dest_addr);
		return 0x00000401;
	}
	esme->allowed_prefix = octstr_duplicate(*source_addr);
	}
	else
	{
	debug("server_test.c",0, "Already  check_sender_api for sender- %s esme sender id -%s ",octstr_get_cstr(pdu->u.submit_sm.source_addr), octstr_get_cstr(esme->allowed_prefix));

	}
#endif 
#if 0
    if (    denied_prefix   &&
            !allowed_prefix &&
            (does_prefix_match(denied_prefix, allow_dest_addr) == 1)
        )
    {
        error(0, "ESME[%s]: Number rejected by global denied prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr));
        octstr_destroy(allow_dest_addr);
        return 0x00000401;
    }


    if (    denied_prefix   &&
            allowed_prefix  &&
            (does_prefix_match(allowed_prefix, allow_dest_addr) != 1) &&
            (does_prefix_match(denied_prefix, allow_dest_addr) == 1)
        )
    {
        error(0, "ESME[%s]: Number rejected by global allowed/denied prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr));
        octstr_destroy(allow_dest_addr);
        return 0x00000401;
    }

	if((does_prefix_match(cdma_series,allow_dest_addr)==1) && (gw_isalphanumeric(octstr_get_cstr(*source_addr)) || octstr_len(*source_addr)<10))
    {

        error(0, "ESME[%s]: dest address is CDMA %s and source address is alphanumeric :%s  ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr),octstr_get_cstr(*source_addr));
        octstr_destroy(allow_dest_addr);
        return 0x00000401;
    }

    if (    denied_prefix   &&
            allowed_prefix  &&
            (does_prefix_match(denied_prefix, allow_dest_addr) == 1)
        )
    {
        /*This condition will come when same series is present in allowed/denied - reject the sms*/
        error(0, "ESME[%s]: Number rejected by global allowed/denied prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr));
        octstr_destroy(allow_dest_addr);
        return 0x00000401;
    }

    /*Now check for account allowed/denied prefixes*/
    if (    esme->allowed_prefix &&
            !(esme->denied_prefix) &&
            (does_prefix_match(esme->allowed_prefix, allow_dest_addr) != 1)
        )
    {
        error(0, "ESME[%s]: Number rejected by account allowed prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr));
        octstr_destroy(allow_dest_addr);
		return 0x00000402;
    }

	if (    esme->denied_prefix     &&
            !(esme->allowed_prefix) &&
            (does_prefix_match(esme->denied_prefix, allow_dest_addr) == 1)
        )
    {
        error(0, "ESME[%s]: Number rejected by account denied prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr));
        octstr_destroy(allow_dest_addr);
        return 0x00000402;

    }
    if (    esme->denied_prefix     &&
            esme->allowed_prefix    &&
            (does_prefix_match(esme->allowed_prefix, allow_dest_addr) != 1) &&
            (does_prefix_match(esme->denied_prefix, allow_dest_addr) == 1)
        )
    {
        error(0, "ESME[%s]: Number rejected by account allowed/denied prefix :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr)) ;
        octstr_destroy(allow_dest_addr);
        return 0x00000402;
    }
#endif
    if(octstr_len(*dest_addr) < 10)
    {
           error(0, "ESME[%s]: Number rejected dest_addr less than 10 :%s ",octstr_get_cstr(esme->system_id),octstr_get_cstr(*dest_addr));
        octstr_destroy(allow_dest_addr);
         return 0x00000401;
    }
    /*verify priority flag*/
    if(pdu->u.submit_sm.priority_flag < 0 || pdu->u.submit_sm.priority_flag > 3)
    {
        error(0, "ESME[%s]: Invalid Priority :%d ",octstr_get_cstr(esme->system_id),pdu->u.submit_sm.priority_flag);
        return SMPP_ESME_RINVPRTFLG;
    }

	/*we will not support replacing*/
    if(pdu->u.submit_sm.replace_if_present_flag > 0)
    {
        error(0, "ESME[%s]: Replace_if_present can not be supported :%d ",octstr_get_cstr(esme->system_id),pdu->u.submit_sm.replace_if_present_flag);
        return SMPP_ESME_RINVSUBREP;
    }


    /*verify udh length if there is a mismatch then generated esme invalid esm class*/
    if(pdu->u.submit_sm.esm_class & ESM_CLASS_SUBMIT_UDH_INDICATOR)
    {
        int udhl;
        udhl = octstr_get_char(pdu->u.submit_sm.short_message, 0) + 1;
        debug("server.c",0,"ESME[%s]: UDH length read as %d",octstr_get_cstr(esme->system_id), udhl);
        if (udhl > octstr_len(pdu->u.submit_sm.short_message))
        {
            error(0, "ESME[%s]: Mallformed UDH length indicator 0x%03x while message length "
                     "0x%03lx. Discarding MT message.", octstr_get_cstr(esme->system_id),
                     udhl, octstr_len(pdu->u.submit_sm.short_message));
            return 0x00000403;
        }
    }
	octstr_destroy(allow_dest_addr);
    return reason;
}


void update_info(SMPP_PDU *pdu,Octstr *account_msg_id,int *status)
{
	Octstr *short_msg = NULL;
	Octstr *text_msg = NULL;
	
	int ret;
	char id[64];
	char sub[4];
	char dlvrd[4];
	char submit_date[12];
	char done_date[12];
	char stat[20];
	char err[4];
	char text[20];
	
	char result[12];
  char *result1 = NULL;
  int Index;
	char *lasts;
	char delims[8]=": ";	
	
	
	if(octstr_len(pdu->u.deliver_sm.short_message) == 0)
		{
			short_msg = pdu->u.deliver_sm.message_payload;
			debug("server_test.c",0, "SHORT MESSAGE RECV in Payload  - %s ",octstr_get_cstr(short_msg));
		}
	else
		{
		  short_msg = pdu->u.deliver_sm.short_message;
		  debug("server_test.c",0, "SHORT MESSAGE RECV in Short Message  - %s ",octstr_get_cstr(short_msg));
		}
	pdu->u.deliver_sm.receipted_message_id	= octstr_duplicate(account_msg_id); // Rama Change on 22/07
	/*
	if(octstr_len(pdu->u.deliver_sm.receipted_message_id) != 0)
		msg_id = octstr_duplicate(pdu->u.deliver_sm.receipted_message_id);
	*/
	
	memset(text,'\0',sizeof(text));
	
	ret = sscanf(octstr_get_cstr(short_msg),
             "id:%64[^s] sub:%s dlvrd:%s submit date:%12[0-9] done date:%12[0-9] stat:%10[^e] err:%s",
                    id, sub, dlvrd, submit_date, done_date, stat,err);
	
#if 0
	Index=0;
     result1 = strtok_r( octstr_get_cstr(short_msg), delims,&lasts );
     while( result1 != NULL )
     {
      	sprintf(result,"%s",result1);
            switch(Index)
             {
                case 1:
                     strcpy(id,result);
                     break;
                case 3:
                     strcpy(sub,result);
                     break;
                case 5:
                     strcpy(dlvrd,result);
                     break;
                case 8:
                     strcpy(submit_date,result);
                     break;
                case 11:
                     strcpy(done_date,result);
                      break;
                case 13:
                     //strcpy(stat,result);
                     strcpy(stat,result);
                     break;
              case 15:
                     strcpy(err,result);
                     break;
                case 17:
										if(octstr_len(pdu->u.deliver_sm.short_message) == 0)
										{
                    	strcpy(text,"Long message");
										}
										else
										{
                    	strcpy(text,result);
										}
                    break;
                default :
                        break;
                }
            Index++;
            result1 = strtok_r( NULL, delims,&lasts );
        }
	#endif
	//times_octstr_get_text(text,short_msg,8);
	if(strcmp(stat,"DELIVRD")==0)
	    *status = 1;
	else if(strcmp(stat,"EXPIRED")==0)
	    *status = 2;
	else if(strcmp(stat,"DELETED")==0)
	    *status = 3;
	else if(strcmp(stat,"UNDELIV")==0)
	    *status = 4;
	else if(strcmp(stat,"ACCEPTD")==0)
     	    *status = 5;
	else if(strcmp(stat,"UNKNOWN")==0)
	    *status = 6;
  	else if(strcmp(stat,"REJECTD")==0)
       	    *status = 7;
	else
	    *status = 10; 
	
	//strncpy(stat1,stat,10);
	if(strlen(submit_date)<1)
	{
		debug("server_test.c",0, "Message recvd with Null_submit_date - %s  -%s",octstr_get_cstr(short_msg),text);
    strcpy(submit_date,done_date);
	}

		//debug("server_test.c",0, "SHORT MESSAGE RECV  - %s - %s and stat [%s]",octstr_get_cstr(short_msg),text,stat);
	/*this is assuming that all fileds coming from SMSC are correct and they are simply copies for the
	client*/
	//pdu->u.deliver_sm.short_message = octstr_format(
		//					"id:%s sub:%s dlvrd:%s submit date:%s done date:%s stat:%s err:%s text:%s",
			//				octstr_get_cstr(account_msg_id),sub,dlvrd,submit_date,done_date,stat,err,text);
	pdu->u.deliver_sm.short_message = octstr_format(
							"id:%s sub:%s dlvrd:%s submit date:%s done date:%s stat:%s err:%s  text:%s",
							octstr_get_cstr(account_msg_id),sub,dlvrd,submit_date,done_date,stat,err,text);
	
	
	octstr_destroy(short_msg);
	/*copy rest of the fields */
	
}


void write_esme_conn(ESME *esme,int mode,void *session_str,int thread_session_id)
{
	
	
	Octstr *new =  NULL;
	SMPP_PDU *pdu = NULL;
	int ret;
	struct msg_deliver *msg_deliver_ptr;
	Octstr *converted = NULL;
	struct db_extract_values db_extract;
	int status;
	
	if(session_str == NULL)
		return;
	while(1)
	{
		if((mode == 1)&&(((ESME_TX*)session_str)->data_to_write != NULL))
			msg_deliver_ptr = gwlist_extract_first(((ESME_TX*)session_str)->data_to_write);
		else if(((ESME_RX*)session_str)->data_to_write != NULL)
			msg_deliver_ptr = gwlist_extract_first(((ESME_RX*)session_str)->data_to_write);
		else
			break;
		if(msg_deliver_ptr == NULL)
			break;
		/*	
		try = bin_to_ascii(os);
		debug("server_test.c",0, "Data received server_test:%s : %d :%d",octstr_get_cstr(try),octstr_len(try),octstr_len(os));
		octstr_destroy(try);
		*/
		
		new = octstr_from_position(msg_deliver_ptr->os,4);/*this is beacuse pdu unpack can read data only if length is not
		present*/
		
		/*
		try = bin_to_ascii(new);
		debug("server_test.c",0, "Data received server_test1:%s : %d :%d",octstr_get_cstr(try),octstr_len(try),octstr_len(new));
		octstr_destroy(try);
		*/
		
		pdu = smpp_pdu_unpack(new);
		if(pdu == NULL) 
		{
			error(0, "PDU unpacking failed!");
			octstr_dump(msg_deliver_ptr->os, 0);			
			octstr_destroy(msg_deliver_ptr->os);
			octstr_destroy(msg_deliver_ptr->smsc_id);
			octstr_destroy(msg_deliver_ptr->smsc_msg_id);
			octstr_destroy(msg_deliver_ptr->account_msg_id);
			gw_free(msg_deliver_ptr);
			octstr_destroy(new);
			continue;		
		}
		else
		{
			
			 debug("test.smpp", 0, "ESME[ID:%s,Mode:%d,Session:%d] Sending PDU :%s",
			 octstr_get_cstr(esme->system_id),mode,thread_session_id,pdu->type_name);
			switch(pdu->type)
			{
				case deliver_sm:
				{
					if(mode == 1)
						pdu->u.deliver_sm.sequence_number = counter_increase(((ESME_TX*)session_str)->seq_number_counter);
					else
						pdu->u.deliver_sm.sequence_number = counter_increase(((ESME_RX*)session_str)->seq_number_counter);
					update_info(pdu,msg_deliver_ptr->account_msg_id,&status);
					
					/*remove database entries*/
					if(status != 5)
					{
						strcpy(db_extract.smsc_id,octstr_get_cstr(msg_deliver_ptr->smsc_id));
						strcpy(db_extract.smsc_msg_id,octstr_get_cstr(msg_deliver_ptr->smsc_msg_id));
						//send_msg_database_q(DATABASE_MSGQ_TYPE,REMOVE_VALUES,0,&db_extract);
					}
					break;
				}//deliver sm
				
				case submit_sm_resp:
				{
					break;
				}//submit sm resp
				
				
			}//switch pdu->type
			smpp_pdu_dump(pdu);
		} 
		
		converted = smpp_pdu_pack(pdu);
		if(mode == 1)
			ret = conn_write(((ESME_TX*)session_str)->conn, converted);
		else
		{
    		ret = conn_write(((ESME_RX*)session_str)->conn, converted);
			counter_increase(((ESME_RX*)session_str)->msg_sent);
		}
		if(ret == -1)	
		{
			info(0, "Connectivity lost : %s", octstr_get_cstr(esme->system_id));
		}
		//info(0, "Data written on :%s with return value :%d",octstr_get_cstr(esme->system_id),ret);

		octstr_destroy(msg_deliver_ptr->os);
		octstr_destroy(msg_deliver_ptr->smsc_id);
		octstr_destroy(msg_deliver_ptr->smsc_msg_id);
		octstr_destroy(msg_deliver_ptr->account_msg_id);
		
		octstr_destroy(converted);
		octstr_destroy(new);
		
		gw_free(msg_deliver_ptr);
		smpp_pdu_destroy(pdu);
		
	}//while	
}
Octstr *print_esme_status(int status_type)
{
	
	char *lb;
    int para = 0;
	Octstr *tmp;
	int i,j;
	int details = 1;
	char no[10];
	char no1[64];
	long online;
	int loop_value = 0;
	ESME_TX *esmetx;
	ESME_RX *esmerx;
	ESME *esme;
	
	lb = (char *)bb_status_linebreak(status_type);
	if (lb  == NULL)
        return octstr_create("Un-supported format");
	
	 if(status_type == BBSTATUS_HTML || status_type == BBSTATUS_WML)
        para = 1;
		
	
	if(esme_list_len() == 0)
	{
		return octstr_format("%sNo ESME connections%s\n\n", 
									para ? "<p>" : "",
                                 	para ? "</p>" : "");		
	}	
	
	
		
	if (status_type != BBSTATUS_XML)
        tmp = octstr_format("%sConnected ESME :%s", para ? "<p>" : "", lb);
    else
        tmp = octstr_format("<smscs><count>%d</count>\n\t", esme_list_len());
	
	gw_rwlock_rdlock(&esme_list_lock);
	
	
	for(i = 0; i < gwlist_len(esme_list);i++)
	{
		esme = gwlist_get(esme_list,i);
		/*First display common things*/
		details = 1;
		if(status_type == BBSTATUS_HTML)
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
		else if(status_type == BBSTATUS_TEXT)
			octstr_append_cstr(tmp, "    ");
			
		octstr_append(tmp, esme->system_id);
		octstr_append_cstr(tmp,"-------ACCOUNT DETAILS ");	
		if(status_type == BBSTATUS_HTML)
			octstr_append_cstr(tmp, "</b>&nbsp;&nbsp;&nbsp;&nbsp;");
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "  ");
		}
		
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");
		
		/*No of sessions in tx mode*/
		if(status_type == BBSTATUS_HTML)	
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		}
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
		}
		snprintf(no,sizeof(no),"%d",details);
		snprintf(no1,sizeof(no1),"%d",gwlist_len(esme->tx_esme));
		octstr_append_cstr(tmp,no);
		octstr_append_cstr(tmp,".Connected TX Sessions:");
		octstr_append_cstr(tmp, no1);
		details++;
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");
	
		/*No of sessions in RX mode*/
		if(status_type == BBSTATUS_HTML)	
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		}
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
		}
		snprintf(no,sizeof(no),"%d",details);
		snprintf(no1,sizeof(no1),"%d",gwlist_len(esme->rx_esme));
		octstr_append_cstr(tmp,no);
		octstr_append_cstr(tmp,".Connected RX Sessions:");
		octstr_append_cstr(tmp, no1);
		details++;
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");
		/*Vesrion*/
		if(status_type == BBSTATUS_HTML)	
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		}
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
		}
		snprintf(no,sizeof(no),"%d",details);
		octstr_append_cstr(tmp,no);			
		octstr_append_cstr(tmp,".SMPP Version Support:");
		snprintf(no,sizeof(no),"%d",esme->interface_version);
		octstr_append_cstr(tmp,no);
		details++;
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");
		/*Log File Path*/
		if(status_type == BBSTATUS_HTML)	
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		}
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
		}
		snprintf(no,sizeof(no),"%d",details);
		octstr_append_cstr(tmp,no);			
		octstr_append_cstr(tmp,".Log file Path:");
		octstr_append(tmp, esme->log_file);
		details++;
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");
		
		/*max submits unconnected*/
		if(status_type == BBSTATUS_HTML)	
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		}
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
		}
		snprintf(no,sizeof(no),"%d",details);
		octstr_append_cstr(tmp,no);			
		octstr_append_cstr(tmp,".Max submits allowed:");
		snprintf(no,sizeof(no),"%d",esme->max_submits_unconnect);
		octstr_append_cstr(tmp,no);
		details++;
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");

		/*submits unconnected so far*/
		if(status_type == BBSTATUS_HTML)	
		{
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
		}
		else if(status_type == BBSTATUS_TEXT)
		{
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
			octstr_append_cstr(tmp, "    ");
		}
		snprintf(no,sizeof(no),"%d",details);
		octstr_append_cstr(tmp,no);			
		octstr_append_cstr(tmp,".Submits unconnect:");
		snprintf(no,sizeof(no),"%d",max_submits_unct_account(esme,0));
		octstr_append_cstr(tmp,no);
		details++;
		if(status_type == BBSTATUS_HTML)				
			octstr_append_cstr(tmp,"<br>");
		else if(status_type == BBSTATUS_TEXT)
			 octstr_append_cstr(tmp,"\n");

		for(j = 0 ; j < gwlist_len(esme->tx_esme);j++)
		{
			
			details = 1;
			esmetx = gwlist_get(esme->tx_esme,j);
			if(status_type == BBSTATUS_HTML)
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
			else if(status_type == BBSTATUS_TEXT)
				octstr_append_cstr(tmp, "    ");
				
			octstr_append(tmp, esme->system_id);
			
			if(status_type == BBSTATUS_HTML)
				octstr_append_cstr(tmp, "</b>&nbsp;&nbsp;&nbsp;&nbsp;");
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "  ");
			}
			/*Account Name*/
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);
			octstr_append_cstr(tmp,".System Id:");
			octstr_append(tmp, esme->system_id);
			details++;
			
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");

			/*Mode*/
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Mode of bind:");
			if(esmetx->mode == 1)
				octstr_append_cstr(tmp,"Transmitter");
			else if(esmetx->mode == 3)
				octstr_append_cstr(tmp,"Transceiver");
			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");

			/*Session ID*/
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			online = (esmetx->session_id);
			snprintf(no1,sizeof(no1),"%ld",online);
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Session ID:");
			octstr_append_cstr(tmp,no1);	
		
			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
			/*Status*/
					
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Connection Status:");
			if(esmetx->status == ESME_UNCONNECTED)
				octstr_append_cstr(tmp, "UNCONNECTED");
			else if(esmetx->status == ESME_BINDED)
				octstr_append_cstr(tmp, "CONNECTED");	
			else if((esmetx->status == ESME_DISCONNECT)|| (esmetx->status == ESME_SEND_UNBIND))
				octstr_append_cstr(tmp, "UNCONNECTED");	
			else if(esmetx->status == ESME_SMSC_UNCONNECTED)
				octstr_append_cstr(tmp, "SMSCs UNCONNECTED");
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
			
			/*Client IP*/
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
				
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".ESME client IP:");
			octstr_append(tmp, esmetx->client_ip);
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
			
			/*Online Time*/
			online = time(NULL) - (esmetx->start_time);
			snprintf(no1,sizeof(no1),"%lds",online);
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Online Time:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
			/*SMS Received*/
			online = counter_value(esmetx->msg_received) ;
			snprintf(no1,sizeof(no1),"%ld",online);
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".SMS Received:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
		
			/*SMS Sent*/
			online = counter_value(esmetx->msg_sent);
			snprintf(no1,sizeof(no1),"%ld",online);
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".SMS Sent:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
		
			/*Rejected*/
			online = counter_value(esmetx->msg_rejected);
			snprintf(no1,sizeof(no1),"%lds",online);
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".SMS Rejected:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
		/*sms submitted with smsc unconnected*/
			online = counter_value(esmetx->submits_unconnect);
			snprintf(no1,sizeof(no1),"%ld",online);
			if(status_type == BBSTATUS_HTML)
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}

			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);
			octstr_append_cstr(tmp,".Submits SMSC Unconnected:");
			octstr_append_cstr(tmp,no1);
			details++;
			if(status_type == BBSTATUS_HTML)
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");

			/*SMS Per Sec*/
			float msg_sec;
			online = time(NULL) - (esmetx->start_time);
			if(online == 0)
				msg_sec = 0;
			else
				msg_sec = counter_value(esmetx->msg_sent)/online;
			snprintf(no1,sizeof(no1),"%.2f",msg_sec);
			
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".SMS per second:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
				
		
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				octstr_append_cstr(tmp,"\n");			

		}//tx esme
		for(j = 0 ; j < gwlist_len(esme->rx_esme);j++)
		{
			
			details = 1;
			esmerx = gwlist_get(esme->rx_esme,j);
			if(status_type == BBSTATUS_HTML)
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
			else if(status_type == BBSTATUS_TEXT)
				octstr_append_cstr(tmp, "    ");
				
			octstr_append(tmp, esme->system_id);
			
			if(status_type == BBSTATUS_HTML)
				octstr_append_cstr(tmp, "</b>&nbsp;&nbsp;&nbsp;&nbsp;");
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "  ");
			}
			/*Account Name*/
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);
			octstr_append_cstr(tmp,".System Id:");
			octstr_append(tmp, esme->system_id);
			details++;
			
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");

			/*Mode*/
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Mode of bind:");
			if(esmerx->mode == 2)
				octstr_append_cstr(tmp,"Receiver");
			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");

			/*Session ID*/
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			online = (esmerx->session_id);
			snprintf(no1,sizeof(no1),"%ld",online);
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Session ID:");
			octstr_append_cstr(tmp,no1);	
		
			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
					
			/*Status*/
					
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Connection Status:");
			if(esmerx->status == ESME_UNCONNECTED)
				octstr_append_cstr(tmp, "UNCONNECTED");
			else if(esmerx->status == ESME_BINDED)
				octstr_append_cstr(tmp, "CONNECTED");	
			else if((esmerx->status == ESME_DISCONNECT)|| (esmerx->status == ESME_SEND_UNBIND))
				octstr_append_cstr(tmp, "UNCONNECTED");	
			else if(esmerx->status == ESME_SMSC_UNCONNECTED)
				octstr_append_cstr(tmp, "SMSCs UNCONNECTED");
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
			
			/*Client IP*/
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
				
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".ESME client IP:");
			octstr_append(tmp, esmerx->client_ip);
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
			
			/*Online Time*/
			online = time(NULL) - (esmerx->start_time);
			snprintf(no1,sizeof(no1),"%lds",online);
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".Online Time:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
			
					
			/*SMS Sent*/
			online = counter_value(esmerx->msg_sent);
			snprintf(no1,sizeof(no1),"%ld",online);
			if(status_type == BBSTATUS_HTML)	
			{
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
				octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
			}
			else if(status_type == BBSTATUS_TEXT)
			{
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
				octstr_append_cstr(tmp, "    ");
			}
			
			snprintf(no,sizeof(no),"%d",details);
			octstr_append_cstr(tmp,no);			
			octstr_append_cstr(tmp,".SMS Sent:");
			octstr_append_cstr(tmp,no1);			
			details++;
			if(status_type == BBSTATUS_HTML)				
				octstr_append_cstr(tmp,"<br>");
			else if(status_type == BBSTATUS_TEXT)
				 octstr_append_cstr(tmp,"\n");
		}//rx esme
		
	}//for loop for no of esme

	gw_rwlock_unlock(&esme_list_lock);
	
	if(para)
	{
		//octstr_append_cstr(tmp, "</p>");
		octstr_append_cstr(tmp, "</p>");
		//printf("\n\nAPPENDING P TWICE\n\n");

	}
	if (status_type == BBSTATUS_XML)
		octstr_append_cstr(tmp, "</smscs>\n");
	else
		octstr_append_cstr(tmp, "\n\n");
	/*
	else if (status_type == BBSTATUS_TEXT) 
	{

	}
	*/
	
	return tmp;
	
}
int stop_esme(Octstr *esme)
{
	ESME *esme_pos = NULL;
    long i = -1;


    gw_rwlock_rdlock(&esme_list_lock);
    for (i = 0; i < gwlist_len(esme_list); i++)
	{
		esme_pos = gwlist_get(esme_list, i);
		if((esme_pos != NULL)&&(octstr_compare(esme_pos->system_id,esme) == 0))
		{
			break;
		}		
	}
	if(i >= gwlist_len(esme_list))
		return -1;
	
	if(esme_pos->esmetx != NULL && esme_pos->tx_thread_id != -1)
	{
		esme_pos->esmetx->status = ESME_SEND_UNBIND;
		gwthread_wakeup(esme_pos->tx_thread_id);
    }
	if(esme_pos->esmerx != NULL && esme_pos->rx_thread_id != -1)
	{
		esme_pos->esmerx->status = ESME_SEND_UNBIND;
		gwthread_wakeup(esme_pos->rx_thread_id);
    }
	gw_rwlock_unlock(&esme_list_lock);
    return 0;
}

long max_submits_unct_account(ESME *esme,int lock)
{
	int i;
	int number = 0;
	ESME_TX *esme_tx;
	if(esme == NULL)
		return 0;
	if(gwlist_len(esme->tx_esme) == 0)
		return 0;
	if(lock)
    	gw_rwlock_rdlock(&esme_list_lock);
	for(i = 0 ; i < gwlist_len(esme->tx_esme);i++)
	{
		esme_tx = gwlist_get(esme->tx_esme,i);
		number += counter_value(esme_tx->submits_unconnect);
	}
	if(lock)
		gw_rwlock_unlock(&esme_list_lock);
	return number;
}

int  check_smsc_api()
{
  FILE *fp;
  char path[512];
  char URL[1035];
  int ret=0;

  /* Open the command for reading. */
  sprintf(URL,"curl -s \"http://localhost:33000/smsc_status\"");
  fp = popen(URL, "r");
  if (fp == NULL) {
    error(0,"Failed to run command[%s]\n",URL);
  }

  /* Read the output a line at a time - output it. */
  while (fgets(path, sizeof(path), fp) != NULL) {
  }

  ret = strcmp(path,"nondnd");	
  /* close */
  pclose(fp);

  if(!ret)
  {
	  return 1;
  }
  else
  {
	  return 0;
  }
}
