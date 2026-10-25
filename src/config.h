#ifndef CONFIG_H
#define CONFIG_H
#include <stddef.h>
typedef struct {
    char host[64];  int port;
    char user[32];  char pass[32];
    char local_dir[128]; char remote_dir[128];
    int  timeout_ms;   /* 1000..60000 */
    int  notify;       /* 1 = system notifications */
} cfg_t;
void cfg_defaults(cfg_t *c);
int  cfg_load(cfg_t *c, const char *path);   /* 0 ok, -1 file missing/unreadable (defaults kept) */
int  cfg_save(const cfg_t *c, const char *path);  /* 0 ok, -1 error */
int  cfg_validate(const cfg_t *c, char *err, size_t errlen); /* 0 valid */
#endif
