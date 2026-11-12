/*
 * 3DS NDSP API for PS2: software-mix source PCM on the EE, hand 16-bit
 * interleaved stereo to audsrv over SIF RPC, ultimately played by SPU2.
 *
 * Requires LIBSD on IOP and an embedded PS2SDK audsrv.irx. The mixer core
 * remains testable on the host without a PS2 emulator.
 */
#include <3ds.h>
#include <audsrv.h>
#include <kernel.h>
#include <malloc.h>
#include <stdlib.h>
#include <delaythread.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#if defined(PS2_AUDIO_DIAGNOSTIC) || defined(PS2_DIAG_PCM_HEARTBEAT)
#include <timer.h>
#endif

#include "ndsp_mixer.h"
#include "audsrv_stream.h"
#include "boot_trace.h"

#define OUTPUT_FRAMES 512
#define OUTPUT_BYTES (OUTPUT_FRAMES * 2 * sizeof(s16))

extern u8 gd_ps2_audsrv_irx_begin[];
extern u8 gd_ps2_audsrv_irx_end[];

static Ps2NdspMixer mixer;
static s16 output[OUTPUT_FRAMES * 2] __attribute__((aligned(64)));
static volatile bool running;
static bool initialized;
static bool irx_ready;
static int mixer_sema = -1;
static int mix_thread_id = -1;
static void *mix_thread_stack;
static ndspCallback audio_callback;
static void *audio_callback_data;
static unsigned audio_blocks;
static unsigned audio_partial_writes;
static bool audio_observed_nonzero;

static int ps2_audio_wait(int bytes, void *ctx)
{
    (void)ctx;
#ifdef PS2_AUDIO_DIAGNOSTIC
    if (audio_blocks == 288)
        puts("GDPS2 AUDIO RPC: entering wait for block 289");
    u64 begin = GetTimerSystemTime();
    int result = audsrv_wait_audio(bytes);
    if (audio_blocks == 288)
        printf("GDPS2 AUDIO RPC: wait returned block 289 status=%d\n", result);
    u64 elapsed_ms = ((GetTimerSystemTime() - begin) / (kBUSCLK / 1000));
    if (elapsed_ms >= 100)
        printf("GDPS2 AUDIO WAIT: %llu ms, block=%u, bytes=%d, status=%d\n",
               (unsigned long long)elapsed_ms, audio_blocks + 1, bytes, result);
    return result;
#else
    return audsrv_wait_audio(bytes);
#endif
}

static int ps2_audio_send(const char *data, int bytes, void *ctx)
{
    (void)ctx;
    int originally_requested = bytes;
#ifdef PS2_AUDIO_DIAGNOSTIC
    if (audio_blocks == 288)
        puts("GDPS2 AUDIO RPC: entering send for block 289");
#endif
#ifdef PS2_AUDIO_TEST_PARTIAL_SEND
    /* Only the standalone audio diagnostic ELF opts in: deterministically
     * exercise the positive-short-write branch on an actual IOP stream. */
    if (bytes > 664) bytes = 664;
#endif
    int count = audsrv_play_audio(data, bytes);
#ifdef PS2_AUDIO_DIAGNOSTIC
    if (audio_blocks == 288)
        printf("GDPS2 AUDIO RPC: send returned block 289 count=%d\n", count);
#endif
    if (count > 0 && count < originally_requested) {
        ++audio_partial_writes;
        if (audio_partial_writes <= 4)
            printf("NDSP PS2: partial IOP write %d/%d; retrying remainder\n",
                   count, originally_requested);
    }
    return count;
}

static void lock_mixer(void)
{
    if (mixer_sema > 0) WaitSema(mixer_sema);
}

static void unlock_mixer(void)
{
    if (mixer_sema > 0) SignalSema(mixer_sema);
}

static void audio_worker(void *arg)
{
    (void)arg;
#ifdef PS2_DIAG_MIXER_PRIORITY
    ee_thread_status_t info;
    if (ReferThreadStatus(TH_SELF, &info) >= 0)
        printf("GDPS2 THREAD: audio worker initial=%d current=%d\n",
               info.initial_priority, info.current_priority);
#endif
#ifdef PS2_DIAG_PCM_HEARTBEAT
    u64 mix_ticks = 0, lock_ticks = 0, render_ticks = 0, submit_ticks = 0;
    unsigned nonzero_blocks = 0;
    u64 longest_submit_ticks = 0;
#endif
    while (running) {
        /* Wait is blocking, allowing the IOP/SPU2 playback rate to pace
         * mixing. No spinning and no guessed DelayThread frame lengths. */
        if (!running) break;
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 304)
            printf("GDPS2 PCM STEP: loop begin after=%u\n", audio_blocks);
#endif
        ndspCallback cb;
        void *cb_data;
#ifdef PS2_AUDIO_DIAGNOSTIC
        bool music_playing, sfx_playing;
#endif
#ifdef PS2_DIAG_PCM_HEARTBEAT
        bool music_playing_brief;
#endif
        #ifdef PS2_DIAG_PCM_HEARTBEAT
        u64 block_start = GetTimerSystemTime();
        #endif
        lock_mixer();
#ifdef PS2_DIAG_PCM_HEARTBEAT
        u64 after_lock = GetTimerSystemTime();
        lock_ticks += after_lock - block_start;
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        u64 render_start = GetTimerSystemTime();
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 304)
            printf("GDPS2 PCM STEP: mutex acquired after=%u\n", audio_blocks);
#endif
        PS2_MixerRender(&mixer, output, OUTPUT_FRAMES);
#ifdef PS2_DIAG_PCM_HEARTBEAT
        render_ticks += GetTimerSystemTime() - after_lock;
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        unsigned long render_ms = (unsigned long)((GetTimerSystemTime() - render_start) / (kBUSCLK / 1000));
        if (render_ms >= 50)
            printf("GDPS2 MIXER: block=%u render=%lu ms\n", audio_blocks+1, render_ms);
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 304)
            printf("GDPS2 PCM STEP: rendered after=%u\n", audio_blocks);
#endif
        cb = audio_callback;
        cb_data = audio_callback_data;
#ifdef PS2_DIAG_PCM_HEARTBEAT
        music_playing_brief = PS2_MixerIsPlaying(&mixer, 0);
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        music_playing = PS2_MixerIsPlaying(&mixer, 0);
        sfx_playing = PS2_MixerIsPlaying(&mixer, 1);
#endif
#ifdef PS2_DIAG_FRAME_TIMING
        const bool music_playing_gap = PS2_MixerIsPlaying(&mixer, 0);
#endif
        unlock_mixer();
#ifdef PS2_DIAG_FRAME_TIMING
        {
            /* Music queue ran dry while it was playing: audible gap */
            static bool was_playing;
            static unsigned gaps;
            if (was_playing && !music_playing_gap) printf("GDPS2 AUDIO GAP #%u (music queue empty)\n", ++gaps);
            was_playing = music_playing_gap;
        }
#endif
#ifdef PS2_DIAG_PCM_HEARTBEAT
        mix_ticks += GetTimerSystemTime() - block_start;
        /* Sample a small number of positions to avoid expensive diagnostic
         * scanning on every mixer block. Count coarse nonzero coverage. */
        for (int k = 0; k < OUTPUT_FRAMES * 2; k += 17) {
            if (output[k] != 0) { ++nonzero_blocks; break; }
        }
#endif
        if (!audio_observed_nonzero) {
            for (int i=0; i<OUTPUT_FRAMES * 2; ++i) {
                if (output[i]) {
                    audio_observed_nonzero = true;
                    printf("NDSP PS2: first nonzero stereo PCM block (sample=%d)\n",
                           (int)output[i]);
                    break;
                }
            }
        }
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 304)
            printf("GDPS2 PCM STEP: before send next=%u\n", audio_blocks + 1);
#endif
        #ifdef PS2_DIAG_PCM_HEARTBEAT
        u64 send_start = GetTimerSystemTime();
        #endif
        int written = PS2_AudioSubmitAll((const char *)output, OUTPUT_BYTES,
                                         ps2_audio_wait, ps2_audio_send, NULL);
#ifdef PS2_DIAG_PCM_HEARTBEAT
        u64 send_elapsed = GetTimerSystemTime() - send_start;
        submit_ticks += send_elapsed;
        if (send_elapsed > longest_submit_ticks) longest_submit_ticks = send_elapsed;
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 304)
            printf("GDPS2 PCM STEP: after send next=%u result=%d\n",
                   audio_blocks + 1, written);
#endif
        if (written != OUTPUT_BYTES) {
            printf("NDSP PS2: PCM submission stopped (code=%d, intended=%d, audsrv_errno=%d)\n",
                   written, OUTPUT_BYTES, audsrv_get_error());
            break;
        }
        if (++audio_blocks == 128)
            printf("NDSP PS2: submitted 128 PCM blocks (%u partial writes retried)\n",
                   audio_partial_writes);
#ifdef PS2_DIAG_PCM_HEARTBEAT
        if ((audio_blocks % 256u) == 0u) {
            unsigned peak = 0;
            for (int sample = 0; sample < OUTPUT_FRAMES * 2; ++sample) {
                int v = output[sample];
                unsigned a = (unsigned)(v < 0 ? -v : v);
                if (a > peak) peak = a;
            }
            printf("GDPS2 AUDIO HEARTBEAT: blocks=%u music=%d peak=%u nonzero_blocks=%u/256 partial=%u mix_ms=%llu lock_ms=%llu render_ms=%llu submit_ms=%llu max_submit_ms=%llu\n",
                   audio_blocks, music_playing_brief, peak, nonzero_blocks,
                   audio_partial_writes,
                   (unsigned long long)(mix_ticks / (kBUSCLK / 1000)),
                   (unsigned long long)(lock_ticks / (kBUSCLK / 1000)),
                   (unsigned long long)(render_ticks / (kBUSCLK / 1000)),
                   (unsigned long long)(submit_ticks / (kBUSCLK / 1000)),
                   (unsigned long long)(longest_submit_ticks / (kBUSCLK / 1000)));
            mix_ticks = lock_ticks = render_ticks = submit_ticks = longest_submit_ticks = 0;
            nonzero_blocks = 0;
        }
#endif
#ifdef PS2_DIAG_MENU_STAGES
        if ((audio_blocks % 64) == 0)
            printf("GDPS2 AUDIO STAGE: submitted PCM blocks=%u\n", audio_blocks);
#endif
#ifdef PS2_AUDIO_DIAGNOSTIC
        if ((audio_blocks % 16) == 0) {
            unsigned peak = 0, nonzero = 0;
            for (unsigned i = 0; i < OUTPUT_FRAMES * 2; ++i) {
                int v = output[i];
                unsigned a = (unsigned)(v < 0 ? -v : v);
                if (a) ++nonzero;
                if (a > peak) peak = a;
            }
            printf("GDPS2 PCM: block=%u peak=%u nonzero=%u music=%d sfx=%d partial=%u\n",
                   audio_blocks, peak, nonzero, music_playing, sfx_playing,
                   audio_partial_writes);
        }
#endif
        /* This is the game's refill request for music_player.c; never call
         * while holding the mixer semaphore (the callback may enqueue). */
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 305)
            printf("GDPS2 PCM STEP: callback begin block=%u callback=%d\n",
                   audio_blocks, cb != NULL);
#endif
        if (cb) cb(cb_data);
#ifdef PS2_AUDIO_DIAGNOSTIC
        if (audio_blocks >= 288 && audio_blocks <= 305)
            printf("GDPS2 PCM STEP: callback end block=%u\n", audio_blocks);
#endif
    }
    running = false;
    ExitThread();
}

Result ndspInit(void)
{
    if (initialized) return 0;
    PS2_BootEvent("GDPS2 AUDIO: ndspInit entry; sceSifInitRpc next");
    sceSifInitRpc(0);

    if (!irx_ready) {
        /* LOADFILE on retail IOPs needs the EE-memory module load patch.
         * An already loaded LIBSD is acceptable; SifLoadModule may report
         * a negative code for that case, so audsrv_init is the final check. */
        PS2_BootEvent("GDPS2 AUDIO: before sbv_patch_enable_lmb");
        int patch = sbv_patch_enable_lmb();
        PS2_BootResult("AUDIO sbv_patch_enable_lmb", patch);
        if (patch != 0) {
            /* sbv returns 1 when the expected LOADFILE dispatch pattern
             * isn't found. It is NOT success or 'already patched'. The
             * normal release remains conservative. The hardware debug
             * build probes alternative module-loading routes and logs
             * their real results before proceeding to audsrv_init. */
            PS2_BootEvent("GDPS2 AUDIO: LOADFILE patch unavailable (nonzero); trying buffer/file load anyway");
        }
        PS2_BootEvent("GDPS2 AUDIO: before SifLoadModule rom0:LIBSD");
        int libsd = SifLoadModule("rom0:LIBSD", 0, NULL);
        printf("NDSP PS2: LIBSD module result %d\n", libsd);
        PS2_BootValue("AUDIO LIBSD module id", libsd);
        u32 length = (u32)(gd_ps2_audsrv_irx_end - gd_ps2_audsrv_irx_begin);
        PS2_BootValue("AUDIO embedded audsrv.irx bytes", (int)length);
        if (!length || length > 256*1024) {
            PS2_BootFatal("invalid audsrv.irx size", (int)length);
            return -1;
        }
        int module = -1;
        if (patch != 0) {
            /* Patch pattern not found: launcher (OPL/uLaunchELF/FMCB) often
             * already patched LOADFILE, or the IOP ROM differs. Try the
             * buffer route first; it works when already patched. */
            int mod_result = 0;
            PS2_BootEvent("GDPS2 AUDIO: SifExecModuleBuffer audsrv.irx (patch unavailable)");
            module = SifExecModuleBuffer(gd_ps2_audsrv_irx_begin, length,
                                         0, NULL, &mod_result);
            PS2_BootValue("AUDIO audsrv.irx module result", mod_result);
            if (module < 0) {
                /* Last resort: load from file on the IOP-visible device. */
                const char *paths[] = {
                    "mass:/GDPS2/audsrv.irx", "mass0:/GDPS2/audsrv.irx"
                };
                for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
                    char line[150];
                    snprintf(line, sizeof(line), "GDPS2 AUDIO: before SifLoadModule %s", paths[i]);
                    PS2_BootEvent(line);
                    module = SifLoadModule(paths[i], 0, NULL);
                    snprintf(line, sizeof(line), "AUDIO SifLoadModule %s id", paths[i]);
                    PS2_BootValue(line, module);
                    if (module >= 0) break;
                }
            }
        }
        if (module < 0) {
            int mod_result = 0;
            PS2_BootEvent("GDPS2 AUDIO: before SifExecModuleBuffer audsrv.irx (patched LOADFILE)");
            module = SifExecModuleBuffer(gd_ps2_audsrv_irx_begin, length,
                                         0, NULL, &mod_result);
            PS2_BootValue("AUDIO audsrv.irx module result", mod_result);
        }
        PS2_BootValue("AUDIO audsrv.irx module id", module);
        if (module < 0) {
            PS2_BootFatal("unable to load audsrv.irx by file or buffer", module);
            return -1;
        }
        irx_ready = true;
    }
    PS2_BootEvent("GDPS2 AUDIO: before audsrv_init RPC");
    int status = audsrv_init();
    PS2_BootResult("AUDIO audsrv_init", status);
    if (status != AUDSRV_ERR_NOERROR) {
        printf("NDSP PS2: audsrv_init failed: %d (%s)\n",
               status, audsrv_get_error_string());
        return -1;
    }
    audsrv_fmt_t fmt = { PS2_NDSP_OUTPUT_RATE, 16, 2 };
    PS2_BootEvent("GDPS2 AUDIO: before audsrv_set_format 44100/16/stereo");
    status = audsrv_set_format(&fmt);
    PS2_BootResult("AUDIO audsrv_set_format", status);
    if (status != AUDSRV_ERR_NOERROR) {
        printf("NDSP PS2: set_format failed: %d (%s)\n",
               status, audsrv_get_error_string());
        audsrv_quit();
        return -1;
    }
    /* Minecraft's SoundManager_PS2.cpp checks the driver initialization
     * before touching voices and explicitly opens its output at MAX_VOLUME.
     * This is a master/output volume, not the game's per-channel NDSP gain.
     * Keep them separate and fail visibly if the IOP refuses the setting. */
    PS2_BootEvent("GDPS2 AUDIO: before audsrv_set_volume");
    status = audsrv_set_volume(MAX_VOLUME);
    PS2_BootResult("AUDIO audsrv_set_volume", status);
    if (status != AUDSRV_ERR_NOERROR) {
        printf("NDSP PS2: set_volume(%d) failed: %d (%s)\n",
               MAX_VOLUME, status, audsrv_get_error_string());
        audsrv_quit();
        return -1;
    }
    printf("NDSP PS2: audsrv master volume=%d%%, format=44100Hz PCM16 stereo\n",
           MAX_VOLUME);
    ee_sema_t s;
    memset(&s, 0, sizeof(s));
    s.init_count = 1;
    s.max_count = 1;
    mixer_sema = CreateSema(&s);
    PS2_BootValue("AUDIO CreateSema id", mixer_sema);
    if (mixer_sema < 0) goto fail;
    mix_thread_stack = memalign(64, 32*1024);
    PS2_BootEvent(mix_thread_stack ? "GDPS2 AUDIO: mixer stack allocated" : "GDPS2 AUDIO: mixer stack allocation failed");
    if (!mix_thread_stack) goto fail;

    PS2_MixerInit(&mixer);
    audio_blocks = 0;
    audio_partial_writes = 0;
    audio_observed_nonzero = false;
    audio_callback = NULL;
    audio_callback_data = NULL;
#ifdef PS2_DIAG_NO_MIXER_THREAD
    /* Isolate mpg123 from audsrv's EE worker without changing its codec,
     * original MP3 file path or NDSP queue semantics. DIAGNOSTIC ONLY. */
    initialized = true;
    puts("GDPS2 DIAG: audsrv mixer worker disabled for decode isolation; audio output muted");
    return 0;
#endif
    ee_thread_t t;
    memset(&t, 0, sizeof(t));
    t.func = (void *)audio_worker;
    t.stack = mix_thread_stack;
    t.stack_size = 32*1024;
    t.gp_reg = &_gp;
    t.initial_priority = 0x18;
#ifdef PS2_DIAG_MIXER_PRIORITY
    ee_thread_status_t main_info;
    if (ReferThreadStatus(TH_SELF, &main_info) >= 0)
        printf("GDPS2 THREAD: initializing NDSP main initial=%d current=%d\n",
               main_info.initial_priority, main_info.current_priority);
    t.initial_priority = PS2_DIAG_MIXER_PRIORITY;
    printf("GDPS2 THREAD: mixer diagnostic initial priority=%d (normal=24)\n",
           t.initial_priority);
#endif
    mix_thread_id = CreateThread(&t);
    PS2_BootValue("AUDIO CreateThread id", mix_thread_id);
    if (mix_thread_id <= 0) goto fail;
    running = true;
    PS2_BootEvent("GDPS2 AUDIO: before StartThread mixer");
    if (StartThread(mix_thread_id, NULL) < 0) {
        running = false;
        DeleteThread(mix_thread_id);
        mix_thread_id = -1;
        goto fail;
    }
    initialized = true;
    PS2_BootEvent("GDPS2 AUDIO: mixer worker started");
    puts("NDSP PS2: software mixer started (44100 Hz / stereo PCM16)");
#ifdef PS2_DIAG_MAIN_PRIORITY
    ee_thread_status_t before, after;
    int before_ok = ReferThreadStatus(TH_SELF, &before);
    int result = ChangeThreadPriority(TH_SELF, PS2_DIAG_MAIN_PRIORITY);
    int after_ok = ReferThreadStatus(TH_SELF, &after);
    printf("GDPS2 THREAD: main priority change target=%d result=%d old=%d new=%d status_before=%d status_after=%d\n",
           PS2_DIAG_MAIN_PRIORITY, result,
           before_ok >= 0 ? before.current_priority : -1,
           after_ok >= 0 ? after.current_priority : -1, before_ok, after_ok);
#endif
    return 0;
fail:
    if (mixer_sema > 0) DeleteSema(mixer_sema);
    mixer_sema = -1;
    free(mix_thread_stack);
    mix_thread_stack = NULL;
    audsrv_quit();
    return -1;
}

void ndspExit(void)
{
    if (!initialized) return;
    initialized = false;
    running = false;
    /* The EE worker can be waiting for IOP audio queue space. Waiting for
     * the queue to drain is normally < one block; a hung RPC can still hang
     * here and must be checked on hardware. */
    if (mix_thread_id > 0) {
        ee_thread_status_t st;
        while (ReferThreadStatus(mix_thread_id, &st) >= 0 &&
               st.status != THS_DORMANT) {
            DelayThread(1000);
        }
        DeleteThread(mix_thread_id);
        mix_thread_id = -1;
    }
    free(mix_thread_stack);
    mix_thread_stack = NULL;
    if (mixer_sema > 0) {
        DeleteSema(mixer_sema);
        mixer_sema = -1;
    }
    audsrv_stop_audio();
    audsrv_quit();
}

void ndspSetOutputMode(ndspOutputMode mode)
{
    /* The backend always mixes to 2-channel PCM; mono/surround requests are
     * downmixed to stereo rather than changing the IOP streaming format. */
    (void)mode;
}

void ndspSetCallback(ndspCallback callback, void *data)
{
    lock_mixer();
    audio_callback = callback;
    audio_callback_data = data;
    unlock_mixer();
}

void ndspChnReset(int id)
{
    lock_mixer();
    PS2_MixerReset(&mixer, id);
    unlock_mixer();
}

void ndspChnSetInterp(int id, ndspInterpType interpolation)
{
    (void)id; (void)interpolation;
    /* Linear interpolation for all channels. Polynomials are not ported. */
}

void ndspChnSetRate(int id, float rate)
{
    lock_mixer();
    PS2_MixerSetRate(&mixer, id, rate);
    unlock_mixer();
}

void ndspChnSetFormat(int id, u16 format)
{
    lock_mixer();
    PS2_MixerSetFormat(&mixer, id, format);
    unlock_mixer();
}

void ndspChnSetMix(int id, float mix[12])
{
    lock_mixer();
    PS2_MixerSetMix(&mixer, id, mix);
    unlock_mixer();
}

void ndspChnSetPaused(int id, bool paused)
{
    lock_mixer();
    PS2_MixerSetPaused(&mixer, id, paused);
    unlock_mixer();
}

bool ndspChnIsPaused(int id)
{
    lock_mixer();
    bool paused = PS2_MixerIsPaused(&mixer, id);
    unlock_mixer();
    return paused;
}

bool ndspChnIsPlaying(int id)
{
    lock_mixer();
    bool playing = PS2_MixerIsPlaying(&mixer, id);
    unlock_mixer();
    return playing;
}

void ndspChnWaveBufAdd(int id, ndspWaveBuf *buffer)
{
    lock_mixer();
    PS2_MixerQueue(&mixer, id, buffer);
    unlock_mixer();
}

void ndspChnWaveBufClear(int id) { ndspChnReset(id); }
