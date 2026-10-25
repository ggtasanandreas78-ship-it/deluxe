#ifndef FTP_H
#define FTP_H
#include <stdint.h>
#include <stddef.h>

enum { FTP_OK = 0, FTP_ECONN = -1, FTP_EAUTH = -2, FTP_EPROTO = -3, FTP_EIO = -4,
       FTP_ECANCEL = -5, FTP_ETIMEOUT = -6, FTP_EREMOTE = -7, FTP_EARG = -8 };

typedef struct {
    int  ctrl;                 /* control socket handle, -1 if closed */
    char host[64];
    int  timeout_ms;
    char err[256];             /* human readable description of last error */
    char reply[512];           /* text of last server reply line */
    int  code;                 /* numeric code of last server reply */
    char rbuf[1024]; int rlen, rpos;
} ftp_t;

typedef struct { char name[256]; int is_dir; uint64_t size; } ftp_entry_t;

/* Return non-zero from the callback to cancel the transfer. */
typedef int (*ftp_progress_cb)(uint64_t done, uint64_t total, void *ud);

void ftp_init(ftp_t *f);
int  ftp_connect(ftp_t *f, const char *host, int port, const char *user,
                 const char *pass, int timeout_ms);
int  ftp_connected(const ftp_t *f);
int  ftp_pwd(ftp_t *f, char *out, size_t outlen);
int  ftp_cwd(ftp_t *f, const char *path);
int  ftp_noop(ftp_t *f);   /* keepalive / liveness probe */
int  ftp_mkd(ftp_t *f, const char *path);
int  ftp_size(ftp_t *f, const char *path, uint64_t *size);
/* *out is malloc'ed (free() it); entries sorted dirs-first then by name. */
int  ftp_list(ftp_t *f, const char *path, ftp_entry_t **out, int *count);
int  ftp_download(ftp_t *f, const char *remote, const char *local,
                  ftp_progress_cb cb, void *ud);
int  ftp_upload(ftp_t *f, const char *local, const char *remote,
                ftp_progress_cb cb, void *ud);
void ftp_quit(ftp_t *f);

/* exposed for unit testing */
int  ftp_parse_list_line(const char *line, ftp_entry_t *e);
int  ftp_parse_pasv(const char *reply, char *ip, size_t iplen, int *port);
#endif
