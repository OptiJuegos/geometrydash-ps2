#include "swkbd_edit.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "Keyboard smoke failed line %d: %s\n", __LINE__, #expression); \
    return 1; \
} } while(0)

int main(void)
{
    Ps2KeyboardEdit e;
    PS2_KeyboardInit(&e, "AbCdef", 4, false);
    CHECK(e.length == 4 && strcmp(e.text, "AbCd") == 0);
    CHECK(PS2_KeyboardUpdate(&e, KEY_A) == 0);
    CHECK(strcmp(e.text, "AbCd") == 0); /* full */
    CHECK(PS2_KeyboardUpdate(&e, KEY_Y) == 0);
    CHECK(e.length == 3 && strcmp(e.text, "AbC") == 0);
    CHECK(PS2_KeyboardUpdate(&e, KEY_X) == 0);
    CHECK(e.lowercase);
    CHECK(PS2_KeyboardUpdate(&e, KEY_A) == 0);
    CHECK(strcmp(e.text, "AbCa") == 0);
    CHECK(PS2_KeyboardUpdate(&e, KEY_START) == 1);

    PS2_KeyboardInit(&e, "", 10, true);
    CHECK(PS2_KeyboardUpdate(&e, KEY_START) == 0);
    CHECK(PS2_KeyboardUpdate(&e, KEY_A) == 0);
    CHECK(strcmp(e.text, "A") == 0);
    CHECK(PS2_KeyboardUpdate(&e, KEY_START) == 1);
    CHECK(PS2_KeyboardUpdate(&e, KEY_B) == -1);

    PS2_KeyboardInit(&e, "", 10, false);
    CHECK(PS2_KeyboardCharacter(0, 0, false) == 'A');
    CHECK(PS2_KeyboardCharacter(0, 0, true) == 'a');
    CHECK(PS2_KeyboardCharacter(3, 2, false) == '0');
    CHECK(PS2_KeyboardCharacter(4, 4, false) == ' ');
    CHECK(PS2_KeyboardCharacter(4, 5, false) == '\b');
    CHECK(PS2_KeyboardCharacter(4, 6, false) == '\r');
    CHECK(PS2_KeyboardCharacter(4, 7, false) == 27);

    /* Navigation wraps, selected commands edit/accept/cancel. */
    PS2_KeyboardUpdate(&e, KEY_DLEFT);
    CHECK(e.col == 7);
    PS2_KeyboardUpdate(&e, KEY_DUP);
    CHECK(e.row == 4);
    CHECK(PS2_KeyboardUpdate(&e, KEY_A) == -1);
    PS2_KeyboardUpdate(&e, KEY_DLEFT);
    CHECK(e.col == 6);
    CHECK(PS2_KeyboardUpdate(&e, KEY_A) == 1);
    PS2_KeyboardUpdate(&e, KEY_DLEFT);
    CHECK(e.col == 5);
    PS2_KeyboardUpdate(&e, KEY_A);
    CHECK(e.length == 0);
    PS2_KeyboardUpdate(&e, KEY_DLEFT);
    CHECK(e.col == 4);
    PS2_KeyboardUpdate(&e, KEY_A);
    CHECK(strcmp(e.text, " ") == 0);
    PS2_KeyboardUpdate(&e, KEY_Y);
    CHECK(e.text[0] == 0);

    PS2_KeyboardInit(&e, NULL, 500, false);
    CHECK(e.max_length == 255);
    PS2_KeyboardInit(&e, NULL, -4, false);
    CHECK(e.max_length == 0);
    puts("PASS: PS2 keyboard ASCII, case, length, navigation, edit, accept and cancel");
    return 0;
}
