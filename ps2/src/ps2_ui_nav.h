#pragma once
#include <stdbool.h>
#include <math.h>
#include <3ds.h>

/* PSP-style controller focus for the unified PS2 UI. Coordinates stay in
 * each UI screen's native units: bottom 320x240, top 400x240. */
typedef struct {
    float x, y, hw, hh;
    float clip_x0, clip_y0, clip_x1, clip_y1;
    u32 keys;
    const void *element;
    bool bottom, slider, in_list;
} PS2NavTarget;

#define PS2_NAV_MAX_TARGETS 96
extern PS2NavTarget ps2_nav_targets[PS2_NAV_MAX_TARGETS];
extern int ps2_nav_target_count;
extern const void *ps2_nav_slider_focus;
extern int ps2_nav_slider_nudge;

/* Same nearest-in-direction logic as the PSP navigation cursor, directly
 * reusable by host smoke tests and the real PS2 input backend. */
static inline bool PS2_NavTargetVisible(const PS2NavTarget *t)
{
    float x = t->x + (t->bottom ? 40.f : 0.f);
    if (x < 40.f || x > 360.f || t->y < 0.f || t->y >= 240.f)
        return false;
    if (t->in_list && (t->x < t->clip_x0 || t->x > t->clip_x1 ||
                       t->y < t->clip_y0 || t->y > t->clip_y1))
        return false;
    return true;
}

static inline const PS2NavTarget *PS2_NavTargetAt(const PS2NavTarget *nav,
                                                  int count, float x, float y)
{
    const PS2NavTarget *best = NULL;
    float area = 1e9f;
    for (int i = 0; i < count; ++i) {
        const PS2NavTarget *t = &nav[i];
        if (!PS2_NavTargetVisible(t)) continue;
        float cx = t->x + (t->bottom ? 40.f : 0.f);
        if (x < cx - t->hw || x >= cx + t->hw ||
            y < t->y - t->hh || y >= t->y + t->hh) continue;
        if (t->hw * t->hh < area) { best = t; area = t->hw * t->hh; }
    }
    return best;
}

static inline bool PS2_NavMove(const PS2NavTarget *nav, int count, int dx, int dy,
                               float *cursor_x, float *cursor_y)
{
    float best_score = 1e9f, best_x = 0, best_y = 0;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        const PS2NavTarget *t = &nav[i];
        if (!PS2_NavTargetVisible(t)) continue;
        float x = t->x + (t->bottom ? 40.f : 0.f);
        float vx = x - *cursor_x, vy = t->y - *cursor_y;
        float score;
        if (dx || dy) {
            float along = vx * dx + vy * dy;
            if (along < 6) continue;
            score = along + (fabsf(vx * dy) + fabsf(vy * dx)) * 2.f;
        } else score = vx * vx + vy * vy;
        if (score < best_score) {
            best_score = score; best_x = x; best_y = t->y;
            found = true;
        }
    }
    if (found) { *cursor_x = best_x; *cursor_y = best_y; }
    return found;
}

/* The CROSS press that closes a menu must not become a fresh gameplay jump
 * when menu KEY_TOUCH switches back to gameplay KEY_A while still held. */
static inline bool PS2_NavBlockMenuCross(bool was_menu, bool menu_now,
                                         bool cross_held, bool *pending)
{
    if (was_menu && !menu_now && cross_held) *pending = true;
    if (!cross_held) *pending = false;
    return !menu_now && *pending;
}

void PS2_NavListClip(float x0, float y0, float x1, float y1);
void PS2_NavListClipEnd(void);
/* Called from UI update: filter coordinates on the other logical screen. */
bool PS2_NavScreenActive(bool bottom);
/* Called from the PS2 GS back end: draw focus outline in top-screen units. */
bool PS2_NavFocusRect(float *x0, float *y0, float *x1, float *y1);
bool PS2_NavCursorPoint(float *x, float *y);
