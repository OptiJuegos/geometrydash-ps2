#ifndef GDPS2_ASSET_PAK_H
#define GDPS2_ASSET_PAK_H

#include <stdio.h>
#include <sys/stat.h>

/* Compatible with Minecraft/OptiCraft's MCPK version 1. Keys are paths
 * relative to romfs/ (for example "menus/main_menu.txt"). Read-only.
 * The table is cached, but file contents are streamed, not unpacked into RAM. */
int PS2_PakMount(const char *path);
void PS2_PakUnmount(void);
int PS2_PakMounted(void);
int PS2_PakExists(const char *key);
int PS2_PakStat(const char *key, struct stat *st);
FILE *PS2_PakFopen(const char *key);

#endif
