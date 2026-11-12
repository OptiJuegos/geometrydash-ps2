#pragma once

#include <stddef.h>

#define AMP_MIN 0.1f
#define AMP_MAX 1.0f

#define AMP_DECAY 0.15f
#define AMP_I_DECAY (1.f - AMP_DECAY)

#define POWER_THRESH_MULTIPLIER 0.75f

extern volatile float amplitude;

void audio_init();
int play_music(char *path, bool loop, float seek);
int play_music_buf(void *buf, size_t sz, bool loop, float seek);
void seek_music(float time);
void stop_music();
void toggle_playback_music();
void pause_playback_music();
void unpause_playback_music();
void fade_to_amplitude(float new_amplitude);