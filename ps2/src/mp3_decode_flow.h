/* PS2-specific mpg123 result handling. MP3 stays compressed on ROMFS. */
#ifndef GDPS2_MP3_DECODE_FLOW_H
#define GDPS2_MP3_DECODE_FLOW_H

#include <stddef.h>
#include <mpg123.h>

typedef enum {
    GDPS2_MP3_DATA,
    GDPS2_MP3_RETRY,
    GDPS2_MP3_EOF,
    GDPS2_MP3_FORMAT,
    GDPS2_MP3_ERROR
} Gdps2Mp3Action;

/* mpg123_read's done is a BYTE count, never samples or frames. */
static inline Gdps2Mp3Action gdps2_mp3_action(int status, size_t done,
                                               size_t capacity, unsigned channels)
{
    if (!channels || channels > 2 || done > capacity ||
        (done % (sizeof(short) * channels)) != 0)
        return GDPS2_MP3_ERROR;
    if (status == MPG123_NEW_FORMAT) return GDPS2_MP3_FORMAT;
    if (status == MPG123_DONE)
        return done ? GDPS2_MP3_DATA : GDPS2_MP3_EOF;
    if (status == MPG123_OK)
        return done ? GDPS2_MP3_DATA : GDPS2_MP3_RETRY;
    /* NEED_MORE requires explicit feeding, which our file reader does not use. */
    return GDPS2_MP3_ERROR;
}

#endif
