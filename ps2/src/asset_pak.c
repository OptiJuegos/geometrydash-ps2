#define _GNU_SOURCE
#include "asset_pak.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__PS2__)
#include <kernel.h>
#else
#include <pthread.h>
#endif

#define MAX_ENTRIES 100000u
#define MAX_NAMES (16u * 1024u * 1024u)
/* A bounded per-stream read-ahead cache. Optical media is especially slow
 * when fopencookie/newlib feeds a large texture a tiny chunk at a time. */
#define PAK_READ_AHEAD (16u * 1024u)
typedef struct {
    uint32_t hash, name, offset, size;
} PakEntry;
typedef struct {
    int fd;
    FILE *file;
    unsigned refs;
#if defined(__PS2__)
    int semaphore;
#else
    pthread_mutex_t mutex;
#endif
} PakBackend;
typedef struct {
    PakBackend *backend;
    uint32_t start, size, position;
    unsigned char *cache;
    uint32_t cache_start;
    size_t cache_length;
} PakStream;

static PakEntry *entries;
static uint32_t entry_count;
static char *names;
static char archive_path[512];
static PakBackend *archive_io;

/* Use the actual libc fopen, bypassing the ROMFS path wrapper. */
extern FILE *__real_fopen(const char *, const char *);

static void backend_lock(PakBackend *io)
{
#if defined(__PS2__)
    WaitSema(io->semaphore);
#else
    pthread_mutex_lock(&io->mutex);
#endif
}

static void backend_unlock(PakBackend *io)
{
#if defined(__PS2__)
    SignalSema(io->semaphore);
#else
    pthread_mutex_unlock(&io->mutex);
#endif
}

static PakBackend *backend_open(const char *path, uint32_t *length)
{
    PakBackend *io = calloc(1, sizeof(*io));
    if (!io) return NULL;
    io->fd = -1;
#if defined(__PS2__)
    ee_sema_t params = {0};
    params.init_count = 1;
    params.max_count = 1;
    io->semaphore = CreateSema(&params);
    if (io->semaphore < 0) { free(io); return NULL; }
#else
    if (pthread_mutex_init(&io->mutex, NULL)) { free(io); return NULL; }
#endif
    io->refs = 1;
    /* Try the POSIX descriptor, like Minecraft. Some PS2 virtual drives
     * cannot seek reliably; preserve the known-working stdio fallback. */
    int flags = O_RDONLY;
#ifdef O_BINARY
    flags |= O_BINARY; /* MinGW host tests must not translate CRLF. */
#endif
    int fd = open(path, flags);
    if (fd >= 0) {
        off_t end = lseek(fd, 0, SEEK_END);
        unsigned char signature[4];
        if (end >= 32 && (uint64_t)end <= UINT32_MAX &&
            lseek(fd, 0, SEEK_SET) == 0 &&
            read(fd, signature, sizeof(signature)) == sizeof(signature) &&
            memcmp(signature, "MCPK", sizeof(signature)) == 0 &&
            lseek(fd, 0, SEEK_SET) == 0) {
            io->fd = fd;
            *length = (uint32_t)end;
            return io;
        }
        close(fd);
    }
    io->file = __real_fopen(path, "rb");
    if (io->file && !fseek(io->file, 0, SEEK_END)) {
        long end = ftell(io->file);
        if (end >= 32 && (uint64_t)end <= UINT32_MAX &&
            !fseek(io->file, 0, SEEK_SET)) {
            *length = (uint32_t)end;
            return io;
        }
    }
    if (io->file) fclose(io->file);
#if defined(__PS2__)
    DeleteSema(io->semaphore);
#else
    pthread_mutex_destroy(&io->mutex);
#endif
    free(io);
    return NULL;
}

static void backend_retain(PakBackend *io)
{
    backend_lock(io);
    ++io->refs;
    backend_unlock(io);
}

static void backend_release(PakBackend *io)
{
    if (!io) return;
    backend_lock(io);
    const int last = --io->refs == 0;
    backend_unlock(io);
    if (!last) return;
    if (io->fd >= 0) close(io->fd);
    if (io->file) fclose(io->file);
#if defined(__PS2__)
    DeleteSema(io->semaphore);
#else
    pthread_mutex_destroy(&io->mutex);
#endif
    free(io);
}

/* The seek and read are one locked operation: mixer and renderer can read
 * concurrently from independent virtual files but the same underlying FD. */
static size_t backend_read(PakBackend *io, uint32_t offset,
                           void *destination, size_t length)
{
    backend_lock(io);
    size_t count = 0;
    if (io->fd >= 0) {
        if (lseek(io->fd, (off_t)offset, SEEK_SET) == (off_t)offset) {
            while (count < length) {
                ssize_t got = read(io->fd, (char *)destination + count, length - count);
                if (got <= 0) break;
                count += (size_t)got;
            }
        }
    } else if (!fseek(io->file, (long)offset, SEEK_SET)) {
        count = fread(destination, 1, length, io->file);
    }
    backend_unlock(io);
    return count;
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int equal_key(const char *stored, const char *key)
{
    while (*stored && *key) {
        char a = *stored++, b = *key++;
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (tolower((unsigned char)a) != tolower((unsigned char)b))
            return 0;
    }
    return *stored == 0 && *key == 0;
}

static uint32_t key_hash(const char *key)
{
    uint32_t hash = 0x811c9dc5u;
    while (*key) {
        unsigned char c = (unsigned char)*key++;
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        hash = (hash ^ c) * 0x01000193u;
    }
    return hash;
}

static const PakEntry *lookup(const char *key)
{
    if (!entries || !key || !*key) return NULL;
    while (*key == '/') ++key;
    const uint32_t hash = key_hash(key);
    uint32_t lo = 0, hi = entry_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (entries[mid].hash < hash) lo = mid + 1;
        else hi = mid;
    }
    for (; lo < entry_count && entries[lo].hash == hash; ++lo)
        if (equal_key(names + entries[lo].name, key)) return &entries[lo];
    return NULL;
}

static int directory_exists(const char *key)
{
    if (!entries || !key) return 0;
    while (*key == '/') ++key;
    size_t len = strlen(key);
    while (len && key[len - 1] == '/') --len;
    if (!len) return 1;
    for (uint32_t i = 0; i < entry_count; ++i) {
        const char *name = names + entries[i].name;
        size_t j = 0;
        while (j < len && name[j] &&
               tolower((unsigned char)name[j]) == tolower((unsigned char)key[j]))
            ++j;
        if (j == len && name[j] == '/') return 1;
    }
    return 0;
}

void PS2_PakUnmount(void)
{
    free(entries);
    free(names);
    entries = NULL;
    names = NULL;
    entry_count = 0;
    archive_path[0] = 0;
    PakBackend *io = archive_io;
    archive_io = NULL;
    /* Open virtual streams retain their descriptor until fclose(). */
    backend_release(io);
}

int PS2_PakMounted(void) { return archive_path[0] != 0; }

int PS2_PakMount(const char *path)
{
    if (!path || !*path || strlen(path) >= sizeof(archive_path)) return 0;
    /* The index and the selected archive path are cached for this session.
     * Never re-open/re-parse the same archive merely to probe it again. */
    if (PS2_PakMounted()) return strcmp(archive_path, path) == 0;
    PS2_PakUnmount();
    uint32_t file_size = 0;
    PakBackend *io = backend_open(path, &file_size);
    if (!io) return 0;
    unsigned char header[32];
    unsigned char *table_data = NULL;
    uint32_t count, table, name_start, name_bytes;
    int ok = 0;
    if (backend_read(io, 0, header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "MCPK", 4) || be32(header + 4) != 1) goto done;
    count = be32(header + 8);
    table = be32(header + 12);
    name_start = be32(header + 16);
    name_bytes = be32(header + 20);
    if (!count || count > MAX_ENTRIES || name_bytes > MAX_NAMES ||
        table < 32 || (uint64_t)table + (uint64_t)count * 16u > name_start ||
        (uint64_t)name_start + name_bytes > file_size) goto done;
    entries = calloc(count, sizeof(*entries));
    names = malloc((size_t)name_bytes + 1);
    table_data = malloc((size_t)count * 16u);
    if (!entries || !names || !table_data) goto done;
    if (backend_read(io, name_start, names, name_bytes) != name_bytes) goto done;
    names[name_bytes] = 0;
    /* Minecraft also reads the index in one contiguous transfer. Avoid
     * 176 tiny individual lseek/read cycles before showing a loading screen. */
    if (backend_read(io, table, table_data, (size_t)count * 16u) != (size_t)count * 16u)
        goto done;
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char *raw = table_data + (size_t)i * 16u;
        PakEntry *e = &entries[i];
        e->hash = be32(raw);
        e->name = be32(raw + 4);
        e->offset = be32(raw + 8);
        e->size = be32(raw + 12);
        if (e->name >= name_bytes || !memchr(names + e->name, 0, name_bytes - e->name) ||
            (uint64_t)e->offset + e->size > file_size ||
            (i && entries[i - 1].hash > e->hash)) goto done;
    }
    entry_count = count;
    memcpy(archive_path, path, strlen(path) + 1);
    archive_io = io;
    printf("GDPS2 PAK: mounted %s (%u entries, %s persistent descriptor)\n",
           path, (unsigned)count, io->fd >= 0 ? "POSIX" : "stdio");
    ok = 1;
done:
    free(table_data);
    if (!ok) {
        backend_release(io);
        PS2_PakUnmount();
    }
    return ok;
}

int PS2_PakExists(const char *key)
{
    return lookup(key) != NULL || directory_exists(key);
}

int PS2_PakStat(const char *key, struct stat *st)
{
    const PakEntry *e = lookup(key);
    if (!e && !directory_exists(key)) { errno = ENOENT; return -1; }
    if (!st) { errno = EINVAL; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = (e ? S_IFREG | 0444 : S_IFDIR | 0555);
    st->st_size = e ? (off_t)e->size : 0;
    return 0;
}

static ssize_t pak_read(void *cookie, char *buffer, size_t length)
{
    PakStream *stream = cookie;
    if (stream->position >= stream->size) return 0;
    if (length > stream->size - stream->position)
        length = stream->size - stream->position;
    if (!stream->cache) {
        /* Low-memory fallback: identical semantics, no extra allocation. */
        size_t n = backend_read(stream->backend, stream->start + stream->position,
                                buffer, length);
        stream->position += (uint32_t)n;
        return n ? (ssize_t)n : -1;
    }
    size_t copied = 0;
    while (copied < length) {
        const uint32_t pos = stream->position;
        if (pos < stream->cache_start ||
            pos - stream->cache_start >= stream->cache_length) {
            /* Only seek/fetch when leaving this 16-KiB region. Every small
             * subsequent read is served from EE RAM, not another CD seek. */
            const size_t want = stream->size - pos > PAK_READ_AHEAD
                ? PAK_READ_AHEAD : (size_t)(stream->size - pos);
            stream->cache_start = pos;
            stream->cache_length = backend_read(stream->backend,
                stream->start + pos, stream->cache, want);
            if (stream->cache_length == 0) {
                if (copied) break;
                return -1;
            }
        }
        const size_t available = stream->cache_length -
            (size_t)(pos - stream->cache_start);
        const size_t take = length - copied < available
            ? length - copied : available;
        memcpy(buffer + copied,
               stream->cache + (size_t)(pos - stream->cache_start), take);
        copied += take;
        stream->position += (uint32_t)take;
    }
    return (ssize_t)copied;
}

static int pak_seek(void *cookie, off_t *offset, int whence)
{
    PakStream *stream = cookie;
    int64_t target = *offset;
    if (whence == SEEK_CUR) target += stream->position;
    else if (whence == SEEK_END) target += stream->size;
    else if (whence != SEEK_SET) { errno = EINVAL; return -1; }
    if (target < 0 || target > stream->size) { errno = EINVAL; return -1; }
    stream->position = (uint32_t)target;
    *offset = (off_t)target;
    return 0;
}

static int pak_close(void *cookie)
{
    PakStream *stream = cookie;
    backend_release(stream->backend);
    free(stream->cache);
    free(stream);
    return 0;
}

FILE *PS2_PakFopen(const char *key)
{
    const PakEntry *e = lookup(key);
    if (!e) { errno = ENOENT; return NULL; }
    PakStream *stream = malloc(sizeof(*stream));
    if (!stream) return NULL;
    if (!archive_io) { free(stream); errno = ENOENT; return NULL; }
    stream->backend = archive_io;
    backend_retain(stream->backend);
    stream->start = e->offset;
    stream->size = e->size;
    stream->position = 0;
    stream->cache = malloc(PAK_READ_AHEAD);
    stream->cache_start = 0;
    stream->cache_length = 0;
    cookie_io_functions_t callbacks = { pak_read, NULL, pak_seek, pak_close };
    FILE *f = fopencookie(stream, "rb", callbacks);
    if (!f) pak_close(stream);
    return f;
}
