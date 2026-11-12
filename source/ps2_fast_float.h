#ifndef GD_PS2_FAST_FLOAT_H
#define GD_PS2_FAST_FLOAT_H

#include <stdlib.h>

/* Geometry Dash level/object/trigger coordinates are stored as float.
 * The PS2 EE has a single-precision FPU; avoid parsing to a software
 * double only to narrow it to float afterwards. Keep historical parsing
 * on other ports, where a precision change isn't required. */
static inline float gd_parse_level_float(const char *text)
{
#ifdef __PS2__
    return strtof(text, NULL);
#else
    return (float)atof(text);
#endif
}

#endif
