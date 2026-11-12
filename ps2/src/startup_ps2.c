#include "startup_ps2.h"

#include <3ds.h>
#include <stdio.h>

#include "ps2_aspect.h"

enum { LOGO_HALVES = 2 };

#define FADE_IN_S  1.0f
#define SHOW_S     2.0f
#define FADE_OUT_S 1.0f

static float seconds_since(u64 start)
{
    return (float)(svcGetSystemTick() - start) / (float)SYSCLOCK_ARM11;
}

void PS2_RunStartupLogo(C3D_RenderTarget *target)
{
    C2D_SpriteSheet sheet = C2D_SpriteSheetLoad("romfs:/gfx/startup_logo.t3x");
    if (!sheet) {
        puts("GDPS2 STARTUP: no gfx/startup_logo sheet, skipping the logo");
        return;
    }
    if (C2D_SpriteSheetCount(sheet) < LOGO_HALVES) {
        C2D_SpriteSheetFree(sheet);
        return;
    }

    /* The 640x480 picture fills the visible 4:3 area: 320x240 logical
     * units from x = GDPS2_CROP_X, so each half is drawn at half size. */
    const float total = FADE_IN_S + SHOW_S + FADE_OUT_S;
    const u64 start = svcGetSystemTick();
    hidScanInput();
    for (float t = 0; t < total; t = seconds_since(start)) {
        hidScanInput();
        if (hidKeysDown() & (KEY_START | KEY_A)) break;

        float black;
        if (t < FADE_IN_S) black = 1.f - t / FADE_IN_S;
        else if (t < FADE_IN_S + SHOW_S) black = 0.f;
        else black = (t - FADE_IN_S - SHOW_S) / FADE_OUT_S;
        if (black > 1.f) black = 1.f;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(target);
        C2D_TargetClear(target, C2D_Color32(255, 255, 255, 255));
        C2D_DrawRectSolid(0, 0, 0, GDPS2_SOURCE_W, GDPS2_SOURCE_H, C2D_Color32(255, 255, 255, 255));
        for (int i = 0; i < LOGO_HALVES; i++) {
            C2D_Image half = C2D_SpriteSheetGetImage(sheet, i);
            C2D_DrawImageAt(half, GDPS2_CROP_X + i * (GDPS2_VISIBLE_W / 2), 0, 0.5f, NULL, 0.5f, 0.5f);
        }
        C2D_Fade(C2D_Color32(0, 0, 0, (u8)(black * 255.f + 0.5f)));
        C3D_FrameEnd(0);
    }

    /* the fade is kept between frames: the loading screen starts clean */
    C2D_Fade(0);
    C2D_SpriteSheetFree(sheet);
}
