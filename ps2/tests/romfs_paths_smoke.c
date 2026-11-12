#include "romfs_paths.h"
#include <stdio.h>
#include <string.h>

static int check(const char *path, const char *base, Ps2PathKind expected_kind,
                 const char *expected)
{
    char out[512];
    Ps2PathKind result = PS2_PathMap(path, base, out, sizeof(out));
    if (result == expected_kind && (expected == NULL || strcmp(expected, out) == 0))
        return 1;
    fprintf(stderr, "ROMFS map unexpected: %s -> kind=%d expected=%d result=%s expected=%s\n",
            path, (int)result, (int)expected_kind,
            result == PS2_PATH_INVALID ? "(invalid)" : out,
            expected ? expected : "(invalid)");
    return 0;
}

int main(void)
{
    if (!check("romfs:/menus/main_menu.txt", "host:", PS2_PATH_ASSET,
               "host:romfs/menus/main_menu.txt")) return 1;
    if (!check("romfs:/gfx/sprites.p2sheet", "mass:/GDPS2", PS2_PATH_ASSET,
               "mass:/GDPS2/romfs/gfx/sprites.p2sheet")) return 2;
    if (!check("romfs:/main_levels/Level.gmd", "cdrom0:", PS2_PATH_ASSET,
               "cdrom0:romfs/main_levels/Level.gmd")) return 3;
    if (!check("/3ds/gd3ds/config.json", "host:", PS2_PATH_SAVE,
               "host:save/config.json")) return 4;
    if (!check("sdmc:/3ds/gd3ds/data/levels.json", "mass:/GDPS2", PS2_PATH_SAVE,
               "mass:/GDPS2/save/data/levels.json")) return 5;
    if (!check("sdmc:/3ds/gd3ds", "host:", PS2_PATH_SAVE, "host:save/")) return 6;
    if (!check("/3ds/gd3ds/config.json", "mc0:GDPS2", PS2_PATH_SAVE,
               "mc0:GDPS2/save/config.json")) return 15;
    if (!check("sdmc:/3ds/gd3ds/data/4D50A9F46C866548.d", "mc1:GDPS2",
               PS2_PATH_SAVE, "mc1:GDPS2/save/data/4D50A9F46C866548.d")) return 16;
    if (!check("sdmc:/3ds/other/save.dat", "host:", PS2_PATH_SAVE,
               "host:save/3ds/other/save.dat")) return 7;
    if (!check("host:myfile", "host:", PS2_PATH_NATIVE, "host:myfile")) return 8;
    if (!check("local.txt", "host:", PS2_PATH_NATIVE, "local.txt")) return 9;
    if (!check("romfs:/../escape", "host:", PS2_PATH_INVALID, NULL)) return 10;
    if (!check("sdmc:/3ds/gd3ds/sub/../../escape", "host:", PS2_PATH_INVALID, NULL)) return 11;
    if (!check("romfs:/gfx/bad\\name", "host:", PS2_PATH_INVALID, NULL)) return 12;
    if (!check("romfs:/gfx/evil:device", "host:", PS2_PATH_INVALID, NULL)) return 13;
    char tiny[12];
    if (PS2_PathMap("romfs:/gfx/sprites.p2sheet", "host:", tiny, sizeof(tiny))
            != PS2_PATH_INVALID) return 14;
    puts("PASS: ROMFS host, mass, cd, MC0/MC1 save paths, native paths, overflow and traversal rejection");
    return 0;
}
