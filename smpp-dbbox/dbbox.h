
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include<sys/msg.h>

#include "gwlib/gwlib.h"


int dbbox_httpadmin_start(Cfg *config);
void dbbox_httpadmin_stop(void);


enum 
{
    PROGRAM_RUNNING = 0,
    PROGRAM_ISOLATED = 1,	/* do not receive new messgaes from UDP/SMSC */
    PROGRAM_SUSPENDED = 2,	/* do not transfer any messages */
    PROGRAM_SHUTDOWN = 3,
    PROGRAM_DEAD = 4,
    PROGRAM_FULL = 5         /* message queue too long, do not accept new messages */
};
