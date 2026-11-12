#include <3ds.h>
#include "music_player.h"
#include "math_helpers.h"
#include <mpg123.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include <unistd.h>
#ifdef __PS2__
#include <errno.h>
#include <strings.h>
#include "asset_pak.h"
#include "mp3_decode_flow.h"
#include "ima_wav.h"
#endif
#ifdef __PSP__
#include "../psp/src/mp3_me_psp.h"
#endif
#ifdef PS2_MP3_DIAGNOSTIC
#include <timer.h>
#endif
#include "menus/settings.h"
#include "state.h"

#define THREAD_AFFINITY -1           // Execute thread on any core
#define THREAD_STACK_SZ 32 * 1024    // 32kB stack for audio thread

#define MUSIC_CHANNEL 0
#define MUSIC_BUF_SIZE 4096
#ifdef __PS2__
/* 12 * 2048 stereo frames at 32 kHz = about 0.77s of PCM queued.
 * This tolerates short scheduling stalls better than the original 0.26s.
 * Songs stay compressed on disk and are decoded incrementally. */
#define NUM_BUFS 12
#else
#define NUM_BUFS 4
#endif

static u32 *audioBuffer;
static LightEvent soundEvent;
static LightEvent seekEvent;
static LightLock decoderLock;

// format of the song being played, set by decoder_open
static size_t buffSize;
static int32_t rate;
static int audio_channels;

static volatile bool quit = false;
static volatile bool looping = false;
static volatile bool skip = false;
static volatile bool paused = false;
static volatile bool restart_requested = false;
#ifdef __PS2__
// The audio thread opens the song some time after play_music returns. Until
// then there is no decoder to seek, and seek_music only leaves its target for
// the thread.
static volatile bool decoder_ready;
#endif

volatile float amplitude = 0;

// a song played from memory (play_music_buf), read by libmpg123 / the PSP's ME
static void *song_buf = NULL;
static size_t song_sz = 0;
static size_t song_pos = 0;

static volatile float seek_target = 0;

static Thread threadId = NULL;

static ndspWaveBuf waveBuf[NUM_BUFS];

static inline u32 pcm_rate(void)
{
    return rate;
}

static inline u32 pcm_bytes(void)
{
    return buffSize;
}

static inline u32 pcm_channels(void)
{
    return audio_channels;
}

/* ---- Decoders ------------------------------------------------------------
 * audio_thread reads the song through decoder_open / read / seek / close and
 * does not care what is behind them:
 *   DECODER_MPG123  libmpg123 on the CPU: every platform, and the custom songs
 *   DECODER_ME      the PSP's Media Engine (psp/src/mp3_me_psp.c)
 *   DECODER_WAV     PS2: the game's songs ship as IMA-ADPCM .wav, a quarter of
 *                   the PCM size and a few integer ops per sample to decode
 * Callers still ask for "X.mp3"; on PS2 that plays "X.wav" when it exists.
 */
typedef enum {
    DECODER_NONE,
    DECODER_MPG123,
    DECODER_ME,
    DECODER_WAV
} DecoderKind;

static DecoderKind decoder = DECODER_NONE;
static mpg123_handle *mh;

#ifdef __PS2__
static bool mpg123_library_ready;
static ImaWav wav;

// The .wav next to an .mp3 path (or the path itself if it is a .wav).
static bool wav_sibling(const char *path, char *out, size_t cap) {
    size_t len = path ? strlen(path) : 0;

    if (len < 5 || len >= cap) return false;
    memcpy(out, path, len + 1);
    if (strcasecmp(out + len - 4, ".mp3") == 0) memcpy(out + len - 3, "wav", 4);
    else if (strcasecmp(out + len - 4, ".wav") != 0) return false;
    return access(out, F_OK) == 0;
}

static bool wav_start(const char *path) {
    char wav_path[512];

    if (!wav_sibling(path, wav_path, sizeof(wav_path))) return false;
    if (!ima_wav_open(&wav, wav_path)) {
        printf("GDPS2 WAV: %s is not a usable IMA-ADPCM file\n", wav_path);
        return false;
    }
    decoder = DECODER_WAV;
    rate = wav.rate;
    audio_channels = wav.channels;
    printf("GDPS2 WAV: %s rate=%ld channels=%d frames=%ld\n", wav_path,
           (long)rate, audio_channels, wav.total_frames);
    return true;
}
#endif

#ifdef __PSP__
static void *me_file; // the song read whole, when it came as a path

static void *read_whole_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    void *data = NULL;
    long n;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && (data = malloc(n)) && fread(data, 1, n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = n > 0 ? (size_t)n : 0;
    return data;
}

// the CPU's libmpg123 is only the fallback if this can't start
static bool me_start(const char *path) {
    const void *data = song_buf;
    size_t size = song_sz;

    if (!data) {
        data = me_file = read_whole_file(path, &size);
    }
    if (data && me_mp3_open(data, size, (int *)&rate, &audio_channels)) {
        decoder = DECODER_ME;
        return true;
    }
    free(me_file);
    me_file = NULL;
    return false;
}
#endif

static ssize_t read_mem(void *handle, void *buf, size_t count) {
    (void)handle;

    if (!song_buf || song_pos >= song_sz) return 0;

    size_t available = song_sz - song_pos;
    size_t to_read = count < available ? count : available;

    if (to_read > 0) {
        memcpy(buf, (u8*)song_buf + song_pos, to_read);
        song_pos += to_read;
    }

    return (ssize_t)to_read;
}

#ifdef __PS2__
/* A libmpg123 opened by filename bypasses the application's fopen wrapper.
 * For archive-only MP3s give it the same bounded FILE* used by WAV/atlases. */
static ssize_t read_pak_mp3(void *handle, void *buf, size_t count) {
    FILE *f = (FILE *)handle;
    size_t n = fread(buf, 1, count, f);
    return n == 0 && ferror(f) ? -1 : (ssize_t)n;
}
static off_t seek_pak_mp3(void *handle, off_t offset, int whence) {
    FILE *f = (FILE *)handle;
    return fseek(f, (long)offset, whence) == 0 ? (off_t)ftell(f) : -1;
}
static void close_pak_mp3(void *handle) {
    fclose((FILE *)handle);
}
#endif

static off_t seek_mem(void *handle, off_t offset, int whence) {
    (void)handle;

    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (off_t)song_pos + offset;
            break;
        case SEEK_END:
            new_pos = (off_t)song_sz + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0) new_pos = 0;
    if ((size_t)new_pos > song_sz) new_pos = (off_t)song_sz;

    song_pos = (size_t)new_pos;
    return (off_t)song_pos;
}

static bool mpg123_start(void *file) {
    int err = 0;
    int encoding = 0;

#ifdef __PS2__
    /* Initialize once for compatibility with older mpg123 builds. In API 49
     * (the local header) mpg123_init() is documented as a no-op; this is not
     * claimed to be the cause of past silence or stalls. */
    if (!mpg123_library_ready) {
        int library_status = mpg123_init();
        if (library_status != MPG123_OK) {
            printf("GDPS2 MP3: mpg123_init failed code=%d\n", library_status);
            return false;
        }
        mpg123_library_ready = true;
        puts("GDPS2 MP3: library initialized once for process lifetime");
    }
#endif
    if ((mh = mpg123_new(NULL, &err)) == NULL) {
        output_log("Couldn't new it\n");
#ifdef __PS2__
        printf("GDPS2 MP3: mpg123_new failed err=%d\n", err);
#endif
        return false;
    }
#ifdef __PS2__
    puts("GDPS2 MP3: mpg123_new ok");
#endif

    mpg123_format_none(mh);

    mpg123_format(mh, 44100, MPG123_STEREO, MPG123_ENC_SIGNED_16);
    mpg123_format(mh, 44100, MPG123_MONO,   MPG123_ENC_SIGNED_16);

    mpg123_format(mh, 48000, MPG123_STEREO, MPG123_ENC_SIGNED_16);
    mpg123_format(mh, 48000, MPG123_MONO,   MPG123_ENC_SIGNED_16);
#ifdef __PS2__
    /* All 21 shipped PS2 ROMFS MP3s (including the menu loop) are 32 kHz.
     * Only allowing 44.1/48 kHz rejected their original decoded format.
     * The NDSP EE mixer resamples 32000 -> 44100 for audsrv/SPU2. */
    mpg123_format(mh, 32000, MPG123_STEREO, MPG123_ENC_SIGNED_16);
    mpg123_format(mh, 32000, MPG123_MONO,   MPG123_ENC_SIGNED_16);
    /* Custom MP3s may use MPEG-2/2.5 sample rates. The PS2 mixer accepts
     * these rates and resamples their PCM16 output to the SPU2 rate. */
    static const long custom_rates[] = {8000, 11025, 12000, 16000, 22050, 24000};
    for (unsigned i = 0; i < sizeof(custom_rates)/sizeof(custom_rates[0]); ++i) {
        mpg123_format(mh, custom_rates[i], MPG123_STEREO, MPG123_ENC_SIGNED_16);
        mpg123_format(mh, custom_rates[i], MPG123_MONO, MPG123_ENC_SIGNED_16);
    }
#endif

    if (song_buf) {
        song_pos = 0;
        mpg123_replace_reader_handle(mh, read_mem, seek_mem, NULL);

        if (mpg123_open_handle(mh, NULL) != MPG123_OK) {
            output_log("Couldn't open memory stream\n");
            return false;
        }
    } else {
#ifdef __PS2__
        printf("GDPS2 MP3: opening file stream\n");
#endif
        int open_result;
#ifdef __PS2__
        const char *path = (const char *)file;
        if (path && !strncmp(path, "romfs:/", 7) &&
            PS2_PakMounted() && PS2_PakExists(path + 7)) {
            FILE *packed = fopen(path, "rb");
            if (!packed) return false;
            mpg123_replace_reader_handle(mh, read_pak_mp3, seek_pak_mp3, close_pak_mp3);
            open_result = mpg123_open_handle(mh, packed);
        } else
#endif
        {
            open_result = mpg123_open(mh, file);
        }
        if (open_result != MPG123_OK) {
            output_log("Couldn't open file\n");
#ifdef __PS2__
            printf("GDPS2 MP3: mpg123_open failed status=%d (%s)\n",
                   open_result, mpg123_strerror(mh));
#endif
            return false;
        }
    }
#ifdef __PS2__
    puts("GDPS2 MP3: stream open OK; querying output format");
#endif

    if (mpg123_getformat(mh, &rate, &audio_channels, &encoding) != MPG123_OK) {
        output_log("Couldn't get format\n");
        return false;
    }
#ifdef __PS2__
    printf("GDPS2 MP3: decoder open rate=%ld channels=%d encoding=%d\n",
           (long)rate, audio_channels, encoding);
#endif

    decoder = DECODER_MPG123;
    return true;
}

// Opens the song (a path, or song_buf) with the first decoder that takes it.
static bool decoder_open(void *file) {
#ifdef __PS2__
    printf("GDPS2 MP3: decoder init begin (%s)\n", song_buf ? "memory" : "file");
#endif
    bool opened = false;

#ifdef __PSP__
    opened = me_start(file);
#endif
#ifdef __PS2__
    if (!opened && !song_buf) opened = wav_start(file);
#endif
    if (!opened) opened = mpg123_start(file);

    if (opened) {
        buffSize = MUSIC_BUF_SIZE * audio_channels;
    } else if (mh) { // mpg123_new worked but the song didn't open
        mpg123_delete(mh);
        mh = NULL;
    }
    return opened;
}

static void decoder_close(void) {
    switch (decoder) {
#ifdef __PSP__
    case DECODER_ME:
        me_mp3_close();
        free(me_file);
        me_file = NULL;
        break;
#endif
#ifdef __PS2__
    case DECODER_WAV:
        ima_wav_close(&wav);
        break;
#endif
    case DECODER_MPG123:
        if (mh) {
            mpg123_close(mh);
            mpg123_delete(mh);
            mh = NULL;
        }
#ifndef __PS2__
        mpg123_exit();
#endif
        break;
    default:
        break;
    }
    decoder = DECODER_NONE;
}

/* Fills `buffer` with up to pcm_bytes() of interleaved PCM16 and returns the
 * number of *samples* (not frames, not bytes). `status` takes the mpg123 result
 * the PS2 loop reads; the other decoders map onto OK / DONE. */
static size_t decoder_read(void *buffer, int *status) {
    size_t done = 0;

    switch (decoder) {
#ifdef __PS2__
    case DECODER_WAV: {
        size_t want = buffSize / (sizeof(int16_t) * audio_channels);
        size_t frames = ima_wav_read(&wav, (int16_t *)buffer, want);

        if (status) *status = frames < want ? MPG123_DONE : MPG123_OK;
        return frames * audio_channels;
    }
#endif
#ifdef __PSP__
    case DECODER_ME:
        if (status) *status = MPG123_OK;
        return me_mp3_read(buffer, buffSize) / sizeof(int16_t);
#endif
    case DECODER_MPG123: {
        int result = mpg123_read(mh, (unsigned char *)buffer, buffSize, &done);

        if (status) *status = result;
#ifdef __PS2__
        static int error_logs;
        if (result != MPG123_OK && result != MPG123_DONE &&
            result != MPG123_NEW_FORMAT && error_logs++ < 5) {
            printf("GDPS2 MP3: decode status=%d bytes=%lu (%s)\n", result,
                   (unsigned long)done, mpg123_strerror(mh));
        }
#endif
        return done / sizeof(int16_t);
    }
    default:
        if (status) *status = MPG123_ERR;
        return 0;
    }
}

// Moves to a frame (a sample per channel). Past the end goes back to 0.
static bool decoder_seek(long frame) {
    switch (decoder) {
#ifdef __PS2__
    case DECODER_WAV:
        return ima_wav_seek(&wav, frame);
#endif
#ifdef __PSP__
    case DECODER_ME:
        me_mp3_seek(frame <= (long)me_mp3_length() ? frame : 0);
        return true;
#endif
    case DECODER_MPG123:
        if (frame <= mpg123_length(mh)) return mpg123_seek(mh, frame, SEEK_SET) >= 0;
        return mpg123_seek(mh, 0, SEEK_SET) >= 0;
    default:
        return false;
    }
}

#ifdef __PS2__
static const char *decoder_error_text(void) {
    return decoder == DECODER_MPG123 ? mpg123_strerror(mh) : "none";
}

// Only mpg123 can change format mid-stream; the PS2 loop accepts the notice
// only when rate, channels and encoding are still the ones the mixer was set to.
static bool decoder_format_unchanged(void) {
    long new_rate = 0;
    int new_channels = 0, new_encoding = 0;

    if (decoder != DECODER_MPG123) return true;
    return mpg123_getformat(mh, &new_rate, &new_channels, &new_encoding) == MPG123_OK &&
           new_rate == pcm_rate() && new_channels == (int)pcm_channels() &&
           new_encoding == MPG123_ENC_SIGNED_16;
}
#endif

/* ---- Playback ------------------------------------------------------------ */

static void audioCallback(void *const nul_) {
    (void)nul_;  // Unused

    if(quit || skip) { // Quit flag
        return;
    }

    LightEvent_Signal(&soundEvent);
}


void audio_init() {
#ifdef PS2_DIAG_MENU_STAGES
    puts("GDPS2 MP3 STAGE: audio_init enter");
#endif
    LightEvent_Init(&soundEvent, RESET_ONESHOT);

    // sized by the u32 stride waveBuf[i].data_vaddr uses below: with int16_t
    // the last buffers ran past the block (harmless in the 3DS linear heap,
    // fatal in the PSP's single one)
#ifdef __PS2__
    /* buffSize is already a PCM *byte* count.  The old u32-stride allocation
     * wasted 8x memory for stereo and left gaps between each decode block. */
    audioBuffer = (u32*)linearAlloc(NUM_BUFS * pcm_bytes());
#ifdef PS2_DIAG_MENU_STAGES
    printf("GDPS2 MP3 STAGE: allocated buffer=%p bytes=%lu\n", audioBuffer,
           (unsigned long)(NUM_BUFS * pcm_bytes()));
#endif
#else
    audioBuffer = (u32*)linearAlloc(NUM_BUFS * pcm_bytes() * pcm_channels() * sizeof(u32));
#endif

    ndspChnReset(MUSIC_CHANNEL);
#ifdef PS2_DIAG_MENU_STAGES
    puts("GDPS2 MP3 STAGE: reset NDSP channel returned");
#endif
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(MUSIC_CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(MUSIC_CHANNEL, pcm_rate());
    ndspChnSetFormat(MUSIC_CHANNEL, pcm_channels() == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    ndspSetCallback(audioCallback, NULL);
#ifdef PS2_DIAG_MENU_STAGES
    puts("GDPS2 MP3 STAGE: NDSP callback configured");
#endif

    if (paused) ndspChnSetPaused(MUSIC_CHANNEL, paused);

    for (int i = 0; i < NUM_BUFS; i++) {
        memset(&waveBuf[i], 0, sizeof(ndspWaveBuf));
#ifdef __PS2__
        waveBuf[i].data_vaddr = (u8 *)audioBuffer + i * pcm_bytes();
#else
        waveBuf[i].data_vaddr = audioBuffer + i * pcm_bytes() * pcm_channels();
#endif
    }

    apply_volume_settings();
#ifdef PS2_DIAG_MENU_STAGES
    puts("GDPS2 MP3 STAGE: audio_init returned");
#endif
}

void audio_exit() {
#ifdef __PS2__
    decoder_ready = false;
#endif
    ndspChnReset(MUSIC_CHANNEL);
    // a song that didn't start (the thread left before audio_init) comes here
    // too: what the last one left must not be freed twice
    linearFree(audioBuffer);
    audioBuffer = NULL;
    decoder_close();
}

float calculate_amplitude(float power) {
    if (state.practice_mode && !settingsState.practiceMusicSync) return 0.0f;
    if (game_state == STATE_MAIN_MENU) return 0.5f;

    static float prev = 0.0f;
    static float pulse = 0.0f;
    static float avg_delta = 0.0f;
    static float prev_power = 0.0f;

    float delta = power - prev;
    float abs_delta = fabsf(delta);

    // Low-pass filter the delta
    avg_delta = avg_delta + ((abs_delta - avg_delta) * 0.1f);

    // Detect note change: large RMS increase indicates a new note
    float rms_delta = power - prev_power;
    float abs_rms_delta = fabsf(rms_delta);
    float rms_thresh = avg_delta * POWER_THRESH_MULTIPLIER;

    if (abs_rms_delta > rms_thresh && rms_delta > 0.0f) {
        // Note changed, do pulse
        pulse = AMP_MAX;
    }

    pulse *= AMP_I_DECAY;

    if (pulse > AMP_MAX) pulse = AMP_MAX;
    if (pulse < AMP_MIN) pulse = AMP_MIN;

    prev = power;
    prev_power = power;

    return pulse;
}

float calculate_power(int16_t *samples, size_t frames, int channels) {
    uint64_t sum_squares = 0;
    uint32_t count = 0;

    for (size_t i = 0; i < frames; i++) {
        int16_t mono;

        if (channels == 2) {
            int16_t left  = samples[i*2];
            int16_t right = samples[i*2 + 1];
            mono = (left + right) / 2;
        } else {
            mono = samples[i];
        }

        sum_squares += mono * mono;
        count++;
    }

    if (count == 0) return 0.0f;

    return ((float)sum_squares / count) / (32768.0f * 32768.0f);
}

void audio_thread(void *const file) {
    skip = false;
    bool lastbuf = false;
#ifdef __PS2__
    bool first_decoded_pcm = false;
    bool loop_at_next_buffer = false;
#endif
#ifdef PS2_MP3_DIAGNOSTIC
    unsigned diag_decodes = 0, diag_wakes = 0;
#endif
#ifdef PS2_DIAG_MENU_STAGES
    bool first_decode_probe = true;
#endif

    if (!decoder_open(file)) return;

#ifdef PS2_DIAG_MENU_STAGES
    puts("GDPS2 MP3 STAGE: before audio_init");
#endif
    audio_init();
#ifdef __PS2__
    decoder_ready = true;
#endif
#ifdef PS2_DIAG_MENU_STAGES
    puts("GDPS2 MP3 STAGE: after audio_init / before first decode");
#endif

    if (seek_target > 0) {   
        seek_music(seek_target);    
        seek_target = -1;
    }

    while (!quit && !skip) {
        for (int i = 0; i < NUM_BUFS; i++) {
            ndspWaveBuf *buf = &waveBuf[i];

#ifdef __PS2__
            /* Protect buffer status, decoder read, PCM data and enqueue together.
             * External seek_music() holds the same lock while clearing the queue. */
#ifdef PS2_DIAG_MENU_STAGES
            if (first_decode_probe) puts("GDPS2 MP3 STAGE: before decoderLock");
#endif
            LightLock_Lock(&decoderLock);
#ifdef PS2_DIAG_MENU_STAGES
            if (first_decode_probe) puts("GDPS2 MP3 STAGE: decoderLock acquired");
#endif
            if (restart_requested) {
                loop_at_next_buffer = false;
                restart_requested = false;
            }
#endif
            if (buf->status == NDSP_WBUF_DONE || buf->status == NDSP_WBUF_FREE) {
#ifndef __PS2__
                LightLock_Lock(&decoderLock);
#endif
#ifdef PS2_MP3_DIAGNOSTIC
                u64 t0 = GetTimerSystemTime();
#endif
#ifdef __PS2__
                size_t read = 0;
                int status = MPG123_OK;
                bool yielded_data = false;
                bool sought_this_buffer = false;
                /* A zero-byte OK / format notice is NOT end of stream. Bound
                 * retries to avoid a busy loop on a stuck/malformed decoder. */
                for (int attempts = 0; attempts < 4 && !quit; ++attempts) {
                    if (loop_at_next_buffer) {
                        if (!decoder_seek(0)) {
                            printf("GDPS2 MP3: failed to seek to loop start (%s)\n",
                                   decoder_error_text());
                            lastbuf = true;
                            break;
                        }
                        loop_at_next_buffer = false;
                        sought_this_buffer = true;
                    }
#ifdef PS2_DIAG_MENU_STAGES
                    if (first_decode_probe) puts("GDPS2 MP3 STAGE: before mpg123_read");
#endif
                    read = decoder_read(buf->data_pcm16, &status);
#ifdef PS2_DIAG_MENU_STAGES
                    if (first_decode_probe) {
                        printf("GDPS2 MP3 STAGE: mpg123_read returned samples=%lu status=%d\n",
                               (unsigned long)read, status);
                        first_decode_probe = false;
                    }
#endif
                    size_t bytes = read * sizeof(int16_t);
                    Gdps2Mp3Action action = gdps2_mp3_action(status, bytes,
                                                               pcm_bytes(), pcm_channels());
                    if (action == GDPS2_MP3_FORMAT) {
                        if (!decoder_format_unchanged()) {
                            printf("GDPS2 MP3: unsupported mid-stream format change\n");
                            lastbuf = true;
                            break;
                        }
                        /* In the supported unchanged format, any returned PCM
                         * is valid. A format notice without PCM is retried. */
                        if (!read) continue;
                        action = GDPS2_MP3_DATA;
                    }
                    if (action == GDPS2_MP3_EOF) {
                        if (looping && !sought_this_buffer) {
                            loop_at_next_buffer = true;
                            continue;
                        }
                        lastbuf = true;
                        break;
                    }
                    if (action == GDPS2_MP3_RETRY) continue;
                    if (action == GDPS2_MP3_ERROR) {
                        printf("GDPS2 MP3: decoder stopped status=%d bytes=%lu (%s)\n",
                               status, (unsigned long)bytes, decoder_error_text());
                        lastbuf = true;
                        break;
                    }
                    yielded_data = true;
                    if (status == MPG123_DONE) {
                        if (looping) loop_at_next_buffer = true;
                        else lastbuf = true;
                    }
                    break;
                }
                if (!yielded_data && !lastbuf && !quit) {
                    printf("GDPS2 MP3: no decoder progress after bounded retries\n");
                    lastbuf = true;
                }
                if (!yielded_data) read = 0;
#else
                size_t read = decoder_read(buf->data_pcm16, NULL);
#endif
#ifdef PS2_MP3_DIAGNOSTIC
                u64 dt = (GetTimerSystemTime() - t0) / (kBUSCLK / 1000);
                if (diag_decodes < 24 || dt > 80)
                    printf("GDPS2 MP3 DIAG: decode #%u buf=%d samples=%lu dt=%llu ms\n",
                           diag_decodes, i, (unsigned long)read, (unsigned long long)dt);
                ++diag_decodes;
#endif
#ifndef __PS2__
                LightLock_Unlock(&decoderLock);
#endif

                if (read > 0) {
#ifdef __PS2__
                    if (!first_decoded_pcm) {
                        first_decoded_pcm = true;
                        printf("GDPS2 MP3: decoded first PCM buffer samples=%lu frames=%lu\n",
                               (unsigned long)read,
                               (unsigned long)(read / pcm_channels()));
                    }
#endif
                    /* decoder_read returns PCM16 *samples*, not bytes. */
                    size_t frames = read / pcm_channels();

                    float rms = calculate_power((int16_t*)buf->data_pcm16,
                                            frames,
                                            pcm_channels());

                    amplitude = calculate_amplitude(rms);
                }
#ifdef PS2_MP3_DIAGNOSTIC
                u64 prep_dt = (GetTimerSystemTime() - t0) / (kBUSCLK / 1000);
                if (diag_decodes <= 24 || prep_dt > 80)
                    printf("GDPS2 MP3 DIAG: prepared #%u elapsed=%llu ms\n",
                           diag_decodes, (unsigned long long)prep_dt);
#endif

                /* On PS2 the bounded decoder loop decided whether to stop
                 * or retry; keep the shared decoder lock until enqueue. */
#ifdef __PS2__
                if (read > 0) {
#else
                if (read == 0) {
                    if (looping) {
                        seek_music(0);
                    } else {
                        lastbuf = true;
                        continue;
                    }
                }
#endif

                buf->nsamples = read / pcm_channels();
#ifdef PS2_MP3_DIAGNOSTIC
                u64 q0 = GetTimerSystemTime();
#endif
                ndspChnWaveBufAdd(MUSIC_CHANNEL, buf);
#ifdef PS2_MP3_DIAGNOSTIC
                if (diag_decodes <= 24)
                    printf("GDPS2 MP3 DIAG: enqueue #%u buf=%d frames=%lu status=%d wait=%llu ms\n",
                           diag_decodes, i, (unsigned long)buf->nsamples, buf->status,
                           (unsigned long long)((GetTimerSystemTime()-q0)/(kBUSCLK/1000)));
#endif
#ifdef __PS2__
                }
#endif
            }
#ifdef __PS2__
            LightLock_Unlock(&decoderLock);
            if (lastbuf) break;
#endif
        }

        if (lastbuf) {
            while (!restart_requested && !quit) {
                amplitude = calculate_amplitude(0);
                svcSleepThread((long) 1.666666666666e+7);
            }

            if (quit) break;

            restart_requested = false;
            lastbuf = false;
            continue;
        }

        if (ndspChnIsPaused(MUSIC_CHANNEL)) {
            LightEvent_Wait(&soundEvent);
            continue;
        }

#ifdef PS2_MP3_DIAGNOSTIC
        u64 w0 = GetTimerSystemTime();
#endif
        LightEvent_Wait(&soundEvent);
#ifdef PS2_MP3_DIAGNOSTIC
        u64 wdt = (GetTimerSystemTime() - w0) / (kBUSCLK / 1000);
        if (++diag_wakes <= 24 || wdt > 1000)
            printf("GDPS2 MP3 DIAG: woke #%u wait=%llu ms\n",
                   diag_wakes, (unsigned long long)wdt);
#endif
    }
}

// Play a song defined by a path (on PS2 an X.mp3 plays X.wav when it exists)
int play_music(char *path, bool loop, float seek) {
    // Not found
#ifdef __PS2__
    char wav_path[512];
    const bool wav_song = wav_sibling(path, wav_path, sizeof(wav_path));
    if (!wav_song && access(path, F_OK) != 0) {
#else
    if (access(path, F_OK) != 0) {
#endif
#ifdef __PS2__
        printf("GDPS2 MP3: access failed path=%s errno=%d\n",
               path ? path : "(null)", errno);
#endif
        return 0;
    }

#ifdef PS2_DIAG_MP3_MEMORY
    /* A/B diagnostic only: preload the original compressed MP3 to distinguish
     * HostFS stream contention from decoder/scheduler time. No second thread. */
    if (path && strstr(path, "menuLoop.mp3")) {
        FILE *f = fopen(path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) {
                long length = ftell(f);
                if (length > 0 && length <= 4*1024*1024 && fseek(f, 0, SEEK_SET) == 0) {
                    void *data = malloc((size_t)length);
                    if (data && fread(data, 1, (size_t)length, f) == (size_t)length) {
                        fclose(f);
                        printf("GDPS2 MP3 MEMDIAG: original MP3 cached in EE RAM, bytes=%ld\n", length);
                        return play_music_buf(data, (size_t)length, loop, seek);
                    }
                    free(data);
                }
            }
            fclose(f);
        }
        puts("GDPS2 MP3 MEMDIAG: failed to cache MP3; fallback to normal file path");
    }
#endif

    LightEvent_Init(&seekEvent, RESET_ONESHOT);
    quit = false;
    looping = loop;

    seek_target = seek;

    /* libctru uses s32; on PS2SDK int32_t is long, but s32 is int. */
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // ... then subtract 1, as lower number => higher actual priority ...
#ifdef __PS2__
    /* The main EE thread may keep running through the vblank wait. A music
     * worker with numerically lower priority than main can starve even after
     * mpg123 opens the MP3, leaving the song silent. Let the decoder run when
     * it has a PCM buffer to fill; the mixer remains higher priority. */
    priority -= 1;
#else
    priority -= 1;
#endif
    // ... finally, clamp it between 0x18 and 0x3F to guarantee that it's valid.
    priority = priority < 0x18 ? 0x18 : priority;
    priority = priority > 0x3F ? 0x3F : priority;
#ifdef PS2_DIAG_MP3_PRIORITY
    printf("GDPS2 MP3 PRIO: normal request=0x%lx => EE=0x%lx; test request=0x12 => EE=0x02, mixer EE=0x18\n",
           (long)priority, (long)(0x20 + priority - 0x30));
    priority = 0x12; /* Comparison ONLY; normal ELF still uses original. */
#endif

    threadId = threadCreate(audio_thread, path,
                                          THREAD_STACK_SZ, priority,
                                          THREAD_AFFINITY, true);
#ifdef __PS2__
    if (!threadId) {
        printf("GDPS2 MP3: decoder thread creation failed for %s\n", path);
        return 0;
    }

#endif

#ifndef __PS2__
    if (seek > 0) LightEvent_Wait(&seekEvent);
#endif
    return 1;
}

// Set position in seconds
void seek_music(float time) {
    if (time < 0) time = 0;
#ifdef __PS2__
    if (!decoder_ready) {
        seek_target = time;
        return;
    }
#endif

    int location = time * pcm_rate();
    if (!quit) {
        bool oldstate = ndspChnIsPaused(MUSIC_CHANNEL);
        ndspChnSetPaused(MUSIC_CHANNEL, true); //Pause playback...
        
        LightLock_Lock(&decoderLock);
        // Flush old audio
        ndspChnReset(MUSIC_CHANNEL);
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnSetInterp(MUSIC_CHANNEL, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(MUSIC_CHANNEL, pcm_rate());
        ndspChnSetFormat(MUSIC_CHANNEL, pcm_channels() == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
        ndspSetCallback(audioCallback, NULL);

        decoder_seek(location);

        for (int i = 0; i < NUM_BUFS; i++) {
            memset(&waveBuf[i], 0, sizeof(ndspWaveBuf));
#ifdef __PS2__
            waveBuf[i].data_vaddr = (u8 *)audioBuffer + i * pcm_bytes();
#else
            waveBuf[i].data_vaddr = audioBuffer + i * pcm_bytes() * pcm_channels();
#endif
        }
        LightLock_Unlock(&decoderLock);
        ndspChnSetPaused(MUSIC_CHANNEL, oldstate); //once the seeking is done, playback can continue.

        apply_volume_settings();
        
        restart_requested = true;
        LightEvent_Signal(&seekEvent);
    }
}

// Pause or unpause playback
void toggle_playback_music() {
    paused = !ndspChnIsPaused(MUSIC_CHANNEL);
    ndspChnSetPaused(MUSIC_CHANNEL, paused);
}

void pause_playback_music() {
    paused = true;
    ndspChnSetPaused(MUSIC_CHANNEL, paused);
}

void unpause_playback_music() {
    paused = false;
    ndspChnSetPaused(MUSIC_CHANNEL, paused);
}

void fade_to_amplitude(float new_amplitude) {
    amplitude = calculate_amplitude(new_amplitude);
}

// Play an mp3 from a pre-loaded memory buffer (custom songs)
int play_music_buf(void *buf, size_t sz, bool loop, float seek) {
    if (!buf || sz == 0) return 0;

    stop_music();

    LightEvent_Init(&seekEvent, RESET_ONESHOT);
    quit = false;
    looping = loop;
    seek_target = seek;

    song_buf = buf;
    song_sz = sz;
    song_pos = 0;

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
#ifdef __PS2__
    /* Custom MP3s need CPU time during gameplay even when the main thread
     * continuously renders. They used to run below main and could starve. */
    priority -= 1;
#else
    priority -= 1;
#endif
    priority = priority < 0x18 ? 0x18 : priority;
    priority = priority > 0x3F ? 0x3F : priority;
#ifdef PS2_DIAG_MP3_PRIORITY
    printf("GDPS2 MP3 PRIO: memory request=0x%lx => EE=0x%lx; test request=0x12 => EE=0x02\n",
           (long)priority, (long)(0x20 + priority - 0x30));
    priority = 0x12;
#endif

    threadId = threadCreate(audio_thread, NULL, THREAD_STACK_SZ, priority, THREAD_AFFINITY, true);
    if (!threadId) {
        free(song_buf);
        song_buf = NULL;
        song_sz = 0;
        song_pos = 0;
        return 0;
    }

#ifndef __PS2__
    /* PS2 applies seek_target asynchronously in audio_thread. Waiting here
     * could hang forever if a custom MP3 is corrupt and decoder_open fails. */
    if (seek > 0) LightEvent_Wait(&seekEvent);
#endif
    return 1;
}

// Stop playback
void stop_music() {
    if (!quit && threadId) {
        quit = true;
        
        LightEvent_Signal(&soundEvent);
        
        threadJoin(threadId, U64_MAX);
        
        threadId = NULL;
        audio_exit();
    }

    if (song_buf) {
        free(song_buf);
        song_buf = NULL;
    }
    song_sz = 0;
    song_pos = 0;
}
