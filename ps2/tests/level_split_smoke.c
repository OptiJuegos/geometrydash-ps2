#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/utils/gd_split_tokens.h"

static void free_tokens(char **tokens, int count)
{
    for (int i = 0; i < count; ++i) free(tokens[i]);
    free(tokens);
}

/* Baseline: identical algorithm to the original level_loading.c splitter. */
static char **baseline(const char *str, char delimiter, int *out_count)
{
    char **result = NULL;
    int count = 0;
    const char *start = str, *p = str;
    while (*p) {
        if (*p == delimiter) {
            int len = (int)(p - start);
            if (len > 0) {
                char *token = malloc((size_t)len + 1);
                assert(token);
                memcpy(token, start, (size_t)len);
                token[len] = '\0';
                char **grown = realloc(result, sizeof(*result) * (size_t)(count + 1));
                assert(grown);
                result = grown;
                result[count++] = token;
            }
            start = p + 1;
        }
        ++p;
    }
    if (p > start) {
        int len = (int)(p - start);
        char *token = malloc((size_t)len + 1);
        assert(token);
        memcpy(token, start, (size_t)len);
        token[len] = '\0';
        char **grown = realloc(result, sizeof(*result) * (size_t)(count + 1));
        assert(grown);
        result = grown;
        result[count++] = token;
    }
    *out_count = count;
    return result;
}

static void compare(const char *str, char delimiter)
{
    int before_count = -1, after_count = -1;
    char **before = baseline(str, delimiter, &before_count);
    char **after = gd_split_tokens(str, delimiter, &after_count);
    assert(before_count == after_count);
    for (int i = 0; i < before_count; ++i)
        assert(strcmp(before[i], after[i]) == 0);
    free_tokens(before, before_count);
    free_tokens(after, after_count);
}

static uint32_t seed = 0xf00928a4u;
static uint32_t next(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

int main(void)
{
    const char *samples[] = {
        "", ";", ";;;", "x", ";a", "a;", "a;;b;;c;",
        "1,747,2,5.0,3,-7.0", "0.5a1a0", "a_b__c",
        "1;2;;3;4", "9|10|11|12", "   ", ".001,-1e-5,3.5"
    };
    const char delimiters[] = ";,|_a";
    for (size_t j = 0; j < sizeof(delimiters) - 1; ++j)
        for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i)
            compare(samples[i], delimiters[j]);

    char fuzz[257];
    const char alphabet[] = "a0,,1;;_.|b+z2";
    for (int trial = 0; trial < 5000; ++trial) {
        size_t n = next() % (sizeof(fuzz) - 1);
        for (size_t k = 0; k < n; ++k)
            fuzz[k] = alphabet[next() % (sizeof(alphabet) - 1)];
        fuzz[n] = '\0';
        compare(fuzz, delimiters[trial % (sizeof(delimiters) - 1)]);
    }
    /* Thousands of objects and key/value tokens in one level. */
    char *level = malloc(160000);
    assert(level);
    char *p = level;
    for (int i = 0; i < 5000; ++i) {
        int n = sprintf(p, "1,%d,2,3.45,3,-4.2;", i);
        p += n;
    }
    compare(level, ';');
    compare(level, ',');
    free(level);
    puts("PASS PS2 level tokenizer: 5000 random cases, empty fields and 5000-object level");
    return 0;
}
