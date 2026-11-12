#pragma once

#include <stdbool.h>
#include <3ds.h>

/* In level_select.c, CROSS (KEY_A) is the sole button shared between
 * confirming a level and jumping. START can also confirm but is not jump.
 * UP/analog stick may be held independently and must never lock out CROSS. */
static inline u32 GDPS2_MenuConfirmJumpMask(u32 jump_sources)
{
    return jump_sources & KEY_A;
}

/* The button that confirmed a menu must not become a phantom gameplay jump.
 * Arm suppression ONLY if a jump source is still held after level loading.
 * With no inherited hold, the first new press must work on frame one. */
static inline bool GDPS2_JumpEntryHeld(u32 held, u32 jump_sources)
{
    return (held & jump_sources) != 0;
}

static inline bool GDPS2_BlockInheritedJump(bool *pending, u32 held, u32 jump_sources)
{
    if (!*pending) return false;
    if (!GDPS2_JumpEntryHeld(held, jump_sources)) {
        *pending = false;
        return false;
    }
    return true;
}
