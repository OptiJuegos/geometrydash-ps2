#pragma once
#include <stddef.h>

/* ISO9660 naming for assets read from cdrom0:. Pure string code, no I/O, so
 * it is testable on a desktop compiler. ps2/tools/disc_names.py must stay
 * identical: the ISO is written with the Python version and read with this one.
 *
 * Component rule: uppercase, every char outside [A-Z0-9_.] becomes '_'. A name
 * longer than 31 chars (ISO9660 level 2) keeps a truncated stem plus "_" and
 * the FNV-1a 32 hash (8 hex digits) of the original component. */
#define PS2_DISC_NAME_MAX 31

/* Style bits for PS2_DiscPath. The CD driver spelling that works differs
 * between loaders, so the caller tries styles in order and caches the winner. */
#define PS2_DISC_BACKSLASH 1   /* cdrom0:\A\B instead of cdrom0:/A/B */
#define PS2_DISC_VERSION   2   /* append ";1" */
#define PS2_DISC_STYLES    4

/* Returns length written, or -1 if it does not fit. */
int PS2_DiscComponent(const char *in, size_t len, char *out, size_t capacity);

/* rel is a path below the disc root, e.g. "romfs/menus/main_menu.txt".
 * Returns 0 on success, -1 on overflow or unsafe path. */
int PS2_DiscPath(const char *rel, int style, char *out, size_t capacity);
