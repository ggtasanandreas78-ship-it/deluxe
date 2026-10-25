#ifndef LOCALFS_H
#define LOCALFS_H
#include "ftp.h"
/* List a local directory into malloc'ed entries (dirs first, sorted). 0 ok, -1 error (msg in err). */
int localfs_list(const char *path, ftp_entry_t **out, int *count, char *err, size_t errlen);
#endif
