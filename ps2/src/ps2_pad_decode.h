#pragma once

#include <stdbool.h>
#include <3ds.h>
#include <libpad.h>

/* libpad mode byte: the low nibble encodes response words. A digital pad
 * (normally 0x41) has NO stick samples; the ljoy bytes may be zero instead
 * of neutral 128. Interpreting them as axes produces a permanent CPAD_UP,
 * which makes the game's first-jump release gate wait forever. Analog
 * DualShock modes (0x73/0x79) have three or more response words. */
static inline bool GDPS2_PadHasAnalogAxes(u8 mode)
{
    return (mode & 0x0f) >= 3;
}

/* Convert the actual libpad response to the game's 3DS-style HID bits.
 * Leave digital D-pad/CROSS working even with an analog-disabled controller. */
static inline u32 GDPS2_DecodePadKeys(u32 raw, u8 mode, u8 left_h, u8 left_v,
                                       s16 *axis_x, s16 *axis_y)
{
    s16 dx = 0, dy = 0;
    if (GDPS2_PadHasAnalogAxes(mode)) {
        dx = ((s16)left_h - 128) * 2;
        dy = ((s16)left_v - 128) * 2;
    }
    if (axis_x) *axis_x = dx;
    if (axis_y) *axis_y = dy;

    u32 keys = 0;
    if (raw & PAD_CROSS) keys |= KEY_A;
    if (raw & PAD_CIRCLE) keys |= KEY_B;
    if (raw & PAD_TRIANGLE) keys |= KEY_X;
    if (raw & PAD_SQUARE) keys |= KEY_Y;
    if (raw & PAD_START) keys |= KEY_START;
    if (raw & PAD_SELECT) keys |= KEY_SELECT;
    if (raw & PAD_L1) keys |= KEY_L;
    if (raw & PAD_R1) keys |= KEY_R;
    if (raw & PAD_L2) keys |= KEY_ZL;
    if (raw & PAD_R2) keys |= KEY_ZR;
    if (raw & PAD_UP) keys |= KEY_DUP;
    if (raw & PAD_DOWN) keys |= KEY_DDOWN;
    if (raw & PAD_LEFT) keys |= KEY_DLEFT;
    if (raw & PAD_RIGHT) keys |= KEY_DRIGHT;
    if (dx < -80) keys |= KEY_CPAD_LEFT;
    if (dx > 80) keys |= KEY_CPAD_RIGHT;
    if (dy < -80) keys |= KEY_CPAD_UP;
    if (dy > 80) keys |= KEY_CPAD_DOWN;
    if (raw & PAD_R3) keys |= KEY_TOUCH;
    return keys;
}
