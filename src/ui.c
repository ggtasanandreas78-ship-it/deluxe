#include "ui.h"
#include "gfx.h"
#include "ftp.h"
#include "config.h"
#include "localfs.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ state */
typedef struct { char path[256]; ftp_entry_t *e; int n; int sel, top; int writable; } pane_t;
typedef struct { char text[200]; uint32_t color; uint64_t until; } toast_t;
typedef struct {
    int active, upload; char name[256];
    uint64_t done, total, start_ms, last_ms, last_bytes; double speed;
} xfer_t;

static cfg_t cfg, edit;
static ftp_t ftp;
static int screen = SCR_HOME, home_sel = 0, quit = 0;
static pane_t pane[2];            /* 0 = local, 1 = remote */
static int active_pane = 0;
static char status[256] = "Ready"; static uint32_t status_color = C_TEXT_DIM;
static toast_t toasts[3];
static xfer_t xf;
static uint64_t last_activity;
static char pending_overwrite[256];

/* settings editor */
enum { F_HOST, F_PORT, F_USER, F_PASS, F_LOCAL, F_REMOTE, F_TIMEOUT, F_NOTIFY, F_COUNT };
static int set_sel = 0, editing = 0, cursor = 0;

#define ROW_H 36
#define VISIBLE 16

/* ---------------------------------------------------------------- helpers */
static void set_status(uint32_t color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void set_status(uint32_t color, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(status, sizeof status, fmt, ap); va_end(ap);
    status_color = color;
}
static void toast(uint32_t color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void toast(uint32_t color, const char *fmt, ...) {
    char buf[200]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    for (int i = 2; i > 0; i--) toasts[i] = toasts[i - 1];
    snprintf(toasts[0].text, sizeof toasts[0].text, "%s", buf);
    toasts[0].color = color; toasts[0].until = plat_ms() + 5000;
    if (cfg.notify) plat_notify(buf);
}

static void fmt_size(char *o, size_t n, double b) {
    if (b < 1024) snprintf(o, n, "%.0f B", b);
    else if (b < 1024 * 1024) snprintf(o, n, "%.1f KB", b / 1024);
    else if (b < 1024.0 * 1024 * 1024) snprintf(o, n, "%.2f MB", b / 1048576);
    else snprintf(o, n, "%.2f GB", b / 1073741824.0);
}

static void path_join(char *out, size_t n, const char *base, const char *name) {
    if (!strcmp(name, "..")) {
        snprintf(out, n, "%s", base);
        char *sl = strrchr(out, '/');
        if (sl == out) out[1] = 0; else if (sl) *sl = 0;
        if (!out[0]) snprintf(out, n, "/");
        return;
    }
    if (!strcmp(base, "/")) snprintf(out, n, "/%s", name);
    else snprintf(out, n, "%s/%s", base, name);
}

static void pane_set(pane_t *p, ftp_entry_t *list, int n) {
    free(p->e);
    p->e = malloc((size_t)(n + 1) * sizeof *p->e);
    snprintf(p->e[0].name, sizeof p->e[0].name, ".."); p->e[0].is_dir = 1; p->e[0].size = 0;
    if (n) memcpy(p->e + 1, list, (size_t)n * sizeof *list);
    free(list);
    p->n = n + 1; p->sel = 0; p->top = 0;
}

/* ------------------------------------------------------------- connection */
static int refresh_local(const char *newpath) {
    char err[200], path[256]; ftp_entry_t *l; int n;
    snprintf(path, sizeof path, "%s", newpath);
    if (localfs_list(path, &l, &n, err, sizeof err)) { set_status(C_ERR, "%s", err); toast(C_ERR, "%s", err); return -1; }
    snprintf(pane[0].path, sizeof pane[0].path, "%s", path);
    pane_set(&pane[0], l, n);
    char werr[200];
    pane[0].writable = plat_check_dir(path, 1, werr, sizeof werr) == 0;
    return 0;
}

static int refresh_remote(const char *newpath) {
    ftp_entry_t *l; int n;
    if (!ftp_connected(&ftp)) { set_status(C_ERR, "Not connected"); return -1; }
    int rc = ftp_list(&ftp, newpath, &l, &n);
    last_activity = plat_ms();
    if (rc) { set_status(C_ERR, "%s", ftp.err); toast(C_ERR, "%s", ftp.err); return -1; }
    snprintf(pane[1].path, sizeof pane[1].path, "%s", newpath);
    pane_set(&pane[1], l, n);
    return 0;
}

static void do_connect(void) {
    char err[200];
    if (cfg_validate(&cfg, err, sizeof err)) { set_status(C_ERR, "%s", err); toast(C_ERR, "%s", err); return; }
    set_status(C_WARN, "Connecting to %s:%d ...", cfg.host, cfg.port);
    ui_draw(); plat_flip();
    int rc = ftp_connect(&ftp, cfg.host, cfg.port, cfg.user, cfg.pass, cfg.timeout_ms);
    if (rc) { set_status(C_ERR, "%s", ftp.err); toast(C_ERR, "%s", ftp.err); return; }
    last_activity = plat_ms();
    set_status(C_OK, "Connected to %s:%d", cfg.host, cfg.port);
    toast(C_OK, "Connected to %s:%d", cfg.host, cfg.port);
    if (refresh_remote(cfg.remote_dir)) {
        if (refresh_remote("/") == 0) toast(C_WARN, "Remote folder %s unavailable, using /", cfg.remote_dir);
    }
}

static void do_disconnect(void) {
    ftp_quit(&ftp);
    free(pane[1].e); pane[1].e = NULL; pane[1].n = 0;
    set_status(C_TEXT_DIM, "Disconnected");
    toast(C_WARN, "Disconnected");
}

/* --------------------------------------------------------------- transfers */
static int xfer_cb(uint64_t done, uint64_t total, void *ud) {
    (void)ud;
    uint64_t now = plat_ms();
    xf.done = done; xf.total = total;
    if (now - xf.last_ms >= 250) {
        double inst = (double)(done - xf.last_bytes) * 1000.0 / (double)(now - xf.last_ms);
        xf.speed = xf.speed == 0 ? inst : xf.speed * 0.6 + inst * 0.4;
        xf.last_ms = now; xf.last_bytes = done;
    }
    static uint64_t last_draw;
    if (now - last_draw >= 50) {
        last_draw = now; ui_draw(); plat_flip();
        if (plat_buttons() & BTN_CIRCLE) return 1;
    }
    return 0;
}

static void run_transfer(int upload, const char *name, uint64_t size) {
    char local[512], remote[512], err[320];
    path_join(local, sizeof local, pane[0].path, name);
    path_join(remote, sizeof remote, pane[1].path, name);
    if (!ftp_connected(&ftp)) { set_status(C_ERR, "Not connected - connect first"); toast(C_ERR, "Not connected"); return; }
    if (!upload && !pane[0].writable) {
        snprintf(err, sizeof err, "Local folder %s is read-only", pane[0].path);
        set_status(C_ERR, "%s", err); toast(C_ERR, "%s", err); return;
    }
    memset(&xf, 0, sizeof xf);
    xf.active = 1; xf.upload = upload; xf.total = size;
    snprintf(xf.name, sizeof xf.name, "%s", name);
    xf.start_ms = xf.last_ms = plat_ms();
    int rc = upload ? ftp_upload(&ftp, local, remote, xfer_cb, NULL)
                    : ftp_download(&ftp, remote, local, xfer_cb, NULL);
    uint64_t ms = plat_ms() - xf.start_ms; if (!ms) ms = 1;
    xf.active = 0; last_activity = plat_ms();
    char sz[32], sp[32];
    fmt_size(sz, sizeof sz, (double)xf.done);
    fmt_size(sp, sizeof sp, (double)xf.done * 1000.0 / (double)ms);
    if (rc == FTP_OK) {
        set_status(C_OK, "%s %s (%s, avg %s/s)", upload ? "Uploaded" : "Downloaded", name, sz, sp);
        toast(C_OK, "%s complete: %s", upload ? "Upload" : "Download", name);
    } else if (rc == FTP_ECANCEL) {
        set_status(C_WARN, "Cancelled: %s", name); toast(C_WARN, "Cancelled: %s", name);
        if (ftp_noop(&ftp) != FTP_OK) set_status(C_ERR, "Connection lost after cancel - reconnect");
    } else {
        set_status(C_ERR, "%s", ftp.err); toast(C_ERR, "%s", ftp.err);
    }
    if (rc == FTP_OK || rc == FTP_ECANCEL) {
        if (upload) refresh_remote(pane[1].path); else refresh_local(pane[0].path);
    }
}

/* -------------------------------------------------------------- browser UI */
static int entry_exists(pane_t *p, const char *name) {
    for (int i = 1; i < p->n; i++) if (!strcmp(p->e[i].name, name)) return 1;
    return 0;
}

static void browser_select(void) {
    pane_t *p = &pane[active_pane];
    if (!p->e || p->sel >= p->n) return;
    ftp_entry_t *e = &p->e[p->sel];
    if (e->is_dir) {
        char np[512]; path_join(np, sizeof np, p->path, e->name);
        if (active_pane == 0) refresh_local(np); else refresh_remote(np);
        pending_overwrite[0] = 0;
        return;
    }
    int upload = (active_pane == 0);
    pane_t *dst = &pane[upload ? 1 : 0];
    if (entry_exists(dst, e->name) && strcmp(pending_overwrite, e->name) != 0) {
        snprintf(pending_overwrite, sizeof pending_overwrite, "%s", e->name);
        set_status(C_WARN, "'%s' exists at destination - press X again to overwrite", e->name);
        return;
    }
    pending_overwrite[0] = 0;
    run_transfer(upload, e->name, e->size);
}

static void browser_update(uint32_t b) {
    pane_t *p = &pane[active_pane];
    if (b & BTN_CIRCLE) { screen = SCR_HOME; return; }
    if (b & (BTN_L1 | BTN_R1 | BTN_LEFT | BTN_RIGHT)) { active_pane ^= 1; pending_overwrite[0] = 0; return; }
    if (!p->e) { if (b & BTN_CROSS) set_status(C_WARN, "Not connected - go Home and choose Connect"); return; }
    if (b & BTN_UP) { if (p->sel > 0) p->sel--; }
    if (b & BTN_DOWN) { if (p->sel < p->n - 1) p->sel++; }
    if (p->sel < p->top) p->top = p->sel;
    if (p->sel >= p->top + VISIBLE) p->top = p->sel - VISIBLE + 1;
    if (b & BTN_TRIANGLE) { if (active_pane == 0) refresh_local(p->path); else refresh_remote(p->path); }
    if (b & BTN_SQUARE) {
        if (active_pane == 1 && ftp_connected(&ftp)) {
            char np[512]; path_join(np, sizeof np, p->path, "NewFolder");
            if (ftp_mkd(&ftp, np) == FTP_OK) { toast(C_OK, "Created %s", np); refresh_remote(p->path); }
            else { set_status(C_ERR, "%s", ftp.err); toast(C_ERR, "%s", ftp.err); }
        }
    }
    if (b & BTN_CROSS) browser_select();
}

/* ------------------------------------------------------------ settings UI */
static const char *field_name[F_COUNT] = { "Host (IPv4)", "Port", "Username", "Password", "Local folder", "Remote folder", "Timeout (s)", "Notifications" };

static char *field_str(int f) {
    switch (f) { case F_HOST: return edit.host; case F_USER: return edit.user; case F_PASS: return edit.pass;
                 case F_LOCAL: return edit.local_dir; case F_REMOTE: return edit.remote_dir; default: return NULL; }
}
static size_t field_max(int f) {
    switch (f) { case F_HOST: return sizeof edit.host; case F_USER: return sizeof edit.user; case F_PASS: return sizeof edit.pass;
                 case F_LOCAL: return sizeof edit.local_dir; case F_REMOTE: return sizeof edit.remote_dir; default: return 0; }
}
static const char *charset(int f) { return f == F_HOST ? "0123456789." : " !#$%&()*+,-./0123456789:;=@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_abcdefghijklmnopqrstuvwxyz~"; }

static void cycle_char(int f, int dir) {
    char *s = field_str(f); const char *cs = charset(f); size_t len = strlen(s);
    if (len == 0) { s[0] = cs[0]; s[1] = 0; cursor = 0; return; }
    if (cursor >= (int)len) cursor = (int)len - 1;
    const char *p = strchr(cs, s[cursor]); int idx = p ? (int)(p - cs) : 0; int m = (int)strlen(cs);
    idx = (idx + dir + m) % m; s[cursor] = cs[idx];
}

static void settings_save(void) {
    char err[200];
    if (cfg_validate(&edit, err, sizeof err)) { set_status(C_ERR, "%s", err); toast(C_ERR, "%s", err); return; }
    char perr[200];
    if (plat_check_dir(edit.local_dir, 0, perr, sizeof perr)) {
        set_status(C_ERR, "Local folder: %s", perr); toast(C_ERR, "Local folder: %s", perr); return;
    }
    cfg = edit;
    if (cfg_save(&cfg, plat_config_path())) {
        set_status(C_WARN, "Applied, but could not write %s", plat_config_path());
        toast(C_WARN, "Settings not persisted: cannot write %s", plat_config_path());
    } else {
        set_status(C_OK, "Settings saved"); toast(C_OK, "Settings saved");
    }
    if (strcmp(pane[0].path, cfg.local_dir)) refresh_local(cfg.local_dir);
    screen = SCR_HOME;
}

static void settings_update(uint32_t b) {
    if (editing) {
        char *s = field_str(set_sel); size_t len = strlen(s);
        if (b & BTN_UP) cycle_char(set_sel, 1);
        if (b & BTN_DOWN) cycle_char(set_sel, -1);
        if (b & BTN_RIGHT) {
            if (cursor < (int)len - 1) cursor++;
            else if (len + 1 < field_max(set_sel)) { s[len] = charset(set_sel)[0]; s[len + 1] = 0; cursor = (int)len; }
        }
        if ((b & BTN_LEFT) && cursor > 0) cursor--;
        if ((b & BTN_SQUARE) && len) {                       /* delete char at cursor */
            if (cursor >= (int)len) cursor = (int)len - 1;
            memmove(s + cursor, s + cursor + 1, len - (size_t)cursor);
            if (cursor >= (int)strlen(s) && cursor > 0) cursor--;
        }
        if (b & (BTN_CROSS | BTN_CIRCLE)) editing = 0;
        return;
    }
    if (b & BTN_UP) set_sel = (set_sel + F_COUNT - 1) % F_COUNT;
    if (b & BTN_DOWN) set_sel = (set_sel + 1) % F_COUNT;
    int d = ((b & BTN_RIGHT) ? 1 : 0) - ((b & BTN_LEFT) ? 1 : 0);
    int big = ((b & BTN_R1) ? 1 : 0) - ((b & BTN_L1) ? 1 : 0);
    if (set_sel == F_PORT) { edit.port += d + big * 100; if (edit.port < 1) edit.port = 1; if (edit.port > 65535) edit.port = 65535; }
    else if (set_sel == F_TIMEOUT) { edit.timeout_ms += (d + big * 5) * 1000; if (edit.timeout_ms < 1000) edit.timeout_ms = 1000; if (edit.timeout_ms > 60000) edit.timeout_ms = 60000; }
    else if (set_sel == F_NOTIFY) { if (d || (b & BTN_CROSS)) edit.notify ^= 1; }
    else if (field_str(set_sel) && (b & BTN_CROSS)) { editing = 1; cursor = (int)strlen(field_str(set_sel)); if (cursor) cursor--; }
    if (b & (BTN_TRIANGLE | BTN_OPTIONS)) settings_save();
    else if ((b & BTN_CIRCLE)) { screen = SCR_HOME; set_status(C_TEXT_DIM, "Changes discarded"); }
}

/* ---------------------------------------------------------------- home UI */
static const char *home_items[4] = { "Connect", "Browse files", "Settings", "Exit" };

static void home_update(uint32_t b) {
    if (b & BTN_UP) home_sel = (home_sel + 3) % 4;
    if (b & BTN_DOWN) home_sel = (home_sel + 1) % 4;
    if (!(b & BTN_CROSS)) return;
    switch (home_sel) {
    case 0: if (ftp_connected(&ftp)) do_disconnect(); else do_connect(); break;
    case 1: screen = SCR_BROWSER; break;
    case 2: edit = cfg; set_sel = 0; editing = 0; screen = SCR_SETTINGS; break;
    case 3: quit = 1; break;
    }
}

/* ------------------------------------------------------------------- API */
void ui_init(void) {
    memset(&pane, 0, sizeof pane); memset(toasts, 0, sizeof toasts);
    ftp_init(&ftp);
    if (cfg_load(&cfg, plat_config_path()) != 0) {
        cfg_save(&cfg, plat_config_path());
        snprintf(status, sizeof status, "No config found - created defaults at %s", plat_config_path());
    }
    char err[200];
    if (cfg_validate(&cfg, err, sizeof err)) { set_status(C_WARN, "Config issue: %s", err); }
    if (plat_check_dir(cfg.local_dir, 0, err, sizeof err)) {
        toast(C_WARN, "Local folder unusable (%s) - using default", err);
        snprintf(cfg.local_dir, sizeof cfg.local_dir, "%s", plat_default_local_dir());
    }
    refresh_local(cfg.local_dir);
}

void ui_shutdown(void) { ftp_quit(&ftp); free(pane[0].e); free(pane[1].e); pane[0].e = pane[1].e = NULL; }
int  ui_quit_requested(void) { return quit; }

void ui_update(uint32_t pressed) {
    if (!pressed) {
        /* idle: keep the control connection alive and detect drops */
        if (ftp_connected(&ftp) && plat_ms() - last_activity > 20000) {
            if (ftp_noop(&ftp) != FTP_OK) { set_status(C_ERR, "Connection lost: %s", ftp.err); toast(C_ERR, "Connection lost"); }
            last_activity = plat_ms();
        }
        return;
    }
    switch (screen) {
    case SCR_HOME: home_update(pressed); break;
    case SCR_SETTINGS: settings_update(pressed); break;
    case SCR_BROWSER: browser_update(pressed); break;
    }
}

uint32_t ui_poll_input(void) {
    static uint32_t prev; static uint64_t next_rep;
    uint32_t held = plat_buttons(), pressed = held & ~prev;
    uint64_t now = plat_ms();
    uint32_t dpad = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;
    if (pressed & dpad) next_rep = now + 400;
    else if ((held & dpad) && now >= next_rep) { pressed |= held & dpad; next_rep = now + 80; }
    prev = held;
    return pressed;
}

/* ------------------------------------------------------------- rendering */
static void draw_header(void) {
    gfx_clear_gradient(C_BG_TOP, C_BG_BOT);
    gfx_blend_rect(0, 0, SCREEN_W, 110, C_NEON, 14);
    gfx_text(60, 38, "DELUXE PKG MANAGER", C_NEON); gfx_text(61, 38, "DELUXE PKG MANAGER", C_NEON);
    gfx_text(60 + 20 * FONT_W, 38, "v1.0", C_TEXT_DIM);
    char buf[128];
    if (ftp_connected(&ftp)) { snprintf(buf, sizeof buf, "ONLINE  %s:%d", cfg.host, cfg.port); }
    else snprintf(buf, sizeof buf, "OFFLINE");
    int w = gfx_text_width(buf);
    uint32_t c = ftp_connected(&ftp) ? C_OK : C_ERR;
    gfx_fill_rect(SCREEN_W - 60 - w - 40, 46, 16, 16, c);
    gfx_blend_rect(SCREEN_W - 60 - w - 48, 38, 32, 32, c, 40);
    gfx_text(SCREEN_W - 60 - w, 38, buf, c);
    gfx_fill_rect(0, 108, SCREEN_W, 2, C_NEON);
}

static void draw_footer(const char *hint) {
    gfx_blend_rect(0, SCREEN_H - 150, SCREEN_W, 150, RGB(0, 0, 0), 90);
    gfx_fill_rect(0, SCREEN_H - 150, SCREEN_W, 2, C_NEON_DIM);
    gfx_text_clip(60, SCREEN_H - 130, status, status_color, 120);
    gfx_text(60, SCREEN_H - 60, hint, C_TEXT_DIM);
}

static void draw_toasts(void) {
    uint64_t now = plat_ms(); int y = SCREEN_H - 200;
    for (int i = 0; i < 3; i++) {
        if (toasts[i].until <= now) continue;
        int w = gfx_text_width(toasts[i].text) + 48;
        if (w > 1100) w = 1100;
        gfx_blend_rect(SCREEN_W - 60 - w, y - 8, w, 48, C_PANEL, 235);
        gfx_glow_rect(SCREEN_W - 60 - w, y - 8, w, 48, toasts[i].color);
        gfx_text_clip(SCREEN_W - 60 - w + 24, y, toasts[i].text, toasts[i].color, (w - 48) / FONT_W);
        y -= 64;
    }
}

static void draw_pane(int idx, int x, int y, int w, int h) {
    pane_t *p = &pane[idx]; int focus = (active_pane == idx);
    gfx_blend_rect(x, y, w, h, C_PANEL, 220);
    gfx_glow_rect(x, y, w, h, focus ? C_NEON : C_NEON_DIM);
    char head[300];
    if (idx == 0) snprintf(head, sizeof head, "LOCAL  [%s]  %s", p->writable ? "RW" : "RO", p->path);
    else snprintf(head, sizeof head, "REMOTE  %s", ftp_connected(&ftp) ? p->path : "(not connected)");
    gfx_text_clip(x + 20, y + 14, head, focus ? C_NEON : C_TEXT_DIM, (w - 40) / FONT_W);
    gfx_fill_rect(x + 10, y + 52, w - 20, 1, C_NEON_DIM);
    if (!p->e) { gfx_text(x + 20, y + 80, idx ? "Connect from Home to browse the console" : "No listing", C_TEXT_DIM); return; }
    for (int i = 0; i < VISIBLE && p->top + i < p->n; i++) {
        int k = p->top + i, ry = y + 62 + i * ROW_H;
        ftp_entry_t *e = &p->e[k];
        if (k == p->sel) {
            gfx_blend_rect(x + 8, ry - 2, w - 16, ROW_H - 2, C_NEON, focus ? 70 : 28);
            if (focus) gfx_fill_rect(x + 8, ry - 2, 4, ROW_H - 2, C_NEON);
        }
        char nm[300]; snprintf(nm, sizeof nm, "%s%s", e->is_dir ? "[D] " : "    ", e->name);
        gfx_text_clip(x + 24, ry, nm, e->is_dir ? C_NEON : C_TEXT, (w - 280) / FONT_W);
        if (!e->is_dir) { char sz[32]; fmt_size(sz, sizeof sz, (double)e->size); gfx_text(x + w - 24 - gfx_text_width(sz), ry, sz, C_TEXT_DIM); }
    }
}

static void draw_transfer(void) {
    if (!xf.active) return;
    int w = 1100, h = 300, x = (SCREEN_W - w) / 2, y = 330;
    gfx_blend_rect(0, 110, SCREEN_W, SCREEN_H - 110, RGB(0, 0, 0), 150);
    gfx_blend_rect(x, y, w, h, C_PANEL, 250);
    gfx_glow_rect(x, y, w, h, C_NEON);
    char b[256];
    snprintf(b, sizeof b, "%s", xf.upload ? "UPLOADING" : "DOWNLOADING");
    gfx_text(x + 30, y + 24, b, C_NEON);
    gfx_text_clip(x + 30, y + 64, xf.name, C_TEXT, (w - 60) / FONT_W);
    double frac = xf.total ? (double)xf.done / (double)xf.total : 0;
    gfx_progress(x + 30, y + 118, w - 60, 40, frac);
    char d[32], t[32], s[32];
    fmt_size(d, sizeof d, (double)xf.done); fmt_size(t, sizeof t, (double)xf.total); fmt_size(s, sizeof s, xf.speed);
    char eta[32] = "--";
    if (xf.speed > 1 && xf.total > xf.done) snprintf(eta, sizeof eta, "%ds", (int)((double)(xf.total - xf.done) / xf.speed));
    if (xf.total) snprintf(b, sizeof b, "%.1f%%   %s / %s   %s/s   ETA %s", frac * 100, d, t, s, eta);
    else snprintf(b, sizeof b, "%s   %s/s", d, s);
    gfx_text(x + 30, y + 180, b, C_TEXT);
    gfx_text(x + 30, y + 236, "O  Cancel", C_WARN);
}

static void draw_home(void) {
    int x = 60, y = 190, w = 620;
    gfx_blend_rect(x, y, w, 520, C_PANEL, 220); gfx_glow_rect(x, y, w, 520, C_NEON);
    for (int i = 0; i < 4; i++) {
        const char *label = (i == 0 && ftp_connected(&ftp)) ? "Disconnect" : home_items[i];
        int ry = y + 40 + i * 110;
        if (i == home_sel) { gfx_blend_rect(x + 20, ry - 14, w - 40, 76, C_NEON, 60); gfx_glow_rect(x + 20, ry - 14, w - 40, 76, C_NEON); }
        gfx_text(x + 60, ry + 8, label, i == home_sel ? C_NEON : C_TEXT);
    }
    int ix = 760; gfx_blend_rect(ix, y, 1100, 520, C_PANEL, 220); gfx_glow_rect(ix, y, 1100, 520, C_NEON_DIM);
    char b[300];
    gfx_text(ix + 30, y + 24, "CONNECTION PROFILE", C_NEON);
    snprintf(b, sizeof b, "Host    %s:%d", cfg.host, cfg.port); gfx_text(ix + 30, y + 80, b, C_TEXT);
    snprintf(b, sizeof b, "User    %s", cfg.user); gfx_text(ix + 30, y + 120, b, C_TEXT);
    snprintf(b, sizeof b, "Timeout %d s", cfg.timeout_ms / 1000); gfx_text(ix + 30, y + 160, b, C_TEXT);
    snprintf(b, sizeof b, "Local   %s  [%s]", cfg.local_dir, pane[0].writable ? "RW" : "RO"); gfx_text_clip(ix + 30, y + 200, b, C_TEXT, 76);
    snprintf(b, sizeof b, "Remote  %s", cfg.remote_dir); gfx_text_clip(ix + 30, y + 240, b, C_TEXT, 76);
    gfx_text(ix + 30, y + 320, "Tip: make sure an FTP server is running on the target", C_TEXT_DIM);
    gfx_text(ix + 30, y + 360, "console and both devices are on the same network.", C_TEXT_DIM);
}

static void draw_settings(void) {
    int x = 60, y = 150, w = 1800, h = 740;
    gfx_blend_rect(x, y, w, h, C_PANEL, 225); gfx_glow_rect(x, y, w, h, C_NEON);
    gfx_text(x + 30, y + 22, "SETTINGS", C_NEON);
    for (int i = 0; i < F_COUNT; i++) {
        int ry = y + 80 + i * 74; char v[200];
        switch (i) {
        case F_PORT: snprintf(v, sizeof v, "%d", edit.port); break;
        case F_TIMEOUT: snprintf(v, sizeof v, "%d", edit.timeout_ms / 1000); break;
        case F_NOTIFY: snprintf(v, sizeof v, "%s", edit.notify ? "ON" : "OFF"); break;
        case F_PASS: { size_t n = strlen(edit.pass); memset(v, '*', n); v[n] = 0; if (editing && i == set_sel) snprintf(v, sizeof v, "%s", edit.pass); break; }
        default: snprintf(v, sizeof v, "%s", field_str(i)); break;
        }
        if (i == set_sel) { gfx_blend_rect(x + 16, ry - 12, w - 32, 62, C_NEON, editing ? 90 : 50); gfx_fill_rect(x + 16, ry - 12, 5, 62, C_NEON); }
        gfx_text(x + 50, ry + 4, field_name[i], i == set_sel ? C_NEON : C_TEXT_DIM);
        int vx = x + 480;
        gfx_text_clip(vx, ry + 4, v, C_TEXT, 90);
        if (editing && i == set_sel) {
            int cx = vx + cursor * FONT_W;
            gfx_fill_rect(cx, ry + 34, FONT_W, 3, C_WARN);
        }
    }
}

void ui_draw(void) {
    gfx_set_target(plat_backbuffer());
    draw_header();
    const char *hint = "";
    switch (screen) {
    case SCR_HOME: draw_home(); hint = "UP/DOWN Select    X Confirm"; break;
    case SCR_SETTINGS: draw_settings();
        hint = editing ? "UP/DOWN Change char   LEFT/RIGHT Move   [] Delete   X Done"
                       : "UP/DOWN Field   LEFT/RIGHT Adjust (L1/R1 big)   X Edit text   TRIANGLE Save   O Discard";
        break;
    case SCR_BROWSER:
        draw_pane(0, 60, 150, 900, 760); draw_pane(1, 960, 150, 900, 760);
        hint = "L1/R1 Switch pane   X Open / Transfer   TRIANGLE Refresh   [] New remote folder   O Back";
        break;
    }
    draw_footer(hint);
    draw_toasts();
    draw_transfer();
}

/* ------------------------------------------------------------ introspection */
int ui_screen_id(void) { return screen; }
int ui_is_connected(void) { return ftp_connected(&ftp); }
const char *ui_status_text(void) { return status; }
const char *ui_pane_path(int r) { return pane[r].path; }
int ui_pane_count(int r) { return pane[r].n; }
const char *ui_pane_name(int r, int i) { return (i >= 0 && i < pane[r].n) ? pane[r].e[i].name : ""; }
int ui_pane_sel(int r) { return pane[r].sel; }
int ui_active_pane(void) { return active_pane; }
