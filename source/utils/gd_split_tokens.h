#ifndef GD_SPLIT_TOKENS_H
#define GD_SPLIT_TOKENS_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* PS2 level data may contain thousands of comma- or semicolon-separated
 * tokens. The old implementation reallocates the pointer array once per
 * token. Count nonempty fields first, then allocate that array only once.
 * Each token remains individually owned for free_string_array(). */
static inline char **gd_split_tokens(const char *str, char delimiter, int *out_count)
{
    if (!out_count) return NULL;
    *out_count = 0;
    if (!str) return NULL;

    size_t count = 0;
    const char *start = str;
    for (const char *p = str;; ++p) {
        if (*p == delimiter || *p == '\0') {
            if (p > start) {
                if (count == INT_MAX) return NULL;
                ++count;
            }
            if (!*p) break;
            start = p + 1;
        }
    }
    if (!count || count > SIZE_MAX / sizeof(char *)) return NULL;
    char **result = malloc(count * sizeof(*result));
    if (!result) return NULL;

    size_t used = 0;
    start = str;
    for (const char *p = str;; ++p) {
        if (*p == delimiter || *p == '\0') {
            if (p > start) {
                size_t len = (size_t)(p - start);
                char *token = malloc(len + 1);
                if (!token) {
                    for (size_t i = 0; i < used; ++i) free(result[i]);
                    free(result);
                    return NULL;
                }
                memcpy(token, start, len);
                token[len] = '\0';
                result[used++] = token;
            }
            if (!*p) break;
            start = p + 1;
        }
    }
    *out_count = (int)used;
    return result;
}

#endif
