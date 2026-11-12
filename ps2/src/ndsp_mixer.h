#pragma once
#include <3ds.h>

/* Platform-independent PCM engine. The caller serializes access (the PS2
 * backend uses a kernel semaphore); it does not allocate or own wave data. */
#define PS2_NDSP_OUTPUT_RATE 44100

typedef struct {
    ndspWaveBuf *head, *tail;
    float position;
    float rate, gain_l, gain_r;
    u16 format;
    bool paused;
} Ps2NdspChannel;

typedef struct {
    Ps2NdspChannel channel[NDSP_NUM_CHANNELS];
} Ps2NdspMixer;

void PS2_MixerInit(Ps2NdspMixer *mixer);
void PS2_MixerReset(Ps2NdspMixer *mixer, int id);
void PS2_MixerSetRate(Ps2NdspMixer *mixer, int id, float rate);
void PS2_MixerSetFormat(Ps2NdspMixer *mixer, int id, u16 format);
void PS2_MixerSetMix(Ps2NdspMixer *mixer, int id, const float mix[12]);
void PS2_MixerSetPaused(Ps2NdspMixer *mixer, int id, bool paused);
bool PS2_MixerIsPaused(const Ps2NdspMixer *mixer, int id);
bool PS2_MixerIsPlaying(const Ps2NdspMixer *mixer, int id);
void PS2_MixerQueue(Ps2NdspMixer *mixer, int id, ndspWaveBuf *buffer);
void PS2_MixerRender(Ps2NdspMixer *mixer, s16 *interleaved_stereo, size_t frames);
