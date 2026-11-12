#ifndef GD_SPRITE_SORT_SMALL_H
#define GD_SPRITE_SORT_SMALL_H

/* Called after building the SortItem keys. Stable insertion sort avoids
 * three radix passes and three 256-bucket scans for sparse PS2 scenes.
 * The existing radix sorter compares only three low bytes (24 bits), so
 * mask the key here to preserve exactly the same visible layer ordering. */
static inline void gd_stable_sort_small(SortItem *items, int count)
{
    for (int i = 1; i < count; ++i) {
        SortItem item = items[i];
        const uint32_t key = item.key & 0x00ffffffu;
        int j = i;
        while (j > 0 && (items[j - 1].key & 0x00ffffffu) > key) {
            items[j] = items[j - 1];
            --j;
        }
        items[j] = item;
    }
}

#endif
