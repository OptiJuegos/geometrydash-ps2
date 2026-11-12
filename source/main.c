#include <citro2d.h>

#include <malloc.h>
#include <assert.h>
#ifdef __PSP__
#include <psputils.h>
#include <pspthreadman.h> // sceKernelGetSystemTimeLow (the frame timing log)
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "3ds/env.h"
#include "3ds/services/cfgu.h"
#include "objects.h"
#include "level_loading.h"
#include "main.h"
#include "graphics.h"
#include "color_channels.h"
#include "music_player.h"
#include "level/main_levels.h"
#include "fonts/bigFont.h"

#include "save/config.h"

/* The offline PS2 target does not compile source/utils/network.c and this
 * translation unit does not call the curl API. Keep the PSP/3DS include. */
#ifndef __PS2__
#include <curl/curl.h>
#endif

#include "menus/main_menu.h"
#include "menus/level_select.h"
#include "menus/icon_kit.h"
#include "menus/gameplay.h"
#include "menus/soggy.h"
#include "menus/core/ui_screen.h"

#include "player/collision.h"
#include "state.h"

#include "particles/particles.h"
#include "particles/object_particles.h"
#include "particles/coin_effect.h"

#include <stdarg.h>

#ifdef __PSP__
#include "../psp/src/psp_port.h"
#endif
#include "triggers_v2.h"

#ifdef __PS2__
#include "../ps2/src/boot_trace.h"
#include "../ps2/src/romfs_paths.h"
u32 PS2_PresentVcount(void);
#include "../ps2/src/ps2_jump_entry.h"
#include "../ps2/src/ps2_aspect.h"
#include "../ps2/src/startup_ps2.h"
#include <kernel.h>
#define GDPS2_BOOT(number, name) PS2_BootStage((number), (name))
#else
#define GDPS2_BOOT(number, name) ((void)0)
#endif

#include "player/player.h"
#include "particles/circles.h"
#include "menus/settings.h"
#include "menus/creator_menu.h"
#include "menus/external_levels.h"
#include "menus/search_menu.h"
#include "menus/saved_levels.h"
#include "menus/loading_screen.h"
#include "menus/level_complete.h"
#include "menus/online_level_menu.h"

#include "save/saving.h"

#include "endwall.h"
#include "practice.h"

#include "new_best.h"

#include "math_helpers.h"

#include "utils/precise_input.h"

#ifdef DEBUG_LEAKS
#include "utils/leaks_dbg.h"
#endif

#define CITRA_TYPE 0x20000
#define CITRA_VERSION 11

int game_state = STATE_MAIN_MENU;

bool playing_menu_loop = false;

int level_result = 0;

bool cheated = false;

float global_volume;
float music_volume;
float sound_volume;

bool cheats_used[CHEAT_COUNT];

const char *cheat_names[CHEAT_COUNT] = {
    "Noclip",
    "Hitboxes"
};


float accumulator = 0.f;
bool fixed_dt = true;

#define TARGET_FPS 60
#define PI_SUBSTEP_BUCKETS (STEPS_HZ / TARGET_FPS)

u32 pi_substep_presses[PI_SUBSTEP_BUCKETS];

PrintConsole console;

C3D_RenderTarget* top;
C3D_RenderTarget* top_right;
C3D_RenderTarget* bot;

SFX play_sound;
SFX quit_sound;
SFX explode_sound;
SFX end_sound;
SFX honk;
SFX coin_sound;

ParticleSystem touch_drag_particles;
ParticleSystem touch_explosion_particles;
ParticleSystem glitter_particles_bottom;
ParticleSystem slow_speed_particles_bottom;
ParticleSystem normal_speed_particles_bottom;
ParticleSystem fast_speed_particles_bottom;
ParticleSystem faster_speed_particles_bottom;
ParticleSystem end_wall_particles;
ParticleSystem end_wall_firework;
ParticleSystem level_complete_effect_p1;
ParticleSystem level_complete_effect_p2;

float slow_speed_particles_timer = 0.f;
float normal_speed_particles_timer = 0.f;
float fast_speed_particles_timer = 0.f;
float faster_speed_particles_timer = 0.f;

bool alt_title_screen;

// Checks if the game is being emulated by citra/azahar
bool is_citra() {
    s64 version = 0;
    svcGetSystemInfo(&version, CITRA_TYPE, CITRA_VERSION);
    return version != 0;
}

void no_dsp_firmware(void) {
    consoleInit(GFX_TOP, NULL);
    printf("\x1b[01;00H///////////////////FATAL///ERROR//////////////////");
    printf("\x1b[03;00HNDSP could not be initalized!");
    printf("\x1b[05;00HThis is probably because your dspfirm is missing.");
    printf("\x1b[07;00HFor 3DS users, open the Luma menu, go to");
    printf("\x1b[08;00Hmiscellaneous options and press \"Dump DSP");
    printf("\x1b[09;00Hfirmware\".");
    printf("\x1b[11;00HFor Citra/Azahar users, open your emulator folder");
    printf("\x1b[12;00Hand create a file named \"dspfirm.cdc\" in ");
    printf("\x1b[13;00H/sdmc/3ds/. You only need the file to be there, ");
    printf("\x1b[14;00Hit can be empty for all the emulator cares.");
    printf("\x1b[16;00HFor more information, check the GitHub.");
    printf("\x1b[18;00HPress start to exit.");
    printf("\x1b[30;00H//////////////////////////////////////////////////");
    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;

        gfxSwapBuffers();
        gfxFlushBuffers();
    }
    
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    exit(22);
}

#ifndef __PS2__
static FILE *log_file = NULL;

static void ensure_log_file(void) {
    if (log_file) return;  // already open
    
    log_file = fopen(CONFIG_ROOT "output.txt", "w");
}
#endif

static void close_log_file(void) {
#ifndef __PS2__
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
#endif
}

int output_log(const char *fmt, ...) {
#ifdef __PS2__
    /* Logs to EE stdout only. Debug output on a Memory Card is slow,
     * causes stutter in menus and can wear flash for no save-data benefit. */
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
    return ret;
#else
    ensure_log_file();

    if (!log_file) return 0; // failed to open log

    va_list args1, args2;
    int ret;

    va_start(args1, fmt);
    va_copy(args2, args1);  // duplicate args for two outputs

    // Output to log file
    ensure_log_file();
    if (log_file) {
        ret = vfprintf(log_file, fmt, args1);
        fflush(log_file);
    }

    // Output to console (stdout)
    ret = vprintf(fmt, args2);
    
    va_end(args2);
    va_end(args1);

    return ret;
#endif
}

#define MAX_AUDIO_DB 9

float get_volume_slider() {
    u8 slider = 63;
    HIDUSER_GetSoundVolume(&slider);

    return slider / 63.f;
}

float slider_to_volume(float v) {
    return expm1f(v * log1pf(MAX_AUDIO_DB)) / MAX_AUDIO_DB;
}

void set_channel_volume(int channel, float volume) {
    float mix[12] = {0};

    float gain = slider_to_volume(volume);

    mix[0] = gain;
    mix[1] = gain;

    ndspChnSetMix(channel, mix);
}

void apply_volume_settings() {
    set_channel_volume(0, music_volume);

    for (int i = 1; i <= 7; i++) {
        set_channel_volume(i, sound_volume);
    }
}

float sprite_drawing_time = 0;
float physics_calc_time = 0;
float particle_calc_time = 0;
float triggers_time = 0;

float delta = 0;
#ifdef __PSP__
bool psp_autoload_test; // started from an autoload.gmd (logs what it draws)
float psp_teleport_watch; // autoload test: seconds left of logging the player after a teleport
#endif
unsigned int level_frame = 0;
unsigned int frame_counter = 0;

bool exiting_level = false;

bool song_loaded;

void update_player_effects(float delta) {
    for (int i = 0; i < 2; i++) {
        updateParticleSystem(&drag_particles[i], delta);
        updateParticleSystem(&drag_particles_2[i], delta);
        updateParticleSystem(&ship_fire_particles[i], delta);
        updateParticleSystem(&ship_secondary_particles[i], delta);
        updateParticleSystem(&secondary_particles[i], delta);
        updateParticleSystem(&burst_particles[i], delta);
        updateParticleSystem(&land_particles[i], delta);
        updateParticleSystem(&explosion_particles[i], delta);
    }
    updateParticleSystem(&brick_destroy_particles, delta);
    updateParticleSystem(&glitter_particles, delta);
    updateParticleSystem(&slow_speed_particles, delta);
    updateParticleSystem(&normal_speed_particles, delta);
    updateParticleSystem(&fast_speed_particles, delta);
    updateParticleSystem(&faster_speed_particles, delta);
    updateParticleSystem(&coin_pickup_particles, delta);
}

void allocate_particles() {
    initParticleSystem(&drag_particles[0], &drag_effect);
    initParticleSystem(&drag_particles[1], &drag_effect);

    initParticleSystem(&drag_particles_2[0], &ship_drag_effect);
    initParticleSystem(&drag_particles_2[1], &ship_drag_effect);

    initParticleSystem(&ship_fire_particles[0], &drag_effect);
    initParticleSystem(&ship_fire_particles[1], &drag_effect);

    initParticleSystem(&secondary_particles[0], &drag_effect);
    initParticleSystem(&secondary_particles[1], &drag_effect);
    
    initParticleSystem(&ship_secondary_particles[0], &drag_effect);
    initParticleSystem(&ship_secondary_particles[1], &drag_effect);

    initParticleSystem(&burst_particles[0], &burst_effect);
    initParticleSystem(&burst_particles[1], &burst_effect);

    initParticleSystem(&land_particles[0], &land_effect);
    initParticleSystem(&land_particles[1], &land_effect);
    
    initParticleSystem(&explosion_particles[0], &explode_effect);
    initParticleSystem(&explosion_particles[1], &explode_effect);

    initParticleSystem(&brick_destroy_particles, &glass_destroy_01);
    initParticleSystem(&glitter_particles, &glitter_effect);
    initParticleSystem(&slow_speed_particles, &speed_effect_slow);
    initParticleSystem(&normal_speed_particles, &speed_effect_normal);
    initParticleSystem(&fast_speed_particles, &speed_effect_fast);
    initParticleSystem(&faster_speed_particles, &speed_effect_vfast);
    initParticleSystem(&coin_pickup_particles, &coin_pickup_effect);

    initParticleSystem(&end_wall_particles, &end_effect_portal);
    initParticleSystem(&end_wall_firework, &firework);
    initParticleSystem(&level_complete_effect_p1, &level_complete_01);
    initParticleSystem(&level_complete_effect_p2, &level_complete_01);
}

void free_particles() {
    for (int i = 0; i < 2; i++) {
        freeParticleData(&drag_particles[i].data);
        freeParticleData(&drag_particles_2[i].data);
        freeParticleData(&ship_fire_particles[i].data);
        freeParticleData(&secondary_particles[i].data);
        freeParticleData(&ship_secondary_particles[i].data);
        freeParticleData(&burst_particles[i].data);
        freeParticleData(&land_particles[i].data);
        freeParticleData(&explosion_particles[i].data);
    }

    freeParticleData(&brick_destroy_particles.data);
    freeParticleData(&glitter_particles.data);
    freeParticleData(&slow_speed_particles.data);
    freeParticleData(&normal_speed_particles.data);
    freeParticleData(&fast_speed_particles.data);
    freeParticleData(&faster_speed_particles.data);
    freeParticleData(&coin_pickup_particles.data);

    freeParticleData(&end_wall_particles.data);
    freeParticleData(&end_wall_firework.data);
    freeParticleData(&level_complete_effect_p1.data);
    freeParticleData(&level_complete_effect_p2.data);
}

void init_particles(Color p1_color, Color p2_color) {    
    slow_speed_particles.stationary = true;
    normal_speed_particles.stationary = true;
    fast_speed_particles.stationary = true;
    faster_speed_particles.stationary = true;

    slow_speed_particles.affectedByMirror = false;
    normal_speed_particles.affectedByMirror = false;
    fast_speed_particles.affectedByMirror = false;
    faster_speed_particles.affectedByMirror = false;

    end_wall_firework.affectedByMirror = false;

    level_complete_effect_p1.stationary = true;
    level_complete_effect_p2.stationary = true;

    // One shot bursts fly towards you, the trails stay glued to the player
    explosion_particles[0].depth = 1.f;
    explosion_particles[1].depth = 1.f;

    burst_particles[0].depth = 0.5f;
    burst_particles[1].depth = 0.5f;

    land_particles[0].depth = 0.35f;
    land_particles[1].depth = 0.35f;

    coin_pickup_particles.depth = 0.6f;
    brick_destroy_particles.depth = 0.6f;

    // Ambience sweeps past the player instead of sitting flat behind the level
    glitter_particles.depth = 0.4f;

    slow_speed_particles.depth = 0.6f;
    normal_speed_particles.depth = 0.6f;
    fast_speed_particles.depth = 0.6f;
    faster_speed_particles.depth = 0.6f;

    // Its particles spiral inwards, so they start at you and fall into the portal
    end_wall_particles.depth = 0.7f;
    end_wall_particles.depthInwards = true;

    end_wall_firework.depth = 0.8f;
    level_complete_effect_p1.depth = 0.8f;
    level_complete_effect_p2.depth = 0.8f;

    Color p1_not_white = get_white_if_black(p1_color);
    Color p2_not_white = get_white_if_black(p2_color);

    drag_particles[0].cfg.startColorRed   = p1_not_white.r / 255.f;
    drag_particles[0].cfg.startColorGreen = p1_not_white.g / 255.f;
    drag_particles[0].cfg.startColorBlue  = p1_not_white.b / 255.f;

    drag_particles[1].cfg.startColorRed   = p2_not_white.r / 255.f;
    drag_particles[1].cfg.startColorGreen = p2_not_white.g / 255.f;
    drag_particles[1].cfg.startColorBlue  = p2_not_white.b / 255.f;

    burst_particles[0].cfg.startColorRed   = p1_not_white.r / 255.f;
    burst_particles[0].cfg.startColorGreen = p1_not_white.g / 255.f;
    burst_particles[0].cfg.startColorBlue  = p1_not_white.b / 255.f;

    burst_particles[0].cfg.finishColorRed   = p1_not_white.r / 255.f;
    burst_particles[0].cfg.finishColorGreen = p1_not_white.g / 255.f;
    burst_particles[0].cfg.finishColorBlue  = p1_not_white.b / 255.f;

    burst_particles[1].cfg.startColorRed   = p2_not_white.r / 255.f;
    burst_particles[1].cfg.startColorGreen = p2_not_white.g / 255.f;
    burst_particles[1].cfg.startColorBlue  = p2_not_white.b / 255.f;
    
    burst_particles[1].cfg.finishColorRed   = p2_not_white.r / 255.f;
    burst_particles[1].cfg.finishColorGreen = p2_not_white.g / 255.f;
    burst_particles[1].cfg.finishColorBlue  = p2_not_white.b / 255.f;

    secondary_particles[0].cfg.startColorRed   = p2_not_white.r / 255.f;
    secondary_particles[0].cfg.startColorGreen = p2_not_white.g / 255.f;
    secondary_particles[0].cfg.startColorBlue  = p2_not_white.b / 255.f;
    secondary_particles[0].cfg.finishColorAlpha = 1.f;
    secondary_particles[0].cfg.maxParticles = 15;
    secondary_particles[0].cfg.speed = 75 - 30;

    secondary_particles[1].cfg.startColorRed   = p1_not_white.r / 255.f;
    secondary_particles[1].cfg.startColorGreen = p1_not_white.g / 255.f;
    secondary_particles[1].cfg.startColorBlue  = p1_not_white.b / 255.f;
    secondary_particles[1].cfg.finishColorAlpha = 1.f;
    secondary_particles[1].cfg.maxParticles = 15;
    secondary_particles[1].cfg.speed = 75 - 30;

    ship_secondary_particles[0].cfg.startColorRed   = 255.f/ 255.f;
    ship_secondary_particles[0].cfg.startColorGreen = 70.f / 255.f;
    ship_secondary_particles[0].cfg.startColorBlue  = 10.f / 255.f;

    ship_secondary_particles[0].cfg.finishColorRed   = 255.f/ 255.f;
    ship_secondary_particles[0].cfg.finishColorGreen = 20.f / 255.f;
    ship_secondary_particles[0].cfg.finishColorBlue  = 0.f / 255.f;

    ship_secondary_particles[1].cfg = ship_secondary_particles[0].cfg;

    ship_fire_particles[0].cfg.startColorRed   = 255.f / 255.f;
    ship_fire_particles[0].cfg.startColorGreen = 170.f / 255.f;
    ship_fire_particles[0].cfg.startColorBlue  = 10.f / 255.f;
    
    ship_fire_particles[0].cfg.finishColorRed   = 255.f / 255.f;
    ship_fire_particles[0].cfg.finishColorGreen = 65.f / 255.f;
    ship_fire_particles[0].cfg.finishColorBlue  = 0.f / 255.f;

    ship_fire_particles[0].cfg.speed = 75 - 30;
    ship_fire_particles[0].cfg.maxRadius = 130;
    ship_fire_particles[1].cfg = ship_fire_particles[0].cfg;

    land_particles[0].cfg.startColorRed   = p1_not_white.r / 255.f;
    land_particles[0].cfg.startColorGreen = p1_not_white.g / 255.f;
    land_particles[0].cfg.startColorBlue  = p1_not_white.b / 255.f;

    land_particles[1].cfg.startColorRed   = p2_not_white.r / 255.f;
    land_particles[1].cfg.startColorGreen = p2_not_white.g / 255.f;
    land_particles[1].cfg.startColorBlue  = p2_not_white.b / 255.f;

    explosion_particles[0].cfg.startColorRed   = p1_not_white.r / 255.f;
    explosion_particles[0].cfg.startColorGreen = p1_not_white.g / 255.f;
    explosion_particles[0].cfg.startColorBlue  = p1_not_white.b / 255.f;

    explosion_particles[1].cfg.startColorRed   = p2_not_white.r / 255.f;
    explosion_particles[1].cfg.startColorGreen = p2_not_white.g / 255.f;
    explosion_particles[1].cfg.startColorBlue  = p2_not_white.b / 255.f;

    glitter_particles.cfg.startColorRed   = p1_not_white.r / 255.f;
    glitter_particles.cfg.startColorGreen = p1_not_white.g / 255.f;
    glitter_particles.cfg.startColorBlue  = p1_not_white.b / 255.f;
    glitter_particles.cfg.startColorAlpha = 1.f;
    
    glitter_particles.cfg.finishColorRed   = p1_not_white.r / 255.f;
    glitter_particles.cfg.finishColorGreen = p1_not_white.g / 255.f;
    glitter_particles.cfg.finishColorBlue  = p1_not_white.b / 255.f;

    slow_speed_particles.cfg.startColorRed   = 255 / 255.f;
    slow_speed_particles.cfg.startColorGreen = 255 / 255.f;
    slow_speed_particles.cfg.startColorBlue  = 0 / 255.f;

    normal_speed_particles.cfg.startColorRed   = 0 / 255.f;
    normal_speed_particles.cfg.startColorGreen = 190 / 255.f;
    normal_speed_particles.cfg.startColorBlue  = 255 / 255.f;

    fast_speed_particles.cfg.startColorRed   = 0 / 255.f;
    fast_speed_particles.cfg.startColorGreen = 255 / 255.f;
    fast_speed_particles.cfg.startColorBlue  = 0 / 255.f;

    faster_speed_particles.cfg.startColorRed   = 230 / 255.f;
    faster_speed_particles.cfg.startColorGreen = 65 / 255.f;
    faster_speed_particles.cfg.startColorBlue  = 255 / 255.f;

    coin_pickup_particles.cfg.startColorRed   = 255 / 255.f;
    coin_pickup_particles.cfg.startColorGreen = 190 / 255.f;
    coin_pickup_particles.cfg.startColorBlue  = 0 / 255.f;
    
    end_wall_particles.cfg.startColorRed   = p1_not_white.r / 255.f;
    end_wall_particles.cfg.startColorGreen = p1_not_white.g / 255.f;
    end_wall_particles.cfg.startColorBlue  = p1_not_white.b / 255.f;
    
    end_wall_particles.cfg.finishColorRed   = p1_not_white.r / 255.f;
    end_wall_particles.cfg.finishColorGreen = p1_not_white.g / 255.f;
    end_wall_particles.cfg.finishColorBlue  = p1_not_white.b / 255.f;

    end_wall_particles.scale = 1.5f;

    end_wall_firework.cfg.startColorRed   = p1_not_white.r / 255.f;
    end_wall_firework.cfg.startColorGreen = p1_not_white.g / 255.f;
    end_wall_firework.cfg.startColorBlue  = p1_not_white.b / 255.f;
    
    end_wall_firework.cfg.finishColorRed   = p1_not_white.r / 255.f;
    end_wall_firework.cfg.finishColorGreen = p1_not_white.g / 255.f;
    end_wall_firework.cfg.finishColorBlue  = p1_not_white.b / 255.f;

    
    level_complete_effect_p1.cfg.startColorRed   = p1_not_white.r / 255.f;
    level_complete_effect_p1.cfg.startColorGreen = p1_not_white.g / 255.f;
    level_complete_effect_p1.cfg.startColorBlue  = p1_not_white.b / 255.f;

    level_complete_effect_p1.cfg.finishColorRed   = p1_not_white.r / 255.f;
    level_complete_effect_p1.cfg.finishColorGreen = p1_not_white.g / 255.f;
    level_complete_effect_p1.cfg.finishColorBlue  = p1_not_white.b / 255.f;
    
    level_complete_effect_p2.cfg.startColorRed   = p2_not_white.r / 255.f;
    level_complete_effect_p2.cfg.startColorGreen = p2_not_white.g / 255.f;
    level_complete_effect_p2.cfg.startColorBlue  = p2_not_white.b / 255.f;

    level_complete_effect_p2.cfg.finishColorRed   = p2_not_white.r / 255.f;
    level_complete_effect_p2.cfg.finishColorGreen = p2_not_white.g / 255.f;
    level_complete_effect_p2.cfg.finishColorBlue  = p2_not_white.b / 255.f;
}

u32 jump_key_mask(void) {
    return (settingsState.yJump ? KEY_Y : KEY_A) | KEY_UP;
}

static bool touch_jump_filter(u16 px, u16 py) {
    bool on_pause_button = px > 320 - 30 && py < 30;
    bool on_practice_ui = state.practice_mode && (px > 92 && px < 222 && py > 175 && py < 222);
    return !(on_pause_button || on_practice_ui);
}

void sync_precise_input(bool suppress_held) {
    pi_set_jump_keys(jump_key_mask());
    pi_set_touch_filter(touch_jump_filter);
    pi_reset();
    if (suppress_held) {
        pi_suppress_until_release();
    }
}

void game_loop() {
#ifdef DEBUG_LEAKS
    current_generation++;
    dump_leaks();
#endif

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_Fade(0);
    for (int eye = 0; begin_top_eye(eye); eye++) {
        begin_eye_layer(DEPTH_UI);
#ifdef __PS2__
        /* Right-aligned loading caption must stay inside the 320x240 crop. */
        draw_text(&bigFont_fontCharset, &bigFont_sheet,
                  GDPS2_CROP_X + GDPS2_VISIBLE_W - 12, SCREEN_HEIGHT - 10,
                  0.5f, 0.5f, 1.0f, true, "Loading...");
#elif defined(__PSP__)
        // the PSP cuts 8 lines off the top screen's bottom (480x272)
        draw_text(&bigFont_fontCharset, &bigFont_sheet, SCREEN_WIDTH - 14, SCREEN_HEIGHT - 22, 0.5f, 0.5f, 1.0f, true, "Loading...");
#else
        draw_text(&bigFont_fontCharset, &bigFont_sheet, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10, 0.5f, 0.5f, 1.0f, true, "Loading...");
#endif
        end_eye_layer();
    }
    C3D_FrameEnd(0);
    
    char *path;

    if (state.custom_level) {
        path = state.custom_level_path;
    } else {
        path = main_levels[curr_level_id].gmd_path;
    }
#ifdef __PS2__
    printf("GDPS2 LEVEL: selected_index=%d custom=%d path=%s\n",
           curr_level_id, state.custom_level, path ? path : "(null)");
#endif

    update_player_colors();

    int returned = load_level(path);
    level_result = returned;
    if (returned) {
        printf("\x1b[9;1HFailed %d", returned);

        game_state = (state.custom_level ? STATE_EXTERNAL_LEVELS : STATE_LEVEL_SELECT);
        return;
    }

    if (!state.custom_level) {
        snprintf(level_info.level_name, sizeof(level_info.level_name), "%s", main_levels[curr_level_id].level_name);
    }

    play_level_song(level_info.song_offset);

    if (song_loaded) pause_playback_music();

    set_fade_status(FADE_STATUS_IN);

    bool being_faded = true;

    first_load_init_variables();

#ifdef PS2_DIAG_NOCLIP_LEVEL
    /* Only a separate instrumentation ELF: traverse long levels without
     * periodic death/reload stalls masquerading as mid-level frame spikes.
     * NEVER enabled in the release build or normal player controls. */
    state.noclip = true;
    puts("GDPS2 FT: diagnostic noclip ON (release unaffected)");
#endif

    init_op_system();

    gameplay_screen_init();
    
    // Particle
    allocate_particles();
    init_particles(p1_color, p2_color);
    clear_practice_mode();

    exiting_level = false;
    fixed_dt = true;

    /* PS2: sample the pad again after loading. CROSS can confirm the menu
     * and jump, so suppress only that inherited CROSS, if it remains held.
     * Do not arm the gate for UP/analog movement; an unrelated held input
     * must never prevent later CROSS presses from jumping. */
#ifdef __PS2__
    hidScanInput();
    const u32 ps2_inherited_menu_jump_mask =
        GDPS2_MenuConfirmJumpMask(jump_key_mask());
    bool ps2_wait_for_jump_release = GDPS2_JumpEntryHeld(
        hidKeysHeld(), ps2_inherited_menu_jump_mask);
    sync_precise_input(ps2_wait_for_jump_release);
#else
    sync_precise_input(false);
#endif
    memset(pi_substep_presses, 0, sizeof(pi_substep_presses));
#ifdef __PSP__
    // which level, for a log that ends in a crash
    psp_log("level: %s\n", level_info.level_name);
    psp_log_memory("level loaded");
#endif

    u64 lastTime = svcGetSystemTick();
    u64 start = svcGetSystemTick();
#ifdef PS2_DIAG_FRAME_TIMING
    /* Per-120-frame windows, not printf every frame: logging itself would
     * cause the frametime spikes we are trying to measure. */
    unsigned profile_frames = 0, profile_over20 = 0, profile_over33 = 0;
    unsigned profile_max_steps = 0;
    float profile_frame_sum = 0, profile_update_sum = 0, profile_draw_sum = 0;
    float profile_frame_peak = 0, profile_update_peak = 0, profile_draw_peak = 0;
    /* update split: sum and peak of physics, collision, triggers, objects, particles */
    float prof_sec_sum[7] = {0}, prof_sec_peak[7] = {0};
    unsigned long prof_sprites_sum = 0;
    unsigned prof_sprites_peak = 0;
    float prof_objdraw_sum = 0, prof_objdraw_peak = 0;
    float prof_player_sum = 0, prof_player_peak = 0;
    /* triggers split: handle_triggers, trig2_update, obj_anim_tick, colors+lbg */
    float prof_trg_sum[4] = {0}, prof_trg_peak[4] = {0}, prof_trg[4] = {0};
#endif
    bool old_wide = settingsState.wideEnabled;
    bool old_stereo = settingsState.stereoEnabled;

    // Main loop
    while (aptMainLoop()) {
        start = svcGetSystemTick();
        hidScanInput();

        pi_poll();
        
        touchPosition touchPos;
        hidTouchRead(&touchPos);

        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kUp = hidKeysUp();
        static u32 kHeldPaused;

        state.hitbox_display = 0;

        if (kDown & KEY_X && settingsState.enableDebugBindings) {
            state.noclip ^= 1;
        }

        if (state.noclip) {
            cheated = true;
            cheats_used[CHEAT_NOCLIP] = true;
        }

        if ((kDown & KEY_L) && (kHeld & KEY_B) && settingsState.enableDebugBindings)
            state.profiling ^= 1;

        if ((kDown & KEY_R) && (kHeld & KEY_B) && settingsState.enableDebugBindings) {
            cheated = true;
            cheats_used[CHEAT_HITBOX_DISPLAY] = true;
            if (settingsState.hitboxesEnabled && settingsState.hitboxTrail) {
                settingsState.hitboxesEnabled = false;
                settingsState.hitboxTrail = false;
            } else if (settingsState.hitboxesEnabled && !settingsState.hitboxTrail) {
                settingsState.hitboxTrail = true;
            } else settingsState.hitboxesEnabled = true;
        }      

        if (settingsState.hitboxesEnabled || state.hitbox_enabled_when_dead) {
            state.hitbox_display = 1;
        }
        
        if (settingsState.hitboxTrail) {
            settingsState.hitboxesEnabled = true;
            state.hitbox_display = 2;
        }

        if (settingsState.hitboxesEnabled || settingsState.hitboxTrail) {
            cheated = true;
            cheats_used[CHEAT_HITBOX_DISPLAY] = true;
        }
        
        int steps = 0;

        bool in_bounds = touch_jump_filter(touchPos.px, touchPos.py);
        
        kHeldPaused &= ~kUp;
        if(!game_paused){
            kHeld &= ~kHeldPaused;
        }
        
        global_volume = get_volume_slider();

        bool buttonPressed = (kDown & jump_key_mask()) != 0;
        bool buttonHeld = (kHeld & jump_key_mask()) != 0;

        bool touch_pressed = in_bounds && (kDown & KEY_TOUCH);
        bool touch_held = in_bounds && (kHeld & KEY_TOUCH);

#ifdef __PS2__
        /* Block only a held menu press, never a fresh press after release. */
        if (GDPS2_BlockInheritedJump(&ps2_wait_for_jump_release,
                                     kHeld, ps2_inherited_menu_jump_mask)) {
            buttonPressed = buttonHeld = touch_pressed = touch_held = false;
        }
#endif

        if (!pi_enabled) {
            state.old_input = state.input;
            state.input.pressedJump = (buttonPressed || touch_pressed) == true;
            state.input.holdJump = (state.input.pressedJump || buttonHeld || touch_held) == true;
        }
        
        for (int i = 0; i < 2; i++) {
            drag_particles[i].emitting = false;
            drag_particles_2[i].stationary = true;
            drag_particles_2[i].emitting = false;
            ship_fire_particles[i].emitting = false;
            secondary_particles[i].emitting = false;
            ship_secondary_particles[i].emitting = false;
            burst_particles[i].emitting = false;
            land_particles[i].emitting = false;
        }

        u64 now = svcGetSystemTick();
        delta = (now - lastTime) / (CPU_TICKS_PER_MSEC * 1000);
#if defined(__PSP__) || defined(__PS2__)
        // Frames go on screen on vblanks (59.94 Hz): the time between two of
        // them is a whole number of vblanks, exactly. Measured with the clock
        // it came a little early or late, and a frame ran 3 or 5 physics
        // steps instead of 4, which looked like stutter
        {
            static u32 last_vc;
#ifdef __PS2__
            u32 vc = PS2_PresentVcount();
#else
            u32 vc = PSP_PresentVcount();
#endif
            u32 k = vc - last_vc;
            if (last_vc && k >= 1 && k <= 15) delta = k / 59.94f;
            last_vc = vc;
        }
#endif
        u64 frame_window_start = lastTime;
        lastTime = now;

        if (state.death_timer <= 0)  {
            float physics_delta = delta;
            physics_calc_time = 0;
            number_of_collisions = 0;
            number_of_collisions_checks = 0;
            collision_time = 0;
            player_time = 0;
            handle_player_time = 0;
            
            brick_destroy_particles.emitting = false;
            glitter_particles.emitting = false;
            glitter_particles_bottom.emitting = false;
            slow_speed_particles_bottom.emitting = false;
            normal_speed_particles_bottom.emitting = false;
            fast_speed_particles_bottom.emitting = false;
            faster_speed_particles_bottom.emitting = false;
            
            if (physics_delta > 0.5f) physics_delta = STEPS_DT_UNMOD; // Avoid spiral of death

            if (!game_paused) {
                if (fixed_dt) {
                    physics_delta = STEPS_DT_UNMOD;
                    if (!being_faded) fixed_dt = false;
                }
                accumulator += physics_delta;

                // in case of merge conflicts: this needs to stay after the accumulator deposit above.
                // planned is how many substeps this frame is about to run, so if we calculate it
                // before the deposit, the accumulator only has the leftover from last frame and
                // planned will always be 1, making every input land on a single substep like normal
                // frame latching (no error, it will just silently get less precise)
                u32 planned = (u32)(accumulator / STEPS_DT_UNMOD);
                pi_begin_frame((u32)frame_window_start, (u32)now, planned ? planned : 1);

                // Run simulation in fixed steps
                while (accumulator >= STEPS_DT_UNMOD) {
                    u64 start_physics = svcGetSystemTick();

                    if (pi_enabled) {
                        pi_apply_substep((u32)steps);
                        state.old_input = state.input;
                        state.input.pressedJump = pi_pressed() == true;
                        state.input.holdJump = (pi_hold() || state.input.pressedJump) == true;
#ifdef __PS2__
                        if (ps2_wait_for_jump_release) {
                            state.input.pressedJump = false;
                            state.input.holdJump = false;
                        }
#endif
                        if (pi_pressed()){
                            pi_substep_presses[steps < PI_SUBSTEP_BUCKETS ? steps : PI_SUBSTEP_BUCKETS - 1]++;
                        }
                    }

                    state.current_player = 0;
                    state.old_player = state.player;

                    trail = &trail_p1;
                    wave_trail = &wave_trail_p1;
                    handle_player(&state.player);
                    handle_mirror_transition();

                    state.level_progress = (state.player.x / level_info.last_obj_x) * 100;
                    if (state.level_progress < 0) {
                        state.level_progress = 0;
                    }
                    
                    if (state.level_progress > 100) {
                        state.level_progress = 100;
                    }

                    if (state.dead) break;
                    
                    handle_auto_checkpoints(STEPS_DT);

                    if (state.dual) {
                        // Run second player
                        state.old_player = state.player2;
                        state.current_player = 1;
                        trail = &trail_p2;
                        wave_trail = &wave_trail_p2;
                        handle_player(&state.player2);

                        if (state.dead) break;
                    }
                    
                    run_camera();
                    handle_bg_flash();
                    handle_respawn_effect();

                    u64 end_physics = svcGetSystemTick();
                    float physics_time = (end_physics - start_physics) / (CPU_TICKS_PER_MSEC);

#if defined(__PSP__) || defined(__PS2__)
                    /* PSP and PS2 have one EE/CPU running gameplay alongside
                     * audio work. Enlarging STEPS_DT after a slow physics
                     * iteration makes the next step jump forward and then
                     * subtracts too much from the fixed-step accumulator.
                     * Let that accumulator catch up instead, exactly as the
                     * PSP build does; never stretch one collision step. */
                    frame_skipped = 0;
#else
                    if (physics_time / 1000 >= STEPS_DT_UNMOD) {
                        frame_skipped = (int) ((physics_time / 1000) * STEPS_HZ);
                    }
                    else frame_skipped = 0;
#endif

                    physics_calc_time += physics_time;

                    accumulator -= STEPS_DT;
                    steps++;
                    level_frame++;
                }
            }
        }

        if (!game_paused) {
            frame_counter++;

            if (state.dead && state.death_timer <= 0.f) {
                state.death_timer = (settingsState.quickRetry ? 0.5f : 1.f);
                bool had_new_best = false;
                if (!cheated) {
                    LevelData *level_data_sel = (state.custom_level ? &level_data : &main_level_data[curr_level_id]);
                    // Save new best
                    int progress = (int)state.level_progress;
                    if (state.practice_mode) {
                        if (state.current_data.max_practice < progress) {
                            state.current_data.max_practice = progress;
                            level_data_sel->practice_progress = progress;
                        }
                    } else {
                        if (state.current_data.max_normal < progress) {
                            init_new_best_popup(progress);
                            had_new_best = true;
                            state.current_data.max_normal = progress;
                            level_data_sel->normal_progress = progress;
                        }
                    }
                }

                // New best makes the respawn timer a bit longer
                if (had_new_best) state.death_timer = 1.4f;

                handle_death((state.current_player == 1) ? &state.player2 : &state.player, true);
            }

            if (state.death_timer > 0.f) {
                state.death_timer -= delta;

                fade_to_amplitude(0);

                if (state.player.gamemode == GAMEMODE_DART) {
                    if (wave_trail_p1.opacity > 0) wave_trail_p1.opacity -= 0.02f * 4;
            
                    if (wave_trail_p1.opacity <= 0) {
                        wave_trail_p1.opacity = 0;
                        wave_trail_p1.nuPoints = 0;
                    }
                }

                if (state.player2.gamemode == GAMEMODE_DART) {
                    if (wave_trail_p2.opacity > 0) wave_trail_p2.opacity -= 0.02f * 4;

                    if (wave_trail_p2.opacity <= 0) {
                        wave_trail_p2.opacity = 0;
                        wave_trail_p2.nuPoints = 0;
                    }
                }
                
                handle_mirror_transition();

                if (state.death_timer <= 0.f) {
                    init_variables();
                    reload_level(); 
                    //resets UI coins on gameplay screen
                    reset_coins();

                    if (state.practice_mode) {
                        if (checkpoint_count > 0) {
                            restore_checkpoint();
                        } else {
                            seek_music(level_info.song_offset);
                        }
                    }

                    if (song_loaded) unpause_playback_music();
                    sync_precise_input(false);
                    fixed_dt = true;
                    state.dead = false;
                    state.hitbox_display = 0;
                }
            }

            handle_practice_mode();

            u64 start_trig = svcGetSystemTick();
#if defined(__PSP__) || defined(__PS2__)
            // the channels' own colors back, before triggers change them
            trig2_unpulse_channels();
#endif
            handle_triggers();
#ifdef PS2_DIAG_FRAME_TIMING
            u64 t_trg1 = svcGetSystemTick();
#endif
#if defined(__PSP__) || defined(__PS2__)
            trig2_update(delta); // moves, fades, spawns, pulses
#ifdef PS2_DIAG_FRAME_TIMING
            u64 t_trg2 = svcGetSystemTick();
#endif
            obj_anim_tick(delta); // fire, water, monsters (graphics.c)
#endif
#ifdef PS2_DIAG_FRAME_TIMING
            u64 t_trg3 = svcGetSystemTick();
#endif
            handle_col_triggers();
            calculate_lbg();
#if defined(__PSP__) || defined(__PS2__)
            trig2_pulse_channels(); // and the pulses over them, for this frame
#endif
            u64 end_trig = svcGetSystemTick();
#ifdef PS2_DIAG_FRAME_TIMING
            prof_trg[0] = (t_trg1 - start_trig) / CPU_TICKS_PER_MSEC;
            prof_trg[1] = (t_trg2 - t_trg1) / CPU_TICKS_PER_MSEC;
            prof_trg[2] = (t_trg3 - t_trg2) / CPU_TICKS_PER_MSEC;
            prof_trg[3] = (end_trig - t_trg3) / CPU_TICKS_PER_MSEC;
#endif
            u64 ticks_trig = end_trig - start_trig;
            triggers_time = ticks_trig / CPU_TICKS_PER_MSEC;

            u64 start_obj = svcGetSystemTick();
            create_objects();
            u64 end_obj = svcGetSystemTick();
            u64 ticks_obj = end_obj - start_obj;
            sprite_drawing_time = ticks_obj / CPU_TICKS_PER_MSEC;

            u64 start_part = svcGetSystemTick();
            update_player_effects(delta);

            // End wall particles
            if (level_info.wall_y > 0) {
                updateParticleSystem(&end_wall_particles, delta);
                updateParticleSystem(&end_wall_firework, delta);
                updateParticleSystem(&level_complete_effect_p1, delta);
                updateParticleSystem(&level_complete_effect_p2, delta);

                handle_level_complete_popup(delta);
            }

            handle_new_best_popup(delta);

            update_collect_effect(delta);

            float calc_x_speed_particles = SCREEN_WIDTH_AREA;
            float calc_y_speed_particles = (SCREEN_HEIGHT_AREA / 2);

            slow_speed_particles.emitterX = calc_x_speed_particles;
            slow_speed_particles.emitterY = calc_y_speed_particles;
            slow_speed_particles.emitting = slow_speed_particles_timer > 0;
            if (slow_speed_particles_timer > 0) {
                slow_speed_particles_timer -= delta;
            }

            normal_speed_particles.emitterX = calc_x_speed_particles;
            normal_speed_particles.emitterY = calc_y_speed_particles;
            normal_speed_particles.emitting = normal_speed_particles_timer > 0;
            if (normal_speed_particles_timer > 0) {
                normal_speed_particles_timer -= delta;
            }

            fast_speed_particles.emitterX = calc_x_speed_particles;
            fast_speed_particles.emitterY = calc_y_speed_particles;
            fast_speed_particles.emitting = fast_speed_particles_timer > 0;
            if (fast_speed_particles_timer > 0) {
                fast_speed_particles_timer -= delta;
            }

            faster_speed_particles.emitterX = calc_x_speed_particles;
            faster_speed_particles.emitterY = calc_y_speed_particles;
            faster_speed_particles.emitting = faster_speed_particles_timer > 0;
            if (faster_speed_particles_timer > 0) {
                faster_speed_particles_timer -= delta;
            }
            
            end_wall_particles.emitterX = level_info.wall_x - 15;
            end_wall_particles.emitterY = level_info.wall_y;
            end_wall_particles.emitting = level_info.wall_y > 0;

            update_use_effects(delta, get_use_effect_array_ptr(GFX_TOP));
            update_use_effects(delta, get_use_effect_array_ptr(GFX_TOP_BUT_ABOVE_LEVEL));
            update_object_particles(delta);
            u64 end_part = svcGetSystemTick();
            u64 ticks_part = end_part - start_part;
            particle_calc_time = ticks_part / CPU_TICKS_PER_MSEC;

            // Update trails
            MotionTrail_Update(&trail_p1, delta);
            MotionTrail_UpdateWaveTrail(&wave_trail_p1, delta);
            update_p1_trail(&state.player, 0);

            MotionTrail_Update(&trail_p2, delta);
            MotionTrail_UpdateWaveTrail(&wave_trail_p2, delta);
            update_p1_trail(&state.player2, 1);
#ifdef __PSP__
            {
                // autoload test: what's on screen and its colors, now and then
                extern bool psp_autoload_test;
                static float dump_t;
                if (psp_autoload_test && psp_teleport_watch > 0) {
                    psp_teleport_watch -= delta;
                    psp_log("  after teleport: %.0f,%.1f vel %.0f ground %d ud %d\n", state.player.x, state.player.y,
                            state.player.vel_y, state.player.on_ground, state.player.upside_down);
                }
                dump_t += delta;
                if (psp_autoload_test && dump_t > 2.f) {
                    dump_t = 0;
                    psp_log("dump at x %.0f\n", state.player.x);
                    for (int o = 0; o < objects.count; o++) {
                        if (objects.x[o] < state.camera_x || objects.x[o] > state.camera_x + 420) continue;
                        if (!is_valid_object(objects.id[o])) continue;
                        int cm = objects.col_channel[o], cd = objects.detail_col_channel[o];
                        Color a = channels[get_col_channel_index(v2_channel(cm))].color;
                        Color b = channels[get_col_channel_index(v2_channel(cd))].color;
                        psp_log("  id %d at %.0f,%.0f v19 %d main %d (%d,%d,%d) detail %d (%d,%d,%d)\n",
                                objects.id[o], objects.x[o], objects.y[o], objects.v1p9_col_channel[o],
                                cm, a.r, a.g, a.b, cd, b.r, b.g, b.b);
                    }
                }
            }
#endif

            // If not dual, fade the p2 wave trial
            if (!state.dual) {
                if (wave_trail_p2.opacity > 0) wave_trail_p2.opacity -= (0.02f * STEPS_HZ) * delta;
                
                if (wave_trail_p2.opacity <= 0) {
                    wave_trail_p2.opacity = 0;
                    wave_trail_p2.nuPoints = 0;
                }
            }

            if (state.mirroring) {
                state.mirror_timer += delta;
                if (state.mirror_timer > MIRROR_DURATION) {
                    state.mirroring = false;
                    state.mirror_factor = state.intended_mirror_factor;
                    state.mirror_speed_factor = 1 - 2*state.mirror_factor;

                    // When mirror transition ends, put a wave trail point
                    if (state.player.gamemode == GAMEMODE_DART) {
                        wave_trail_p1.positionR = (Vec2D){ state.player.x, state.player.y };
                        MotionTrail_AddWavePoint(&wave_trail_p1);
                    }
                    if (state.dual && state.player2.gamemode == GAMEMODE_DART) {
                        wave_trail_p2.positionR = (Vec2D){ state.player2.x, state.player2.y };
                        MotionTrail_AddWavePoint(&wave_trail_p2);
                    }
                }
            }
        } else{
            kHeldPaused = kHeld;
        }
        
        // If the wide or 3D settings have been changed, reinitialize screens
        if (settingsState.wideEnabled != old_wide || settingsState.stereoEnabled != old_stereo) {
            gspWaitForVBlank();
            apply_screen_modes();
            gspWaitForVBlank();
            reinitialize_screens();
            old_wide = settingsState.wideEnabled;
            old_stereo = settingsState.stereoEnabled;
        }

        // Frees a render target, so keep it out of the frame below
        update_stereo_target();

        // Handle level being completed
        if (level_info.completing) {
            int status = handle_wall_cutscene(delta);
            // Exiting
            if (status == 1) {
                exiting_level = true;
                // Restarting level
            } else if (status == 2) {
                level_info.completing = false;
                level_complete_destroy();
                continue;
            }
        }

        u64 end = svcGetSystemTick();
        u64 ticks = end - start;
#ifdef __PSP__
        u32 perf_t0 = sceKernelGetSystemTimeLow(), perf_t1 = 0, perf_t2 = 0;
#endif

        // Render the scene
        do {
            update_bottom_particles(delta);
            update_touch_effect(delta);

#ifdef __PSP__
            perf_t1 = sceKernelGetSystemTimeLow();
#endif
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
#ifdef __PSP__
            perf_t2 = sceKernelGetSystemTimeLow();
#endif
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
            draw_fade();

            // Top screen, drawn once per eye when 3D is on
            for (int eye = 0; begin_top_eye(eye); eye++) {
                begin_eye_layer(DEPTH_BACKGROUND);
                draw_background(state.background_x / 8, -(state.camera_y / 8) + 200);
                end_eye_layer();

                C2D_ViewScale(SCALE, SCALE);
                C2D_ViewTranslate(0, CAM_Y_MTX_OFFSET);

                // The level rides in front of the screen, the background stays way back
                begin_eye_layer(DEPTH_LEVEL);

                draw_objects();

                draw_end_wall(delta);

                draw_attempt_text();

                draw_ground(state.ground_x, state.camera_y, 0, false, SCREEN_WIDTH);

                if (state.ground_y_gfx > 2) {
                    if (state.camera_y - LEVEL_Y_OFFSET + state.ground_y_gfx > 0) draw_ground(state.ground_x, state.camera_y, state.camera_y + state.ground_y_gfx - LEVEL_Y_OFFSET, false, SCREEN_WIDTH);
                    draw_ground(state.ground_x, state.camera_y, state.camera_y - LEVEL_Y_OFFSET + SCREEN_HEIGHT_AREA - state.ground_y_gfx, true, SCREEN_WIDTH);
                }

                change_blending(true);
                draw_use_effects(get_use_effect_array_ptr(GFX_TOP_BUT_ABOVE_LEVEL));

                if (level_info.wall_y > 0) {
                    drawParticleSystem(&end_wall_firework, 0, 0, 1);
                    drawParticleSystem(&level_complete_effect_p1, 0, 0, 1);
                    drawParticleSystem(&level_complete_effect_p2, 0, 0, 1);
                }

                end_eye_layer();

                change_blending(false);

                if (level_info.wall_y > 0) {
                    begin_eye_layer(DEPTH_POPUP);
                    draw_level_complete_popup();
                    end_eye_layer();
                }

                begin_eye_layer(DEPTH_POPUP);
                draw_new_best_popup();
                end_eye_layer();

                C2D_ViewTranslate(0, -CAM_Y_MTX_OFFSET);
                C2D_ViewScale(1/SCALE, 1/SCALE);

                begin_eye_layer(DEPTH_POPUP);
                gameplay_screen_top_loop();
                draw_level_complete_top();
                end_eye_layer();
            }

            // Bottom screen
            C2D_SceneBegin(bot);
            C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));

#ifndef __PS2__
            /* On 3DS the second physical framebuffer needs its own
             * background. On PS2 both targets share the same GS framebuffer:
             * drawing this background after the level completely obscured
             * the original game's objects and player. The top scene already
             * fills the PS2 stage; keep bottom HUD/touch effects below. */
            draw_background((state.background_x / 8) + 40, 200);
#endif

            C2D_ViewScale(SCALE, SCALE);
            C2D_ViewTranslate(0, CAM_Y_MTX_OFFSET);
            
            change_blending(true);
            draw_bottom_particles();
            change_blending(false);

#ifndef __PS2__
            draw_ground(state.ground_x + 52.5f, 0.f, -71.f, false, SCREEN_BOT_WIDTH);
            draw_ground(state.ground_x + 52.5f, 0.f, 210.f, true, SCREEN_BOT_WIDTH);
#endif

            C2D_ViewTranslate(0, -CAM_Y_MTX_OFFSET);
            C2D_ViewScale(1/SCALE, 1/SCALE);
            
            change_blending(true);
            draw_touch_effect();
            change_blending(false);

            gameplay_screen_bot_loop();
            draw_level_complete();

            if (state.profiling) {
                float processingTime = ((ticks / CPU_TICKS_PER_MSEC)) * 6;
                float drawingTime = C3D_GetDrawingTime() * 6;
                float fps = 1 / delta;
                if (fps > 60) fps = 60;

                #define DEBUG_TEXT_SCALE 0.4f, 0.4f
                
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0, 6,  DEBUG_TEXT_SCALE, 0, true, "CPU: %6.2f%% (%6.2f%% %6.2f%%)", (C3D_GetProcessingTime() * 6) + processingTime, C3D_GetProcessingTime() * 6, processingTime);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0, 18, DEBUG_TEXT_SCALE, 0, true, "GPU: %6.2f%%", drawingTime);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0, 30, DEBUG_TEXT_SCALE, 0, true, "FPS: %6.1f", fps);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0, 42, DEBUG_TEXT_SCALE, 0, true, "Linear free: %d", linearSpaceFree());
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 42, DEBUG_TEXT_SCALE, 0, true, "CMDBuf: %6.2f%%", C3D_GetCmdBufUsage()*100.0f);

                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 66,  DEBUG_TEXT_SCALE, 0, true, "%d steps", steps);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 54,  DEBUG_TEXT_SCALE, 0, true, "Particle: %6.2f%%", particle_calc_time * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 78,  DEBUG_TEXT_SCALE, 0, true, "Triggers: %6.2f%%", triggers_time * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 90,  DEBUG_TEXT_SCALE, 0, true, "Collision %d/%d", number_of_collisions, number_of_collisions_checks);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 102, DEBUG_TEXT_SCALE, 0, true, "Physics: %6.2f%%", physics_calc_time * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 114, DEBUG_TEXT_SCALE, 0, true, " - Coll: %6.2f%%", collision_time * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 126, DEBUG_TEXT_SCALE, 0, true, " - Play: %6.2f%%", player_time * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 180, 138, DEBUG_TEXT_SCALE, 0, true, " - Hndl: %6.2f%%", handle_player_time * 6);

                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   54,  DEBUG_TEXT_SCALE, 0, true, "SprDraw:  %6.2f%%", (sprite_drawing_time) * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   66,  DEBUG_TEXT_SCALE, 0, true, " - Creating: %6.2f%%", (object_creating_time) * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   78,  DEBUG_TEXT_SCALE, 0, true, " - Sorting:  %6.2f%%", (object_sorting_time) * 6);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   102,  DEBUG_TEXT_SCALE, 0, true, "Drawing:  %6.2f%%", (object_drawing_time) * 6);

                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   114,  DEBUG_TEXT_SCALE, 0, true, "Touch:  %d, %d", touchPos.px, touchPos.py);

                // im jut going to assume 60fps for this (4 buckets)
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   126,  DEBUG_TEXT_SCALE, 0, true, "InputTicks: %d|%d|%d|%d", (int)pi_substep_presses[0], (int)pi_substep_presses[1], (int)pi_substep_presses[2], (int)pi_substep_presses[3]);

                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   138,  DEBUG_TEXT_SCALE, 0, true, "Player");
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   150,  DEBUG_TEXT_SCALE, 0, true, "- Tick: %d", state.player.frame);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   162,  DEBUG_TEXT_SCALE, 0, true, "- X: %.2f", state.player.x);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   174,  DEBUG_TEXT_SCALE, 0, true, "- Y: %.2f", state.player.y);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   186,  DEBUG_TEXT_SCALE, 0, true, "- VX: %.2f", state.player.vel_x * STEPS_DT);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0,   198,  DEBUG_TEXT_SCALE, 0, true, "- VY: %.2f", state.player.vel_y * STEPS_DT);

                
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   138,  DEBUG_TEXT_SCALE, 0, true, "Camera");
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   150,  DEBUG_TEXT_SCALE, 0, true, "- X: %.2f", state.camera_x);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   162,  DEBUG_TEXT_SCALE, 0, true, "- Y: %.2f", state.camera_y);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   174,  DEBUG_TEXT_SCALE, 0, true, "- IntY: %.2f", state.camera_intended_y);
                
                struct mallinfo mi = mallinfo();
                
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   138 + 50,  DEBUG_TEXT_SCALE, 0, true, "Heap");
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   150 + 50,  DEBUG_TEXT_SCALE, 0, true, "- Allocated: 0x%X bytes", mi.uordblks);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   162 + 50,  DEBUG_TEXT_SCALE, 0, true, "- Free:        0x%X bytes",  envGetHeapSize() - mi.uordblks);
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 110,   174 + 50,  DEBUG_TEXT_SCALE, 0, true, "- Arena:      0x%X bytes",  envGetHeapSize());
            }

            if (state.noclip) {
#ifdef __PSP__
                // the 3DS's bottom edge falls off the PSP's screen
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 4, 222, 0.35f, 0.35f, 0, true, "Noclip Activated");
#else
                draw_text(&bigFont_fontCharset, &bigFont_sheet, 0, 234, 0.5f, 0.5f, 0, true, "Noclip Activated");
#endif
            }
            C2D_ViewReset();

            C3D_FrameEnd(0);
        } while (handle_fading());

#ifdef __PSP__
        // where a frame's time goes, averaged every 2 s of play, to the log
        if (settingsState.enableDebugBindings && !game_paused && !state.dead) {
            static u32 n, sum[7], peak[7];
            u32 now = sceKernelGetSystemTimeLow();
            u32 v[7] = {
                (u32)(ticks / CPU_TICKS_PER_MSEC * 1000),  // the update, all of it
                (u32)(physics_calc_time * 1000),           //  physics and collisions
                (u32)(sprite_drawing_time * 1000),         //  the objects' sprites: pick, sort, color
                (u32)(particle_calc_time * 1000),          //  particles and effects
                (u32)(triggers_time * 1000),               //  triggers and colors
                perf_t2 - perf_t1,                         // waiting for the GE / the vblank
                now - perf_t2,                             // drawing (the CPU's side)
            };
            for (int i = 0; i < 7; i++) {
                sum[i] += v[i];
                if (v[i] > peak[i]) peak[i] = v[i];
            }
            (void)perf_t0;
            if (++n == 120) {
                psp_log("perf ms (avg/max): update %.1f/%.1f [physics %.1f/%.1f objects %.1f/%.1f particles %.1f/%.1f triggers %.1f/%.1f] wait %.1f/%.1f draw %.1f/%.1f, %d sprites\n",
                    sum[0] / 120000.f, peak[0] / 1000.f, sum[1] / 120000.f, peak[1] / 1000.f,
                    sum[2] / 120000.f, peak[2] / 1000.f, sum[3] / 120000.f, peak[3] / 1000.f,
                    sum[4] / 120000.f, peak[4] / 1000.f, sum[5] / 120000.f, peak[5] / 1000.f,
                    sum[6] / 120000.f, peak[6] / 1000.f, sprite_count);
                n = 0;
                memset(sum, 0, sizeof(sum));
                memset(peak, 0, sizeof(peak));
            }
        }
#endif

#ifdef PS2_DIAG_FRAME_TIMING
        {
            u64 finished = svcGetSystemTick();
            float frame_ms = (float)(finished - start) / CPU_TICKS_PER_MSEC;
            float update_ms = (float)(end - start) / CPU_TICKS_PER_MSEC;
            float draw_ms = (float)(finished - end) / CPU_TICKS_PER_MSEC;
            {
                const float sec[7] = { physics_calc_time, collision_time, triggers_time,
                                        sprite_drawing_time, particle_calc_time,
                                        object_creating_time, object_sorting_time };
                for (int i = 0; i < 7; i++) {
                    prof_sec_sum[i] += sec[i];
                    if (sec[i] > prof_sec_peak[i]) prof_sec_peak[i] = sec[i];
                }
            }
            for (int i = 0; i < 4; i++) {
                prof_trg_sum[i] += prof_trg[i];
                if (prof_trg[i] > prof_trg_peak[i]) prof_trg_peak[i] = prof_trg[i];
                prof_trg[i] = 0;
            }
            ++profile_frames;
            profile_frame_sum += frame_ms;
            profile_update_sum += update_ms;
            profile_draw_sum += draw_ms;
            if (frame_ms > profile_frame_peak) profile_frame_peak = frame_ms;
            if (update_ms > profile_update_peak) profile_update_peak = update_ms;
            if (draw_ms > profile_draw_peak) profile_draw_peak = draw_ms;
            if (frame_ms > 20.0f) ++profile_over20;
            if (frame_ms > 33.3f) ++profile_over33;
            if ((unsigned)steps > profile_max_steps) profile_max_steps = (unsigned)steps;
            prof_sprites_sum += (unsigned)sprite_count;
            prof_objdraw_sum += object_drawing_time;
            prof_player_sum += player_graphics_time;
            if (player_graphics_time > prof_player_peak) prof_player_peak = player_graphics_time;
            if (object_drawing_time > prof_objdraw_peak) prof_objdraw_peak = object_drawing_time;
            if ((unsigned)sprite_count > prof_sprites_peak) prof_sprites_peak = (unsigned)sprite_count;
            if (profile_frames >= 120) {
                printf("GDPS2 FT: frames=120 avg=%.2fms max=%.2fms over20=%u over33=%u update_avg=%.2fms update_max=%.2fms draw_avg=%.2fms draw_max=%.2fms max_physics_steps=%u\n",
                    profile_frame_sum/120.0f, profile_frame_peak,
                    profile_over20, profile_over33,
                    profile_update_sum/120.0f, profile_update_peak,
                    profile_draw_sum/120.0f, profile_draw_peak, profile_max_steps);
                printf("GDPS2 FT UPD: physics=%.2f/%.2f collision=%.2f/%.2f triggers=%.2f/%.2f objects=%.2f/%.2f particles=%.2f/%.2f create=%.2f/%.2f sort=%.2f/%.2f ms (avg/max)\n",
                    prof_sec_sum[0]/120.0f, prof_sec_peak[0], prof_sec_sum[1]/120.0f, prof_sec_peak[1],
                    prof_sec_sum[2]/120.0f, prof_sec_peak[2], prof_sec_sum[3]/120.0f, prof_sec_peak[3],
                    prof_sec_sum[4]/120.0f, prof_sec_peak[4], prof_sec_sum[5]/120.0f, prof_sec_peak[5],
                    prof_sec_sum[6]/120.0f, prof_sec_peak[6]);
                printf("GDPS2 FT SPR: objdraw=%.2f/%.2fms player_fx=%.2f/%.2fms sprites_avg=%lu sprites_max=%u\n",
                    prof_objdraw_sum/120.0f, prof_objdraw_peak, prof_player_sum/120.0f, prof_player_peak, prof_sprites_sum/120u, prof_sprites_peak);
                printf("GDPS2 FT TRG: handle=%.2f/%.2f trig2=%.2f/%.2f anim=%.2f/%.2f colors=%.2f/%.2f ms (avg/max)\n",
                    prof_trg_sum[0]/120.0f, prof_trg_peak[0], prof_trg_sum[1]/120.0f, prof_trg_peak[1],
                    prof_trg_sum[2]/120.0f, prof_trg_peak[2], prof_trg_sum[3]/120.0f, prof_trg_peak[3]);
                for (int i = 0; i < 4; i++) prof_trg_sum[i] = prof_trg_peak[i] = 0;
                prof_sprites_sum = 0;
                prof_sprites_peak = 0;
                prof_objdraw_sum = prof_objdraw_peak = 0;
                prof_player_sum = prof_player_peak = 0;
                for (int i = 0; i < 7; i++) prof_sec_sum[i] = prof_sec_peak[i] = 0;
                profile_frames = profile_over20 = profile_over33 = profile_max_steps = 0;
                profile_frame_sum = profile_update_sum = profile_draw_sum = 0;
                profile_frame_peak = profile_update_peak = profile_draw_peak = 0;
            }
        }
#endif

        if (being_faded) {
            if (song_loaded) unpause_playback_music();
            being_faded = false;
        }

        if (exiting_level) {
            game_paused = false;
            in_level_complete = false;
            break;
        }
    }

    LevelData *level_data_sel = (state.custom_level ? &level_data : &main_level_data[curr_level_id]);

#ifdef __PSP__
    psp_log("teardown: begin\n");
#endif
    level_data_sel->attempts += state.current_data.attempts;
    level_data_sel->jumps += state.current_data.jumps;
    level_data_sel->normal_progress = state.current_data.max_normal;
    level_data_sel->practice_progress = state.current_data.max_practice;

    total_attempts += state.current_data.attempts;
    total_jumps += state.current_data.jumps;

    if (state.custom_level) {
        save_level_progress();
    } else {
        save_main_level_progress(curr_level_id);
    }

    cfg_save();

#ifdef __PSP__
    psp_log("teardown: cfg_save done\n");
#endif
    free_particles();

#ifdef __PSP__
    psp_log("teardown: free_particles done\n");
#endif
    unload_level();

#ifdef __PSP__
    psp_log("teardown: unload_level done\n");
#endif
    clear_respawn_effect();

#ifdef __PSP__
    psp_log("teardown: clear_respawn_effect done\n");
#endif
    level_complete_destroy();

#ifdef __PSP__
    psp_log("teardown: level_complete_destroy done\n");
#endif
    ui_unload_screen(&default_screen);
#ifdef __PSP__
    psp_log("teardown: default_screen unloaded\n");
#endif
    ui_unload_screen(&default_screen_top);
#ifdef __PSP__
    psp_log("teardown: default_screen_top unloaded\n");
#endif
    
    if (song_loaded) unpause_playback_music();

    game_state = (state.custom_level ? STATE_EXTERNAL_LEVELS : STATE_LEVEL_SELECT);
}

void game_assets_init() {
    // Load graphics
    spriteSheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    if (!spriteSheet) svcBreak(USERBREAK_PANIC);
    
    spriteSheet2 = C2D_SpriteSheetLoad("romfs:/gfx/portals.t3x");
    if (!spriteSheet2) svcBreak(USERBREAK_PANIC);
    
    glowSheet = C2D_SpriteSheetLoad("romfs:/gfx/glow.t3x");
    if (!glowSheet) svcBreak(USERBREAK_PANIC);

    iconSheet = C2D_SpriteSheetLoad("romfs:/gfx/icons.t3x");
    if (!iconSheet) svcBreak(USERBREAK_PANIC);

    trailSheet = C2D_SpriteSheetLoad("romfs:/gfx/trails.t3x");
    if (!trailSheet) svcBreak(USERBREAK_PANIC);

    particleSheet = C2D_SpriteSheetLoad("romfs:/gfx/particles.t3x");
    if (!particleSheet) svcBreak(USERBREAK_PANIC);

    initParticleSystem(&touch_drag_particles, &touch_drag_effect);
    touch_drag_particles.relativeStationary = true;

    initParticleSystem(&touch_explosion_particles, &touch_explosion_effect);
    touch_explosion_particles.relativeStationary = true;

    initParticleSystem(&glitter_particles_bottom, &glitter_effect);
    glitter_particles_bottom.cfg.startColorAlpha = 1.f;
    glitter_particles_bottom.dontApplyCamY = true;

    initParticleSystem(&slow_speed_particles_bottom, &speed_effect_slow);
    slow_speed_particles_bottom.relativeStationary = true;

    initParticleSystem(&normal_speed_particles_bottom, &speed_effect_normal);
    normal_speed_particles_bottom.relativeStationary = true;

    initParticleSystem(&fast_speed_particles_bottom, &speed_effect_fast);
    fast_speed_particles_bottom.relativeStationary = true;

    initParticleSystem(&faster_speed_particles_bottom, &speed_effect_vfast);
    faster_speed_particles_bottom.relativeStationary = true;

    
}

void load_sfx() {
    load_wav("romfs:/sfx/playSound_01.wav", &play_sound);
    load_wav("romfs:/sfx/quitSound_01.wav", &quit_sound);
    load_wav("romfs:/sfx/explode_11.wav", &explode_sound);
    load_wav("romfs:/sfx/endStart_02.wav", &end_sound);
    load_wav("romfs:/sfx/honk.wav", &honk);
    load_wav("romfs:/sfx/highscoreGet02.wav", &coin_sound);
}

#ifdef __PSP__
// Settings > Graphics > Show FPS: the frames of the last half second, in the
// top left corner over everything (c2d_psp.c calls this at each frame's end)
void psp_frame_overlay(void) {
    static u64 last;
    static int frames, shown;
    u64 now = svcGetSystemTick();

    frames++;
    if (!last) last = now;
    float ms = (float)(now - last) / CPU_TICKS_PER_MSEC;
    if (ms >= 500.f) {
        shown = (int)(frames * 1000.f / ms + 0.5f);
        frames = 0;
        last = now;
    }

    if (!settingsState.showFps) return;
    PSP_OverlayBegin(top, bot);
    C2D_ViewReset();
    change_blending(false);
    draw_text(&bigFont_fontCharset, &bigFont_sheet, 5, 14, 0.4f, 0.4f, 0, false, "FPS: %d", shown);
}
#endif

int main(int argc, char* argv[]) {
    // Init libs
    GDPS2_BOOT(0, "main entry");
#ifdef __PS2__
    GDPS2_BOOT(1, "SIF RPC before ROMFS host: probe");
    PS2_BootInitRpc();
#ifdef PS2_DIAG_FRAME_TIMING
    /* Diagnostic ELF only: keep printf output in a file next to the ELF. */
    if (freopen("host:ft.log", "w", stdout)) setvbuf(stdout, NULL, _IOLBF, 0);
#endif
    PS2_BootInitUsbLog();
#endif
    GDPS2_BOOT(2, "romfsInit");
#ifdef __PS2__
    PS2_SetLaunchPath(argc > 0 ? argv[0] : NULL);
    Result ps2_romfs_status = romfsInit();
    PS2_BootResult("romfsInit", (int)ps2_romfs_status);
    if (ps2_romfs_status != 0) {
        PS2_BootFatal("ROMFS unavailable (check assets.pak or romfs next to ELF)", (int)ps2_romfs_status);
        puts("GDPS2 FATAL: ROMFS unavailable; expected assets.pak or romfs/menus/main_menu.txt next to ELF or on a mounted device");
        return 1;
    }
#else
    romfsInit();
#endif
    GDPS2_BOOT(3, "gfxInitDefault / gsKit TexManager");
    gfxInitDefault();
    GDPS2_BOOT(4, "C3D_Init and C2D_Init");
#ifdef __PS2__
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4)) {
        PS2_BootResult("C3D_Init", -1);
        svcBreak(USERBREAK_PANIC);
    }
    PS2_BootResult("C3D_Init", 0);
    if (!C2D_Init(MAX_SPRITES)) {
        PS2_BootResult("C2D_Init", -1);
        svcBreak(USERBREAK_PANIC);
    }
    PS2_BootResult("C2D_Init", 0);
#else
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
    C2D_Init(MAX_SPRITES);
#endif
    C2D_Prepare();
    osSetSpeedupEnable(1);

    GDPS2_BOOT(5, "configuration and platform settings");
    cfg_init();
    cfguInit();

    // Sort out wide / 3D before making the targets, they need different sizes
    GDPS2_BOOT(6, "screen targets and layout");
    apply_screen_modes();
    reinitialize_screens();

    srand(time(NULL));
    alt_title_screen = (rand() & (128 - 1)) == 0;
    
    C2D_SetTintMode(C2D_TintMult);
    
    GDPS2_BOOT(7, "ndspInit (audsrv/SPU2)");
    Result dsp_status = ndspInit();
#ifdef __PS2__
    PS2_BootResult("ndspInit", (int)dsp_status);
#endif
    if(dsp_status) {
#ifdef __PS2__
        /* no_dsp_firmware() is a 3DS-only error screen. It uses ANSI
         * text and exits via the 3DS console path, which is not usable
         * on a physical PS2 and obscures the actual audsrv failure. */
        PS2_BootFatal("PS2 audsrv/SPU2 initialization failed; stopping before game assets", (int)dsp_status);
        return 1;
#else
        no_dsp_firmware();
#endif
    }
#ifdef __PS2__
    else {
        /* PS2SDK enters main() at EE priority 1. That starved both the
         * mpg123 decoder and audsrv mixer despite 60 FPS GS rendering.
         * In PCSX2, changing the main thread to 32 (mixer=24) restored
         * ~256 PCM blocks per 3 seconds with no DelayThread in FrameEnd.
         * Only the native PS2 port changes scheduling; PSP/3DS is intact. */
        const int old_priority = ChangeThreadPriority(TH_SELF, 0x20);
        ee_thread_status_t status;
        if (ReferThreadStatus(TH_SELF, &status) < 0 || status.current_priority != 0x20) {
            printf("GDPS2 WARNING: could not normalize main EE priority (result=%d)\n",
                   old_priority);
        } else {
            printf("GDPS2 PS2: main EE priority %d -> %d; audsrv mixer priority 24\n",
                   old_priority, status.current_priority);
        }
    }
#endif

#ifdef __PS2__
    // the OptiProjects logo, before the loading screen
    PS2_RunStartupLogo(top);
#endif

    GDPS2_BOOT(8, "required loading screen atlases");
    required_loading_screen_assets_init();

    GDPS2_BOOT(9, "loading screen UI initialization");
    loading_screen_init();

    u64 start = svcGetSystemTick();

    GDPS2_BOOT(10, "first loading screen draw");
    loading_screen_update(0);

    GDPS2_BOOT(11, "UI atlases");
    ui_assets_init();
    GDPS2_BOOT(12, "gameplay atlases and particles");
    game_assets_init();
    loading_screen_update(10);

    GDPS2_BOOT(13, "load main level progress and stats");
    load_main_level_progress();
    
    loading_screen_update(25);

    calculate_stats();

    GDPS2_BOOT(14, "cache_all_sprites");
    cache_all_sprites();

    loading_screen_update(40);
    
    GDPS2_BOOT(15, "effect pools and player colors");
    init_default_use_effect_pools();
    update_player_colors();

    loading_screen_update(75);

    GDPS2_BOOT(16, "load_sfx WAV assets");
    load_sfx();

    memset(&level_info, 0, sizeof(LoadedLevelInfo));
    
    loading_screen_update(90);

    u64 end = svcGetSystemTick();
    // Wait a minimum of 3 seconds. Use integer ticks: the old 1e9 literal
    // invoked software double, and a 3e9-ns wait overflowed a 32-bit long.
    const u64 min_loading_ticks = (u64)SYSCLOCK_ARM11 * 3u;
    const u64 elapsed_loading_ticks = end - start;
    if (elapsed_loading_ticks < min_loading_ticks) {
        const u64 ticks_to_wait = min_loading_ticks - elapsed_loading_ticks;
        svcSleepThread((s64)(ticks_to_wait * 1000000000ULL / SYSCLOCK_ARM11));
    }
    loading_screen_update(100);

    // Unload loading screen
    ui_unload_screen(&default_screen);
    ui_unload_screen(&default_screen_top);

    // Set to known value
    change_blending(false);

#ifdef __PSP__
    // For testing: an autoload.gmd next to the EBOOT is played right away,
    // with noclip (it plays through by itself)
    FILE *autoload = fopen("autoload.gmd", "rb");
    if (autoload) {
        fclose(autoload);
        snprintf(state.custom_level_path, sizeof(state.custom_level_path), "autoload.gmd");
        state.custom_level = true;
        state.noclip = true;
        game_state = STATE_GAME;
        psp_autoload_test = true;
    }
#endif

    GDPS2_BOOT(17, "enter game state loop / STATE_MAIN_MENU");
    bool exit = false;
    while (aptMainLoop() && !exit) {
        // Update color if changed menus
        Color p1_not_white = get_white_if_black(p1_color);

        touch_drag_particles.cfg.startColorRed = p1_not_white.r / 255.f;
        touch_drag_particles.cfg.startColorGreen = p1_not_white.g / 255.f;
        touch_drag_particles.cfg.startColorBlue = p1_not_white.b / 255.f;

        touch_explosion_particles.cfg.startColorRed = p1_not_white.r / 255.f;
        touch_explosion_particles.cfg.startColorGreen = p1_not_white.g / 255.f;
        touch_explosion_particles.cfg.startColorBlue = p1_not_white.b / 255.f;

        glitter_particles_bottom.cfg.startColorRed = get_white_if_black(p1_color).r / 255.f;
        glitter_particles_bottom.cfg.startColorGreen = get_white_if_black(p1_color).g / 255.f;
        glitter_particles_bottom.cfg.startColorBlue = get_white_if_black(p1_color).b / 255.f;
        glitter_particles_bottom.cfg.finishColorRed = get_white_if_black(p1_color).r / 255.f;
        glitter_particles_bottom.cfg.finishColorGreen = get_white_if_black(p1_color).g / 255.f;
        glitter_particles_bottom.cfg.finishColorBlue = get_white_if_black(p1_color).b / 255.f;

        slow_speed_particles_bottom.cfg.startColorRed = 255 / 255.f;
        slow_speed_particles_bottom.cfg.startColorGreen = 255 / 255.f;
        slow_speed_particles_bottom.cfg.startColorBlue = 0 / 255.f;

        normal_speed_particles_bottom.cfg.startColorRed = 0 / 255.f;
        normal_speed_particles_bottom.cfg.startColorGreen = 190 / 255.f;
        normal_speed_particles_bottom.cfg.startColorBlue = 255 / 255.f;

        fast_speed_particles_bottom.cfg.startColorRed = 0 / 255.f;
        fast_speed_particles_bottom.cfg.startColorGreen = 255 / 255.f;
        fast_speed_particles_bottom.cfg.startColorBlue = 0 / 255.f;

        faster_speed_particles_bottom.cfg.startColorRed = 230 / 255.f;
        faster_speed_particles_bottom.cfg.startColorGreen = 65 / 255.f;
        faster_speed_particles_bottom.cfg.startColorBlue = 255 / 255.f;

        switch (game_state) {
            case STATE_MAIN_MENU:
#ifdef __PS2__
                PS2_BootEvent("GDPS2 STATE: MAIN_MENU");
#endif
                main_menu_loop();
                break;
            case STATE_LEVEL_SELECT:
#ifdef __PS2__
                PS2_BootEvent("GDPS2 STATE: LEVEL_SELECT");
#endif
                level_select_loop();
                break;
            case STATE_ICON_KIT:
                icon_kit_loop();
                break;
            case STATE_GAME:
#ifdef __PS2__
                PS2_BootEvent("GDPS2 STATE: GAME");
#endif
                game_loop();
                break;
            case STATE_CREATOR_MENU:
                creator_menu_loop();
                break;
            case STATE_SEARCH_MENU:
                search_menu_loop();
                break;
            case STATE_SAVED_LEVELS:
                saved_levels_loop();
                break;
            case STATE_ONLINE_LEVEL:
                online_menu_loop();
                break;
            case STATE_EXTERNAL_LEVELS:
                external_levels_loop();
                break;
            case STATE_SOGGY: // Sog
                soggy_menu_loop();
                break;
            case STATE_EXIT:
                exit = true;
                break;
        }
    }

    close_log_file();

    free_cached_sprites();

    free_main_level_progress();

    // Delete graphics
    C2D_SpriteSheetFree(spriteSheet);
    C2D_SpriteSheetFree(spriteSheet2);
    C2D_SpriteSheetFree(glowSheet);
    C2D_SpriteSheetFree(bgSheet);
    C2D_SpriteSheetFree(bg2Sheet);
    C2D_SpriteSheetFree(iconSheet);
    C2D_SpriteSheetFree(trailSheet);
    C2D_SpriteSheetFree(particleSheet);
    C2D_SpriteSheetFree(ui_sheet);
    C2D_SpriteSheetFree(ui_2_sheet);
    C2D_SpriteSheetFree(groundSheet);
    C2D_SpriteSheetFree(bigFont_sheet);
    C2D_SpriteSheetFree(chatFont_sheet);
    C2D_SpriteSheetFree(goldFont_sheet);
    C2D_SpriteSheetFree(window_sheet);
    C2D_SpriteSheetFree(bar_sheet);

    cfg_fini();

    // Deinit libs
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    ndspExit();
    return 0;
}
