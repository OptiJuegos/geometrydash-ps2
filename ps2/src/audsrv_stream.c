#include "audsrv_stream.h"

int PS2_AudioSubmitAll(const char *data, int bytes,
                       Ps2AudioWaitFn wait_fn, Ps2AudioSendFn send_fn,
                       void *context)
{
    if (!data || bytes <= 0 || !wait_fn || !send_fn) return -1;
    int sent = 0;
    while (sent < bytes) {
        const int pending = bytes - sent;
        if (wait_fn(pending, context) != 0) return -2;
        int n = send_fn(data + sent, pending, context);
        if (n <= 0) return -3;
        if (n > pending) return -4;
        sent += n;
    }
    return sent;
}
