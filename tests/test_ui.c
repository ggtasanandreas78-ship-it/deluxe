/* Drives the real UI code with scripted button presses against tests/mock_ftp.py,
   and dumps PPM screenshots. Env: FTP_PORT LOCAL_DIR SHOT_DIR DELUXE_CONFIG */
#include "ui.h"
#include "gfx.h"
#include "config.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t host_buttons;
extern char host_last_notify[256];
extern void (*host_flip_hook)(void);

static int fails, passes;
#define CHECK(cond, ...) do { if (cond) { passes++; printf("PASS  "); } else { fails++; printf("FAIL  "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static const char *shotdir;
static void shot(const char *name) {
    char p[600]; snprintf(p, sizeof p, "%s/%s.ppm", shotdir, name);
    FILE *f = fopen(p, "wb"); fb_t *fb = plat_backbuffer();
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int i = 0; i < fb->w * fb->h; i++) { unsigned char c[3] = { (fb->px[i] >> 16) & 255, (fb->px[i] >> 8) & 255, fb->px[i] & 255 }; fwrite(c, 1, 3, f); }
    fclose(f);
}
static void press(uint32_t b) { ui_update(b); ui_draw(); }
static int contains(const char *h, const char *n) { return strstr(h, n) != NULL; }

/* flip hook: capture a frame / simulate Circle during a transfer */
static int armed, flips_since, shot_at, cancel_at; static char shot_name[64];
static void hook(void) {
    if (!armed) return;
    flips_since++;
    if (shot_at && flips_since == shot_at) shot(shot_name);
    if (cancel_at && flips_since == cancel_at) host_buttons = BTN_CIRCLE;
}
static void arm(int shot_flip, const char *name, int cancel_flip) {
    armed = 1; flips_since = 0; shot_at = shot_flip; cancel_at = cancel_flip; host_buttons = 0;
    snprintf(shot_name, sizeof shot_name, "%s", name);
}
static void disarm(void) { armed = 0; host_buttons = 0; }

static void focus(int remote) { if (ui_active_pane() != remote) press(BTN_R1); }
static int select_name(int remote, const char *name) {
    focus(remote);
    for (int i = 0; i < ui_pane_count(remote); i++) press(BTN_UP);   /* go to top */
    for (int i = 0; i < ui_pane_count(remote) + 1; i++) {
        if (!strcmp(ui_pane_name(remote, ui_pane_sel(remote)), name)) return 1;
        press(BTN_DOWN);
    }
    return 0;
}

int main(void) {
    shotdir = getenv("SHOT_DIR");
    int port = atoi(getenv("FTP_PORT")); const char *L = getenv("LOCAL_DIR");
    plat_init(); host_flip_hook = hook;
    cfg_t c; cfg_defaults(&c);
    snprintf(c.host, sizeof c.host, "127.0.0.1"); c.port = port;
    snprintf(c.user, sizeof c.user, "ps4"); snprintf(c.pass, sizeof c.pass, "secret");
    snprintf(c.local_dir, sizeof c.local_dir, "%s", L); c.timeout_ms = 3000;
    cfg_save(&c, plat_config_path());

    ui_init(); ui_draw(); shot("01_home");
    CHECK(ui_screen_id() == SCR_HOME && !ui_is_connected(), "starts on Home, offline");

    /* Browse while offline: clear message, no crash */
    press(BTN_DOWN); press(BTN_CROSS);
    CHECK(ui_screen_id() == SCR_BROWSER, "Browse opens browser");
    focus(1); press(BTN_CROSS);
    CHECK(contains(ui_status_text(), "Not connected"), "offline remote pane -> '%s'", ui_status_text());
    press(BTN_CIRCLE); press(BTN_UP);

    /* connect */
    press(BTN_CROSS);
    CHECK(ui_is_connected(), "Connect succeeded -> '%s'", ui_status_text());
    CHECK(contains(host_last_notify, "Connected"), "notification sent: '%s'", host_last_notify);
    ui_draw(); shot("02_home_online");
    CHECK(ui_pane_count(1) >= 4, "remote listing loaded (%d entries incl ..)", ui_pane_count(1));

    /* browser */
    press(BTN_DOWN); press(BTN_CROSS); ui_draw(); shot("03_browser");
    CHECK(ui_screen_id() == SCR_BROWSER, "browser screen");

    /* download with live progress overlay */
    CHECK(select_name(1, "big.bin"), "selected remote big.bin");
    arm(10, "04_download_progress", 0);
    press(BTN_CROSS); disarm();
    CHECK(contains(ui_status_text(), "Downloaded big.bin"), "download via UI -> '%s'", ui_status_text());

    /* overwrite protection */
    select_name(1, "big.bin");
    press(BTN_CROSS);
    CHECK(contains(ui_status_text(), "exists") , "overwrite needs confirmation -> '%s'", ui_status_text());
    ui_draw(); shot("05_overwrite_prompt");
    press(BTN_CROSS);
    CHECK(contains(ui_status_text(), "Downloaded big.bin"), "second X overwrites -> '%s'", ui_status_text());

    { char cmd[1300]; snprintf(cmd, sizeof cmd, "cp '%s/big.bin' '%s/big_ok.bin'", L, L); if (system(cmd)) return 2; }

    /* upload */
    CHECK(select_name(0, "up_src.bin"), "selected local up_src.bin");
    arm(6, "06_upload_progress", 0);
    press(BTN_CROSS); disarm();
    CHECK(contains(ui_status_text(), "Uploaded up_src.bin"), "upload via UI -> '%s'", ui_status_text());
    int seen = 0; for (int i = 0; i < ui_pane_count(1); i++) if (!strcmp(ui_pane_name(1, i), "up_src.bin")) seen = 1;
    CHECK(seen, "remote pane refreshed and shows uploaded file");

    /* cancel with Circle (cancel uses a different file so no overwrite prompt) */
    CHECK(select_name(1, "big.bin"), "select big.bin for cancel test");
    /* make local copy absent so no confirm: remove it first */
    char lp[600]; snprintf(lp, sizeof lp, "%s/big.bin", L); remove(lp); focus(0); press(BTN_TRIANGLE); focus(1);
    select_name(1, "big.bin");
    arm(0, "x", 4);
    press(BTN_CROSS); disarm();
    CHECK(contains(ui_status_text(), "Cancelled"), "Circle cancels transfer -> '%s'", ui_status_text());
    CHECK(ui_is_connected(), "still connected after cancel");

    /* remote error shown to user: enter missing/forbidden? use read-only upload */
    press(BTN_CIRCLE); /* home */
    CHECK(ui_screen_id() == SCR_HOME, "back to home");

    /* settings */
    press(BTN_DOWN); press(BTN_CROSS);                    /* Browse(1) -> Settings(2) */
    CHECK(ui_screen_id() == SCR_SETTINGS, "settings opened");
    press(BTN_CROSS);                                      /* edit host */
    for (int i = 0; i < 5; i++) press(BTN_LEFT);           /* cursor onto a '.' */
    press(BTN_UP);                                         /* '.' -> '0' => 12700.0.1 */
    press(BTN_CROSS);
    ui_draw(); shot("07_settings_editing");
    press(BTN_TRIANGLE);
    CHECK(ui_screen_id() == SCR_SETTINGS && contains(ui_status_text(), "IPv4"), "invalid host rejected -> '%s'", ui_status_text());
    press(BTN_CROSS); for (int i = 0; i < 5; i++) press(BTN_LEFT);
    press(BTN_DOWN); press(BTN_CROSS);                     /* '0' -> '.' again */
    press(BTN_DOWN);                                        /* port */
    press(BTN_RIGHT); press(BTN_RIGHT); press(BTN_RIGHT);
    press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN); press(BTN_DOWN); /* notify */
    press(BTN_CROSS);                                       /* toggle OFF */
    ui_draw(); shot("08_settings");
    press(BTN_TRIANGLE);
    CHECK(ui_screen_id() == SCR_HOME && contains(ui_status_text(), "saved"), "valid settings saved -> '%s'", ui_status_text());
    cfg_t r; cfg_load(&r, plat_config_path());
    CHECK(r.port == port + 3 && !strcmp(r.host, "127.0.0.1") && r.notify == 0, "config file persisted (port=%d host=%s notify=%d)", r.port, r.host, r.notify);

    /* disconnect then fallback on bad local folder */
    press(BTN_UP); press(BTN_UP);                          /* Settings(2) -> Connect/Disconnect(0) */
    press(BTN_CROSS);
    CHECK(!ui_is_connected(), "disconnect works -> '%s'", ui_status_text());
    ui_shutdown();
    snprintf(c.local_dir, sizeof c.local_dir, "/definitely/not/a/dir"); cfg_save(&c, plat_config_path());
    ui_init();
    CHECK(strcmp(ui_pane_path(0), "/definitely/not/a/dir") != 0, "bad local folder falls back to '%s'", ui_pane_path(0));
    ui_shutdown();

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
