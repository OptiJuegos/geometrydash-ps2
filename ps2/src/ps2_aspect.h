#pragma once

/* True 4:3 PS2 view. The 3DS 400x240 top screen is wider than 4:3:
 * crop 40 logical units per edge and keep all sprite dimensions uniform
 * when presented on a 4:3 NTSC display (640x448 has rectangular pixels). */
enum {
    GDPS2_SOURCE_W = 400,
    GDPS2_SOURCE_H = 240,
    GDPS2_VISIBLE_W = 320,
    GDPS2_CROP_X = (GDPS2_SOURCE_W - GDPS2_VISIBLE_W) / 2,
    GDPS2_FRAME_W = 640,
    GDPS2_FRAME_H = 448
};

static inline float GDPS2_PhysicalX(float logical_x)
{
    return (logical_x - GDPS2_CROP_X) *
           ((float)GDPS2_FRAME_W / GDPS2_VISIBLE_W);
}

static inline float GDPS2_PhysicalY(float logical_y)
{
    return logical_y * ((float)GDPS2_FRAME_H / GDPS2_SOURCE_H);
}
