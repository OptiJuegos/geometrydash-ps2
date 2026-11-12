/* Test the exact stable, low-24-bit ordering used by the existing sorter. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t key;
    int ordinal;
} SortItem;
#include "../../source/utils/sprite_sort_small.h"

static uint32_t seed = 0x92ac3fe1u;
static uint32_t rand32(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static void radix_reference(SortItem *items, int n)
{
    SortItem work[16];
    SortItem *src = items, *dst = work;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned count[256] = {0};
        for (int i = 0; i < n; ++i)
            ++count[(src[i].key >> shift) & 255u];
        unsigned sum = 0;
        for (int b = 0; b < 256; ++b) {
            unsigned old = count[b];
            count[b] = sum;
            sum += old;
        }
        for (int i = 0; i < n; ++i)
            dst[count[(src[i].key >> shift) & 255u]++] = src[i];
        SortItem *tmp = src; src = dst; dst = tmp;
    }
    if (src != items) memcpy(items, src, (size_t)n * sizeof(*items));
}

int main(void)
{
    for (int n = 0; n <= 16; ++n) {
        for (int trial = 0; trial < 8000; ++trial) {
            SortItem actual[16], expected[16];
            for (int i = 0; i < n; ++i) {
                uint32_t key = rand32();
                if (trial % 3 == 0) key &= 7u; /* stable tie cases */
                if (trial % 5 == 0) key |= 0xff000000u; /* radix ignores high byte */
                actual[i] = (SortItem){key, i};
            }
            memcpy(expected, actual, (size_t)n * sizeof(*actual));
            gd_stable_sort_small(actual, n);
            radix_reference(expected, n);
            for (int i = 0; i < n; ++i) {
                assert(actual[i].key == expected[i].key);
                assert(actual[i].ordinal == expected[i].ordinal);
            }
        }
    }
    puts("PASS: 136000 sparse sprite sorts identical to 3-pass stable radix");
    return 0;
}
