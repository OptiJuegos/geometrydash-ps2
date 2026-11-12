/* IMA-ADPCM WAV (format 0x11) streaming decoder for the PS2 music path.
 *
 * Songs are 4:1 compressed on disc and decoded a block at a time into the
 * same interleaved PCM16 the mixer already takes from mpg123. Blocks have a
 * fixed size, so seeking costs one fseek plus one block decode.
 * Header-only so the host test can build it without the PS2 SDK. */
#ifndef GDPS2_IMA_WAV_H
#define GDPS2_IMA_WAV_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMA_WAV_MAX_ALIGN 8192

typedef struct {
    FILE *f;
    int channels;
    long rate;
    int align;            /* bytes per block */
    int spb;              /* frames per block */
    long data_off;
    long total_frames;
    long next_block;      /* block the next ima_wav_read decodes */
    long file_block;      /* block the file position is at, -1 unknown */
    long blocks;
    uint8_t *blk;
    int16_t *pcm;         /* the decoded block, interleaved */
    int pcm_pos, pcm_len; /* frames */
    long pos;             /* frames handed out */
} ImaWav;

static const int16_t ima_wav_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767
};

static const int8_t ima_wav_index_adjust[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static inline uint32_t ima_wav_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint32_t ima_wav_le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static inline void ima_wav_close(ImaWav *w)
{
    if (w->f) fclose(w->f);
    free(w->blk);
    free(w->pcm);
    memset(w, 0, sizeof(*w));
}

/* Reads and decodes block `index` into w->pcm. Returns 0 past the end. */
static inline int ima_wav_load_block(ImaWav *w, long index)
{
    if (index < 0 || index >= w->blocks) return 0;
    /* Streaming reads blocks in order: an fseek per block would make stdio
     * refill its whole buffer from the disc every time. */
    if (index != w->file_block &&
        fseek(w->f, w->data_off + index * (long)w->align, SEEK_SET) != 0) {
        w->file_block = -1;
        return 0;
    }
    memset(w->blk, 0, (size_t)w->align);
    w->file_block = index + 1;
    if (fread(w->blk, 1, (size_t)w->align, w->f) < (size_t)(4 * w->channels)) return 0;

    const int ch = w->channels;
    int pred[2], idx[2];
    for (int c = 0; c < ch; ++c) {
        pred[c] = (int16_t)ima_wav_le16(w->blk + 4 * c);
        idx[c] = w->blk[4 * c + 2];
        if (idx[c] > 88) idx[c] = 88;
        w->pcm[c] = (int16_t)pred[c];
    }
    /* After the header: 4-byte words (8 nibbles = 8 frames), channels
     * interleaved word by word. Low nibble is the earlier frame. */
    const int groups = (w->spb - 1) / 8;
    for (int g = 0; g < groups; ++g) {
        for (int c = 0; c < ch; ++c) {
            const uint8_t *word = w->blk + 4 * ch + (g * ch + c) * 4;
            for (int k = 0; k < 8; ++k) {
                int n = (word[k >> 1] >> ((k & 1) * 4)) & 15;
                int step = ima_wav_steps[idx[c]];
                /* Same reconstruction as ffmpeg's encoder tracks. */
                int diff = ((2 * (n & 7) + 1) * step) >> 3;
                if (n & 8) diff = -diff;
                pred[c] += diff;
                if (pred[c] > 32767) pred[c] = 32767;
                if (pred[c] < -32768) pred[c] = -32768;
                idx[c] += ima_wav_index_adjust[n & 7];
                if (idx[c] < 0) idx[c] = 0;
                if (idx[c] > 88) idx[c] = 88;
                w->pcm[(1 + g * 8 + k) * ch + c] = (int16_t)pred[c];
            }
        }
    }
    w->pcm_pos = 0;
    w->pcm_len = w->spb;
    w->next_block = index + 1;
    return 1;
}

/* Returns 1 on success. Only mono/stereo 4-bit IMA is accepted. */
static inline int ima_wav_open(ImaWav *w, const char *path)
{
    uint8_t h[40];
    int have_fmt = 0;
    long fact = 0, data_bytes = 0;

    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "rb");
    if (!w->f) return 0;
    /* One block per fread would hit the disc every time without this.
     * It has to come before the first read. */
    setvbuf(w->f, NULL, _IOFBF, 32 * 1024);
    if (fread(h, 1, 12, w->f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        goto fail;

    for (;;) {
        if (fread(h, 1, 8, w->f) != 8) goto fail;
        long size = (long)ima_wav_le32(h + 4);
        long next = ftell(w->f) + size + (size & 1);

        if (!memcmp(h, "fmt ", 4)) {
            if (size < 16 || size > (long)sizeof(h) ||
                fread(h, 1, (size_t)size, w->f) != (size_t)size) goto fail;
            if (ima_wav_le16(h) != 0x11 || ima_wav_le16(h + 14) != 4) goto fail;
            w->channels = (int)ima_wav_le16(h + 2);
            w->rate = (long)ima_wav_le32(h + 4);
            w->align = (int)ima_wav_le16(h + 12);
            w->spb = size >= 20 ? (int)ima_wav_le16(h + 18) : 0;
            have_fmt = 1;
        } else if (!memcmp(h, "fact", 4) && size >= 4) {
            uint8_t b[4];
            if (fread(b, 1, 4, w->f) != 4) goto fail;
            fact = (long)ima_wav_le32(b);
        } else if (!memcmp(h, "data", 4)) {
            if (!have_fmt) goto fail;
            w->data_off = ftell(w->f);
            data_bytes = size;
            break;
        }
        if (fseek(w->f, next, SEEK_SET) != 0) goto fail;
    }

    if (w->channels < 1 || w->channels > 2 || w->rate <= 0 ||
        w->align < 4 * w->channels || w->align > IMA_WAV_MAX_ALIGN ||
        (w->align - 4 * w->channels) % (4 * w->channels) != 0) goto fail;
    if (w->spb <= 0) w->spb = (w->align - 4 * w->channels) * 2 / w->channels + 1;
    if ((w->spb - 1) / 8 * 4 * w->channels + 4 * w->channels > w->align) goto fail;

    w->blocks = (data_bytes + w->align - 1) / w->align;
    w->total_frames = w->blocks * (long)w->spb;
    if (fact > 0 && fact < w->total_frames) w->total_frames = fact;
    w->blk = (uint8_t *)malloc((size_t)w->align);
    w->pcm = (int16_t *)malloc((size_t)w->spb * (size_t)w->channels * sizeof(int16_t));
    if (!w->blk || !w->pcm || w->total_frames <= 0) goto fail;
    w->file_block = -1; /* the header parse moved the position */
    return 1;

fail:
    ima_wav_close(w);
    return 0;
}

/* Interleaved frames into `out`. Returns frames written, 0 at the end. */
static inline size_t ima_wav_read(ImaWav *w, int16_t *out, size_t frames)
{
    size_t done = 0;

    while (done < frames && w->pos < w->total_frames) {
        if (w->pcm_pos >= w->pcm_len && !ima_wav_load_block(w, w->next_block)) {
            w->pos = w->total_frames;
            break;
        }
        size_t n = (size_t)(w->pcm_len - w->pcm_pos);
        if (n > frames - done) n = frames - done;
        if ((long)n > w->total_frames - w->pos) n = (size_t)(w->total_frames - w->pos);
        memcpy(out + done * (size_t)w->channels,
               w->pcm + (size_t)w->pcm_pos * (size_t)w->channels,
               n * (size_t)w->channels * sizeof(int16_t));
        w->pcm_pos += (int)n;
        w->pos += (long)n;
        done += n;
    }
    return done;
}

static inline int ima_wav_seek(ImaWav *w, long frame)
{
    if (frame < 0 || frame >= w->total_frames) frame = 0;
    if (!ima_wav_load_block(w, frame / w->spb)) return 0;
    w->pcm_pos = (int)(frame % w->spb);
    w->pos = frame;
    return 1;
}

#endif
