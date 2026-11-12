/* PS2SDK libpad -> subset of 3DS libctru HID required by Geometry Dash.
 * The 3DS-style ring lets source/utils/precise_input.c sample button edges
 * after its PS2 timing backend becomes available.
 */
#include <3ds.h>
#include <tamtypes.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <timer.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "ctru_ps2_input.h"
#include "ps2_pad_decode.h"
#include "ps2_ui_nav.h"
#include "../../source/main.h"

/* Entry and word offsets match source/utils/precise_input.c. */
#define PAD_TICK_WORD     0
#define PAD_IDX_WORD      4
#define PAD_RING_WORD     (0x28 / 4)
#define TOUCH_TICK_WORD   42
#define TOUCH_IDX_WORD    46
#define TOUCH_RING_WORD   ((0xA8 + 0x20) / 4)

static u32 hid_memory[0x400] __attribute__((aligned(64)));
vu32 *hidSharedMem = hid_memory;

static char pad_buffer[256] __attribute__((aligned(64)));
static bool pad_initialized;
static bool pad_open;
static bool pad_read_logged;
static unsigned pad_edge_logs;
static u32 keys_now, keys_prev;
static u32 physical_now, physical_prev;
static touchPosition touch_pos = {160, 120};
static circlePosition circle_pos;

/* PSP-style focus uses the previous UI update's real hitboxes. The current
 * update refills ps2_nav_targets through ui_screen.c. Store coordinates only:
 * unloaded menu elements must never leave a dereferenceable stale pointer. */
static PS2NavTarget nav[PS2_NAV_MAX_TARGETS];
static int nav_count;
static float cursor_x = 200.f, cursor_y = 120.f; /* top-screen logical */
static bool nav_focused;
static bool cursor_by_dpad;
static int snap_pending;
static int nav_repeat;
static int last_game_state = -1;
static bool last_paused, last_complete;
static bool last_menu_touch, pending_menu_cross_release;

static bool ps2_menu_touch(void)
{
    return game_state != STATE_GAME || game_paused || in_level_complete;
}

static const PS2NavTarget *nav_at(float x, float y)
{
    return PS2_NavTargetAt(nav, nav_count, x, y);
}

static bool nav_move(int dx, int dy)
{
    return PS2_NavMove(nav, nav_count, dx, dy, &cursor_x, &cursor_y);
}

bool PS2_NavScreenActive(bool bottom)
{
    if (!ps2_menu_touch() || !nav_focused) return true;
    const PS2NavTarget *t = nav_at(cursor_x, cursor_y);
    return t ? t->bottom == bottom : bottom;
}

bool PS2_NavCursorPoint(float *x, float *y)
{
    if (!ps2_menu_touch() || !nav_focused) return false;
    *x = cursor_x; *y = cursor_y;
    return true;
}

bool PS2_NavFocusRect(float *x0, float *y0, float *x1, float *y1)
{
    if (!ps2_menu_touch() || !nav_focused) return false;
    const PS2NavTarget *t = nav_at(cursor_x, cursor_y);
    if (!t) return false;
    float x = t->x + (t->bottom ? 40.f : 0.f);
    *x0 = x - t->hw; *x1 = x + t->hw;
    *y0 = t->y - t->hh; *y1 = t->y + t->hh;
    if (*x0 < 40) *x0 = 40;
    if (*x1 > 360) *x1 = 360;
    if (*y0 < 0) *y0 = 0;
    if (*y1 > 240) *y1 = 240;
    return *x1 > *x0 && *y1 > *y0;
}

static void nav_scan(u32 *keys)
{
    int count = ps2_nav_target_count;
    if (count > PS2_NAV_MAX_TARGETS) count = PS2_NAV_MAX_TARGETS;
    memcpy(nav, ps2_nav_targets, count * sizeof(nav[0]));
    nav_count = count;
    ps2_nav_target_count = 0;
    ps2_nav_slider_focus = NULL;

    if (game_state != last_game_state || game_paused != last_paused ||
        in_level_complete != last_complete) {
        last_game_state = game_state;
        last_paused = game_paused;
        last_complete = in_level_complete;
        cursor_x = 200.f; cursor_y = 120.f;
        nav_focused = false; cursor_by_dpad = false; snap_pending = 2;
    }
    const bool in_menu = ps2_menu_touch();
    const bool block_cross = PS2_NavBlockMenuCross(
        last_menu_touch, in_menu, (physical_now & KEY_A) != 0,
        &pending_menu_cross_release);
    last_menu_touch = in_menu;
    if (!in_menu) {
        nav_focused = false;
        if (block_cross) *keys &= ~KEY_A;
        return;
    }

    if (snap_pending && --snap_pending == 0) {
        if (nav_move(0, 0)) { nav_focused = true; cursor_by_dpad = true; }
        else snap_pending = 1;
    }
    if (cursor_by_dpad && nav_count && !nav_at(cursor_x, cursor_y)) {
        nav_focused = nav_move(0, 0);
    }

    /* PSP also supports moving the virtual cursor freely with the stick. */
    float ax = circle_pos.dx / 256.f, ay = circle_pos.dy / 256.f;
    if (fabsf(ax) > 0.25f || fabsf(ay) > 0.25f) {
        if (fabsf(ax) <= 0.25f) ax = 0;
        if (fabsf(ay) <= 0.25f) ay = 0;
        cursor_x += ax * fabsf(ax) * 6.f;
        cursor_y += ay * fabsf(ay) * 6.f;
        if (cursor_x < 40) cursor_x = 40;
        if (cursor_x > 359) cursor_x = 359;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y > 239) cursor_y = 239;
        nav_focused = true;
        cursor_by_dpad = false;
        snap_pending = 0;
    }
    /* In menus the analog stick is a pointer, not a second D-pad. Do not
     * accidentally trigger level arrows or scroll a list while pointing. */
    *keys &= ~(KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT);
    circle_pos.dx = circle_pos.dy = 0;

    /* Keep directional binds (like level-select L/R arrows) working. Only
     * unbound D-pad directions move focus, exactly like the PSP UI. */
    static const struct { u32 key; int dx, dy; } directions[] = {
        {KEY_DUP, 0, -1}, {KEY_DDOWN, 0, 1},
        {KEY_DLEFT, -1, 0}, {KEY_DRIGHT, 1, 0}
    };
    const u32 direction_mask = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;
    if (physical_now & direction_mask) ++nav_repeat;
    else nav_repeat = 0;
    const PS2NavTarget *current = nav_at(cursor_x, cursor_y);
    for (unsigned i = 0; i < sizeof(directions) / sizeof(directions[0]); ++i) {
        u32 key = directions[i].key;
        if (!(physical_now & key)) continue;
        bool edge = !(physical_prev & key);
        bool repeat = nav_repeat > 20 && !(nav_repeat % 4);
        bool bound = false;
        for (int j = 0; j < nav_count; ++j) {
            if (nav[j].keys & key) { bound = true; break; }
        }
        if (bound) continue;
        if (current && current->in_list && directions[i].dy) continue;
        *keys &= ~key;
        if (current && current->slider && directions[i].dx) {
            if (edge || repeat) {
                ps2_nav_slider_focus = current->element;
                ps2_nav_slider_nudge = directions[i].dx;
            }
        } else if (edge && nav_move(directions[i].dx, directions[i].dy)) {
            nav_focused = true; cursor_by_dpad = true;
        }
    }

    current = nav_at(cursor_x, cursor_y);
    if (nav_focused) {
        touch_pos.px = (u16)(current ? current->x : cursor_x - 40.f);
        touch_pos.py = (u16)(current ? current->y : cursor_y);
        if (touch_pos.px == 0 && touch_pos.py == 0) touch_pos.px = 1;
        /* Do not fire all KEY_A-bound actions: only the focused button may
         * receive a synthesized touch. During gameplay CROSS remains jump. */
        if (physical_now & KEY_A) {
            *keys &= ~KEY_A;
            *keys |= KEY_TOUCH;
        }
    } else {
        touch_pos.px = touch_pos.py = 0;
    }
}

#ifdef PS2_DIAG_AUTONAV
/* Explicit test-only build: inject one START edge in each of the first two
 * screens. Not present in the normal release/experimental ELF. */
extern int game_state;
extern int curr_level_id;
static int diag_previous_state = -1;
static unsigned diag_state_scans;
#endif

/* Map PS2's 147.456 MHz bus-clock source onto the 3DS HID timebase. */
static u64 ps2_tick(void)
{
    const u64 cycles = GetTimerSystemTime();
    const u64 freq = 268111856ULL;
    return (cycles / kBUSCLK) * freq +
           ((cycles % kBUSCLK) * freq) / kBUSCLK;
}

static void set_tick(int offset, u64 value)
{
    hid_memory[offset] = (u32)value;
    hid_memory[offset + 1] = (u32)(value >> 32);
}

static u64 get_tick(int offset)
{
    return hid_memory[offset] | ((u64)hid_memory[offset + 1] << 32);
}

static void push_sample(int tick_word, int idx_word, int ring_word,
                        int entry_words, const u32 *entry, u64 now)
{
    u32 idx = (hid_memory[idx_word] + 1) & 7;
    if (idx == 0) {
        set_tick(tick_word + 2, get_tick(tick_word));
        set_tick(tick_word, now);
    }
    for (int i = 0; i < entry_words; ++i)
        hid_memory[ring_word + idx * entry_words + i] = entry[i];
    hid_memory[idx_word] = idx;
}

bool PS2_InputInit(void)
{
    const int sio = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    const int pad = SifLoadModule("rom0:PADMAN", 0, NULL);
    memset(hid_memory, 0, sizeof(hid_memory));
    keys_now = keys_prev = 0;
    physical_now = physical_prev = 0;
    nav_count = ps2_nav_target_count = 0;
    nav_focused = false;
    snap_pending = 2;
    last_menu_touch = pending_menu_cross_release = false;
    pad_read_logged = false;
    pad_edge_logs = 0;
    const u64 now = ps2_tick();
    const u64 interval = 268111856ULL / 60;
    set_tick(PAD_TICK_WORD, now);
    set_tick(PAD_TICK_WORD + 2, now - interval * 8);
    set_tick(TOUCH_TICK_WORD, now);
    set_tick(TOUCH_TICK_WORD + 2, now - interval * 8);

    if (sio < 0 || pad < 0 || !padInit(0)) {
        printf("PS2: pad modules unavailable (%d, %d)\n", sio, pad);
        return false;
    }
    pad_initialized = true;
    pad_open = padPortOpen(0, 0, pad_buffer) != 0;
    if (!pad_open) printf("PS2: padPortOpen failed\n");
    return pad_open;
}

void PS2_InputShutdown(void)
{
    if (pad_open) padPortClose(0, 0);
    if (pad_initialized) padEnd();
    pad_open = pad_initialized = false;
}

void hidScanInput(void)
{
    struct padButtonStatus pad;
    u32 raw = 0;
    u8 pad_mode = 0;
    u8 left_h = 128, left_v = 128;
    keys_prev = keys_now;
    keys_now = 0;
    if (pad_open) {
        int state = padGetState(0, 0);
        if ((state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) &&
            padRead(0, 0, &pad)) {
            raw = (~(u32)pad.btns) & 0xFFFF;
            pad_mode = pad.mode;
            left_h = pad.ljoy_h;
            left_v = pad.ljoy_v;
            if (!pad_read_logged) {
                printf("GDPS2 PAD: port 0 responding, mode=0x%02x analog=%d (not proof of physical controller)\n",
                       (unsigned)pad_mode, (int)GDPS2_PadHasAnalogAxes(pad_mode));
                pad_read_logged = true;
            }
        }
    }

#ifdef PS2_DIAG_NO_INPUT
    /* A pure menu-audio benchmark must not allow live SDL controller
     * presses to navigate to a level and repeatedly reset the MP3 stream. */
    raw = 0;
    pad_mode = 0;
#endif
    physical_prev = physical_now;
    physical_now = GDPS2_DecodePadKeys(raw, pad_mode, left_h, left_v,
                                   &circle_pos.dx, &circle_pos.dy);
    keys_now = physical_now;

#ifdef PS2_DIAG_AUTONAV
    if (game_state != diag_previous_state) {
        diag_previous_state = game_state;
        diag_state_scans = 0;
        printf("GDPS2 DIAG: entering state=%d\n", game_state);
#if defined(PS2_DIAG_LEVEL_INDEX) && PS2_DIAG_LEVEL_INDEX >= 0
        if (game_state == 1) {
            curr_level_id = PS2_DIAG_LEVEL_INDEX;
            printf("GDPS2 DIAG: selecting level index=%d\n", curr_level_id);
        }
#endif
    }
    if (++diag_state_scans == (game_state == 0 ? 90u : 110u) &&
        (game_state == 0 || game_state == 1)) {
#if defined(PS2_DIAG_LEVEL_INDEX) && PS2_DIAG_LEVEL_INDEX >= 0
        if (game_state == 1) curr_level_id = PS2_DIAG_LEVEL_INDEX;
#endif
        keys_now |= KEY_START;
        printf("GDPS2 DIAG: simulated START state=%d scan=%u level=%d\n",
               game_state, diag_state_scans, curr_level_id);
    }
#endif

    nav_scan(&keys_now);

    if (pad_read_logged && (keys_now & ~keys_prev) && pad_edge_logs++ < 24) {
        printf("GDPS2 PAD: key-down bits=0x%08lx\n", (unsigned long)(keys_now & ~keys_prev));
    }

    const u64 tick = ps2_tick();
    const u32 buttons[] = {keys_now, keys_now & ~keys_prev, ~keys_now & keys_prev, 0};
    const u32 touch[] = {(u32)touch_pos.px | ((u32)touch_pos.py << 16),
                         (keys_now & KEY_TOUCH) ? 1u : 0u};
    push_sample(PAD_TICK_WORD, PAD_IDX_WORD, PAD_RING_WORD, 4, buttons, tick);
    push_sample(TOUCH_TICK_WORD, TOUCH_IDX_WORD, TOUCH_RING_WORD, 2, touch, tick);
}

u32 hidKeysDown(void) { return keys_now & ~keys_prev; }
u32 hidKeysHeld(void) { return keys_now; }
u32 hidKeysUp(void) { return ~keys_now & keys_prev; }
void hidTouchRead(touchPosition *pos) { if (pos) *pos = touch_pos; }
void hidCircleRead(circlePosition *pos) { if (pos) *pos = circle_pos; }
