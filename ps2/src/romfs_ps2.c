/* File/path compatibility layer for the offline Geometry Dash PS2 port.
 * The game uses romfs:/ and /3ds/gd3ds/ paths from its original 3DS code.
 * Assets are READ-ONLY and saves use a separate folder on a writable device.
 * Device drivers must already be present: no fake USB or ISO mount is made.
 */
#include <3ds.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>
#include <delaythread.h>

#include "romfs_paths.h"
#include "disc_names.h"
#include "boot_trace.h"
#include "asset_pak.h"
#include "asset_locator.h"

/* Candidate paths come from the ELF, CWD, and mounted PS2 devices. */
static Ps2AssetCandidates candidates;
static const char *asset_roots[GDPS2_ASSET_MAX_ROOTS + 1];
static char scanned_root[512];
static const char *launch_path;
/* Save destinations may be different from the asset device. The special
 * mc0:GDPS2 format follows PS2SDK's mc0:APP/file POSIX convention. */
static const char *save_root = "host:";
static int asset_root_index;
/* A successful resolution is immutable until romfsExit(). No new device
 * probes, USB retries or directory scans during gameplay. */
static int romfs_resolved;
static unsigned romfs_probe_calls;

FILE *__real_fopen(const char *, const char *);
int __real_open(const char *, int, ...);
int __real_stat(const char *, struct stat *);
DIR *__real_opendir(const char *);
int __real_access(const char *, int);
int __real_mkdir(const char *, mode_t);
int __real_remove(const char *);
int __real_unlink(const char *);
int __real_rename(const char *, const char *);

/* Disc assets: cdrom0: only answers exact ISO9660 names (uppercase, usually
 * ";1"), and the accepted separator differs between loaders. Try each spelling
 * once, then remember the one that worked. */
static int disc_style = -1;

static int is_disc_root(const char *root) {
    return root && (!strncmp(root, "cdrom", 5) || !strncmp(root, "cdfs", 4));
}
static int is_usb_root(const char *root) {
    return root && !strncmp(root, "mass", 4);
}
static int is_host_root(const char *root) {
    return root && !strncmp(root, "host:", 5);
}
static int on_disc(void) { return is_disc_root(asset_roots[asset_root_index]); }

/* "romfs:/a/b" -> "romfs/a/b". Returns 0 if path is not an asset path. */
static int disc_rel(const char *path, char rel[512])
{
    if (strncmp(path, "romfs:/", 7)) return 0;
    return snprintf(rel, 512, "romfs/%s", path + 7) < 512;
}

typedef int (*DiscTry)(const char *mapped, void *ctx);

/* Runs try() on every ISO9660 spelling. The launch directory is not fixed. */
static int disc_each_relative(const char *relative, DiscTry try, void *ctx)
{
    char rel[512], mapped[512];
    const char *root = asset_roots[asset_root_index];
    const char *colon = strchr(root, ':');
    const char *prefix = colon ? colon + 1 : "";
    while (*prefix == '/' || *prefix == '\\') ++prefix;
    int n = *prefix ? snprintf(rel, sizeof(rel), "%s/%s", prefix, relative) :
                      snprintf(rel, sizeof(rel), "%s", relative);
    if (n < 0 || n >= (int)sizeof(rel)) return 0;
    int first = disc_style >= 0 ? disc_style : 0;
    for (int k = 0; k < PS2_DISC_STYLES; ++k) {
        int style = (first + k) % PS2_DISC_STYLES;
        if (PS2_DiscPath(rel, style, mapped, sizeof(mapped)) < 0) return 0;
        int r = try(mapped, ctx);
        if (r) {
            if (disc_style != style) {
                printf("GDPS2 ROMFS: disc spelling style=%d (%s)\n", style, mapped);
                disc_style = style;
            }
            return r;
        }
    }
    return 0;
}

static int disc_each(const char *path, DiscTry try, void *ctx)
{
    char rel[512];
    return disc_rel(path, rel) ? disc_each_relative(rel, try, ctx) : 0;
}

struct DiscFopen { const char *mode; FILE *file; };
static int try_fopen(const char *mapped, void *ctx)
{
    struct DiscFopen *c = ctx;
    c->file = __real_fopen(mapped, c->mode);
    return c->file != NULL;
}

struct DiscOpen { int flags, mode, fd; };
static int try_open(const char *mapped, void *ctx)
{
    struct DiscOpen *c = ctx;
    c->fd = __real_open(mapped, c->flags, c->mode);
    return c->fd >= 0;
}

/* The cdrom0: IOP driver can fail stat() for files that open fine. */
struct DiscStat { struct stat *st; };
static int try_stat(const char *mapped, void *ctx)
{
    struct DiscStat *c = ctx;
    if (__real_stat(mapped, c->st) == 0) return 1;
    FILE *f = __real_fopen(mapped, "rb");
    if (!f) return 0;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    fclose(f);
    memset(c->st, 0, sizeof(*c->st));
    c->st->st_mode = S_IFREG | 0444;
    c->st->st_size = size < 0 ? 0 : size;
    return 1;
}

static int try_exists(const char *mapped, void *ctx)
{
    (void)ctx;
    FILE *f = __real_fopen(mapped, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int try_pak(const char *path, void *ctx)
{
    (void)ctx;
    if (!PS2_PakMount(path)) return 0;
    if (PS2_PakExists("menus/main_menu.txt")) return 1;
    PS2_PakUnmount();
    return 0;
}

static int probe_pak(unsigned index)
{
    char path[512];
    const char *base = asset_roots[index];
    if (is_disc_root(base))
        return disc_each_relative("assets.pak", try_pak, NULL);
    const size_t len = strlen(base);
    if (!len) return 0;
    int n = snprintf(path, sizeof(path), "%s%sassets.pak", base,
                     base[len-1] == ':' || base[len-1] == '/' ? "" : "/");
    return n > 0 && n < (int)sizeof(path) && try_pak(path, NULL);
}

/* Called from main before romfsInit. */
static int launched_from_disc;
void PS2_SetLaunchPath(const char *argv0)
{
    launch_path = argv0;
    launched_from_disc = PS2_AssetIsDisc(argv0);
    printf("GDPS2 ROMFS: launch=%s disc=%d\n",
           argv0 ? argv0 : "(null)", launched_from_disc);
}

static const char *select_base(Ps2PathKind kind)
{
    return kind == PS2_PATH_ASSET ? asset_roots[asset_root_index] :
           save_root;
}

static Ps2PathKind resolve(const char *path, char out[512])
{
    /* An empty base is not allowed, even for passthrough paths. */
    Ps2PathKind kind = PS2_PathMap(path, "host:", out, 512);
    if (kind == PS2_PATH_INVALID || kind == PS2_PATH_NATIVE) return kind;
    return PS2_PathMap(path, select_base(kind), out, 512);
}

static int probe_candidate(unsigned i)
{
    ++romfs_probe_calls;
    char path[512];
    asset_root_index = (int)i;
    disc_style = -1;
    if (probe_pak(i)) {
        printf("GDPS2 ROMFS: MCPK at %s\n", asset_roots[i]);
        return 1;
    }
    if (is_disc_root(asset_roots[i])) {
        struct DiscFopen c = { "rb", NULL };
        if (!disc_each("romfs:/menus/main_menu.txt", try_fopen, &c))
            return 0;
        fclose(c.file);
        PS2_BootPathProbe(asset_roots[i], 0, 1);
        return 1;
    }
    if (PS2_PathMap("romfs:/menus/main_menu.txt", asset_roots[i],
                    path, sizeof(path)) != PS2_PATH_ASSET) return 0;
    FILE *f = __real_fopen(path, "rb");
    if (f) {
        fclose(f);
        PS2_BootPathProbe(path, 0, 1);
        printf("GDPS2 ROMFS: loose files at %s\n", asset_roots[i]);
        return 1;
    }
    PS2_BootPathProbe(path, errno, 0);
    return 0;
}

/* Last resort only: bound directory enumeration like Minecraft.
 * Do not enumerate cdrom0: (IOP optical directory listing is unreliable). */
static int scan_usb_children(unsigned slot, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        const char *root = asset_roots[i];
        if (!((i == 0 && !PS2_AssetIsDisc(root)) ||
              ((PS2_AssetIsUsb(root) || PS2_AssetIsHost(root)) &&
               PS2_AssetIsDeviceRoot(root))))
            continue;
        DIR *dir = __real_opendir(root);
        if (!dir) continue;
        struct dirent *entry;
        unsigned examined = 0;
        int found = 0;
        while (examined < 64 && (entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (!name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
                continue;
            ++examined;
            if (strchr(name, ':') || strchr(name, '/') || strchr(name, '\\'))
                continue;
            size_t len = strlen(root);
            int n = snprintf(scanned_root, sizeof(scanned_root), "%s%s%s",
                             root, root[len-1] == '/' ? "" : "/", name);
            if (n > 0 && n < (int)sizeof(scanned_root)) {
                asset_roots[slot] = scanned_root;
                if (probe_candidate(slot)) {
                    printf("GDPS2 ROMFS: discovered install %s\n", scanned_root);
                    found = 1;
                    break;
                }
            }
        }
        closedir(dir);
        if (found) return 1;
    }
    return 0;
}

Result romfsInit(void)
{
    if (romfs_resolved) return 0;
    char cwd[512] = {0};
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
    PS2_AssetCandidatesInit(&candidates, launch_path, cwd);
    const unsigned count = candidates.count;
    for (unsigned i = 0; i < count; ++i)
        asset_roots[i] = candidates.paths[i];
    asset_root_index = 0;
    disc_style = -1;
    save_root = "host:";
    romfs_probe_calls = 0;
    PS2_PakUnmount();
    int found = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (launched_from_disc && is_usb_root(asset_roots[i])) continue;
        if (probe_candidate(i)) { found = 1; break; }
    }
    /* Match Minecraft: give an initially slow USB mount a bounded retry,
     * but do not delay normal/host boots or retry optical drives. */
    if (!found && PS2_AssetIsUsb(launch_path)) {
        for (unsigned attempt = 0; attempt < 30 && !found; ++attempt) {
            DelayThread(100000);
            for (unsigned i = 0; i < count; ++i)
                if (is_usb_root(asset_roots[i]) && probe_candidate(i)) {
                    found = 1;
                    break;
                }
        }
    }
    if (!found && !launched_from_disc)
        found = scan_usb_children(count, count);
    if (!found) asset_root_index = 0;
    if (found && is_usb_root(asset_roots[asset_root_index]))
        save_root = asset_roots[asset_root_index];
    if (found) {
        romfs_resolved = 1;
        printf("GDPS2 ROMFS: location cached, root=%s, probes=%u, pak=%d\n",
               asset_roots[asset_root_index] ? asset_roots[asset_root_index] : "(none)",
               romfs_probe_calls, PS2_PakMounted());
    }
    printf("PS2 ROMFS: assets=%s, initial saves=%s (%s)\n",
           count ? asset_roots[asset_root_index] : "(none)", save_root,
           found ? "asset probe OK" : "no game assets found");
    return found ? 0 : -1;
}

Result romfsExit(void)
{
    /* Releasing the archive/index makes a later explicit init probe anew. */
    PS2_PakUnmount();
    romfs_resolved = 0;
    return 0;
}

static int save_present(const char *base)
{
    char path[512];
    if (PS2_PathMap("/3ds/gd3ds/config.json", base, path, sizeof(path))
        != PS2_PATH_SAVE) return 0;
    FILE *file = __real_fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int ensure_real_folder(const char *folder)
{
    struct stat st;
    if (__real_stat(folder, &st) == 0) return S_ISDIR(st.st_mode);
    if (__real_mkdir(folder, 0777) == 0) return 1;
    /* Some IOP devices do not implement stat fully but report EEXIST for
     * already-created directories. Never delete/recreate a saved folder. */
    if (errno == EEXIST) return 1;
    return __real_stat(folder, &st) == 0 && S_ISDIR(st.st_mode);
}

static int memory_card_ready(int port)
{
    int type = MC_TYPE_NONE, free_clusters = 0, formatted = 0, status = -999;
    if (mcGetInfo(port, 0, &type, &free_clusters, &formatted) < 0 ||
        mcSync(MC_WAIT, NULL, &status) < 0) return 0;
    /* MCMAN may return formatted=0 even for a formatted card. Its sync
     * result distinguishes -1 (new formatted) from -2 (unformatted). */
    printf("GDPS2 SAVE: mc%d: type=%d free=%d formatted=%d result=%d\n",
           port, type, free_clusters, formatted, status);
    return type == MC_TYPE_PS2 && (status == 0 || status == -1);
}

static int use_memory_card(const char *base)
{
    char folder[64];
    if (snprintf(folder, sizeof(folder), "%s", base) >= (int)sizeof(folder) ||
        !ensure_real_folder(folder)) return 0;
    if (snprintf(folder, sizeof(folder), "%s/save", base) >= (int)sizeof(folder) ||
        !ensure_real_folder(folder)) return 0;
    save_root = base;
    printf("GDPS2 SAVE: using Memory Card %s/save/\n", save_root);
    return 1;
}

void PS2_SaveInit(void)
{
    const int asset_from_host = is_host_root(asset_roots[asset_root_index]);
    static const char *cards[] = { "mc0:GDPS2", "mc1:GDPS2" };
    int ready[2] = {0, 0};

    /* Never silently abandon already-existing PCSX2/ps2link saves. */
    if (asset_from_host && save_present("host:")) {
        save_root = "host:";
        puts("GDPS2 SAVE: existing host:save/config.json, preserving host saves");
        return;
    }

    /* Pad initialization has already loaded SIO2MAN. These are the classic
     * ROM modules used by the bundled PS2SDK libmc sample. No formatting. */
    int man = SifLoadModule("rom0:MCMAN", 0, NULL);
    int serv = SifLoadModule("rom0:MCSERV", 0, NULL);
    int init = mcInit(MC_TYPE_MC);
    printf("GDPS2 SAVE: MCMAN=%d MCSERV=%d mcInit=%d\n", man, serv, init);
    if (init >= 0) {
        for (int i = 0; i < 2; ++i) ready[i] = memory_card_ready(i);
        /* Prefer an existing card save over initializing a new location. */
        for (int i = 0; i < 2; ++i)
            if (ready[i] && save_present(cards[i]) && use_memory_card(cards[i]))
                return;
        for (int i = 0; i < 2; ++i)
            if (ready[i] && use_memory_card(cards[i])) return;
    }

    /* Existing USB/host mapping remains the fallback when there is no
     * formatted and writable card; never redirect saves to read-only CD. */
    if (asset_from_host) save_root = "host:";
    else if (is_usb_root(asset_roots[asset_root_index])) save_root = asset_roots[asset_root_index];
    else save_root = "host:";
    printf("GDPS2 SAVE: no usable Memory Card; fallback %s%ssave/\n",
           save_root, save_root[strlen(save_root)-1] == ':' ? "" : "/");
}

static int can_modify(Ps2PathKind kind)
{
    if (kind == PS2_PATH_ASSET) { errno = EROFS; return 0; }
    if (kind == PS2_PATH_INVALID) { errno = EINVAL; return 0; }
    return 1;
}

/* Helper for saving settings; 3DS allows mkdir of nested SD paths. */
static void ensure_parent_dirs(const char *path)
{
    char folder[512];
    size_t n = strlen(path);
    if (n >= sizeof(folder)) return;
    memcpy(folder, path, n+1);
    char *colon = strchr(folder, ':');
    char *begin = colon ? colon + 1 : folder;
    while (*begin == '/') ++begin;
    for (char *p = begin; *p; ++p) {
        if (*p != '/') continue;
        *p = 0;
        __real_mkdir(folder, 0777);
        *p = '/';
    }
}

static int is_disc_asset(const char *path)
{
    return on_disc() && path && !strncmp(path, "romfs:/", 7);
}

static const char *pak_key(const char *path)
{
    return path && !strncmp(path, "romfs:/", 7) ? path + 7 : NULL;
}

/* Custom MP3s are user content, often too large for an 8 MB Memory Card.
 * Saves may live on mc0:/mc1: while the song files live beside the game on
 * USB or host:. Accept the directory name shown by older PS2 menus too. */
static const char *custom_song_name(const char *path)
{
    static const char *prefixes[] = {
        "/3ds/gd3ds/saved_songs/", "sdmc:/3ds/gd3ds/saved_songs/"
    };
    if (!path) return NULL;
    for (unsigned i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); ++i) {
        size_t prefix_len = strlen(prefixes[i]);
        if (strncmp(path, prefixes[i], prefix_len)) continue;
        const char *name = path + prefix_len;
        const char *end = name;
        while (*end >= '0' && *end <= '9') ++end;
        if (end > name && !strcmp(end, ".mp3")) return name;
    }
    return NULL;
}

/* Read-only fallback. The normal selected save directory is tried first by
 * the calling wrapper, so an existing song there always wins. */
static FILE *open_external_custom_song(const char *path)
{
    const char *name = custom_song_name(path);
    const char *base = asset_roots[asset_root_index];
    static const char *folders[] = {
        "save/saved_songs", "saved_songs", "save/custom_songs", "custom_songs"
    };
    if (!name || !base || !*base) return NULL;
    const char *roots[] = {base, is_disc_root(base) ? "mass:/GDPS2" : NULL};
    for (unsigned r = 0; r < sizeof(roots)/sizeof(roots[0]); ++r) {
        if (!roots[r]) continue;
        size_t root_len = strlen(roots[r]);
        const char *separator = roots[r][root_len - 1] == ':' ||
                                roots[r][root_len - 1] == '/' ? "" : "/";
        for (unsigned f = 0; f < sizeof(folders)/sizeof(folders[0]); ++f) {
            char mapped[512];
            int length = snprintf(mapped, sizeof(mapped), "%s%s%s/%s",
                                  roots[r], separator, folders[f], name);
            if (length <= 0 || length >= (int)sizeof(mapped)) continue;
            FILE *song = __real_fopen(mapped, "rb");
            if (song) {
                printf("GDPS2 SONG: custom MP3 opened from %s\n", mapped);
                return song;
            }
        }
    }
    return NULL;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    char mapped[512];
    const char *key = pak_key(path);
    if (key && PS2_PakMounted() && PS2_PakExists(key)) {
        if (!mode || strpbrk(mode, "wa+")) { errno = EROFS; return NULL; }
        return PS2_PakFopen(key);
    }
    if (is_disc_asset(path)) {
        if (!mode || strpbrk(mode, "wa+")) { errno = EROFS; return NULL; }
        struct DiscFopen c = { mode, NULL };
        if (disc_each(path, try_fopen, &c)) return c.file;
        printf("GDPS2 ROMFS: disc open failed original=%s errno=%d\n", path, errno);
        return NULL;
    }
    Ps2PathKind kind = resolve(path, mapped);
    if (kind == PS2_PATH_INVALID || !mode) { errno = EINVAL; return NULL; }
    if (kind == PS2_PATH_ASSET && strpbrk(mode, "wa+")) {
        errno = EROFS; return NULL;
    }
    if (kind == PS2_PATH_SAVE && strpbrk(mode, "wa+")) ensure_parent_dirs(mapped);
    FILE *result = __real_fopen(mapped, mode);
    if (!result && kind == PS2_PATH_SAVE && !strpbrk(mode, "wa+"))
        result = open_external_custom_song(path);
    if (!result && kind == PS2_PATH_SAVE && custom_song_name(path))
        printf("GDPS2 SONG: MP3 missing (selected save path %s)\n", mapped);
    if (!result && kind == PS2_PATH_ASSET)
        printf("GDPS2 ROMFS: open failed original=%s mapped=%s errno=%d\n",
               path, mapped, errno);
    return result;
}

int __wrap_open(const char *path, int flags, int mode)
{
    char mapped[512];
    if (is_disc_asset(path)) {
        if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) { errno = EROFS; return -1; }
        struct DiscOpen c = { flags, mode, -1 };
        return disc_each(path, try_open, &c) ? c.fd : -1;
    }
    Ps2PathKind kind = resolve(path, mapped);
    if (kind == PS2_PATH_INVALID) { errno = EINVAL; return -1; }
    if (kind == PS2_PATH_ASSET && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) {
        errno = EROFS;
        return -1;
    }
    if (kind == PS2_PATH_SAVE && (flags & O_CREAT)) ensure_parent_dirs(mapped);
    return __real_open(mapped, flags, mode);
}

int __wrap_stat(const char *path, struct stat *st)
{
    char mapped[512];
    const char *key = pak_key(path);
    if (key && PS2_PakMounted() && PS2_PakExists(key))
        return PS2_PakStat(key, st);
    if (is_disc_asset(path)) {
        struct DiscStat c = { st };
        if (disc_each(path, try_stat, &c)) return 0;
        errno = ENOENT;
        return -1;
    }
    if (resolve(path, mapped) == PS2_PATH_INVALID) { errno=EINVAL; return -1; }
    return __real_stat(mapped, st);
}

DIR *__wrap_opendir(const char *path)
{
    char mapped[512];
    /* readdir() on cdrom0: returns no entries; never pretend it works. */
    if (is_disc_asset(path)) { errno = ENOTSUP; return NULL; }
    if (resolve(path, mapped) == PS2_PATH_INVALID) { errno=EINVAL; return NULL; }
    return __real_opendir(mapped);
}

int __wrap_access(const char *path, int mode)
{
    char mapped[512];
    const char *key = pak_key(path);
    if (key && PS2_PakMounted() && PS2_PakExists(key)) {
        if (mode & W_OK) { errno = EROFS; return -1; }
        return 0;
    }
    if (is_disc_asset(path)) {
        if (mode & W_OK) { errno = EROFS; return -1; }
        if (disc_each(path, try_exists, NULL)) return 0;
        errno = ENOENT;
        return -1;
    }
    Ps2PathKind kind = resolve(path, mapped);
    if (kind == PS2_PATH_INVALID) { errno=EINVAL; return -1; }
    if (kind == PS2_PATH_ASSET && (mode & W_OK)) { errno=EROFS; return -1; }
    if (__real_access(mapped, mode) == 0) return 0;
    /* PS2 IOP device implementations may reject access() despite allowing
     * fopen("rb"); the popup must report the same result as the loader. */
    if (kind == PS2_PATH_SAVE && !(mode & (W_OK | X_OK)) && custom_song_name(path)) {
        FILE *song = __real_fopen(mapped, "rb");
        if (!song) song = open_external_custom_song(path);
        if (song) { fclose(song); return 0; }
    }
    return -1;
}

int __wrap_remove(const char *path)
{
    char mapped[512];
    if (!can_modify(resolve(path, mapped))) return -1;
    return __real_remove(mapped);
}
int __wrap_unlink(const char *path)
{
    char mapped[512];
    if (!can_modify(resolve(path, mapped))) return -1;
    return __real_unlink(mapped);
}
int __wrap_rename(const char *from, const char *to)
{
    char a[512], b[512];
    if (!can_modify(resolve(from, a)) || !can_modify(resolve(to, b))) return -1;
    return __real_rename(a, b);
}
int __wrap_mkdir(const char *path, mode_t mode)
{
    char mapped[512];
    Ps2PathKind kind = resolve(path, mapped);
    if (!can_modify(kind)) return -1;
    if (kind == PS2_PATH_SAVE) ensure_parent_dirs(mapped);
    return __real_mkdir(mapped, mode);
}
