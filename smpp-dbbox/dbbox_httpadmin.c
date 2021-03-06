

//#include <unistd.h>

#include "dbbox_httpadmin.h"
#include "dbbox.h"
#include<sys/utsname.h>


#if defined(HAVE_LIBSSL) || defined(HAVE_WTLS_OPENSSL) 
#include <openssl/opensslv.h>
#endif
#ifdef HAVE_MYSQL 
#include <mysql_version.h>
#include <mysql.h>
#endif
#ifdef HAVE_SQLITE 
#include <sqlite.h>
#endif



static volatile sig_atomic_t httpadmin_running;
extern volatile sig_atomic_t program_status;


static long	ha_port;
static Octstr *ha_interface;
static Octstr *ha_password;
static Octstr *ha_status_pw;
static Octstr *ha_allow_ip;
static Octstr *ha_deny_ip;


static Octstr *httpd_check_status(void)
{
    if (program_status == PROGRAM_SHUTDOWN || program_status == PROGRAM_DEAD)
	return octstr_create("Avalanche has already started, too late to "
	    	    	     "save the sheeps");
    return NULL;
}

/*
 * check if the password matches. Return NULL if
 * it does (or is not required)
 */
static Octstr *httpd_check_authorization(List *cgivars, int status)
{
    Octstr *password;
    static double sleep = 0.01;

    password = http_cgi_variable(cgivars, "password");

    if(status) 
	{
		if (ha_status_pw == NULL)
	    	return NULL;

		if (password == NULL)
	    	goto denied;

		if (octstr_compare(password, ha_password)!=0
	    	&& octstr_compare(password, ha_status_pw)!=0)
	    	goto denied;
    }
    else 
	{
		if (password == NULL || octstr_compare(password, ha_password)!=0)
	    	goto denied;
    }
    sleep = 0.0;
    return NULL;	/* allowed */

denied:
    gwthread_sleep(sleep);
    sleep += 1.0;		/* little protection against brute force
				 * password cracking */
    return octstr_create("Denied");
}

char* dbbox_get_status_name(sig_atomic_t program_status)
{
	char *ptr;
	switch(program_status)
	{
		case PROGRAM_RUNNING:
		{
			ptr = "Running";
			return ptr;			
		}
		case PROGRAM_ISOLATED:
		{
			ptr = "Isolated";
			return ptr;			
		}
		case PROGRAM_SUSPENDED:
		{
			ptr = "Suspended";
			return ptr;			
		}
		case PROGRAM_SHUTDOWN:
		{
			ptr = "Shutdown";
			return ptr;			
		}
		case PROGRAM_DEAD:
		{
			ptr = "Dead";
			return ptr;			
		}
		case PROGRAM_FULL:
		{
			ptr = "Full";
			return ptr;			
		}
		
	}//program_status
}
static Octstr *httpd_version(List *cgivars, int status_type)
{
	
	struct utsname u;
	Octstr *version_format;
		
    uname(&u);
	

	
	return  	octstr_format(GW_NAME ":DBBOX Process %s.	"
								"%sProgram Status: %s		"
								"%sBuild: %s. 				"
								"%sCompiler: %s.			"
								"%sSystem: %s. 				"
								"%sRelease: %s.				"
								"%sVersion: %s.				"
								"%sMachine: %s.				"
								"%sHostname: %s.			"
								"%sIP: %s.					"
								"%sLibxml version %s.		"

#ifdef HAVE_LIBSSL
    	        "Using "
	#ifdef HAVE_WTLS_OPENSSL
             "WTLS library "
	#endif
             "%s."
#endif

#ifdef HAVE_MYSQL
             "%sMySQL: Server %s, Client %s."
#endif

#ifdef HAVE_SDB
             "%sLibSDB: %s."
#endif
#ifdef HAVE_SQLITE
             "%sSQLite: %s."
#endif
             
			 "%sMalloc : %s ",
			 GW_VERSION,
			 bb_status_linebreak(status_type),
			 dbbox_get_status_name(program_status),
			 bb_status_linebreak(status_type),
#ifdef __GNUC__ 
             (__DATE__ " " __TIME__),
			 bb_status_linebreak(status_type),
             __VERSION__,	 
#else 
             "unknown" , "unknown",
#endif 
			 
			 bb_status_linebreak(status_type),u.sysname,
			 bb_status_linebreak(status_type),u.release, 
			 bb_status_linebreak(status_type),u.version, 
			 bb_status_linebreak(status_type),u.machine,
			 bb_status_linebreak(status_type),octstr_get_cstr(get_official_name()),
			 bb_status_linebreak(status_type),octstr_get_cstr(get_official_ip()),
			 bb_status_linebreak(status_type),LIBXML_DOTTED_VERSION,
#ifdef HAVE_LIBSSL
             OPENSSL_VERSION_TEXT,
#endif

#ifdef HAVE_MYSQL
             bb_status_linebreak(status_type),MYSQL_SERVER_VERSION, mysql_get_client_info(),
#endif
#ifdef HAVE_SDB
             bb_status_linebreak(status_type),LIBSDB_VERSION,
#endif
#ifdef HAVE_SQLITE
             bb_status_linebreak(status_type),SQLITE_VERSION,
#endif
            bb_status_linebreak(status_type), octstr_get_cstr(gwmem_type())
			);
	
	
}
Octstr *httpd_shutdown(List *cgivars, int status_type)
{
	Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) 
		return reply;
	info(0,"SMPP-DBBOX  Received ADMIN SHUTDOWM command");
	dbbox_shutdown();
	return octstr_create("Bringing Dbbox Process down");	
}

Octstr *httpd_dbbox_status(List *cgivars, int status_type)
{
	return print_dbbox_status(status_type);
}



/* Known httpd commands and their functions */
static struct httpd_command {
    const char *command;
    Octstr * (*function)(List *cgivars, int status_type);
} httpd_commands[] = {
    //{ "status", httpd_status },
    //{ "store-status", httpd_store_status },
    //{ "log-level", httpd_loglevel },
    //{ "shutdown", httpd_shutdown },
    //{ "suspend", httpd_suspend },
    //{ "isolate", httpd_isolate },
    //{ "resume", httpd_resume },
    //{ "restart", httpd_restart },
    //{ "flush-dlr", httpd_flush_dlr },
	{ "version", httpd_version},
	{ "smpp_dbbox_shutdown", httpd_shutdown},	
	{ "smpp_dbbox_status", httpd_dbbox_status},	
    { NULL , NULL } /* terminate list */
};


static void httpd_serve(HTTPClient *client, Octstr *ourl, List *headers,
    	    	    	Octstr *body, List *cgivars)
{
    Octstr *reply, *final_reply, *url;
    char *content_type;
    char *header, *footer;
    int status_type;
    int i;
    long pos;

    reply = final_reply = NULL; /* for compiler please */
    url = octstr_duplicate(ourl);

    /* Set default reply format according to client
     * Accept: header */
    if (http_type_accepted(headers, "text/vnd.wap.wml")) 
	{
		status_type = BBSTATUS_WML;
		content_type = "text/vnd.wap.wml";
    }
    else if (http_type_accepted(headers, "text/html")) 
	{
		status_type = BBSTATUS_HTML;
		content_type = "text/html";
    }
    else if (http_type_accepted(headers, "text/xml")) 
	{
		status_type = BBSTATUS_XML;
		content_type = "text/xml";
    } 
	else 
	{
		status_type = BBSTATUS_TEXT;
		content_type = "text/plain";
    }

    /* kill '/cgi-bin' prefix */
    pos = octstr_search(url, octstr_imm("/cgi-bin/"), 0);
    if (pos != -1)
        octstr_delete(url, pos, 9);
    else if (octstr_get_char(url, 0) == '/')
        octstr_delete(url, 0, 1);

    /* look for type and kill it */
    pos = octstr_search_char(url, '.', 0);
    if (pos != -1) 
	{
        Octstr *tmp = octstr_copy(url, pos+1, octstr_len(url) - pos - 1);
        octstr_delete(url, pos, octstr_len(url) - pos);

        if (octstr_str_compare(tmp, "txt") == 0)
            status_type = BBSTATUS_TEXT;
        else if (octstr_str_compare(tmp, "html") == 0)
            status_type = BBSTATUS_HTML;
        else if (octstr_str_compare(tmp, "xml") == 0)
            status_type = BBSTATUS_XML;
        else if (octstr_str_compare(tmp, "wml") == 0)
            status_type = BBSTATUS_WML;

        octstr_destroy(tmp);
    }

    for (i=0; httpd_commands[i].command != NULL; i++) 
	{
        if (octstr_str_compare(url, httpd_commands[i].command) == 0) {
            reply = httpd_commands[i].function(cgivars, status_type);
            break;
        }
    }

    /* check if command found */
    if (httpd_commands[i].command == NULL) {
        char *lb = bb_status_linebreak(status_type);
	reply = octstr_format("Unknown command `%S'.%sPossible commands are:%s",
            ourl, lb, lb);
        for (i=0; httpd_commands[i].command != NULL; i++)
            octstr_format_append(reply, "%s%s", httpd_commands[i].command, lb);
    }

    gw_assert(reply != NULL);

    if (status_type == BBSTATUS_HTML) {
	header = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n"
 	    "<html>\n<title>" GW_NAME "</title>\n<body>\n<p>";
	footer = "</p>\n</body></html>\n";
	content_type = "text/html";
    } else if (status_type == BBSTATUS_WML) {
	header = "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" "
            "\"http://www.wapforum.org/DTD/wml_1.1.xml\">\n"
            "\n<wml>\n <card>\n  <p>";
	footer = "  </p>\n </card>\n</wml>\n";
	content_type = "text/vnd.wap.wml";
    } else if (status_type == BBSTATUS_XML) {
	header = "<?xml version=\"1.0\"?>\n"
            "<gateway>\n";
        footer = "</gateway>\n";
    } else {
	header = "";
	footer = "";
	content_type = "text/plain";
    }
    final_reply = octstr_create(header);
    octstr_append(final_reply, reply);
    octstr_append_cstr(final_reply, footer);
    
    /* debug("bb.http", 0, "Result: '%s'", octstr_get_cstr(final_reply));
     */
    http_destroy_headers(headers);
    headers = gwlist_create();
    http_header_add(headers, "Content-Type", content_type);

    http_send_reply(client, HTTP_OK, headers, final_reply);

    octstr_destroy(url);
    octstr_destroy(ourl);
    octstr_destroy(body);
    octstr_destroy(reply);
    octstr_destroy(final_reply);
    http_destroy_headers(headers);
    http_destroy_cgiargs(cgivars);
}



static void httpadmin_run(void *arg)
{
    HTTPClient *client;
    Octstr *ip, *url, *body;
    List *headers, *cgivars;

    while(program_status != PROGRAM_DEAD) 
	{
		if (program_status == PROGRAM_SHUTDOWN)
		{
	    	printf("\nCALLING SERVER SHUTDWON1\n");
			//shutdown_smpp_server();
			printf("\nCALLING SERVER SHUTDWON2\n");
    	}
		client = http_accept_request(ha_port, &ip, &url, &headers, &body, 
	    	    	    	     &cgivars);
		if (client == NULL)
	    	break;
		if (is_allowed_ip(ha_allow_ip, ha_deny_ip, ip) == 0) 
		{
	    	info(0, "HTTP admin tried from denied host <%s>, disconnected",octstr_get_cstr(ip));
	    	http_close_client(client);
	    	continue;
		}
        httpd_serve(client, url, headers, body, cgivars);
		octstr_destroy(ip);
    }

    httpadmin_running = 0;
}

int dbbox_httpadmin_start(Cfg *cfg)
{
    CfgGroup *grp;
    int ssl = 0; 
    
    if (httpadmin_running) 
	return -1;


    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    if (cfg_get_integer(&ha_port, grp, octstr_imm("admin-port")) == -1)
		panic(0, "Missing admin-port variable, cannot start HTTP admin");

    ha_interface = cfg_get(grp, octstr_imm("admin-interface"));
    ha_password = cfg_get(grp, octstr_imm("admin-password"));
    if (ha_password == NULL)
		panic(0, "You MUST set HTTP admin-password");
    
    ha_status_pw = cfg_get(grp, octstr_imm("status-password"));

    ha_allow_ip = cfg_get(grp, octstr_imm("admin-allow-ip"));
    ha_deny_ip = cfg_get(grp, octstr_imm("admin-deny-ip"));

    http_open_port_if(ha_port, ssl, ha_interface);

    if (gwthread_create(httpadmin_run, NULL) == -1)
		panic(0, "Failed to start a new thread for HTTP admin");

    httpadmin_running = 1;
    return 0;
}


void dbbox_httpadmin_stop(void)
{
    http_close_all_ports();
    gwthread_join_every(httpadmin_run);
    octstr_destroy(ha_interface);    
    octstr_destroy(ha_password);
    octstr_destroy(ha_status_pw);
    octstr_destroy(ha_allow_ip);
    octstr_destroy(ha_deny_ip);
    ha_password = NULL;
    ha_status_pw = NULL;
    ha_allow_ip = NULL;
    ha_deny_ip = NULL;
}

