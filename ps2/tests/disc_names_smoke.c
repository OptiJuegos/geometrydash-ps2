#include "disc_names.h"
#include <stdio.h>

/* Prints PS2_DiscPath(arg, 0) for every argument; tools/test_disc_names.py
 * compares the output with disc_names.py. */
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        char out[512];
        if (PS2_DiscPath(argv[i], 0, out, sizeof(out)) < 0) { puts("ERR"); continue; }
        puts(out + 8); /* skip "cdrom0:/" */
    }
    return 0;
}
