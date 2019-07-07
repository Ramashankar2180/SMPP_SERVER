
#ifndef SERVER_HTTP_H
#define SERVER_HTTP_H 

#include "gwlib/gwlib.h"
#include "server.h"
#include <signal.h>

int httpadmin_start(Cfg *config);
void httpadmin_stop(void);

char* get_server_status_name(sig_atomic_t program_status);

/*HTTP Admin related functions*/
Octstr *print_status_connected_esme(int status_type);/*in esme.c*/
Octstr *program_print_status(int status_type);/*in config.c*/
Octstr *server_print_status_configured_esme(int status_type);/*in server.c*/

Octstr *print_server_status(int status_type);/*in server.c - server status command*/
void smpp_server_shutdown();/*in server.c - server shutdown command*/
Octstr *print_esme_status(int status_type);/*in esme.c - print esme parameters*/
int emse_stop(Octstr *esme);/*in esme.c - sends unbind to the esme*/
#endif
