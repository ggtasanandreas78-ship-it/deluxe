#include "config.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void cfg_defaults(cfg_t *c) {
    memset(c, 0, sizeof *c);
    snprintf(c->host, sizeof c->host, "192.168.1.10");
    c->port = 2121;
    snprintf(c->user, sizeof c->user, "anonymous");
    snprintf(c->pass, sizeof c->pass, "anonymous");
    snprintf(c->local_dir, sizeof c->local_dir, "%s", plat_default_local_dir());
    snprintf(c->remote_dir, sizeof c->remote_dir, "/");
    c->timeout_ms = 10000;
    c->notify = 1;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

int cfg_load(cfg_t *c, const char *path) {
    cfg_defaults(c);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[320];
    while (fgets(line, sizeof line, f)) {
        trim(line);
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq) continue;
        *eq = 0; const char *k = line, *v = eq + 1;
        if (!strcmp(k, "host")) snprintf(c->host, sizeof c->host, "%s", v);
        else if (!strcmp(k, "port")) c->port = atoi(v);
        else if (!strcmp(k, "user")) snprintf(c->user, sizeof c->user, "%s", v);
        else if (!strcmp(k, "pass")) snprintf(c->pass, sizeof c->pass, "%s", v);
        else if (!strcmp(k, "local_dir")) snprintf(c->local_dir, sizeof c->local_dir, "%s", v);
        else if (!strcmp(k, "remote_dir")) snprintf(c->remote_dir, sizeof c->remote_dir, "%s", v);
        else if (!strcmp(k, "timeout_ms")) c->timeout_ms = atoi(v);
        else if (!strcmp(k, "notify")) c->notify = atoi(v) ? 1 : 0;
    }
    fclose(f);
    return 0;
}

int cfg_save(const cfg_t *c, const char *path) {
    char dir[256]; snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl && sl != dir) { *sl = 0; mkdir(dir, 0777); }   /* single level; ignore EEXIST */
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "# DELUXE PKG MANAGER settings\nhost=%s\nport=%d\nuser=%s\npass=%s\nlocal_dir=%s\nremote_dir=%s\ntimeout_ms=%d\nnotify=%d\n",
                     c->host, c->port, c->user, c->pass, c->local_dir, c->remote_dir, c->timeout_ms, c->notify) > 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

int cfg_validate(const cfg_t *c, char *err, size_t n) {
    if (!net_valid_ipv4(c->host)) { snprintf(err, n, "Host must be an IPv4 address like 192.168.1.10"); return -1; }
    if (c->port < 1 || c->port > 65535) { snprintf(err, n, "Port must be 1-65535"); return -1; }
    if (c->timeout_ms < 1000 || c->timeout_ms > 60000) { snprintf(err, n, "Timeout must be 1-60 seconds"); return -1; }
    if (c->local_dir[0] != '/') { snprintf(err, n, "Local folder must be an absolute path"); return -1; }
    if (c->remote_dir[0] != '/') { snprintf(err, n, "Remote folder must start with /"); return -1; }
    return 0;
}
