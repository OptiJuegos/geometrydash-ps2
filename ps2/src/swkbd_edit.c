#include "swkbd_edit.h"
#include <string.h>

void PS2_KeyboardInit(Ps2KeyboardEdit *e, const char *initial,
                      int max_length, bool requires_text)
{
    if (!e) return;
    memset(e, 0, sizeof(*e));
    e->max_length = max_length < 0 ? 0 : max_length > 255 ? 255 : max_length;
    e->requires_text = requires_text;
    if (!initial) return;
    size_t n = strlen(initial);
    if (n > (size_t)e->max_length) n = e->max_length;
    memcpy(e->text, initial, n);
    e->text[n] = 0;
    e->length = (int)n;
}

char PS2_KeyboardCharacter(int row, int col, bool lowercase)
{
    const char *letters[] = {
        "ABCDEFGH", "IJKLMNOP", "QRSTUVWX", "YZ012345", "6789 _<>"
    };
    if (row < 0 || row > 4 || col < 0 || col > 7) return 0;
    char c = letters[row][col];
    if (row == 4 && col == 5) return '\b';
    if (row == 4 && col == 6) return '\r';
    if (row == 4 && col == 7) return 27;
    if (lowercase && c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return c;
}

static int accept(const Ps2KeyboardEdit *e)
{
    return e->requires_text && !e->length ? 0 : 1;
}

int PS2_KeyboardUpdate(Ps2KeyboardEdit *e, u32 keys)
{
    if (!e) return -1;
    if (keys & KEY_B) return -1;
    if (keys & KEY_START) return accept(e);
    if (keys & KEY_X) e->lowercase = !e->lowercase;
    if (keys & KEY_Y) {
        if (e->length) e->text[--e->length] = 0;
    }
    if (keys & KEY_DLEFT) e->col = (e->col + 7) % 8;
    if (keys & KEY_DRIGHT) e->col = (e->col + 1) % 8;
    if (keys & KEY_DUP) e->row = (e->row + 4) % 5;
    if (keys & KEY_DDOWN) e->row = (e->row + 1) % 5;
    if (keys & KEY_A) {
        char c = PS2_KeyboardCharacter(e->row, e->col, e->lowercase);
        if (c == 27) return -1;
        if (c == '\r') return accept(e);
        if (c == '\b') {
            if (e->length) e->text[--e->length] = 0;
        } else if (c && e->length < e->max_length) {
            e->text[e->length++] = c;
            e->text[e->length] = 0;
        }
    }
    return 0;
}
