/* DELUXE PKG MANAGER - PS4 entry point */
#include "platform.h"
#include "ui.h"
#include <stdlib.h>

int main(void) {
    if (plat_init() != 0) return 1;
    net_init();
    ui_init();
    while (!ui_quit_requested()) {
        ui_update(ui_poll_input());
        ui_draw();
        plat_flip();
    }
    ui_shutdown();
    plat_shutdown();
    return 0;
}
