#include "mp3_decode_flow.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "mp3 flow test failed line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

typedef struct { int status; size_t bytes; } FakeRead;

/* Deterministic mpg123_read stub: returns a status and a byte count. */
static int fake_mpg123_read(const FakeRead *item, size_t *done)
{
    *done = item->bytes;
    return item->status;
}

static Gdps2Mp3Action step(FakeRead item, unsigned channels)
{
    size_t bytes = 0;
    int status = fake_mpg123_read(&item, &bytes);
    return gdps2_mp3_action(status, bytes, 8192, channels);
}

int main(void)
{
    CHECK(step((FakeRead){MPG123_OK, 8192}, 2) == GDPS2_MP3_DATA);
    CHECK(step((FakeRead){MPG123_OK, 0}, 2) == GDPS2_MP3_RETRY);
    CHECK(step((FakeRead){MPG123_DONE, 0}, 2) == GDPS2_MP3_EOF);
    CHECK(step((FakeRead){MPG123_DONE, 4096}, 2) == GDPS2_MP3_DATA);
    CHECK(step((FakeRead){MPG123_NEW_FORMAT, 0}, 2) == GDPS2_MP3_FORMAT);
    CHECK(step((FakeRead){MPG123_NEW_FORMAT, 1024}, 2) == GDPS2_MP3_FORMAT);
    CHECK(step((FakeRead){MPG123_NEED_MORE, 0}, 2) == GDPS2_MP3_ERROR);
    CHECK(step((FakeRead){MPG123_ERR, 0}, 2) == GDPS2_MP3_ERROR);
    CHECK(step((FakeRead){MPG123_OK, 3}, 2) == GDPS2_MP3_ERROR);
    CHECK(step((FakeRead){MPG123_OK, 8193}, 2) == GDPS2_MP3_ERROR);
    CHECK(step((FakeRead){MPG123_OK, 8192}, 0) == GDPS2_MP3_ERROR);
    CHECK(step((FakeRead){MPG123_OK, 2048}, 1) == GDPS2_MP3_DATA);

    /* Looping EOF: nothing is enqueued at zero frames. Seek the decoder and
     * retry, without dropping the already queued tail of the track. */
    const FakeRead sequence[] = {
        {MPG123_DONE, 0}, {MPG123_NEW_FORMAT, 0}, {MPG123_OK, 8192}
    };
    int seeks = 0, enqueues = 0;
    for (size_t i = 0; i < sizeof(sequence)/sizeof(sequence[0]); ++i) {
        Gdps2Mp3Action a = step(sequence[i], 2);
        if (a == GDPS2_MP3_EOF) { ++seeks; continue; }
        if (a == GDPS2_MP3_FORMAT || a == GDPS2_MP3_RETRY) continue;
        if (a == GDPS2_MP3_DATA && sequence[i].bytes) ++enqueues;
    }
    CHECK(seeks == 1 && enqueues == 1);

    puts("PASS: mpg123 bytes/status, EOF, looping, NEW_FORMAT, NEED_MORE, zero progress");
    return 0;
}
