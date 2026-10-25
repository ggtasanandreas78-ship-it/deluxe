#ifndef UI_H
#define UI_H
#include <stdint.h>
enum { SCR_HOME, SCR_SETTINGS, SCR_BROWSER };

void     ui_init(void);
uint32_t ui_poll_input(void);        /* edge-detected presses with key-repeat on D-pad */
void     ui_update(uint32_t pressed);/* feed newly pressed buttons */
void     ui_draw(void);              /* render current screen into backbuffer (no flip) */
int      ui_quit_requested(void);
void     ui_shutdown(void);

/* introspection (used by tests) */
int         ui_screen_id(void);
int         ui_is_connected(void);
const char *ui_status_text(void);
const char *ui_pane_path(int remote);
int         ui_pane_count(int remote);
const char *ui_pane_name(int remote, int idx);
int         ui_pane_sel(int remote);
int         ui_active_pane(void);   /* 0 local, 1 remote */
#endif
