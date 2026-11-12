#pragma once
#include <stddef.h>

typedef enum {
    PS2_PATH_INVALID = -1,
    PS2_PATH_NATIVE = 0,
    PS2_PATH_ASSET = 1,
    PS2_PATH_SAVE = 2
} Ps2PathKind;

/* Device base examples: "host:", "mass:/GDPS2", "cdrom0:" (disc is handled in romfs_ps2.c).
 * No filesystem functions are called; safe to test on a desktop compiler. */
Ps2PathKind PS2_PathMap(const char *path, const char *device_base,
                        char *out, size_t capacity);

/* Called after PS2 pad/SIO2 initialization and before cfg_init().
 * Chooses a persistent save device; never formats or deletes a memory card. */
void PS2_SaveInit(void);

/* Pass argv[0] before romfsInit(): a cdrom0: launch probes the disc first. */
void PS2_SetLaunchPath(const char *argv0);
