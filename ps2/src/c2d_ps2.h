#pragma once

#include <gsKit.h>
#include <citro2d.h>

/* The first backend milestone runs beside the hardware bring-up prototype.
 * Attach the GS before loading sprite sheets. The native game integration
 * will call this through the PS2 platform initialisation later. */
void PS2_C2D_Attach(GSGLOBAL *gs);

/* Vblank counter (ctru_ps2.c) and its value when the last frame was presented. */
u32 PS2_VblankCount(void);
u32 PS2_PresentVcount(void);
/* Enable gsKit's on-demand GS texture manager after initializing it on the
 * same GSGLOBAL. The small standalone menu demo keeps eager upload. */
void PS2_C2D_EnableTextureStreaming(bool enabled);

/* Bottom-screen geometry from PSP's @merge directive. Coordinates belong
 * to the original logical 320x240 touch screen, never pre-shifted. */
void PS2_C2D_SetMergedBottom(float dx, float dy, float scale);
