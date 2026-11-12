#pragma once

/* Platform-independent lossless submission of a PCM byte block.
 * audsrv_play_audio() may return fewer bytes than requested, even after
 * audsrv_wait_audio(). Success is exactly bytes submitted, not audible proof. */
typedef int (*Ps2AudioWaitFn)(int bytes, void *context);
typedef int (*Ps2AudioSendFn)(const char *buffer, int bytes, void *context);

/* >= 0: all bytes sent; -1 invalid arguments; -2 wait error;
 * -3 no forward progress or negative write; -4 driver wrote > requested. */
int PS2_AudioSubmitAll(const char *data, int bytes,
                       Ps2AudioWaitFn wait_fn, Ps2AudioSendFn send_fn,
                       void *context);
