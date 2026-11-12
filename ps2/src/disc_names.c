#include "disc_names.h"
#include <stdio.h>
#include <string.h>

static unsigned fnv1a(const char *s, size_t len)
{
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static char map_char(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')
        return c;
    return '_';
}

int PS2_DiscComponent(const char *in, size_t len, char *out, size_t capacity)
{
    if (!in || !out || !len || capacity < PS2_DISC_NAME_MAX + 1) return -1;
    size_t keep = len;
    size_t ext_len = 0;
    if (len > PS2_DISC_NAME_MAX) {
        const char *dot = NULL;
        for (size_t i = len; i > 0; --i)
            if (in[i - 1] == '.') { dot = in + i - 1; break; }
        if (dot && (size_t)(in + len - dot) <= 5) ext_len = (size_t)(in + len - dot);
        /* stem + "_" + 8 hex + ext */
        keep = PS2_DISC_NAME_MAX - ext_len - 9;
    }
    for (size_t i = 0; i < keep; ++i) out[i] = map_char(in[i]);
    size_t n = keep;
    if (len > PS2_DISC_NAME_MAX) {
        snprintf(out + n, 10, "_%08X", fnv1a(in, len));
        n += 9;
        for (size_t i = 0; i < ext_len; ++i) out[n++] = map_char(in[len - ext_len + i]);
    }
    out[n] = 0;
    return (int)n;
}

int PS2_DiscPath(const char *rel, int style, char *out, size_t capacity)
{
    if (!rel || !out || capacity < 16) return -1;
    const char sep = (style & PS2_DISC_BACKSLASH) ? '\\' : '/';
    size_t n = (size_t)snprintf(out, capacity, "cdrom0:%c", sep);
    const char *p = rel;
    while (*p == '/') ++p;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') ++p;
        size_t len = (size_t)(p - start);
        if (!len || (len == 2 && start[0] == '.' && start[1] == '.')) return -1;
        if (n + PS2_DISC_NAME_MAX + 4 >= capacity) return -1;
        int w = PS2_DiscComponent(start, len, out + n, capacity - n);
        if (w < 0) return -1;
        n += (size_t)w;
        if (*p == '/') {
            ++p;
            if (*p) out[n++] = sep;
        }
    }
    if (style & PS2_DISC_VERSION) {
        if (n + 3 >= capacity) return -1;
        out[n++] = ';'; out[n++] = '1';
    }
    out[n] = 0;
    return 0;
}
