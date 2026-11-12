#pragma once

#include <citro2d.h>

/* OptiProjects logo before the loading screen: romfs:/gfx/startup_logo,
 * a 640x480 picture packed by package_romfs.py as two 320x480 halves
 * (a GS page is 512 wide at most). 1 s fade in, 2 s shown, 1 s fade out;
 * Start or Cross skips it. Without the sheet it returns at once. */
void PS2_RunStartupLogo(C3D_RenderTarget *target);
