/* Validate the PS2 controller focus transition that PSP does in hidScanInput.
 * In particular menu CROSS must never turn into an unwanted gameplay jump. */
#include <assert.h>
#include <stdio.h>
#include "../src/ps2_ui_nav.h"

int main(void)
{
    /* Positions from PS2's merged PSP main menu (bottom x+40). */
    const PS2NavTarget menu[] = {
        {.x=160, .y=118, .hw=22, .hh=22, .bottom=true},
        {.x=68, .y=126, .hw=16, .hh=16, .bottom=true},
        {.x=252, .y=126, .hw=16, .hh=16, .bottom=true},
        {.x=132, .y=206, .hw=14, .hh=14, .bottom=true},
        {.x=188, .y=206, .hw=14, .hh=14, .bottom=true},
        /* A 3DS top-screen target outside the PS2 4:3 viewport. */
        {.x=5, .y=120, .hw=20, .hh=20, .bottom=false},
        /* A row that is scrolled outside the visible portion of a list. */
        {.x=150, .y=235, .hw=14, .hh=14, .bottom=true,
         .in_list=true, .clip_x0=0, .clip_x1=320, .clip_y0=20, .clip_y1=215}
    };
    float x=200, y=120;
    assert(PS2_NavMove(menu, 7, 0, 0, &x, &y));
    assert(x == 200 && y == 118); /* Play is selected first */
    assert(PS2_NavTargetAt(menu, 7, x, y) == &menu[0]);
    assert(PS2_NavMove(menu, 7, -1, 0, &x, &y));
    assert(x == 108 && y == 126); /* icon customizer, in bottom coords 68 */
    assert(PS2_NavMove(menu, 7, 1, 0, &x, &y));
    assert(x == 200 && y == 118);
    assert(PS2_NavMove(menu, 7, 0, 1, &x, &y));
    assert(y == 206); /* settings/statistics */
    assert(!PS2_NavTargetVisible(&menu[5]));
    assert(!PS2_NavTargetVisible(&menu[6]));

    /* Icon kit: 7 columns x 3 rows. LEFT/RIGHT must walk icons within the
     * current row, not activate page arrows or change the gamemode tab. */
    PS2NavTarget icons[23] = {0};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 7; ++column) {
            PS2NavTarget *t = &icons[row * 7 + column];
            t->x = 79.f + 27.f * column;
            t->y = 147.f + 27.f * row;
            t->hw = t->hh = 10.5f;
            t->bottom = true;
        }
    }
    icons[21] = (PS2NavTarget){.x=22, .y=174, .hw=13, .hh=20, .bottom=true, .keys=KEY_ZL};
    icons[22] = (PS2NavTarget){.x=298, .y=174, .hw=13, .hh=20, .bottom=true, .keys=KEY_ZR};
    x = 79.f + 40.f; y = 174.f;
    for (int column = 1; column < 7; ++column) {
        assert(PS2_NavMove(icons, 23, 1, 0, &x, &y));
        assert(x == 119.f + 27.f * column && y == 174.f);
        assert(PS2_NavTargetAt(icons, 23, x, y) == &icons[7 + column]);
    }
    for (int column = 5; column >= 0; --column) {
        assert(PS2_NavMove(icons, 23, -1, 0, &x, &y));
        assert(x == 119.f + 27.f * column && y == 174.f);
    }
    assert(PS2_NavMove(icons, 23, 0, -1, &x, &y) && y == 147.f);
    assert(PS2_NavMove(icons, 23, 0, 1, &x, &y) && y == 174.f);
    assert(PS2_NavMove(icons, 23, 0, 1, &x, &y) && y == 201.f);
    assert(!(icons[21].keys & (KEY_DLEFT | KEY_DRIGHT)));
    assert(!(icons[22].keys & (KEY_DLEFT | KEY_DRIGHT)));

    bool pending = false;
    assert(!PS2_NavBlockMenuCross(false, true, true, &pending));
    assert(!pending);
    /* The same CROSS was a menu touch, then the pause menu closed. */
    assert(PS2_NavBlockMenuCross(true, false, true, &pending));
    assert(pending);
    assert(PS2_NavBlockMenuCross(false, false, true, &pending));
    assert(!PS2_NavBlockMenuCross(false, false, false, &pending));
    assert(!pending);
    /* The first genuinely new CROSS press is allowed immediately. */
    assert(!PS2_NavBlockMenuCross(false, false, true, &pending));
    /* Pressing START to leave a menu must not suppress an unrelated CROSS. */
    assert(!PS2_NavBlockMenuCross(true, false, false, &pending));
    /* Menu-to-menu transitions never arm suppression. */
    assert(!PS2_NavBlockMenuCross(true, true, true, &pending));
    puts("PASS: PS2 PSP-style hover navigation, 4:3/list clipping and CROSS release keep gameplay jump working");
    return 0;
}
