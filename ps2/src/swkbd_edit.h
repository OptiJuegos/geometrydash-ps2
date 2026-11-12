#pragma once
#include <3ds.h>

/* Small deterministic input state, independently testable on the host. */
typedef struct {
    char text[256];
    int length;
    int max_length;
    int col, row;
    bool lowercase;
    bool requires_text;
} Ps2KeyboardEdit;

/* Returns -1 on cancel, 0 while editing, 1 on accepted text. */
int PS2_KeyboardUpdate(Ps2KeyboardEdit *edit, u32 keys);
void PS2_KeyboardInit(Ps2KeyboardEdit *edit, const char *initial,
                      int max_length, bool requires_text);
char PS2_KeyboardCharacter(int row, int col, bool lowercase);
