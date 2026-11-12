#pragma once

#include <math.h>

/* Table sine/cosine for the EE: newlib's sinf/cosf are long software
 * routines, and every rotated sprite needs both. 1024 steps per turn with
 * linear interpolation: the error is below 5e-5 even for angles of
 * hundreds of radians (spinning saws), far under a pixel at sprite sizes.
 * Each translation unit keeps its own 5 KB table. */
enum { PS2_SINCOS_STEPS = 1024 };

static inline void PS2_SinCos(float rad, float *s, float *c)
{
    static float table[PS2_SINCOS_STEPS + PS2_SINCOS_STEPS / 4 + 1];
    static int ready;
    if (!ready) {
        for (int i = 0; i < PS2_SINCOS_STEPS + PS2_SINCOS_STEPS / 4 + 1; i++)
            table[i] = sinf((float)i * (float)(2.0 * M_PI / PS2_SINCOS_STEPS));
        ready = 1;
    }
    const float t = rad * (float)(PS2_SINCOS_STEPS / (2.0 * M_PI));
    int i = (int)t;
    if ((float)i > t) i--; /* floor, for negative angles */
    const float f = t - (float)i;
    i &= PS2_SINCOS_STEPS - 1;
    const int j = i + PS2_SINCOS_STEPS / 4; /* cos(x) = sin(x + quarter turn) */
    *s = table[i] + (table[i + 1] - table[i]) * f;
    *c = table[j] + (table[j + 1] - table[j]) * f;
}
