/* Standalone PS2 mixer CPU benchmark: no GS, mpg123, audsrv or threads.
 * Not an alternative GD engine. Intended to isolate EE compute from stalls. */
#include <stdio.h>
#include <timer.h>
#include <kernel.h>
#include "ndsp_mixer.h"

static Ps2NdspMixer mixer;
static s16 samples[4096 * 2] __attribute__((aligned(64)));
static s16 output[512 * 2] __attribute__((aligned(64)));

int main(void)
{
    for (int i=0; i<4096; ++i) {
        samples[2*i] = (s16)((i*73)%30000 - 15000);
        samples[2*i+1] = (s16)(13000 - (i*101)%26000);
    }
    PS2_MixerInit(&mixer);
    PS2_MixerSetRate(&mixer, 0, 32000.f);
    PS2_MixerSetFormat(&mixer, 0, NDSP_FORMAT_STEREO_PCM16);
    ndspWaveBuf wav = {0};
    wav.data_pcm16 = samples;
    wav.nsamples = 4096;
    wav.looping = true;
    PS2_MixerQueue(&mixer, 0, &wav);
    volatile unsigned checksum = 0;
    puts("GDPS2 MIXPROBE: 256 blocks x 512 frames, song stereo PCM16 32000Hz");
    u64 started = GetTimerSystemTime();
    for (int n=0; n<256; ++n) {
        PS2_MixerRender(&mixer, output, 512);
        checksum += (unsigned short)output[17];
    }
    u64 ticks = GetTimerSystemTime() - started;
    printf("GDPS2 MIXPROBE: finished blocks=256 ms=%llu checksum=%u pos=%g\n",
           (unsigned long long)(ticks / (kBUSCLK / 1000)),
           (unsigned)checksum, mixer.channel[0].position);
    return 0;
}
