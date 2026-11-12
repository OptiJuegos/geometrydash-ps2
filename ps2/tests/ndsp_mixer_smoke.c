#include "ndsp_mixer.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "NDSP smoke failed on line %d: %s\n", __LINE__, #expr); \
    return 1; \
} } while (0)

static ndspWaveBuf wav16(s16 *samples, unsigned frames)
{
    ndspWaveBuf b = {0};
    b.data_pcm16 = samples;
    b.nsamples = frames;
    return b;
}

int main(void)
{
    Ps2NdspMixer mixer;
    PS2_MixerInit(&mixer);
    s16 output[24] = {0};
    s16 mono[] = {1000, 2000, 3000, 4000};
    ndspWaveBuf a = wav16(mono, 4);
    PS2_MixerQueue(&mixer, 0, &a);
    CHECK(a.status == NDSP_WBUF_PLAYING);
    CHECK(PS2_MixerIsPlaying(&mixer, 0));
    PS2_MixerRender(&mixer, output, 2);
    CHECK(output[0] == 1000 && output[1] == 1000);
    CHECK(output[2] == 2000 && output[3] == 2000);
    CHECK(a.status == NDSP_WBUF_PLAYING);
    PS2_MixerRender(&mixer, output, 2);
    CHECK(output[0] == 3000 && output[2] == 4000);
    CHECK(a.status == NDSP_WBUF_DONE && !PS2_MixerIsPlaying(&mixer, 0));
    PS2_MixerRender(&mixer, output, 2);
    CHECK(output[0] == 0 && output[1] == 0);

    /* A waiting buffer transitions to playing at the exact boundary. */
    PS2_MixerReset(&mixer, 0);
    s16 x[] = {1000}, y[] = {2000};
    ndspWaveBuf one = wav16(x, 1), two = wav16(y, 1);
    PS2_MixerQueue(&mixer, 0, &one);
    PS2_MixerQueue(&mixer, 0, &two);
    CHECK(two.status == NDSP_WBUF_QUEUED);
    PS2_MixerQueue(&mixer, 0, &two); /* duplicate is rejected */
    CHECK(one.next == &two && two.next == NULL);
    PS2_MixerRender(&mixer, output, 1);
    CHECK(one.status == NDSP_WBUF_DONE && two.status == NDSP_WBUF_PLAYING);
    PS2_MixerRender(&mixer, output, 1);
    CHECK(output[0] == 2000 && two.status == NDSP_WBUF_DONE);

    /* Interpolation at 22050 Hz, output at 44100 Hz. */
    PS2_MixerReset(&mixer, 0);
    PS2_MixerSetRate(&mixer, 0, 22050.f);
    a = wav16(mono, 4);
    PS2_MixerQueue(&mixer, 0, &a);
    PS2_MixerRender(&mixer, output, 4);
    CHECK(output[0] == 1000 && output[2] == 1500);
    CHECK(output[4] == 2000 && output[6] == 2500);
    CHECK(a.status == NDSP_WBUF_PLAYING);

    /* Per-channel volume and pause/resume do not consume queued data. */
    PS2_MixerReset(&mixer, 0);
    float gains[12] = {0.5f, 0.25f};
    PS2_MixerSetMix(&mixer, 0, gains);
    a = wav16(mono, 4);
    PS2_MixerQueue(&mixer, 0, &a);
    PS2_MixerSetPaused(&mixer, 0, true);
    CHECK(PS2_MixerIsPaused(&mixer, 0));
    PS2_MixerRender(&mixer, output, 1);
    CHECK(!output[0] && !output[1] && a.status == NDSP_WBUF_PLAYING);
    PS2_MixerSetPaused(&mixer, 0, false);
    PS2_MixerRender(&mixer, output, 1);
    CHECK(output[0] == 500 && output[1] == 250);

    /* Mono and stereo mixing with pan, signed PCM8 conversion. */
    PS2_MixerInit(&mixer);
    s16 stereo[] = {1000, -500, 2000, -1000};
    a = wav16(stereo, 2);
    PS2_MixerSetFormat(&mixer, 0, NDSP_FORMAT_STEREO_PCM16);
    PS2_MixerQueue(&mixer, 0, &a);
    PS2_MixerRender(&mixer, output, 2);
    CHECK(output[0] == 1000 && output[1] == -500);
    CHECK(output[2] == 2000 && output[3] == -1000);
    CHECK(a.status == NDSP_WBUF_DONE);
    PS2_MixerReset(&mixer, 0);
    s8 pcm8[] = {-10, 10};
    ndspWaveBuf eight = {0};
    eight.data_pcm8 = pcm8;
    eight.nsamples = 2;
    PS2_MixerSetFormat(&mixer, 0, NDSP_FORMAT_MONO_PCM8);
    PS2_MixerQueue(&mixer, 0, &eight);
    PS2_MixerRender(&mixer, output, 2);
    CHECK(output[0] == -2560 && output[2] == 2560);

    /* Loop repeats seamlessly until reset and does not mark DONE early. */
    PS2_MixerReset(&mixer, 0);
    s16 loop_data[] = {100, 200};
    ndspWaveBuf loop = wav16(loop_data, 2);
    loop.looping = true;
    PS2_MixerQueue(&mixer, 0, &loop);
    PS2_MixerRender(&mixer, output, 6);
    CHECK(output[0] == 100 && output[2] == 200);
    CHECK(output[4] == 100 && output[6] == 200);
    CHECK(loop.status == NDSP_WBUF_PLAYING);
    PS2_MixerReset(&mixer, 0);
    CHECK(loop.status == NDSP_WBUF_DONE);

    /* Malformed or unsupported wave input never enters the active queue. */
    ndspWaveBuf invalid = {0};
    PS2_MixerQueue(&mixer, 0, &invalid);
    CHECK(invalid.status == NDSP_WBUF_DONE);
    /* Reproduces the former MP3 EOF path: a valid pointer with zero
     * nsamples must never be inserted into the linked playback queue. */
    PS2_MixerSetFormat(&mixer, 0, NDSP_FORMAT_STEREO_PCM16);
    invalid = wav16(mono, 0);
    PS2_MixerQueue(&mixer, 0, &invalid);
    CHECK(invalid.status == NDSP_WBUF_DONE);
    CHECK(!PS2_MixerIsPlaying(&mixer, 0));
    PS2_MixerSetFormat(&mixer, 0, NDSP_CHANNELS(1) | NDSP_ENCODING(NDSP_ENCODING_ADPCM));
    a = wav16(mono, 4);
    PS2_MixerQueue(&mixer, 0, &a);
    CHECK(a.status == NDSP_WBUF_DONE);

    /* Multiple simultaneous channels add and clip without wraparound. */
    PS2_MixerInit(&mixer);
    s16 peak[] = {30000};
    for (int i=0;i<NDSP_NUM_CHANNELS;++i) {
        static ndspWaveBuf buffers[NDSP_NUM_CHANNELS];
        buffers[i] = wav16(peak, 1);
        PS2_MixerQueue(&mixer, i, &buffers[i]);
    }
    PS2_MixerRender(&mixer, output, 1);
    CHECK(output[0] > 30000 && output[0] <= 32767);
    CHECK(output[1] > 30000 && output[1] <= 32767);

    puts("PASS: NDSP PCM16/PCM8, stereo, interpolation, queues, pause, gain, looping, clipping");
    return 0;
}
