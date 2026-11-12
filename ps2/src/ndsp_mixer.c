#include "ndsp_mixer.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#ifdef PS2_AUDIO_DIAGNOSTIC
#include <stdio.h>
#endif

static bool valid_channel(int id) { return id >= 0 && id < NDSP_NUM_CHANNELS; }

static int frame_channels(const Ps2NdspChannel *c)
{
    return (c->format & 3) == 2 ? 2 : 1;
}

static int encoding(const Ps2NdspChannel *c) { return (c->format >> 2) & 3; }

static bool supported(const Ps2NdspChannel *c)
{
    return ((c->format & 3) == 1 || (c->format & 3) == 2) &&
           (encoding(c) == NDSP_ENCODING_PCM16 || encoding(c) == NDSP_ENCODING_PCM8);
}

static float sample(const Ps2NdspChannel *c, const ndspWaveBuf *b, u32 index, int channel)
{
    size_t offset = ((size_t)index + b->offset) * (size_t)frame_channels(c) +
                    (frame_channels(c) == 2 ? (size_t)channel : 0);
    if (encoding(c) == NDSP_ENCODING_PCM16) return b->data_pcm16[offset];
    /* NDSP PCM8 is signed; convert to full-scale signed 16-bit. */
    return (float)b->data_pcm8[offset] * 256.f;
}

static void finish_head(Ps2NdspChannel *c)
{
    ndspWaveBuf *old = c->head;
    if (!old) return;
    if (old->looping && old->nsamples) return;
    c->head = old->next;
    old->next = NULL;
    old->status = NDSP_WBUF_DONE;
    if (c->head) c->head->status = NDSP_WBUF_PLAYING;
    else { c->tail = NULL; c->position = 0; }
}

void PS2_MixerReset(Ps2NdspMixer *m, int id)
{
    if (!m || !valid_channel(id)) return;
    Ps2NdspChannel *c = &m->channel[id];
    ndspWaveBuf *b = c->head;
    while (b) {
        ndspWaveBuf *next = b->next;
        b->status = NDSP_WBUF_DONE;
        b->next = NULL;
        b = next;
    }
    memset(c, 0, sizeof(*c));
    c->rate = PS2_NDSP_OUTPUT_RATE;
    c->format = NDSP_FORMAT_MONO_PCM16;
    c->gain_l = c->gain_r = 1.f;
}

void PS2_MixerInit(Ps2NdspMixer *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    for (int i=0; i<NDSP_NUM_CHANNELS; ++i) PS2_MixerReset(m, i);
}

void PS2_MixerSetRate(Ps2NdspMixer *m, int id, float rate)
{
    if (!m || !valid_channel(id)) return;
    if (isfinite(rate) && rate >= 100.f && rate <= 192000.f)
        m->channel[id].rate = rate;
    else m->channel[id].rate = PS2_NDSP_OUTPUT_RATE;
}

void PS2_MixerSetFormat(Ps2NdspMixer *m, int id, u16 format)
{
    if (!m || !valid_channel(id)) return;
    /* Changing sample interpretation while buffers play is unsafe. Reset the
     * queue before exposing the new data layout to the mixer. */
    if (m->channel[id].format != format && m->channel[id].head)
        PS2_MixerReset(m, id);
    m->channel[id].format = format;
}

void PS2_MixerSetMix(Ps2NdspMixer *m, int id, const float mix[12])
{
    if (!m || !valid_channel(id) || !mix) return;
    Ps2NdspChannel *c = &m->channel[id];
    c->gain_l = isfinite(mix[0]) && mix[0] > 0.f ?
        (mix[0] < 4.f ? mix[0] : 4.f) : 0.f;
    c->gain_r = isfinite(mix[1]) && mix[1] > 0.f ?
        (mix[1] < 4.f ? mix[1] : 4.f) : 0.f;
}

void PS2_MixerSetPaused(Ps2NdspMixer *m, int id, bool paused)
{
    if (m && valid_channel(id)) m->channel[id].paused = paused;
}

bool PS2_MixerIsPaused(const Ps2NdspMixer *m, int id)
{
    return m && valid_channel(id) && m->channel[id].paused;
}

bool PS2_MixerIsPlaying(const Ps2NdspMixer *m, int id)
{
    return m && valid_channel(id) && m->channel[id].head && !m->channel[id].paused;
}

void PS2_MixerQueue(Ps2NdspMixer *m, int id, ndspWaveBuf *b)
{
    if (!b) return;
    if (!m || !valid_channel(id)) {
#ifdef PS2_AUDIO_DIAGNOSTIC
        printf("GDPS2 QUEUE: bad channel id=%d\n", id);
#endif
        b->status = NDSP_WBUF_DONE; return;
    }
    Ps2NdspChannel *c = &m->channel[id];
    if (!supported(c) || !b->data_vaddr || !b->nsamples) {
#ifdef PS2_AUDIO_DIAGNOSTIC
        static unsigned rejected;
        if (rejected++ < 24)
            printf("GDPS2 QUEUE: rejected id=%d format=0x%x rate=%.0f data=%p frames=%lu paused=%d\n",
                   id, c->format, c->rate, b->data_vaddr,
                   (unsigned long)b->nsamples, c->paused);
#endif
        b->status = NDSP_WBUF_DONE; return;
    }
    /* A second enqueue of a still-active buffer would create a self-cycle. */
    for (ndspWaveBuf *it=c->head; it; it=it->next)
        if (it == b) return;
    b->next = NULL;
    b->status = NDSP_WBUF_QUEUED;
    if (c->tail) c->tail->next = b;
    else {
        c->head = b;
        c->position = 0;
        b->status = NDSP_WBUF_PLAYING;
    }
    c->tail = b;
}

static s16 soft_clip(int v)
{
    const int knee = 28672;
    if (v > knee) {
        int x = v - knee, room = 32767 - knee;
        v = knee + (int)((int64_t)room * x / (x + room));
    } else if (v < -knee) {
        int x = -v - knee, room = 32767 - knee;
        v = -(knee + (int)((int64_t)room * x / (x + room)));
    }
    return (s16)v;
}

/* Fast path for the normal song playback case: one active stereo PCM16
 * channel. Keep the same float interpolation, soft clipping, wave-buffer
 * status transitions and sample positions as the general NDSP mixer, but
 * avoid the per-sample 24-channel scan and repeated sample-format dispatch.
 * Any concurrent SFX or different format uses the original general path. */
#ifndef PS2_MIXER_DISABLE_FAST_STEREO
#ifndef PS2_MIXER_FAST_FIXED
static void render_single_stereo_pcm16(Ps2NdspChannel *c, s16 *out, size_t frames)
{
    const float step = c->rate / (float)PS2_NDSP_OUTPUT_RATE;
    const float gain_l = c->gain_l;
    const float gain_r = c->gain_r;
    for (size_t i = 0; i < frames; ++i) {
        ndspWaveBuf *b = c->head;
        while (b && c->position >= b->nsamples) {
            c->position -= b->nsamples;
            if (b->looping) break;
            finish_head(c);
            b = c->head;
        }
        if (!b || !b->nsamples) {
            out[2*i] = out[2*i+1] = 0;
            continue;
        }
        const u32 idx = (u32)c->position;
        if (idx >= b->nsamples) {
            out[2*i] = out[2*i+1] = 0;
            continue;
        }
        const float frac = c->position - (float)idx;
        const s16 *data = b->data_pcm16 + (size_t)(b->offset + idx)*2u;
        const float l = (float)data[0], r = (float)data[1];
        float l2 = l, r2 = r;
        if (idx + 1u < b->nsamples) {
            l2 = (float)data[2];
            r2 = (float)data[3];
        } else {
            ndspWaveBuf *next = b->looping ? b : b->next;
            if (next && next->nsamples) {
                const s16 *next_pcm = next->data_pcm16 + (size_t)next->offset*2u;
                l2 = (float)next_pcm[0];
                r2 = (float)next_pcm[1];
            }
        }
        float ml = (l + (l2 - l)*frac)*gain_l;
        float mr = (r + (r2 - r)*frac)*gain_r;
        if (ml > 1000000.f) ml = 1000000.f;
        if (ml < -1000000.f) ml = -1000000.f;
        if (mr > 1000000.f) mr = 1000000.f;
        if (mr < -1000000.f) mr = -1000000.f;
        out[2*i] = soft_clip((int)ml);
        out[2*i+1] = soft_clip((int)mr);

        c->position += step;
        while (c->head && c->position >= c->head->nsamples) {
            ndspWaveBuf *done = c->head;
            if (done->looping) {
                c->position = fmodf(c->position, (float)done->nsamples);
                break;
            }
            c->position -= done->nsamples;
            finish_head(c);
        }
    }
}
#endif
#ifdef PS2_MIXER_FAST_FIXED
/* PS2 experiment: phase in Q16 and linear interpolation/gain in Q12.
 * Avoid the R5900 per-sample float conversions/mul/div in hot loops.
 * No content is resampled offline; the original MP3 remains unchanged.
 * The generic path stays available for overlapping SFX and unusual formats. */
static void render_single_stereo_pcm16_fixed(Ps2NdspChannel *c, s16 *out, size_t frames)
{
    const u64 unit = 65536ULL;
    u64 phase = (u64)(c->position * 65536.f);
    const u64 step = (u64)(c->rate * (65536.f / (float)PS2_NDSP_OUTPUT_RATE) + 0.5f);
    const int gl = (int)(c->gain_l * 4096.f + 0.5f);
    const int gr = (int)(c->gain_r * 4096.f + 0.5f);
    for (size_t i=0; i<frames; ++i) {
        ndspWaveBuf *b = c->head;
        while (b && phase >= (u64)b->nsamples * unit) {
            phase -= (u64)b->nsamples * unit;
            if (b->looping) break;
            finish_head(c);
            b = c->head;
        }
        if (!b || !b->nsamples) {
            out[2*i] = out[2*i+1] = 0;
            phase = 0;
            continue;
        }
        const u32 idx = (u32)(phase >> 16);
        if (idx >= b->nsamples) {
            out[2*i] = out[2*i+1] = 0;
            continue;
        }
        const int frac = (int)((phase >> 4) & 4095u);
        const s16 *data = b->data_pcm16 + ((size_t)b->offset + idx)*2u;
        int l = data[0], r = data[1], l2 = l, r2 = r;
        if (idx + 1u < b->nsamples) {
            l2 = data[2]; r2 = data[3];
        } else {
            ndspWaveBuf *next = b->looping ? b : b->next;
            if (next && next->nsamples) {
                const s16 *s = next->data_pcm16 + (size_t)next->offset*2u;
                l2 = s[0]; r2 = s[1];
            }
        }
        const int interp_l = l + ((l2-l)*frac >> 12);
        const int interp_r = r + ((r2-r)*frac >> 12);
        out[2*i] = soft_clip((interp_l * gl) >> 12);
        out[2*i+1] = soft_clip((interp_r * gr) >> 12);
        phase += step;
        while (c->head && phase >= (u64)c->head->nsamples * unit) {
            ndspWaveBuf *done = c->head;
            phase -= (u64)done->nsamples * unit;
            if (done->looping) {
                while (phase >= (u64)done->nsamples * unit)
                    phase -= (u64)done->nsamples * unit;
                break;
            }
            finish_head(c);
        }
    }
    c->position = (float)phase / 65536.f;
}
#endif
#endif

/* Process one channel at a time, so there is no large intermediate stack or
 * a second full-sized mix buffer. Each block is short enough to keep the
 * maximum gain sum within int32_t even with 24 simultaneous channels. */
void PS2_MixerRender(Ps2NdspMixer *m, s16 *out, size_t frames)
{
    if (!m || !out) return;
#ifndef PS2_MIXER_DISABLE_FAST_STEREO
    Ps2NdspChannel *only = NULL;
    int active_channels = 0;
    for (int j=0; j<NDSP_NUM_CHANNELS; ++j) {
        Ps2NdspChannel *c = &m->channel[j];
        if (!c->paused && c->head && supported(c)) {
            ++active_channels;
            only = c;
            if (active_channels > 1) break;
        }
    }
    if (active_channels == 1 && only && only->format == NDSP_FORMAT_STEREO_PCM16) {
#ifdef PS2_MIXER_FAST_FIXED
        render_single_stereo_pcm16_fixed(only, out, frames);
#else
        render_single_stereo_pcm16(only, out, frames);
#endif
        return;
    }
#endif
    /* Chunks are bounded by the driver (512); support larger calls in tests.
     * Use 64-bit integer accumulators to avoid clipping between channels. */
#ifdef PS2_MIXER_ENABLE_STEP_CACHE
    /* The playback rate is fixed for this mixer block. Compute the divisor
     * once per active channel, rather than once per PCM frame and channel.
     * This is an opt-in experiment until measured on real PS2 hardware. */
    float step_per_channel[NDSP_NUM_CHANNELS];
    for (int j = 0; j < NDSP_NUM_CHANNELS; ++j) {
        const Ps2NdspChannel *c = &m->channel[j];
        if (!c->paused && c->head && supported(c))
            step_per_channel[j] = c->rate / (float)PS2_NDSP_OUTPUT_RATE;
    }
#endif
    for (size_t i=0; i<frames; ++i) {
        /* R5900 has a native single-precision FPU but not a double FPU.
         * Double arithmetic in this 512-frame inner loop calls expensive
         * software helpers on PS2. Wave positions never grow beyond a
         * single buffer, so float precision is sufficient for resampling. */
        float l = 0.f, r = 0.f;
        for (int j=0; j<NDSP_NUM_CHANNELS; ++j) {
            Ps2NdspChannel *c = &m->channel[j];
            if (c->paused || !c->head || !supported(c)) continue;
            ndspWaveBuf *b = c->head;
            while (b && c->position >= b->nsamples) {
                c->position -= b->nsamples;
                if (b->looping) break;
                finish_head(c);
                b = c->head;
            }
            if (!b || !b->nsamples) continue;
            u32 idx = (u32)c->position;
            if (idx >= b->nsamples) continue;
            float frac = (float)(c->position - idx);
            float left = sample(c, b, idx, 0);
            float right = sample(c, b, idx, 1);
            float left2 = left, right2 = right;
            if (idx + 1 < b->nsamples) {
                left2 = sample(c, b, idx+1, 0);
                right2 = sample(c, b, idx+1, 1);
            } else if (!b->looping && b->next && b->next->nsamples) {
                left2 = sample(c, b->next, 0, 0);
                right2 = sample(c, b->next, 0, 1);
            } else if (b->looping) {
                left2 = sample(c, b, 0, 0);
                right2 = sample(c, b, 0, 1);
            }
            l += (left + (left2-left)*frac) * c->gain_l;
            r += (right + (right2-right)*frac) * c->gain_r;
#ifdef PS2_MIXER_ENABLE_STEP_CACHE
            c->position += step_per_channel[j];
#else
            c->position += c->rate / (float)PS2_NDSP_OUTPUT_RATE;
#endif
            while (c->head && c->position >= c->head->nsamples) {
                ndspWaveBuf *done = c->head;
                if (done->looping) {
                    /* Fractional phase must wrap even if step > length. */
                    c->position = fmodf(c->position, (float)done->nsamples);
                    break;
                }
                c->position -= done->nsamples;
                finish_head(c);
            }
        }
        if (l > 1000000.f) l = 1000000.f;
        if (l < -1000000.f) l = -1000000.f;
        if (r > 1000000.f) r = 1000000.f;
        if (r < -1000000.f) r = -1000000.f;
        out[2*i] = soft_clip((int)l);
        out[2*i+1] = soft_clip((int)r);
    }
}
