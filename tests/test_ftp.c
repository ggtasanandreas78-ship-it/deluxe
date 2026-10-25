/* Real FTP client tests against tests/mock_ftp.py. Env: FTP_PORT SILENT_PORT LOCAL_DIR */
#include "ftp.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) { passes++; printf("PASS  "); } else { fails++; printf("FAIL  "); } printf(__VA_ARGS__); printf("\n"); } while (0)

typedef struct { uint64_t last; int monotonic; uint64_t final, total; int calls; uint64_t cancel_after; } prog_t;
static int cb(uint64_t done, uint64_t total, void *ud) {
    prog_t *p = ud; p->calls++;
    if (done < p->last) p->monotonic = 0;
    p->last = done; p->final = done; p->total = total;
    return p->cancel_after && done >= p->cancel_after;
}
static int exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

int main(void) {
    int port = atoi(getenv("FTP_PORT")), silent = atoi(getenv("SILENT_PORT"));
    const char *L = getenv("LOCAL_DIR"); char path[512];
    plat_init();

    /* ---- pure parsers ---- */
    ftp_entry_t e;
    CHECK(ftp_parse_list_line("drwxr-xr-x 2 ps4 ps4 4096 Jan 01 12:00 games", &e) && e.is_dir && !strcmp(e.name, "games"), "LIST parse: directory");
    CHECK(ftp_parse_list_line("-rw-r--r-- 1 ps4 ps4 123456 Mar  3  2024 my file.pkg", &e) && !e.is_dir && e.size == 123456 && !strcmp(e.name, "my file.pkg"), "LIST parse: file with spaces + size");
    CHECK(ftp_parse_list_line("lrwxrwxrwx 1 a b 7 Jan 01 12:00 link -> /data", &e) && !strcmp(e.name, "link"), "LIST parse: symlink target stripped");
    CHECK(!ftp_parse_list_line("total 12", &e), "LIST parse: 'total' line ignored");
    CHECK(!ftp_parse_list_line("drwxr-xr-x 2 a b 4096 Jan 01 12:00 ..", &e), "LIST parse: '..' ignored");
    char ip[32]; int pp;
    CHECK(ftp_parse_pasv("Entering Passive Mode (10,0,0,5,19,136)", ip, sizeof ip, &pp) == 0 && !strcmp(ip, "10.0.0.5") && pp == 5000, "PASV parse");
    CHECK(ftp_parse_pasv("Entering Passive Mode (10,0,0,5,19,999)", ip, sizeof ip, &pp) != 0, "PASV parse rejects out-of-range");
    CHECK(ftp_parse_pasv("garbage", ip, sizeof ip, &pp) != 0, "PASV parse rejects garbage");

    CHECK(net_valid_ipv4("192.168.1.10") && net_valid_ipv4("0.0.0.0") && net_valid_ipv4("255.255.255.255"), "ipv4 validator accepts valid");
    CHECK(!net_valid_ipv4("256.1.1.1") && !net_valid_ipv4("1.2.3") && !net_valid_ipv4("1.2.3.4.5") && !net_valid_ipv4("a.b.c.d") && !net_valid_ipv4("1.2.3.") && !net_valid_ipv4("") && !net_valid_ipv4("1.2.3.4x"), "ipv4 validator rejects invalid");

    /* ---- connection failures ---- */
    ftp_t f;
    CHECK(ftp_connect(&f, "not-an-ip", port, "ps4", "secret", 2000) == FTP_EARG, "reject invalid IPv4 -> '%s'", f.err);
    CHECK(ftp_connect(&f, "127.0.0.1", 1, "ps4", "secret", 2000) == FTP_ECONN, "connection refused -> '%s'", f.err);
    CHECK(ftp_connect(&f, "127.0.0.1", silent, "ps4", "secret", 1000) == FTP_ETIMEOUT, "silent server times out -> '%s'", f.err);
    CHECK(ftp_connect(&f, "127.0.0.1", port, "ps4", "WRONG", 2000) == FTP_EAUTH, "wrong password -> '%s'", f.err);
    CHECK(!ftp_connected(&f), "failed login leaves no open socket");

    /* ---- session ---- */
    int rc = ftp_connect(&f, "127.0.0.1", port, "ps4", "secret", 3000);
    CHECK(rc == FTP_OK, "login ok");
    if (rc) return 1;
    char pwd[128];
    CHECK(ftp_pwd(&f, pwd, sizeof pwd) == FTP_OK && !strcmp(pwd, "/"), "PWD = %s", pwd);
    ftp_entry_t *l; int n;
    CHECK(ftp_list(&f, "/", &l, &n) == FTP_OK && n >= 3, "LIST / -> %d entries", n);
    CHECK(n >= 1 && l[0].is_dir, "directories sorted first (first='%s')", l[0].name);
    int found = 0; for (int i = 0; i < n; i++) if (!strcmp(l[i].name, "big.bin") && l[i].size == 5u * 1024 * 1024) found = 1;
    CHECK(found, "big.bin listed with correct size"); free(l);
    CHECK(ftp_list(&f, "/nope", &l, &n) == FTP_EREMOTE, "LIST missing dir -> '%s'", f.err);
    CHECK(ftp_cwd(&f, "/nope") == FTP_EREMOTE, "CWD missing dir -> '%s'", f.err);
    CHECK(ftp_connected(&f), "connection survives remote errors");

    /* download */
    prog_t p = { 0, 1, 0, 0, 0, 0 };
    snprintf(path, sizeof path, "%s/down_big.bin", L);
    CHECK(ftp_download(&f, "/big.bin", path, cb, &p) == FTP_OK, "download big.bin (5 MiB) -> %s", f.err[0] ? f.err : "ok");
    CHECK(p.monotonic && p.final == p.total && p.total == 5u * 1024 * 1024 && p.calls > 5, "progress monotonic, final==total (%llu), %d callbacks", (unsigned long long)p.final, p.calls);

    /* upload */
    p = (prog_t){ 0, 1, 0, 0, 0, 0 };
    snprintf(path, sizeof path, "%s/up_src.bin", L);
    CHECK(ftp_upload(&f, path, "/uploaded.bin", cb, &p) == FTP_OK, "upload 3 MiB -> %s", f.err[0] ? f.err : "ok");
    CHECK(p.monotonic && p.final == p.total && p.total == 3u * 1024 * 1024, "upload progress final==total (%llu)", (unsigned long long)p.final);

    /* errors */
    snprintf(path, sizeof path, "%s/down_missing.bin", L);
    CHECK(ftp_download(&f, "/missing.bin", path, NULL, NULL) == FTP_EREMOTE && !exists(path), "download missing -> '%s' (no stray local file)", f.err);
    snprintf(path, sizeof path, "%s/up_src.bin", L);
    CHECK(ftp_upload(&f, path, "/readonly/x.bin", NULL, NULL) == FTP_EREMOTE, "upload to read-only dir -> '%s'", f.err);
    CHECK(ftp_upload(&f, "/definitely/not/here", "/x.bin", NULL, NULL) == FTP_EIO, "upload of missing local file -> '%s'", f.err);
    snprintf(path, sizeof path, "%s/no_such_dir/x.bin", L);
    CHECK(ftp_download(&f, "/big.bin", path, NULL, NULL) == FTP_EIO, "download to unwritable local path -> '%s'", f.err);
    snprintf(path, sizeof path, "%s/down_drop.bin", L);
    CHECK(ftp_download(&f, "/dropme.bin", path, NULL, NULL) == FTP_EPROTO && !exists(path), "truncated transfer detected -> '%s'", f.err);

    /* cancel */
    p = (prog_t){ 0, 1, 0, 0, 0, 100 * 1024 };
    snprintf(path, sizeof path, "%s/down_cancel.bin", L);
    CHECK(ftp_download(&f, "/big.bin", path, cb, &p) == FTP_ECANCEL && !exists(path), "cancel mid-download -> partial file removed");
    CHECK(ftp_noop(&f) == FTP_OK, "control connection still usable after cancel");

    /* connection loss */
    CHECK(ftp_cwd(&f, "/killme") == FTP_ECONN, "server drop detected -> '%s'", f.err);
    CHECK(!ftp_connected(&f), "status flips to disconnected");
    CHECK(ftp_noop(&f) == FTP_ECONN, "commands after drop fail cleanly -> '%s'", f.err);

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
