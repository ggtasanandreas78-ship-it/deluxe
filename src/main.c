#include <stdio.h>
#include <unistd.h>

/*
 * DELUXE PKG MANAGER - native PS4 bootstrap
 * This first build validates the OpenOrbis build/package pipeline.
 * It is not yet the full graphical UI or FTP implementation.
 */
int main(void) {
    printf("\n");
    printf("============================================\n");
    printf("       DELUXE PKG MANAGER - PS4\n");
    printf("============================================\n");
    printf("Build bootstrap started.\n");
    printf("Full native UI and FTP integration are next.\n");
    printf("Do not use demo transfer values as real status.\n");
    printf("============================================\n");
    for (;;) {
        sleep(10);
    }
    return 0;
}
