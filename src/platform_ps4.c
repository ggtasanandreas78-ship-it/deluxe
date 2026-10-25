/* PS4 (OpenOrbis) implementation of platform.h.
 *
 * !!! NOT COMPILED OR RUN YET !!!  Written against the OpenOrbis headers from memory of the
 * public samples. Expect the first CI run to reveal header/signature mismatches to fix, and
 * on-console testing to reveal runtime issues. Everything above this layer (ftp/ui/gfx/config)
 * is tested on the host; this file is the only part that touches Sony APIs. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Net.h>

#define NUM_FB 2
static int video = -1, pad = -1, cur = 0, net_pool = -1;
static OrbisKernelEqueue flip_eq;
static void *fb_mem[NUM_FB];
static fb_t back;
static OrbisVideoOutBufferAttribute attr;

int plat_init(void) {
    int uid;
    sceUserServiceInitialize(NULL);
    sceUserServiceGetInitialUser(&uid);

    video = sceVideoOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    if (video < 0) return -1;
    size_t sz = (size_t)SCREEN_W * SCREEN_H * 4, aligned = (sz + 0x1fffff) & ~(size_t)0x1fffff;
    off_t phys; void *base = NULL;
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), aligned * NUM_FB, 0x200000, 3, &phys)) return -2;
    if (sceKernelMapDirectMemory(&base, aligned * NUM_FB, 0x33, 0, phys, 0x200000)) return -3;
    for (int i = 0; i < NUM_FB; i++) fb_mem[i] = (char *)base + aligned * (size_t)i;

    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,
                                  SCREEN_W, SCREEN_H, SCREEN_W);
    if (sceVideoOutRegisterBuffers(video, 0, fb_mem, NUM_FB, &attr)) return -4;
    sceKernelCreateEqueue(&flip_eq, "deluxe-flip");
    sceVideoOutAddFlipEvent(flip_eq, video, NULL);

    back.w = SCREEN_W; back.h = SCREEN_H;
    back.px = calloc((size_t)SCREEN_W * SCREEN_H, 4);
    if (!back.px) return -5;

    scePadInit();
    pad = scePadOpen(uid, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    return 0;
}

void plat_shutdown(void) {
    if (video >= 0) sceVideoOutClose(video);
    free(back.px); back.px = NULL;
}

fb_t *plat_backbuffer(void) { return &back; }

void plat_flip(void) {
    uint32_t *dst = fb_mem[cur];
    for (size_t i = 0, n = (size_t)SCREEN_W * SCREEN_H; i < n; i++) dst[i] = back.px[i] | 0xFF000000u;
    sceVideoOutSubmitFlip(video, cur, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    OrbisKernelEvent ev; int cnt; OrbisKernelUseconds to = 100000;
    sceKernelWaitEqueue(flip_eq, &ev, 1, &cnt, &to);
    cur = (cur + 1) % NUM_FB;
}

uint32_t plat_buttons(void) {
    OrbisPadData d; uint32_t b = 0;
    if (pad < 0 || scePadReadState(pad, &d) != 0) return 0;
    if (d.buttons & ORBIS_PAD_BUTTON_UP) b |= BTN_UP;
    if (d.buttons & ORBIS_PAD_BUTTON_DOWN) b |= BTN_DOWN;
    if (d.buttons & ORBIS_PAD_BUTTON_LEFT) b |= BTN_LEFT;
    if (d.buttons & ORBIS_PAD_BUTTON_RIGHT) b |= BTN_RIGHT;
    if (d.buttons & ORBIS_PAD_BUTTON_CROSS) b |= BTN_CROSS;
    if (d.buttons & ORBIS_PAD_BUTTON_CIRCLE) b |= BTN_CIRCLE;
    if (d.buttons & ORBIS_PAD_BUTTON_SQUARE) b |= BTN_SQUARE;
    if (d.buttons & ORBIS_PAD_BUTTON_TRIANGLE) b |= BTN_TRIANGLE;
    if (d.buttons & ORBIS_PAD_BUTTON_L1) b |= BTN_L1;
    if (d.buttons & ORBIS_PAD_BUTTON_R1) b |= BTN_R1;
    if (d.buttons & ORBIS_PAD_BUTTON_OPTIONS) b |= BTN_OPTIONS;
    return b;
}

uint64_t plat_ms(void) { return sceKernelGetProcessTime() / 1000; }
void plat_sleep_ms(int ms) { sceKernelUsleep((unsigned)ms * 1000); }

/* System toast. Community-documented libkernel call; struct layout is the widely used one. */
void plat_notify(const char *msg) {
    OrbisNotificationRequest r;
    memset(&r, 0, sizeof r);
    snprintf(r.message, sizeof r.message, "%s", msg ? msg : "");
    sceKernelSendNotificationRequest(0, &r, sizeof r, 0);
}

const char *plat_config_path(void) { return "/data/deluxe/config.ini"; }
const char *plat_default_local_dir(void) { return "/data"; }

/* ---- networking via sceNet ---- */
int net_init(void) {
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if (sceNetInit() < 0) return -1;
    net_pool = sceNetPoolCreate("deluxe", 1024 * 1024, 0);
    return net_pool < 0 ? -1 : 0;
}


/* IPv4 socket address layout used by the BSD-style PS4 networking API. */
typedef struct {
    uint8_t len;
    uint8_t family;
    uint16_t port;
    uint32_t addr;
    char zero[8];
} DeluxeSockaddrIn;

int net_connect(const char *ip, int port, int timeout_ms) {
    (void)timeout_ms; /* Socket timeout options are not exposed by these headers. */

    int s = sceNetSocket("deluxe", ORBIS_NET_AF_INET,
                         ORBIS_NET_SOCK_STREAM, 0);
    if (s < 0) return -1;

    DeluxeSockaddrIn sa;
    memset(&sa, 0, sizeof sa);
    sa.len = sizeof sa;
    sa.family = ORBIS_NET_AF_INET;
    sa.port = sceNetHtons((uint16_t)port);

    if (sceNetInetPton(ORBIS_NET_AF_INET, ip, &sa.addr) != 1) {
        sceNetSocketClose(s);
        return -1;
    }

    if (sceNetConnect(s, (OrbisNetSockaddr *)&sa, sizeof sa) < 0) {
        sceNetSocketClose(s);
        return -1;
    }
    return s;
}

int net_send_all(int h, const void *buf, size_t len) {
    const char *p = buf;
    while (len) { int n = sceNetSend(h, p, len, 0); if (n <= 0) return -1; p += n; len -= (size_t)n; }
    return 0;
}

int net_recv(int h, void *buf, size_t len, int timeout_ms) {
    (void)timeout_ms;
    int n = sceNetRecv(h, buf, len, 0);
    return n < 0 ? NET_ERR : n;
}

void net_close(int h) { sceNetSocketClose(h); }
