#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/ps2_aspect.h"

static int near(float a, float b) { return fabsf(a-b) < 0.0003f; }

int main(void)
{
    assert(GDPS2_VISIBLE_W * 3 == GDPS2_SOURCE_H * 4);
    assert(GDPS2_CROP_X == 40);
    assert(near(GDPS2_PhysicalX(40), 0));
    assert(near(GDPS2_PhysicalX(200), 320));
    assert(near(GDPS2_PhysicalX(360), 640));
    assert(near(GDPS2_PhysicalY(0), 0));
    assert(near(GDPS2_PhysicalY(240), 448));
    /* 640x448 pixels shown as 4:3 have a horizontal pixel aspect 14/15.
     * A 1-unit physical X step must match a 1-unit Y step on the screen. */
    const float display_par = (4.f/3.f) / (640.f/448.f);
    assert(near((GDPS2_PhysicalX(101)-GDPS2_PhysicalX(100))*display_par,
                GDPS2_PhysicalY(101)-GDPS2_PhysicalY(100)));
    puts("PASS: centered 320x240 viewport on 640x448 GS, true display 4:3 without stretching");
    return 0;
}
