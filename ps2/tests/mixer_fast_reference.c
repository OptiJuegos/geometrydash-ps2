/* Same scenarios compiled twice: normal fast stereo vs reference generic.
 * The complete output, wave-buffer statuses and final phase must match. */
#include "ndsp_mixer.h"
#include <stdio.h>
#include <string.h>

static void run(int rate, int has_sfx, int looping, int count)
{
    Ps2NdspMixer m;
    PS2_MixerInit(&m);
    static s16 pcm0[88], pcm1[62], sfx[10];
    for (int i=0; i<44; ++i) {
        pcm0[2*i] = (s16)((i * 631) % 41000 - 20500);
        pcm0[2*i+1] = (s16)(18500 - (i * 501) % 37000);
    }
    for (int i=0; i<31; ++i) {
        pcm1[2*i] = (s16)(13000 - (i * 917) % 26000);
        pcm1[2*i+1] = (s16)((i * 1103) % 28000 - 14000);
    }
    for (int i=0;i<10;++i) sfx[i] = (s16)(i * 701 - 2000);
    ndspWaveBuf first = {0}, second = {0}, effect = {0};
    first.data_pcm16 = pcm0; first.nsamples = 44;
    second.data_pcm16 = pcm1; second.nsamples = 31;
    second.looping = looping != 0;
    PS2_MixerSetFormat(&m, 0, NDSP_FORMAT_STEREO_PCM16);
    PS2_MixerSetRate(&m, 0, (float)rate);
    float gains[12] = {0.85f, 0.4f};
    PS2_MixerSetMix(&m, 0, gains);
    PS2_MixerQueue(&m, 0, &first);
    PS2_MixerQueue(&m, 0, &second);
    if (has_sfx) {
        effect.data_pcm16 = sfx; effect.nsamples = 10;
        PS2_MixerQueue(&m, 1, &effect);
    }
    for (int offset=0; offset<count; offset+=16) {
        s16 out[32];
        size_t n = (size_t)((count-offset) < 16 ? (count-offset) : 16);
        PS2_MixerRender(&m, out, n);
        for (size_t j=0;j<n*2;++j) printf("%d ", (int)out[j]);
    }
    printf("status %u %u %u phase %.9g head %d\n",
           first.status, second.status, effect.status,
           m.channel[0].position, m.channel[0].head != NULL);
}

int main(void)
{
    run(32000, 0, 0, 150);
    run(32000, 0, 1, 150);
    run(44100, 0, 0, 95);
    run(48000, 0, 1, 119);
    run(32000, 1, 0, 150);
    run(32000, 1, 1, 150);
    return 0;
}
