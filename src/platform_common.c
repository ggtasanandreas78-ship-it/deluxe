/* Code shared by the PS4 and host platforms (plain POSIX + libc only). */
#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int net_valid_ipv4(const char *s) {
    if (!s || !*s) return 0;
    int parts = 0;
    while (1) {
        if (*s < '0' || *s > '9') return 0;
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); if (++digits > 3 || v > 255) return 0; s++; }
        parts++;
        if (*s == '.') { s++; continue; }
        return *s == 0 && parts == 4;
    }
}

int plat_check_dir(const char *path, int mode_write, char *err, size_t errlen) {
    struct stat st;
    if (stat(path, &st) != 0) { snprintf(err, errlen, "%s: %s", path, errno == ENOENT ? "does not exist" : errno == EACCES ? "permission denied" : "inaccessible"); return -1; }
    if (!S_ISDIR(st.st_mode)) { snprintf(err, errlen, "%s is not a folder", path); return -1; }
    DIR *d = opendir(path);
    if (!d) { snprintf(err, errlen, "%s: no read permission", path); return -1; }
    closedir(d);
    if (mode_write) {
        char probe[512]; snprintf(probe, sizeof probe, "%s/.deluxe_probe", path);
        FILE *f = fopen(probe, "wb");
        if (!f) { snprintf(err, errlen, "%s is read-only (%s)", path, errno == EROFS ? "read-only filesystem" : "no write permission"); return -1; }
        fclose(f); remove(probe);
    }
    return 0;
}
