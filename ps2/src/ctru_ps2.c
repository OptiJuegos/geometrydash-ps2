/* Partial but real libctru compatibility for PS2SDK EE.
 * Audio, ROM filesystem, on-screen keyboard and internal Citro2D batching
 * are intentionally NOT emulated here: they still need dedicated ports.
 */
#include <3ds.h>
#include <kernel.h>
#include <timer.h>
#include <delaythread.h>
#include <sifrpc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <malloc.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c2d_ps2.h"
#include "ctru_ps2_input.h"
#include "boot_trace.h"

struct PspThread_s {
    int id;
    int completion;
    ThreadFunc fn;
    void *argument;
    void *stack;
};

static GSGLOBAL *ps2_gs;

/* Counts every vertical blank. Frames reach the screen on vblanks, so the time
 * between two presented frames is a whole number of them; main.c uses that
 * instead of the EE clock for the physics step (same fix as the PSP port). */
static volatile u32 vblank_count;
static int vblank_handler(int cause)
{
    (void)cause;
    ++vblank_count;
    ExitHandler();
    return 0;
}
u32 PS2_VblankCount(void) { return vblank_count; }

u64 svcGetSystemTick(void)
{
    /* 3DS ARM11 268111856 ticks/sec, EE bus clock 147456000 ticks/sec.
     * Split before multiplying to keep the intermediate within 64 bits. */
    u64 now = GetTimerSystemTime();
    const u64 hz = SYSCLOCK_ARM11;
    return (now / kBUSCLK) * hz + (now % kBUSCLK) * hz / kBUSCLK;
}

void svcSleepThread(s64 ns)
{
    if (ns <= 0) return;
    u64 microseconds = ((u64)ns + 999u) / 1000u;
    while (microseconds) {
        /* DelayThread uses a signed 32-bit number of microseconds. */
        u32 step = microseconds > 10000000ULL ? 10000000u : (u32)microseconds;
        DelayThread((s32)step);
        microseconds -= step;
    }
}

u64 osGetTime(void) { return (u64)time(NULL) * 1000u; }

void svcBreak(u32 reason)
{
    PS2_BootFatal("svcBreak", (int)reason);
    printf("FATAL GDPS2: svcBreak(%u)\n", reason);
    fflush(stdout);
    Exit(1);
}

Result svcGetSystemInfo(s64 *out, u32 type, s32 param)
{
    (void)type; (void)param;
    if (!out) return -1;
    *out = 0; /* PS2 is not a 3DS emulator. */
    return 0;
}

Result svcGetThreadPriority(s32 *out, Handle handle)
{
    if (!out) return -1;
    ee_thread_status_t info;
    s32 id = handle == CUR_THREAD_HANDLE ? TH_SELF : (s32)handle;
    if (ReferThreadStatus(id, &info) < 0) return -1;
    s32 priority = 0x30 + (info.current_priority - 0x20);
    if (priority < 0x18) priority = 0x18;
    if (priority > 0x3F) priority = 0x3F;
    *out = priority;
    return 0;
}

void osSetSpeedupEnable(bool enable) { (void)enable; }
float osGet3DSliderState(void) { return 0.f; }

size_t envGetHeapSize(void)
{
    /* A physical-memory upper bound, not necessarily free heap memory. */
    return (size_t)GetMemorySize();
}

void *linearAlloc(size_t size) { return memalign(64, size); }
void linearFree(void *data) { free(data); }

u32 linearSpaceFree(void)
{
    struct mallinfo info = mallinfo();
    return info.fordblks > 0 ? (u32)info.fordblks : 0;
}

bool aptMainLoop(void) { return true; }

Result cfguInit(void) { return 0; }
void cfguExit(void) { }

Result CFGU_GetSystemModel(u8 *model)
{
    if (!model) return -1;
    *model = CFG_MODEL_N3DS; /* Enable the game's efficient non-2DS path. */
    return 0;
}

PrintConsole *consoleInit(gfxScreen_t screen, PrintConsole *console)
{
    (void)screen;
    return console; /* Debug text goes to stdout; no 3DS console overlay. */
}

Result HIDUSER_GetSoundVolume(u8 *volume)
{
    if (!volume) return -1;
    *volume = 63; /* PS2 has no equivalent hardware slider. */
    return 0;
}

void gfxInitDefault(void)
{
    if (ps2_gs) return;
    sceSifInitRpc(0);
    PS2_BootEvent("GDPS2 GFX: PS2_InputInit begin");
    puts("GDPS2 GFX: PS2_InputInit");
    PS2_InputInit();
    PS2_BootEvent("GDPS2 GFX: PS2_InputInit returned");
    puts("GDPS2 GFX: gsKit_init_global");
    ps2_gs = gsKit_init_global();
    if (!ps2_gs) svcBreak(USERBREAK_PANIC);
    PS2_BootEvent("GDPS2 GFX: gsKit_init_global returned");
    ps2_gs->Mode = GS_MODE_NTSC;
    ps2_gs->Width = 640;
    ps2_gs->Height = 448;
    ps2_gs->PSM = GS_PSM_CT16;
    ps2_gs->PSMZ = GS_PSMZ_16;
    ps2_gs->DoubleBuffering = GS_SETTING_ON;
    ps2_gs->ZBuffering = GS_SETTING_OFF;
    ps2_gs->PrimAlphaEnable = GS_SETTING_ON;
    PS2_BootEvent("GDPS2 GFX: NTSC 640x448 CT16 double-buffer, GIF DMA init");
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    PS2_BootEvent("GDPS2 GFX: GIF DMA initialized, gsKit_init_screen begin");
    puts("GDPS2 GFX: gsKit_init_screen");
    gsKit_init_screen(ps2_gs);
    gsKit_add_vsync_handler(vblank_handler);
    PS2_BootEvent("GDPS2 GFX: gsKit_init_screen returned, TexManager begin");
    /* The original game opens dozens of atlas pages. gsKit's texture
     * manager transfers pages on bind instead of reserving all 4 MB at
     * C2D_SpriteSheetLoad time. Inline mode preserves draw ordering. */
    puts("GDPS2 GFX: TexManager init / ETM_INLINE");
    gsKit_TexManager_init(ps2_gs);
    gsKit_TexManager_setmode(ps2_gs, ETM_INLINE);
    gsKit_mode_switch(ps2_gs, GS_ONESHOT);
    gsKit_set_test(ps2_gs, GS_ZTEST_OFF);
    gsKit_set_test(ps2_gs, GS_ATEST_OFF);
    gsKit_set_primalpha(ps2_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_clamp(ps2_gs, GS_CMODE_CLAMP);
    gsKit_set_texfilter(ps2_gs, GS_FILTER_LINEAR);
    PS2_C2D_Attach(ps2_gs);
    PS2_C2D_EnableTextureStreaming(true);
    PS2_BootEvent("GDPS2 GFX: TexManager and C2D backend attached");
    puts("GDPS2 GFX: initialized");
}

void gfxExit(void)
{
    PS2_InputShutdown();
    PS2_C2D_EnableTextureStreaming(false);
    PS2_C2D_Attach(NULL);
    ps2_gs = NULL;
}

void gfxSet3D(bool enable) { (void)enable; } /* No stereoscopic GS output. */
void gfxSetWide(bool enable) { (void)enable; } /* Output stays at NTSC 640x448. */
void gfxSwapBuffers(void)
{
    if (!ps2_gs) return;
    gsKit_queue_exec(ps2_gs);
    gsKit_sync_flip(ps2_gs);
}
void gfxFlushBuffers(void) { }
void gspWaitForVBlank(void) { gsKit_vsync_wait(); }

static int create_semaphore(int initial)
{
    ee_sema_t params;
    memset(&params, 0, sizeof(params));
    params.init_count = initial;
    params.max_count = 1;
    return CreateSema(&params);
}

void LightLock_Init(LightLock *lock)
{
    if (!lock) return;
    lock->sema = create_semaphore(1);
}

void LightLock_Lock(LightLock *lock)
{
    if (!lock) return;
    if (lock->sema <= 0) LightLock_Init(lock);
    if (lock->sema > 0) WaitSema(lock->sema);
}

void LightLock_Unlock(LightLock *lock)
{
    if (lock && lock->sema > 0) SignalSema(lock->sema);
}

void LightEvent_Init(LightEvent *event, ResetType type)
{
    if (!event) return;
    if (event->inited && event->sema > 0) {
        while (PollSema(event->sema) >= 0) { }
    } else {
        event->sema = create_semaphore(0);
        event->inited = event->sema > 0;
    }
    event->type = type;
}

void LightEvent_Signal(LightEvent *event)
{
    if (!event) return;
    if (!event->inited) LightEvent_Init(event, RESET_ONESHOT);
    if (event->sema > 0) SignalSema(event->sema);
}

void LightEvent_Wait(LightEvent *event)
{
    if (!event) return;
    if (!event->inited) LightEvent_Init(event, RESET_ONESHOT);
    if (event->sema <= 0) return;
    WaitSema(event->sema);
    if (event->type == RESET_STICKY) SignalSema(event->sema);
}

void LightEvent_Clear(LightEvent *event)
{
    if (!event || !event->inited || event->sema <= 0) return;
    while (PollSema(event->sema) >= 0) { }
}

static void thread_wrapper(void *opaque)
{
    struct PspThread_s *job = opaque;
    job->fn(job->argument);
    SignalSema(job->completion);
    ExitThread();
}

Thread threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size,
                    int priority, int core_id, bool detached)
{
    (void)core_id; (void)detached;
    if (!entrypoint) return NULL;
    struct PspThread_s *job = calloc(1, sizeof(*job));
    if (!job) return NULL;
    job->completion = -1;
    if (stack_size < 16384) stack_size = 16384;
    job->stack = memalign(64, stack_size);
    if (!job->stack) goto fail;
    job->completion = create_semaphore(0);
    if (job->completion <= 0) goto fail;
    job->fn = entrypoint;
    job->argument = arg;
    ee_thread_t params;
    memset(&params, 0, sizeof(params));
    params.func = (void *)thread_wrapper;
    params.stack = job->stack;
    params.stack_size = (int)stack_size;
    params.gp_reg = &_gp;
    int ps2_priority = 0x20 + (priority - 0x30);
    if (ps2_priority < 1) ps2_priority = 1;
    if (ps2_priority > 126) ps2_priority = 126;
    params.initial_priority = ps2_priority;
    job->id = CreateThread(&params);
    if (job->id <= 0) goto fail;
    if (StartThread(job->id, job) >= 0) return job;
    DeleteThread(job->id);
fail:
    if (job->completion > 0) DeleteSema(job->completion);
    free(job->stack);
    free(job);
    return NULL;
}

Result threadJoin(Thread thread, u64 timeout_ns)
{
    if (!thread) return -1;
    /* Only infinite waits supported for now; caller expects joined audio. */
    if (timeout_ns != U64_MAX) return -2;
    if (WaitSema(thread->completion) < 0) return -1;
    DeleteThread(thread->id);
    DeleteSema(thread->completion);
    free(thread->stack);
    free(thread);
    return 0;
}

void threadFree(Thread thread) { (void)thread; }
