#include "ftp.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define CHUNK (64 * 1024)

static void set_err(ftp_t *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(f->err, sizeof f->err, fmt, ap);
    va_end(ap);
}

void ftp_init(ftp_t *f) { memset(f, 0, sizeof *f); f->ctrl = -1; }
int  ftp_connected(const ftp_t *f) { return f->ctrl >= 0; }

/* ---- control channel ---- */
static int rl(ftp_t *f, char *line, size_t max) {
    size_t n = 0;
    for (;;) {
        if (f->rpos >= f->rlen) {
            int r = net_recv(f->ctrl, f->rbuf, sizeof f->rbuf, f->timeout_ms);
            if (r == NET_TIMEOUT) { set_err(f, "Timed out waiting for server reply"); return FTP_ETIMEOUT; }
            if (r <= 0) { set_err(f, "Connection closed by server"); net_close(f->ctrl); f->ctrl = -1; return FTP_ECONN; }
            f->rlen = r; f->rpos = 0;
        }
        char c = f->rbuf[f->rpos++];
        if (c == '\n') break;
        if (c != '\r' && n + 1 < max) line[n++] = c;
    }
    line[n] = 0;
    return FTP_OK;
}

static int read_reply(ftp_t *f) {
    char line[512], first[4] = {0};
    int rc = rl(f, line, sizeof line);
    if (rc) return rc;
    if (strlen(line) < 3 || !isdigit((unsigned char)line[0]) || !isdigit((unsigned char)line[1]) || !isdigit((unsigned char)line[2])) {
        set_err(f, "Malformed server reply: %.60s", line); return FTP_EPROTO;
    }
    memcpy(first, line, 3);
    if (line[3] == '-') {                     /* multi-line reply */
        for (;;) {
            rc = rl(f, line, sizeof line);
            if (rc) return rc;
            if (!strncmp(line, first, 3) && line[3] == ' ') break;
        }
    }
    f->code = atoi(first);
    snprintf(f->reply, sizeof f->reply, "%s", line[3] ? line + 4 : "");
    return FTP_OK;
}

static int send_cmd(ftp_t *f, const char *fmt, ...) {
    char buf[1100]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf - 2, fmt, ap); va_end(ap);
    if (n < 0 || n >= (int)sizeof buf - 2) { set_err(f, "Command too long"); return FTP_EARG; }
    if (strchr(buf, '\r') || strchr(buf, '\n')) { set_err(f, "Illegal character in command"); return FTP_EARG; }
    buf[n++] = '\r'; buf[n++] = '\n';
    if (f->ctrl < 0) { set_err(f, "Not connected"); return FTP_ECONN; }
    if (net_send_all(f->ctrl, buf, (size_t)n)) { set_err(f, "Failed to send command (connection lost)"); return FTP_ECONN; }
    return read_reply(f);
}

/* Map an unexpected reply to an error code + message. */
static int reply_error(ftp_t *f, const char *what) {
    set_err(f, "%s failed: %d %s", what, f->code, f->reply);
    if (f->code == 530 || f->code == 331 || f->code == 332) return FTP_EAUTH;
    return FTP_EREMOTE;
}

/* ---- connect / quit ---- */
int ftp_connect(ftp_t *f, const char *host, int port, const char *user, const char *pass, int timeout_ms) {
    ftp_init(f);
    f->timeout_ms = timeout_ms;
    snprintf(f->host, sizeof f->host, "%s", host);
    if (!net_valid_ipv4(host)) { set_err(f, "Invalid IPv4 address: %.40s", host); return FTP_EARG; }
    f->ctrl = net_connect(host, port, timeout_ms);
    if (f->ctrl < 0) { set_err(f, "Cannot connect to %s:%d (refused, unreachable or timed out)", host, port); return FTP_ECONN; }
    int rc = read_reply(f);
    if (rc) { ftp_quit(f); return rc; }
    if (f->code != 220) { rc = reply_error(f, "Server greeting"); ftp_quit(f); return rc; }
    rc = send_cmd(f, "USER %s", user[0] ? user : "anonymous");
    if (rc) { ftp_quit(f); return rc; }
    if (f->code == 331 || f->code == 332) {
        rc = send_cmd(f, "PASS %s", pass);
        if (rc) { ftp_quit(f); return rc; }
    }
    if (f->code != 230) { set_err(f, "Login rejected: %d %s", f->code, f->reply); ftp_quit(f); return FTP_EAUTH; }
    rc = send_cmd(f, "TYPE I");
    if (rc) { ftp_quit(f); return rc; }
    if (f->code != 200) { rc = reply_error(f, "TYPE I"); ftp_quit(f); return rc; }
    return FTP_OK;
}

void ftp_quit(ftp_t *f) {
    if (f->ctrl >= 0) {
        char b[] = "QUIT\r\n"; net_send_all(f->ctrl, b, 6);
        net_close(f->ctrl); f->ctrl = -1;
    }
}

/* ---- simple commands ---- */
int ftp_pwd(ftp_t *f, char *out, size_t outlen) {
    int rc = send_cmd(f, "PWD"); if (rc) return rc;
    if (f->code != 257) return reply_error(f, "PWD");
    const char *q = strchr(f->reply, '"'); const char *e = q ? strchr(q + 1, '"') : NULL;
    if (!q || !e) { set_err(f, "Unparseable PWD reply"); return FTP_EPROTO; }
    size_t n = (size_t)(e - q - 1); if (n >= outlen) n = outlen - 1;
    memcpy(out, q + 1, n); out[n] = 0;
    return FTP_OK;
}
int ftp_cwd(ftp_t *f, const char *path) {
    int rc = send_cmd(f, "CWD %s", path); if (rc) return rc;
    if (f->code / 100 != 2) return reply_error(f, "Change directory");
    return FTP_OK;
}
int ftp_noop(ftp_t *f) {
    int rc = send_cmd(f, "NOOP"); if (rc) return rc;
    if (f->code / 100 != 2) return reply_error(f, "NOOP");
    return FTP_OK;
}
int ftp_mkd(ftp_t *f, const char *path) {
    int rc = send_cmd(f, "MKD %s", path); if (rc) return rc;
    if (f->code / 100 != 2) return reply_error(f, "Create directory");
    return FTP_OK;
}
int ftp_size(ftp_t *f, const char *path, uint64_t *size) {
    int rc = send_cmd(f, "SIZE %s", path); if (rc) return rc;
    if (f->code != 213) return reply_error(f, "SIZE");
    *size = strtoull(f->reply, NULL, 10);
    return FTP_OK;
}

/* ---- passive data connection ---- */
int ftp_parse_pasv(const char *reply, char *ip, size_t iplen, int *port) {
    const char *p = strchr(reply, '(');
    p = p ? p + 1 : reply;
    while (*p && !isdigit((unsigned char)*p)) p++;
    int a, b, c, d, p1, p2;
    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &p1, &p2) != 6) return -1;
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255 ||
        p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255) return -1;
    snprintf(ip, iplen, "%d.%d.%d.%d", a, b, c, d);
    *port = p1 * 256 + p2;
    return 0;
}

static int open_data(ftp_t *f, int *dh) {
    int rc = send_cmd(f, "PASV"); if (rc) return rc;
    if (f->code != 227) return reply_error(f, "PASV");
    char ip[32]; int port;
    if (ftp_parse_pasv(f->reply, ip, sizeof ip, &port) || port == 0) { set_err(f, "Unparseable PASV reply: %.80s", f->reply); return FTP_EPROTO; }
    /* Connect to the control host (NAT/private-address safe) rather than the advertised IP. */
    *dh = net_connect(f->host, port, f->timeout_ms);
    if (*dh < 0) { set_err(f, "Data connection to %s:%d failed", f->host, port); return FTP_ECONN; }
    return FTP_OK;
}

/* ---- LIST ---- */
int ftp_parse_list_line(const char *line, ftp_entry_t *e) {
    /* Unix format: perms links owner group size Mon DD HH:MM|YYYY name */
    if (!line[0] || !strchr("-dlbcps", line[0]) || strlen(line) < 10) return 0;
    const char *p = line; int field = 0; uint64_t size = 0;
    while (field < 8) {
        while (*p == ' ') p++;
        if (!*p) return 0;
        const char *s = p;
        while (*p && *p != ' ') p++;
        if (field == 4) size = strtoull(s, NULL, 10);
        field++;
    }
    while (*p == ' ') p++;
    if (!*p) return 0;
    snprintf(e->name, sizeof e->name, "%s", p);
    char *arrow = strstr(e->name, " -> ");               /* symlink */
    if (arrow) *arrow = 0;
    if (!strcmp(e->name, ".") || !strcmp(e->name, "..")) return 0;
    e->is_dir = (line[0] == 'd' || line[0] == 'l');
    e->size = size;
    return 1;
}

static int entcmp(const void *a, const void *b) {
    const ftp_entry_t *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return strcmp(x->name, y->name);
}

int ftp_list(ftp_t *f, const char *path, ftp_entry_t **out, int *count) {
    int dh, rc = open_data(f, &dh); if (rc) return rc;
    rc = (path && path[0]) ? send_cmd(f, "LIST %s", path) : send_cmd(f, "LIST");
    if (rc) { net_close(dh); return rc; }
    if (f->code / 100 != 1) { net_close(dh); return reply_error(f, "LIST"); }
    size_t cap = 4096, len = 0; char *buf = malloc(cap);
    if (!buf) { net_close(dh); set_err(f, "Out of memory"); return FTP_EIO; }
    for (;;) {
        if (len + 2048 > cap) { cap *= 2; char *nb = realloc(buf, cap); if (!nb) { free(buf); net_close(dh); set_err(f, "Out of memory"); return FTP_EIO; } buf = nb; }
        int r = net_recv(dh, buf + len, cap - len - 1, f->timeout_ms);
        if (r == 0) break;
        if (r < 0) { free(buf); net_close(dh); set_err(f, "Directory listing %s", r == NET_TIMEOUT ? "timed out" : "connection error"); return r == NET_TIMEOUT ? FTP_ETIMEOUT : FTP_ECONN; }
        len += (size_t)r;
        if (len > 8u * 1024 * 1024) { free(buf); net_close(dh); set_err(f, "Directory listing too large"); return FTP_EPROTO; }
    }
    net_close(dh); buf[len] = 0;
    rc = read_reply(f);
    if (rc) { free(buf); return rc; }
    if (f->code / 100 != 2) { free(buf); return reply_error(f, "LIST"); }
    int n = 0, ecap = 64; ftp_entry_t *ents = malloc((size_t)ecap * sizeof *ents);
    for (char *ln = strtok(buf, "\n"); ln && ents; ln = strtok(NULL, "\n")) {
        size_t l = strlen(ln); if (l && ln[l - 1] == '\r') ln[l - 1] = 0;
        ftp_entry_t e;
        if (!ftp_parse_list_line(ln, &e)) continue;
        if (n == ecap) { ecap *= 2; ftp_entry_t *ne = realloc(ents, (size_t)ecap * sizeof *ents); if (!ne) break; ents = ne; }
        ents[n++] = e;
    }
    free(buf);
    if (n > 1) qsort(ents, (size_t)n, sizeof *ents, entcmp);
    *out = ents; *count = n;
    return ents ? FTP_OK : FTP_EIO;
}

/* ---- RETR ---- */
int ftp_download(ftp_t *f, const char *remote, const char *local, ftp_progress_cb cb, void *ud) {
    uint64_t total = 0;
    if (ftp_size(f, remote, &total) != FTP_OK) { if (f->ctrl < 0) return FTP_ECONN; total = 0; }
    FILE *fp = fopen(local, "wb");
    if (!fp) { set_err(f, "Cannot create local file %s (read-only or no permission)", local); return FTP_EIO; }
    int dh, rc = open_data(f, &dh);
    if (rc) { fclose(fp); remove(local); return rc; }
    rc = send_cmd(f, "RETR %s", remote);
    if (rc || f->code / 100 != 1) {
        int r = rc ? rc : reply_error(f, "RETR");
        net_close(dh); fclose(fp); remove(local); return r;
    }
    char *buf = malloc(CHUNK);
    if (!buf) { net_close(dh); fclose(fp); remove(local); set_err(f, "Out of memory"); return FTP_EIO; }
    uint64_t done = 0; rc = FTP_OK;
    for (;;) {
        int r = net_recv(dh, buf, CHUNK, f->timeout_ms);
        if (r == 0) break;
        if (r < 0) { rc = r == NET_TIMEOUT ? FTP_ETIMEOUT : FTP_ECONN; set_err(f, "Download %s", r == NET_TIMEOUT ? "timed out" : "connection lost"); break; }
        if (fwrite(buf, 1, (size_t)r, fp) != (size_t)r) { rc = FTP_EIO; set_err(f, "Local write failed (disk full?)"); break; }
        done += (uint64_t)r;
        if (cb && cb(done, total, ud)) { rc = FTP_ECANCEL; set_err(f, "Cancelled by user"); break; }
    }
    free(buf); net_close(dh);
    if (fclose(fp) != 0 && rc == FTP_OK) { rc = FTP_EIO; set_err(f, "Local close failed"); }
    if (rc == FTP_OK) {
        int r2 = read_reply(f);
        if (r2) rc = r2;
        else if (f->code / 100 != 2) rc = reply_error(f, "Transfer");
        else if (total && done != total) { rc = FTP_EPROTO; set_err(f, "Size mismatch: got %llu of %llu bytes", (unsigned long long)done, (unsigned long long)total); }
    } else if (f->ctrl >= 0) {
        read_reply(f);   /* drain the 426/451 that follows an aborted transfer */
    }
    if (rc != FTP_OK) remove(local);
    return rc;
}

/* ---- STOR ---- */
int ftp_upload(ftp_t *f, const char *local, const char *remote, ftp_progress_cb cb, void *ud) {
    FILE *fp = fopen(local, "rb");
    if (!fp) { set_err(f, "Cannot open local file %s", local); return FTP_EIO; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint64_t total = sz < 0 ? 0 : (uint64_t)sz;
    int dh, rc = open_data(f, &dh);
    if (rc) { fclose(fp); return rc; }
    rc = send_cmd(f, "STOR %s", remote);
    if (rc || f->code / 100 != 1) {
        int r = rc ? rc : reply_error(f, "STOR");
        net_close(dh); fclose(fp); return r;
    }
    char *buf = malloc(CHUNK);
    if (!buf) { net_close(dh); fclose(fp); set_err(f, "Out of memory"); return FTP_EIO; }
    uint64_t done = 0; rc = FTP_OK;
    for (;;) {
        size_t n = fread(buf, 1, CHUNK, fp);
        if (n == 0) { if (ferror(fp)) { rc = FTP_EIO; set_err(f, "Local read failed"); } break; }
        if (net_send_all(dh, buf, n)) { rc = FTP_ECONN; set_err(f, "Upload connection lost (remote disk full or no write permission?)"); break; }
        done += n;
        if (cb && cb(done, total, ud)) { rc = FTP_ECANCEL; set_err(f, "Cancelled by user"); break; }
    }
    free(buf); fclose(fp); net_close(dh);
    if (rc == FTP_OK) {
        int r2 = read_reply(f);
        if (r2) rc = r2;
        else if (f->code / 100 != 2) rc = reply_error(f, "Upload");
    } else if (f->ctrl >= 0) {
        read_reply(f);
    }
    return rc;
}
