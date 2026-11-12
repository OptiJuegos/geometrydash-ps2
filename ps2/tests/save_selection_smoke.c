/* Host mocks: test live PS2 path and Memory Card selection logic without
 * touching either a Windows save directory or a PS2 memory card. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <3ds.h>

#include "romfs_paths.h"
#include "asset_pak.h"

/* Save-location tests only need path discovery, not an actual MCPK file. */
static int asset_source;
static int pak_mount_calls;
static int pak_mounted;
int PS2_PakMount(const char *path)
{
    ++pak_mount_calls;
    pak_mounted = asset_source == 3 &&
        strcmp(path, "mass:/GDPS2/assets.pak") == 0;
    return pak_mounted;
}
void PS2_PakUnmount(void) { pak_mounted = 0; }
int PS2_PakMounted(void) { return pak_mounted; }
int PS2_PakExists(const char *key)
{ return pak_mounted && strcmp(key, "menus/main_menu.txt") == 0; }
FILE *PS2_PakFopen(const char *key) { (void)key; return NULL; }
int PS2_PakStat(const char *key, struct stat *st)
{ (void)key; (void)st; return -1; }
int DelayThread(int us) { (void)us; return 0; }

static int host_has_save, card_has_save[2];
static int card_formatted[2], last_mc_port;
static int mc_init_calls, created_count;
static int asset_file_attempts;
static char created[64][128], opened[512];
static const char *available_song;

int SifLoadModule(const char *name, int argc, const char *args)
{ (void)name; (void)argc; (void)args; return 1; }

int mcInit(int type)
{ (void)type; ++mc_init_calls; return 0; }

int mcGetInfo(int port, int slot, int *type, int *free_clusters, int *formatted)
{
    assert(slot == 0 && port >= 0 && port <= 1);
    last_mc_port = port;
    *type = card_formatted[port] ? 2 : 0;
    *free_clusters = 9000;
    *formatted = card_formatted[port] ? 1 : 0;
    return 0;
}

int mcSync(int mode, int *cmd, int *result)
{
    assert(mode == 0);
    (void)cmd;
    *result = card_formatted[last_mc_port] ? -1 : -10;
    return 1;
}

static int contains_created(const char *path)
{
    for (int i=0; i<created_count; ++i)
        if (!strcmp(created[i], path)) return 1;
    return 0;
}

void PS2_BootPathProbe(const char *path, int error, int success)
{ (void)path; (void)error; (void)success; }

FILE *__real_fopen(const char *path, const char *mode)
{
    snprintf(opened, sizeof(opened), "%s", path);
    if (strstr(path, "romfs/menus/main_menu.txt") ||
        strstr(path, "ROMFS/MENUS/MAIN_MENU.TXT")) ++asset_file_attempts;
    if (!strcmp(mode, "rb")) {
        if (available_song && !strcmp(path, available_song)) return tmpfile();
        if (asset_source == 0 && !strcmp(path, "host:romfs/menus/main_menu.txt")) return tmpfile();
        if (asset_source == 1 && !strcmp(path, "mass:/GDPS2/romfs/menus/main_menu.txt")) return tmpfile();
        if (asset_source == 2 && !strcmp(path, "cdrom0:/ROMFS/MENUS/MAIN_MENU.TXT;1")) return tmpfile();
        if (host_has_save && !strcmp(path, "host:save/config.json")) return tmpfile();
        if (card_has_save[0] && !strcmp(path, "mc0:GDPS2/save/config.json")) return tmpfile();
        if (card_has_save[1] && !strcmp(path, "mc1:GDPS2/save/config.json")) return tmpfile();
        errno = ENOENT;
        return NULL;
    }
    if (strchr(mode, 'w')) return tmpfile();
    errno = EINVAL;
    return NULL;
}

int __real_stat(const char *path, struct stat *st)
{
    if (contains_created(path)) {
        memset(st, 0, sizeof(*st));
#ifdef _S_IFDIR
        st->st_mode = _S_IFDIR;
#else
        st->st_mode = S_IFDIR;
#endif
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int __real_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if (created_count >= 64) abort();
    snprintf(created[created_count++], sizeof(created[0]), "%s", path);
    return 0;
}

int __real_open(const char *path, int flags, ...)
{ (void)path; (void)flags; return -1; }
DIR *__real_opendir(const char *path)
{ (void)path; return NULL; }
int __real_access(const char *path, int mode)
{ (void)path; (void)mode; return -1; }
int __real_remove(const char *path)
{ (void)path; return -1; }
int __real_unlink(const char *path)
{ (void)path; return -1; }
int __real_rename(const char *from, const char *to)
{ (void)from; (void)to; return -1; }

FILE *__wrap_fopen(const char *path, const char *mode);
int __wrap_access(const char *path, int mode);

static void reset_case(int asset, int host_save, int mc0, int mc1,
                       int mc0_save, int mc1_save)
{
    romfsExit();
    asset_source = asset;
    host_has_save = host_save;
    card_formatted[0] = mc0;
    card_formatted[1] = mc1;
    card_has_save[0] = mc0_save;
    card_has_save[1] = mc1_save;
    last_mc_port = 0;
    mc_init_calls = created_count = 0;
    available_song = NULL;
    opened[0] = 0;
    assert(romfsInit() == 0);
    if (asset_source == 3) assert(PS2_PakMounted());
    /* A second initialization must reuse the selected root without I/O. */
    int probes = asset_file_attempts;
    int mounts = pak_mount_calls;
    assert(romfsInit() == 0);
    assert(asset_file_attempts == probes);
    assert(pak_mount_calls == mounts);
    PS2_SaveInit();
}

static void check_destination(const char *expected)
{
    FILE *f = __wrap_fopen("/3ds/gd3ds/config.json", "w");
    assert(f != NULL);
    assert(!strcmp(opened, expected));
    fclose(f);
}

static void check_song_location(const char *expected)
{
    const char *song = "/3ds/gd3ds/saved_songs/467339.mp3";
    available_song = expected;
    FILE *f = __wrap_fopen(song, "rb");
    assert(f != NULL);
    assert(!strcmp(opened, expected));
    fclose(f);
    /* Even if the device does not implement access(), the popup must
     * report the same availability as the loader's fopen("rb"). */
    assert(__wrap_access(song, F_OK) == 0);
    assert(!strcmp(opened, expected));
}

int main(void)
{
    reset_case(0, 1, 1, 1, 0, 1);
    check_destination("host:save/config.json");
    assert(mc_init_calls == 0); /* do not migrate an existing host save */

    reset_case(0, 0, 1, 1, 0, 1);
    check_destination("mc1:GDPS2/save/config.json");
    assert(contains_created("mc1:GDPS2"));
    assert(contains_created("mc1:GDPS2/save"));

    reset_case(0, 0, 1, 0, 0, 0);
    check_destination("mc0:GDPS2/save/config.json");

    reset_case(0, 0, 0, 0, 0, 0);
    check_destination("host:save/config.json");

    reset_case(1, 0, 0, 0, 0, 0);
    check_destination("mass:/GDPS2/save/config.json");

    reset_case(2, 0, 0, 1, 0, 0);
    check_destination("mc1:GDPS2/save/config.json");

    reset_case(3, 0, 0, 0, 0, 0);
    check_destination("mass:/GDPS2/save/config.json");

    /* A Memory Card can hold progress while the MP3 stays on USB. */
    reset_case(1, 0, 1, 0, 0, 0);
    check_song_location("mass:/GDPS2/save/saved_songs/467339.mp3");
    check_song_location("mass:/GDPS2/custom_songs/467339.mp3");
    check_song_location("mass:/GDPS2/saved_songs/467339.mp3");
    check_song_location("mass:/GDPS2/save/custom_songs/467339.mp3");

    /* Preserve songs already stored in the chosen Memory Card. */
    check_song_location("mc0:GDPS2/save/saved_songs/467339.mp3");

    /* HostFs and packaged games also keep custom songs external. */
    reset_case(0, 0, 1, 0, 0, 0);
    check_song_location("host:save/saved_songs/467339.mp3");
    reset_case(3, 0, 1, 0, 0, 0);
    check_song_location("mass:/GDPS2/save/saved_songs/467339.mp3");
    /* Disc assets with an external USB song should work as well. */
    reset_case(2, 0, 1, 0, 0, 0);
    check_song_location("mass:/GDPS2/save/saved_songs/467339.mp3");

    available_song = NULL;
    assert(__wrap_fopen("/3ds/gd3ds/saved_songs/467339.mp3", "rb") == NULL);
    assert(__wrap_access("/3ds/gd3ds/saved_songs/467339.mp3", F_OK) == -1);
    assert(__wrap_fopen("/3ds/gd3ds/saved_songs/../../bad.mp3", "rb") == NULL);

    puts("PASS: save-device scenarios, cached MCPK probe, custom MP3 USB/host/MC fallbacks");
    return 0;
}
