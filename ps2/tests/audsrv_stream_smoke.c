#include "audsrv_stream.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char collected[2048];
    int count, calls, waits, mode;
} Fake;

static int fake_wait(int bytes, void *ctx)
{
    Fake *f = (Fake *)ctx;
    ++f->waits;
    if (bytes <= 0) return 9;
    return f->mode == 2 ? 1 : 0;
}

static int fake_send(const char *data, int bytes, void *ctx)
{
    Fake *f = (Fake *)ctx;
    ++f->calls;
    if (f->mode == 1) return 0;
    if (f->mode == 3) return bytes + 1;
    int n = bytes > 664 ? 664 : bytes;
    memcpy(f->collected + f->count, data, (size_t)n);
    f->count += n;
    return n;
}

int main(void)
{
    char pcm[2048];
    for (int i=0; i<2048; ++i) pcm[i] = (char)(i*37+3);
    Fake f = {{0},0,0,0,0};
    int n = PS2_AudioSubmitAll(pcm, 2048, fake_wait, fake_send, &f);
    if (n != 2048 || f.count != n || f.calls != 4 || f.waits != 4 ||
        memcmp(pcm, f.collected, sizeof(pcm)) != 0) return 1;
    f.mode = 1;
    if (PS2_AudioSubmitAll(pcm, 2048, fake_wait, fake_send, &f) != -3) return 2;
    f.mode = 2;
    if (PS2_AudioSubmitAll(pcm, 2048, fake_wait, fake_send, &f) != -2) return 3;
    f.mode = 3;
    if (PS2_AudioSubmitAll(pcm, 2048, fake_wait, fake_send, &f) != -4) return 4;
    if (PS2_AudioSubmitAll(NULL, 2048, fake_wait, fake_send, &f) != -1) return 5;
    puts("PASS: audsrv partial PCM send preserves all bytes, retries and rejects stalls");
    return 0;
}
