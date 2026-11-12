#include "romfs_paths.h"
#include <stdio.h>
#include <string.h>

static int safe_tail(const char *part)
{
    /* Do not permit virtual paths to escape the GDPS2 asset/save directory. */
    if (!part) return 0;
    const char *p = part;
    while (*p) {
        const char *first = p;
        while (*p && *p != '/') {
            if (*p == ':' || *p == '\\' || (unsigned char)*p < 32) return 0;
            ++p;
        }
        size_t len = (size_t)(p - first);
        if (len == 2 && first[0] == '.' && first[1] == '.') return 0;
        if (*p) ++p;
    }
    return 1;
}

Ps2PathKind PS2_PathMap(const char *path, const char *base,
                        char *out, size_t capacity)
{
    if (!path || !base || !out || !capacity || !*base) return PS2_PATH_INVALID;
    const char *tail = path;
    const char *folder = NULL;
    Ps2PathKind kind = PS2_PATH_NATIVE;

    if (!strncmp(path, "romfs:/", 7)) {
        kind = PS2_PATH_ASSET;
        folder = "romfs/";
        tail = path + 7;
    } else if (!strncmp(path, "sdmc:/", 6)) {
        kind = PS2_PATH_SAVE;
        folder = "save/";
        tail = path + 6;
    } else if (!strncmp(path, "/3ds/", 5)) {
        kind = PS2_PATH_SAVE;
        folder = "save/";
        tail = path + 1;
    }

    if (kind == PS2_PATH_NATIVE) {
        int n = snprintf(out, capacity, "%s", path);
        return n >= 0 && (size_t)n < capacity ? kind : PS2_PATH_INVALID;
    }
    if (kind == PS2_PATH_SAVE && !strncmp(tail, "3ds/gd3ds", 9) &&
        (tail[9] == '/' || !tail[9])) {
        tail += 9;
        if (*tail == '/') ++tail;
    }
    if (!safe_tail(tail)) return PS2_PATH_INVALID;
    size_t len = strlen(base);
    const char *separator = base[len - 1] == ':' || base[len - 1] == '/' ? "" : "/";
    int n = snprintf(out, capacity, "%s%s%s%s", base, separator, folder, tail);
    return n >= 0 && (size_t)n < capacity ? kind : PS2_PATH_INVALID;
}
