/* Test the ACTUAL precise_input.c PS2 suppression code without running PS2.
 * The source is included so the hardware ring can be driven synthetically. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __PS2__ 1
#include "../../source/utils/precise_input.c"
#include "../src/ps2_jump_entry.h"

static vu32 shared_hid[0x400];
vu32 *hidSharedMem = shared_hid;
static u64 fake_clock = 100000;
u64 svcGetSystemTick(void) { return fake_clock; }

static void begin(void)
{
    memset((void *)shared_hid, 0, sizeof(shared_hid));
    memset(&pad, 0, sizeof(pad));
    memset(&touch, 0, sizeof(touch));
    pad.tick_word = 0;
    pad.idx_word = 4;
    pad.ring_buffer_offset = 0x28;
    pad.sample_jump = pad_sample_jump;
    touch.tick_word = 42;
    touch.idx_word = 46;
    touch.ring_buffer_offset = 0xA8 + 0x20;
    touch.sample_jump = touch_sample_jump;
    pi_set_jump_keys(KEY_A | KEY_UP);
    pi_set_touch_filter(NULL);
}

static void apply_one(void)
{
    pi_begin_frame((u32)fake_clock, (u32)(fake_clock + 10000), 1);
    pi_apply_substep(0);
    fake_clock += 10000;
}

int main(void)
{
    /* Regression: the former unconditionally armed first-frame gate ate a
     * real jump when CROSS had been released during level loading. */
    const u32 sources = KEY_A | KEY_UP;
    bool waiting = GDPS2_JumpEntryHeld(0, sources);
    assert(!waiting);
    /* A fresh CROSS down on the very first game frame is accepted. */
    assert(!GDPS2_BlockInheritedJump(&waiting, KEY_A, sources));
    /* A menu CROSS still physically held must be ignored until release. */
    waiting = GDPS2_JumpEntryHeld(KEY_A, sources);
    assert(waiting && GDPS2_BlockInheritedJump(&waiting, KEY_A, sources));
    assert(waiting && !GDPS2_BlockInheritedJump(&waiting, 0, sources));
    assert(!waiting && !GDPS2_BlockInheritedJump(&waiting, KEY_A, sources));

    /* CROSS was held while selecting level. No first-frame jump, even if
     * the release-suppressed queue sees another DOWN before release. */
    begin();
    shared_hid[0x28 / 4] = KEY_A;
    pi_reset();
    pi_suppress_until_release();
    assert(pad.suppress_hold_until_release && !touch.suppress_hold_until_release);
    apply_one();
    assert(!pi_hold() && !pi_pressed());
    assert(push_event(&pad, (u32)fake_clock, true));
    apply_one();
    assert(!pi_hold() && !pi_pressed() && pad.suppress_hold_until_release);
    assert(push_event(&pad, (u32)fake_clock, false));
    apply_one();
    assert(!pi_hold() && !pi_pressed() && !pad.suppress_hold_until_release);
    assert(push_event(&pad, (u32)fake_clock, true));
    apply_one();
    assert(pi_hold() && pi_pressed());
    apply_one();
    assert(pi_hold() && !pi_pressed());
    assert(push_event(&pad, (u32)fake_clock, false));
    apply_one();
    assert(!pi_hold() && !pi_pressed());

    /* An idle pad must not swallow the first genuine player jump. */
    begin();
    pi_reset();
    pi_suppress_until_release();
    assert(!pad.suppress_hold_until_release);
    assert(push_event(&pad, (u32)fake_clock, true));
    apply_one();
    assert(pi_hold() && pi_pressed());

    puts("PASS: PS2 jump accepted on first free frame; held menu CROSS suppressed until release (normal + 240Hz)");
    return 0;
}
