/* Diagnostic ONLY: first mpg123_read without NDSP, GS or a second EE thread.
 * Reads the original MP3 directly from the package next to the ELF. */
#include <stdio.h>
#include <mpg123.h>
#include <sifrpc.h>
#include <kernel.h>

int main(void)
{
    int err = 0, status;
    long rate = 0;
    int channels = 0, enc = 0;
    unsigned char pcm[8192];
    size_t done = 0;
    sceSifInitRpc(0);
    puts("GDPS2 PROBE: bootstrap; independent decoder, no NDSP and no GS");
    status = mpg123_init();
    printf("GDPS2 PROBE: mpg123_init=%d\n", status);
    mpg123_handle *mh = mpg123_new(NULL, &err);
    if (!mh) { printf("GDPS2 PROBE: mpg123_new failed=%d\n", err); return 2; }
    mpg123_format_none(mh);
    mpg123_format(mh, 32000, MPG123_STEREO | MPG123_MONO, MPG123_ENC_SIGNED_16);
    puts("GDPS2 PROBE: before mpg123_open");
    status = mpg123_open(mh, "host:package/romfs/songs/menuLoop.mp3");
    printf("GDPS2 PROBE: mpg123_open result=%d\n", status);
    if (status != MPG123_OK) return 3;
    status = mpg123_getformat(mh, &rate, &channels, &enc);
    printf("GDPS2 PROBE: format status=%d rate=%ld channels=%d encoding=%d\n",
           status, rate, channels, enc);
    if (status != MPG123_OK) return 4;
    for (int i = 0; i < 3; ++i) {
        done = 0;
        printf("GDPS2 PROBE: before mpg123_read #%d\n", i);
        status = mpg123_read(mh, pcm, sizeof(pcm), &done);
        printf("GDPS2 PROBE: after mpg123_read #%d status=%d bytes=%lu\n",
               i, status, (unsigned long)done);
        if (status != MPG123_OK && status != MPG123_NEW_FORMAT) break;
    }
    mpg123_close(mh);
    mpg123_delete(mh);
    puts("GDPS2 PROBE: completed");
    return 0;
}
