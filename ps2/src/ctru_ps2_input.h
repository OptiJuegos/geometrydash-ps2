#pragma once

#include <stdbool.h>

/* Initialize PS2 libpad and the 3DS HID-compatible sample buffer.
 * Call sceSifInitRpc(0) before initialization. Returns false on pad errors;
 * the display can still run with no controller connected. */
bool PS2_InputInit(void);
void PS2_InputShutdown(void);
