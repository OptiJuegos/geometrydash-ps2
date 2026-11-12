/* MinGW lacks fopencookie. Exercise the actual POSIX backend and all cookie
 * read/seek callbacks directly; PS2 links the real fopencookie/newlib. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    ssize_t (*read)(void *, char *, size_t);
    ssize_t (*write)(void *, const char *, size_t);
    int (*seek)(void *, off_t *, int);
    int (*close)(void *);
} cookie_io_functions_t;
FILE *fopencookie(void *cookie, const char *mode, cookie_io_functions_t functions)
{
    (void)cookie; (void)mode; (void)functions;
    return NULL;
}
FILE *__real_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}
#include "../src/asset_pak.c"

typedef struct {
    PakStream *stream;
    FILE *reference;
    uint32_t absolute_offset;
    unsigned seed;
    int passed;
} TestWorker;

static void *test_reads(void *ctx)
{
    TestWorker *worker = ctx;
    unsigned char got[200], expected[200];
    worker->passed = 1;
    for (unsigned i = 0; i < 250; ++i) {
        /* Access overlapping and backward offsets across threads, to check
         * that seek/read on one shared descriptor cannot race another. */
        worker->seed = worker->seed * 1664525u + 1013904223u;
        uint32_t pos = worker->seed % (worker->stream->size - 200u);
        off_t target = (off_t)pos;
        if (pak_seek(worker->stream, &target, SEEK_SET) || target != (off_t)pos ||
            pak_read(worker->stream, (char *)got, sizeof(got)) != sizeof(got) ||
            fseek(worker->reference, worker->absolute_offset + pos, SEEK_SET) ||
            fread(expected, 1, sizeof(expected), worker->reference) != sizeof(expected) ||
            memcmp(got, expected, sizeof(got))) {
            worker->passed = 0;
            break;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: smoke assets.pak\n"); return 2; }
    if (!PS2_PakMount(argv[1]) || !PS2_PakMounted() ||
        !PS2_PakExists("menus/main_menu.txt") ||
        !PS2_PakExists("gfx/startup_logo.p2sheet") ||
        !archive_io || archive_io->fd < 0 || archive_io->file) {
        fprintf(stderr, "FAIL: MCPK mount or POSIX backend\n");
        return 1;
    }
    const PakEntry *entry = lookup("gfx/startup_logo.p2sheet");
    if (!entry || entry->size < 1000) return 3;
    enum { WORKERS = 4 };
    TestWorker workers[WORKERS] = {{0}};
    pthread_t threads[WORKERS];
    for (unsigned i = 0; i < WORKERS; ++i) {
        TestWorker *w = &workers[i];
        w->stream = calloc(1, sizeof(*w->stream));
        if (!w->stream) return 4;
        w->stream->backend = archive_io;
        backend_retain(archive_io);
        w->stream->start = entry->offset;
        w->stream->size = entry->size;
        w->stream->cache = malloc(PAK_READ_AHEAD);
        if (!w->stream->cache) return 4;
        w->reference = fopen(argv[1], "rb");
        if (!w->reference) return 5;
        w->absolute_offset = entry->offset;
        w->seed = i * 17117u + 1;
    }
    if (archive_io->refs != WORKERS + 1) return 6;
    /* Unmount must not invalidate streams already reading the old archive. */
    PS2_PakUnmount();
    if (PS2_PakMounted()) return 7;
    for (unsigned i = 0; i < WORKERS; ++i)
        if (pthread_create(&threads[i], NULL, test_reads, &workers[i])) return 8;
    for (unsigned i = 0; i < WORKERS; ++i) {
        pthread_join(threads[i], NULL);
        if (!workers[i].passed) return 9;
        fclose(workers[i].reference);
        pak_close(workers[i].stream);
    }
    puts("PASS MCPK POSIX shared FD, concurrent seeks/reads, post-unmount streams");
    return 0;
}
