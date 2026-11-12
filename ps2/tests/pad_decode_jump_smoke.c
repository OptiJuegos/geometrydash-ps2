/* Exercise the actual PS2 pad button decoder and the gameplay entry gate
 * together. Digital-mode stick bytes are unspecified (often both zero). */
#include <assert.h>
#include <stdio.h>
#include "../src/ps2_pad_decode.h"
#include "../src/ps2_jump_entry.h"

static u32 sample(u16 raw, u8 mode, u8 lx, u8 ly)
{
    s16 x = -1, y = -1;
    u32 keys = GDPS2_DecodePadKeys(raw, mode, lx, ly, &x, &y);
    if (!GDPS2_PadHasAnalogAxes(mode)) {
        assert(x == 0 && y == 0);
        assert(!(keys & (KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT)));
    }
    return keys;
}

int main(void)
{
    const u32 jumps = GDPS2_MenuConfirmJumpMask(KEY_A | KEY_UP);
    assert(jumps == KEY_A);
    /* Real failure case: a controller in digital mode can return x=y=0,
     * previously decoded as CPAD_LEFT | CPAD_UP in EVERY scan. */
    u32 keys = sample(0, 0x41, 0, 0);
    assert(keys == 0);
    bool wait_for_release = GDPS2_JumpEntryHeld(keys, jumps);
    assert(!wait_for_release);
    assert(!GDPS2_BlockInheritedJump(&wait_for_release, keys, jumps));

    /* Press CROSS on the first level frame, then hold it: both are valid. */
    keys = sample(PAD_CROSS, 0x41, 0, 0);
    assert(keys & KEY_A);
    assert(!GDPS2_BlockInheritedJump(&wait_for_release, keys, jumps));
    assert(!GDPS2_BlockInheritedJump(&wait_for_release, keys, jumps));

    /* Enter while CROSS is still held: suppress just that menu hold. */
    wait_for_release = GDPS2_JumpEntryHeld(keys, jumps);
    assert(wait_for_release && GDPS2_BlockInheritedJump(&wait_for_release, keys, jumps));
    keys = sample(0, 0x41, 0, 0);
    assert(!GDPS2_BlockInheritedJump(&wait_for_release, keys, jumps));
    keys = sample(PAD_CROSS, 0x41, 0, 0);
    assert(!GDPS2_BlockInheritedJump(&wait_for_release, keys, jumps));

    /* D-pad UP remains a valid jump, and a normal analog stick still works. */
    assert(sample(PAD_UP, 0x41, 0, 0) & KEY_DUP);
    assert(sample(PAD_L1, 0x41, 0, 0) == KEY_L);
    assert(sample(PAD_R1, 0x41, 0, 0) == KEY_R);
    assert(sample(PAD_L2, 0x41, 0, 0) == KEY_ZL);
    assert(sample(PAD_R2, 0x41, 0, 0) == KEY_ZR);
    assert(!GDPS2_JumpEntryHeld(KEY_DUP, jumps));
    assert(!GDPS2_JumpEntryHeld(KEY_CPAD_UP, jumps));
    assert(GDPS2_MenuConfirmJumpMask(KEY_Y | KEY_UP) == 0);
    assert(sample(0, 0x73, 128, 128) == 0);
    assert(sample(0, 0x73, 128, 0) & KEY_CPAD_UP);
    assert(sample(0, 0x79, 128, 255) & KEY_CPAD_DOWN);
    assert(sample(0, 0x00, 0, 0) == 0); /* disconnected/invalid read */

    puts("PASS: libpad digital 0x41 ignores absent analog axes; CROSS and D-pad jump, entry release gate, analog modes");
    return 0;
}
