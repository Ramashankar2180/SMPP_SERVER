#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include <sys/utsname.h>

Octstr *version_report_string(const char *boxname);
void report_versions(const char *boxname);


