#include "localfs.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int cmp(const void *a, const void *b) {
    const ftp_entry_t *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return strcmp(x->name, y->name);
}

int localfs_list(const char *path, ftp_entry_t **out, int *count, char *err, size_t errlen) {
    DIR *d = opendir(path);
    if (!d) {
        snprintf(err, errlen, "Cannot open %s: %s", path,
                 errno == EACCES ? "permission denied" : errno == ENOENT ? "does not exist" : "error");
        return -1;
    }
    int n = 0, cap = 64; ftp_entry_t *e = malloc((size_t)cap * sizeof *e);
    struct dirent *de;
    while (e && (de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[512]; struct stat st;
        snprintf(full, sizeof full, "%s/%s", path, de->d_name);
        if (stat(full, &st) != 0) continue;
        if (n == cap) { cap *= 2; ftp_entry_t *ne = realloc(e, (size_t)cap * sizeof *e); if (!ne) break; e = ne; }
        snprintf(e[n].name, sizeof e[n].name, "%s", de->d_name);
        e[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        e[n].size = e[n].is_dir ? 0 : (uint64_t)st.st_size;
        n++;
    }
    closedir(d);
    if (!e) { snprintf(err, errlen, "Out of memory"); return -1; }
    if (n > 1) qsort(e, (size_t)n, sizeof *e, cmp);
    *out = e; *count = n;
    return 0;
}
