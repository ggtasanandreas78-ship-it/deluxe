/* Platform abstraction: one implementation for PS4 (OpenOrbis), one for host (tests). */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
#include <stddef.h>

#define SCREEN_W 1920
#define SCREEN_H 1080

typedef struct { uint32_t *px; int w, h; } fb_t;   /* 0x00RRGGBB pixels */

enum {
    BTN_UP = 1 << 0,  BTN_DOWN = 1 << 1,  BTN_LEFT = 1 << 2,   BTN_RIGHT = 1 << 3,
    BTN_CROSS = 1 << 4, BTN_CIRCLE = 1 << 5, BTN_SQUARE = 1 << 6, BTN_TRIANGLE = 1 << 7,
    BTN_L1 = 1 << 8,  BTN_R1 = 1 << 9,    BTN_OPTIONS = 1 << 10
};

/* ---- video / input / time / notifications ---- */
int       plat_init(void);               /* 0 on success */
void      plat_shutdown(void);
fb_t     *plat_backbuffer(void);         /* buffer to draw into */
void      plat_flip(void);               /* present backbuffer, wait vsync */
uint32_t  plat_buttons(void);            /* currently held buttons */
uint64_t  plat_ms(void);                 /* monotonic milliseconds */
void      plat_sleep_ms(int ms);
void      plat_notify(const char *msg);  /* system toast on PS4, log on host */

/* ---- filesystem helpers (path/permission checks) ---- */
/* Returns 0 if dir is readable. mode_write!=0 additionally requires write access
   (probed by creating+removing a temp file). On failure writes a human message. */
int       plat_check_dir(const char *path, int mode_write, char *err, size_t errlen);
const char *plat_config_path(void);
const char *plat_default_local_dir(void);

/* ---- TCP (IPv4 dotted-quad only) ---- */
#define NET_ERR      (-1)
#define NET_TIMEOUT  (-2)
int       net_init(void);
int       net_connect(const char *ip, int port, int timeout_ms);  /* handle>=0 or -1 */
int       net_send_all(int h, const void *buf, size_t len);       /* 0 ok, -1 err */
int       net_recv(int h, void *buf, size_t len, int timeout_ms); /* >0 bytes, 0 closed, NET_ERR, NET_TIMEOUT */
void      net_close(int h);
int       net_valid_ipv4(const char *s);

#endif
