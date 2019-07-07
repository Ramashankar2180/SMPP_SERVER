#include "version.h"


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

Octstr *version_report_string(const char *boxname)
{
	struct utsname u;

    uname(&u);
    return octstr_format(GW_NAME " %s version `%s'.\nBuild `%s', compiler `%s'.\n"
    	    	    	 "System %s, release %s, version %s, machine %s.\n"
			 "Hostname %s, IP %s.\n"
			 "Libxml version %s.\n"
#ifdef HAVE_LIBSSL
             "Using "
#ifdef HAVE_WTLS_OPENSSL
             "WTLS library "
#endif
             "%s.\n"
#endif
#ifdef HAVE_MYSQL
             "Compiled with MySQL %s, using MySQL %s.\n"
#endif
#ifdef HAVE_SDB
             "Using LibSDB %s.\n"
#endif
#ifdef HAVE_SQLITE
             "Using SQLite %s.\n"
#endif
             "Using %s malloc.\n",
			 boxname, GW_VERSION,
#ifdef __GNUC__ 
             (__DATE__ " " __TIME__) ,
             __VERSION__,
#else 
             "unknown" , "unknown",
#endif 
			 u.sysname, u.release, u.version, u.machine,
			 octstr_get_cstr(get_official_name()),
			 octstr_get_cstr(get_official_ip()),
			 LIBXML_DOTTED_VERSION,
#ifdef HAVE_LIBSSL
             OPENSSL_VERSION_TEXT,
#endif
#ifdef HAVE_MYSQL
             MYSQL_SERVER_VERSION, mysql_get_client_info(),
#endif
#ifdef HAVE_SDB
             LIBSDB_VERSION,
#endif
#ifdef HAVE_SQLITE
             SQLITE_VERSION,
#endif
             octstr_get_cstr(gwmem_type()));
}
void report_versions(const char *boxname)
{
	 Octstr *os;
    
    os = version_report_string(boxname);
    debug("gwlib.gwlib", 0, "%s", octstr_get_cstr(os));
    octstr_destroy(os);
}


