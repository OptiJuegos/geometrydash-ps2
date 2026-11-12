/* Usage: ima_wav_smoke <song.wav> <reference.s16le>
 * The reference is the same WAV decoded by ffmpeg. */
#include "ima_wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "ima wav test failed line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

int main(int argc, char **argv)
{
    ImaWav w;
    FILE *ref;
    int16_t *want;
    long ref_bytes;

    CHECK(argc == 3);
    ref = fopen(argv[2], "rb");
    CHECK(ref);
    fseek(ref, 0, SEEK_END);
    ref_bytes = ftell(ref);
    fseek(ref, 0, SEEK_SET);
    want = (int16_t *)malloc((size_t)ref_bytes);
    CHECK(want && fread(want, 1, (size_t)ref_bytes, ref) == (size_t)ref_bytes);
    fclose(ref);

    CHECK(ima_wav_open(&w, argv[1]));
    CHECK(w.channels == 2 && w.rate == 32000);
    printf("blocks=%ld spb=%d frames=%ld ref_frames=%ld\n", w.blocks, w.spb,
           w.total_frames, ref_bytes / 4);
    /* ffmpeg decodes the padded last block; the fact chunk trims it. */
    CHECK(w.total_frames <= ref_bytes / 4 && w.total_frames > ref_bytes / 4 - w.spb);

    /* Whole file in the 2048-frame reads the mixer asks for. */
    {
        int16_t buf[2048 * 2];
        long frames = 0;
        size_t n;
        while ((n = ima_wav_read(&w, buf, 2048)) > 0) {
            CHECK(memcmp(buf, want + frames * 2, n * 4) == 0);
            frames += (long)n;
        }
        CHECK(frames == w.total_frames);
        CHECK(ima_wav_read(&w, buf, 2048) == 0);
    }

    /* Seeks: block start, mid block, last frame, loop back to 0. */
    {
        long spots[] = { 0, 1, w.spb - 1, w.spb, w.spb + 1, 5 * 32000 + 77,
                         w.total_frames - 1 };
        int16_t buf[300 * 2];
        for (unsigned i = 0; i < sizeof(spots) / sizeof(spots[0]); ++i) {
            long left = w.total_frames - spots[i];
            size_t ask = left < 300 ? (size_t)left : 300;
            CHECK(ima_wav_seek(&w, spots[i]));
            CHECK(ima_wav_read(&w, buf, 300) == ask);
            CHECK(memcmp(buf, want + spots[i] * 2, ask * 4) == 0);
        }
        CHECK(ima_wav_seek(&w, 0));
        CHECK(ima_wav_read(&w, buf, 300) == 300);
        CHECK(memcmp(buf, want, 300 * 4) == 0);
    }

    ima_wav_close(&w);
    CHECK(!ima_wav_open(&w, "does-not-exist.wav"));
    free(want);
    puts("ima wav test ok");
    return 0;
}
