/* Host (Linux) implementation of platform.h. Used only for tests/screenshots. */
#define _GNU_SOURCE
#include "platform.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

static fb_t g_fb;
uint32_t host_buttons = 0;           /* tests set this */
char     host_last_notify[256] = ""; /* tests read this */
int      host_flip_count = 0;
void   (*host_flip_hook)(void) = NULL;   /* tests: called on every flip */

int plat_init(void) {
    g_fb.w = SCREEN_W; g_fb.h = SCREEN_H;
    g_fb.px = calloc((size_t)g_fb.w * g_fb.h, 4);
    return g_fb.px ? 0 : -1;
}
void plat_shutdown(void) { free(g_fb.px); g_fb.px = NULL; }
fb_t *plat_backbuffer(void) { return &g_fb; }
void plat_flip(void) { host_flip_count++; if (host_flip_hook) host_flip_hook(); }
uint32_t plat_buttons(void) { return host_buttons; }
uint64_t plat_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
void plat_sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
void plat_notify(const char *msg) { snprintf(host_last_notify, sizeof host_last_notify, "%s", msg); fprintf(stderr, "[notify] %s\n", msg); }

const char *plat_config_path(void) { const char *p = getenv("DELUXE_CONFIG"); return p ? p : "./deluxe_config.ini"; }
const char *plat_default_local_dir(void) { const char *p = getenv("DELUXE_LOCAL"); return p ? p : "/tmp"; }

int net_init(void) { return 0; }

int net_connect(const char *ip, int port, int timeout_ms) {
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) return -1;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    int r = connect(s, (struct sockaddr *)&a, sizeof a);
    if (r < 0 && errno == EINPROGRESS) {
        struct pollfd p = { s, POLLOUT, 0 };
        if (poll(&p, 1, timeout_ms) <= 0) { close(s); return -1; }
        int e = 0; socklen_t l = sizeof e; getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &l);
        if (e) { close(s); return -1; }
    } else if (r < 0) { close(s); return -1; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
    return s;
}
int net_send_all(int h, const void *buf, size_t len) {
    const char *p = buf;
    while (len) { ssize_t n = send(h, p, len, MSG_NOSIGNAL); if (n <= 0) return -1; p += n; len -= (size_t)n; }
    return 0;
}
int net_recv(int h, void *buf, size_t len, int timeout_ms) {
    struct pollfd p = { h, POLLIN, 0 };
    int pr = poll(&p, 1, timeout_ms);
    if (pr == 0) return NET_TIMEOUT;
    if (pr < 0) return NET_ERR;
    ssize_t n = recv(h, buf, len, 0);
    return n < 0 ? NET_ERR : (int)n;
}
void net_close(int h) { close(h); }
