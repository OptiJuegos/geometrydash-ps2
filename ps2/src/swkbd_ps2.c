/* Controller-operated text entry for the offline Geometry Dash PS2 menus.
 * Renders via the already initialized Citro2D/gsKit backend; no on-disk fonts
 * or 3DS system keyboard are required. Characters are ASCII only.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#include "swkbd_edit.h"

static const u8 letters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {15,16,16,16,16,16,15}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {15,16,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {31,4,4,4,4,4,31}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
static const u8 digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static const u8 *glyph(char c)
{
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    return NULL;
}

static void text5(const char *message, float x, float y,
                  float scale, u32 color, size_t limit)
{
    if (!message) return;
    for (size_t i=0; message[i] && i<limit; ++i) {
        char c = message[i];
        const u8 *bitmap = glyph(c);
        if (bitmap) {
            for (int row=0; row<7; ++row)
                for (int col=0; col<5; ++col)
                    if (bitmap[row] & (16 >> col))
                        C2D_DrawRectSolid(x + col*scale, y + row*scale,
                                           0, scale, scale, color);
        } else if (c == '-' || c == '_' || c == '.' || c == ':') {
            float yy = c == '-' ? 3 : c == ':' ? 2 : 6;
            float width = c == '.' || c == ':' ? 1 : 5;
            C2D_DrawRectSolid(x + (c == '.' || c == ':' ? 2*scale : 0),
                              y + yy*scale, 0, width*scale, scale, color);
            if (c == ':')
                C2D_DrawRectSolid(x+2*scale, y+5*scale, 0, scale, scale, color);
        }
        x += 6*scale;
    }
}

static void draw_key(int row, int col, bool selected, bool lowercase)
{
    int x = 35 + col*42;
    int y = 86 + row*29;
    u32 color = selected ? C2D_Color32(242,192,69,255) :
                  C2D_Color32(41,81,142,255);
    u32 textcolor = selected ? C2D_Color32(15,26,56,255) :
                     C2D_Color32(249,249,250,255);
    C2D_DrawRectSolid(x,y,0,36,26,color);
    char c = PS2_KeyboardCharacter(row,col,lowercase);
    if (c == ' ') text5("SPACE",x+4,y+10,0.9f,textcolor,5);
    else if (c == '\b') text5("DEL",x+8,y+8,1.f,textcolor,3);
    else if (c == '\r') text5("OK",x+11,y+8,1.f,textcolor,2);
    else if (c == 27) text5("EXIT",x+5,y+8,1.f,textcolor,4);
    else {
        char label[2] = {c,0};
        text5(label,x+15,y+7,1.6f,textcolor,1);
    }
}

void swkbdInit(SwkbdState *state, SwkbdType type, int buttons, int max_length)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->type = (int)type;
    state->num_buttons = buttons;
    state->max_len = max_length;
}

void swkbdSetFeatures(SwkbdState *state, u32 features)
{
    if (state) state->features = features;
}

void swkbdSetHintText(SwkbdState *state, const char *text)
{
    if (state) snprintf(state->hint, sizeof(state->hint), "%s", text ? text : "");
}

void swkbdSetInitialText(SwkbdState *state, const char *text)
{
    if (state) snprintf(state->initial, sizeof(state->initial), "%s", text ? text : "");
}

void swkbdSetValidation(SwkbdState *state, SwkbdValidInput mode,
                        u32 filter_flags, u32 max_digits)
{
    (void)state; (void)mode; (void)filter_flags; (void)max_digits;
}

void swkbdSetButton(SwkbdState *state, SwkbdButton button,
                    const char *text, bool submit)
{
    (void)state; (void)button; (void)text; (void)submit;
}

SwkbdButton swkbdInputText(SwkbdState *state, char *buffer, size_t capacity)
{
    if (!state || !buffer || !capacity) return SWKBD_BUTTON_LEFT;
    Ps2KeyboardEdit edit;
    int max_length = state->max_len;
    if (max_length >= (int)capacity) max_length = (int)capacity - 1;
    PS2_KeyboardInit(&edit, state->initial, max_length, false);

    C3D_RenderTarget *screen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    if (!screen) return SWKBD_BUTTON_LEFT;
    u32 old_repeat = 0;
    SwkbdButton result = SWKBD_BUTTON_LEFT;
    for (;;) {
        hidScanInput();
        u32 pressed = hidKeysDown();
        u32 arrows = hidKeysHeld() & (KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT);
        if (arrows) {
            ++old_repeat;
            if (old_repeat > 18 && (old_repeat % 5) == 0)
                pressed |= arrows;
        } else old_repeat = 0;
        int decision = PS2_KeyboardUpdate(&edit, pressed);
        if (decision != 0) {
            if (decision > 0) {
                memcpy(buffer, edit.text, (size_t)edit.length + 1);
                result = SWKBD_BUTTON_RIGHT;
            }
            break;
        }
        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) break;
        C2D_SceneBegin(screen);
        C2D_ViewReset();
        u32 white = C2D_Color32(255,255,255,255);
        C2D_DrawRectSolid(0,0,0,400,240,C2D_Color32(9,22,54,255));
        C2D_DrawRectSolid(21,14,0,358,55,C2D_Color32(27,48,93,255));
        text5(state->hint,28,20,1.3f,white,42);
        const char *display = edit.text;
        if (edit.length > 28) display += edit.length - 28;
        text5(display,29,46,1.35f,C2D_Color32(255,220,132,255),28);
        text5("A TYPE  B CANCEL  START OK",26,73,0.9f,white,42);
        for (int row=0;row<5;++row)
            for (int col=0;col<8;++col)
                draw_key(row,col, row == edit.row && col == edit.col, edit.lowercase);
        C3D_FrameEnd(0);
    }
    C3D_RenderTargetDelete(screen);
    return result;
}
