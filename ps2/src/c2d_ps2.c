/*
 * Initial native Citro2D interface backed by the PS2 GS/gsKit.
 * Scope: T8 sprite sheets, rotated/scaled images, rectangles, scene affine.
 * Further Citro2D internals used by the full engine still need porting.
 */
#include "c2d_ps2.h"
#include <gsKit.h>
#include <gsInline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <malloc.h>
#include <kernel.h>
#ifdef PS2_DIAG_FRAME_TIMING
#include <timer.h>
#endif
#include <errno.h>
#include "../../source/utils/c2d_internal.h"
#include "ps2_aspect.h"
#include "ps2_ui_nav.h"
#include "ps2_sincos.h"

typedef struct {
    C3D_Tex compat;            /* first, for image->tex pointer */
    GSTEXTURE texture;
} Ps2Page;

struct C2D_SpriteSheet_s {
    u32 image_count, page_count;
    Ps2Page *pages;
    Tex3DS_SubTexture *subtex;
    u16 *page_of;
};

typedef struct {
    float a, b, c, d, tx, ty;
} Affine;

static GSGLOBAL *gpu;
static bool texture_streaming;
static u32 frame_count;
static u32 frame_bind_requests;
#ifdef PS2_DIAG_BIND_DEDUP
static GSTEXTURE *last_manager_bind;
#endif
#ifdef PS2_DIAG_FRAME_TIMING
static u32 frame_consecutive_same_tex;
static GSTEXTURE *previous_requested_tex;
/* Diagnostic shadow LRU of the TexManager's VRAM residency. gsKit does not
 * report real uploads, so this estimates them: a bind of a page that the
 * LRU model has evicted counts as one transfer of the whole page. */
enum { DIAG_LRU_MAX = 64, DIAG_SEEN_MAX = 64 };
static GSTEXTURE *diag_lru[DIAG_LRU_MAX];
static u32 diag_lru_count, diag_lru_bytes, diag_vram_budget;
static GSTEXTURE *diag_seen[DIAG_SEEN_MAX];
static u16 diag_seen_binds[DIAG_SEEN_MAX];
/* Names of the atlas pages, "file#page", for the per-frame page list. */
enum { DIAG_NAMES_MAX = 160 };
static struct { const GSTEXTURE *tex; char name[28]; } diag_names[DIAG_NAMES_MAX];
static unsigned diag_name_count;

static const char *diag_page_name(const GSTEXTURE *t)
{
    for (unsigned i = 0; i < diag_name_count; i++)
        if (diag_names[i].tex == t) return diag_names[i].name;
    return "?";
}
static u32 frame_unique_pages, frame_shadow_misses, frame_shadow_bytes;
static u64 frame_bind_ticks;  /* EE time inside gsKit_TexManager_bind */
static u64 frame_image_ticks; /* EE time inside C2D_DrawImage */
/* Per-frame draw-call counters: images, rotated images, culled images,
 * sprite/triangle list flushes, blend changes, C2Di triangles. */
enum { DC_IMAGES, DC_ROTATED, DC_CULLED, DC_SPRITE_FLUSH, DC_TRI_FLUSH, DC_BLEND, DC_VTX_TRIS, DC_COUNT };
static u32 diag_dc[DC_COUNT];
#define DIAG_COUNT(k) (++diag_dc[k])
static u32 frame_vram_moves;  /* binds that changed tex->Vram: a fresh upload */

static u32 diag_tex_bytes(const GSTEXTURE *t)
{
    return gsKit_texture_size(t->Width, t->Height, t->PSM) +
           gsKit_texture_size(16, 16, GS_PSM_CT32);
}

static void diag_shadow_bind(GSTEXTURE *t)
{
    u32 i;
    if (!diag_vram_budget) diag_vram_budget = 4u * 1024u * 1024u - gpu->CurrentPointer;
    for (i = 0; i < frame_unique_pages && diag_seen[i] != t; i++) {}
    if (i == frame_unique_pages && frame_unique_pages < DIAG_SEEN_MAX) {
        diag_seen_binds[frame_unique_pages] = 0;
        diag_seen[frame_unique_pages++] = t;
    }
    if (i < DIAG_SEEN_MAX) ++diag_seen_binds[i];
    for (i = 0; i < diag_lru_count && diag_lru[i] != t; i++) {}
    if (i < diag_lru_count) {
        memmove(&diag_lru[1], &diag_lru[0], i * sizeof(diag_lru[0]));
        diag_lru[0] = t;
        return;
    }
    const u32 need = diag_tex_bytes(t);
    ++frame_shadow_misses;
    frame_shadow_bytes += need;
    while (diag_lru_count && (diag_lru_bytes + need > diag_vram_budget ||
                              diag_lru_count >= DIAG_LRU_MAX))
        diag_lru_bytes -= diag_tex_bytes(diag_lru[--diag_lru_count]);
    memmove(&diag_lru[1], &diag_lru[0], diag_lru_count * sizeof(diag_lru[0]));
    diag_lru[0] = t;
    ++diag_lru_count;
    diag_lru_bytes += need;
}

static void diag_shadow_forget(GSTEXTURE *t)
{
    for (u32 i = 0; i < diag_lru_count; i++) {
        if (diag_lru[i] != t) continue;
        diag_lru_bytes -= diag_tex_bytes(t);
        memmove(&diag_lru[i], &diag_lru[i + 1], (--diag_lru_count - i) * sizeof(diag_lru[0]));
        return;
    }
}
#endif
static Affine view = { 1, 0, 0, 1, 0, 0 };
static int current_tint_mode = C2D_TintMult;
static C3D_RenderTarget *current_target;
static C3D_TexEnv tex_envs[6];
static float merge_x = 40.0f, merge_y, merge_scale = 1.0f;
static bool frame_active;
static u32 frame_fade;
static bool scissor_enabled;
static int sc_x0, sc_y0, sc_x1, sc_y1;

#ifndef PS2_DIAG_FRAME_TIMING
#define DIAG_COUNT(k) ((void)0)
#endif

static inline void request_texture(GSTEXTURE *texture)
{
    if (!texture_streaming) return;
    ++frame_bind_requests;
#ifdef PS2_DIAG_FRAME_TIMING
    if (previous_requested_tex == texture) ++frame_consecutive_same_tex;
    previous_requested_tex = texture;
    diag_shadow_bind(texture);
#endif
#ifdef PS2_DIAG_BIND_DEDUP
    /* Diagnostic only. A consecutive identical bind cannot have been
     * evicted by a different texture request. Reset per frame and on free. */
    if (last_manager_bind == texture) return;
    last_manager_bind = texture;
#endif
#ifdef PS2_DIAG_FRAME_TIMING
    const u64 bind_t0 = GetTimerSystemTime();
    const u32 vram_before = texture->Vram;
    gsKit_TexManager_bind(gpu, texture);
    frame_bind_ticks += GetTimerSystemTime() - bind_t0;
    if (texture->Vram != vram_before) ++frame_vram_moves;
#else
    gsKit_TexManager_bind(gpu, texture);
#endif
}

/* Internal Citro2D geometry used by source/utils/gfx.c. */
enum { PS2_C2DI_MAX_VERTICES = 8192, PS2_C2DI_MAX_INDICES = 16384 };
C2Di_Context __C2Di_Context;
static C2Di_Vertex ps2_vertices[PS2_C2DI_MAX_VERTICES];
static u16 ps2_indices[PS2_C2DI_MAX_INDICES];
static C3D_Tex *batched_tex;
static u32 batched_mode;
static void map_xy(float x, float y, float *out_x, float *out_y);
void C2Di_FlushVtxBuf(void);

enum { PS2_LOGICAL_TOP_W = GDPS2_SOURCE_W, PS2_LOGICAL_H = GDPS2_SOURCE_H };
enum { PS2_SCREEN_W = GDPS2_FRAME_W, PS2_SCREEN_H = GDPS2_FRAME_H };
/* The source top display is 400x240 (5:3), NOT 4:3. Fitting the whole
 * image into a PS2 4:3 TV stretches every sprite horizontally/vertically.
 * Use a centered 320x240 logical crop instead of distorting the geometry:
 * the visible region is x=[40,360] and y=[0,240]. The bottom 320x240
 * virtual UI is already merged into exactly this region at x+40.
 * NTSC 640x448 pixels have non-square display aspect on 4:3 televisions;
 * 2.0 GS pixels horizontally and 448/240 vertically yield equal apparent
 * sizes when PCSX2/display is set to 4:3. */

void PS2_C2D_Attach(GSGLOBAL *gs) { gpu = gs; }
void PS2_C2D_EnableTextureStreaming(bool enabled)
{
    texture_streaming = enabled;
#ifdef PS2_DIAG_BIND_DEDUP
    last_manager_bind = NULL;
#endif
}

void PS2_C2D_SetMergedBottom(float dx, float dy, float scale)
{
    merge_x = dx;
    merge_y = dy;
    merge_scale = scale > 0.f ? scale : 1.f;
}

static void logical_to_top(float x, float y, float *top_x, float *top_y)
{
    if (current_target && current_target->screen == GFX_BOTTOM) {
        *top_x = merge_x + x * merge_scale;
        *top_y = merge_y + y * merge_scale;
    } else {
        *top_x = x;
        *top_y = y;
    }
}

static int clamp_i(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

/* GS scissor register endpoints are inclusive. A logical empty rectangle is
 * represented by a one-pixel box; there must be no drawing in that state. */
/* Axis-aligned textured sprites of one texture are queued here and sent as
 * ONE gsKit list primitive (one GIF tag + TEX0/TEX1/CLUT setup) instead of
 * one full packet per sprite. Any state change flushes the batch first, so
 * draw order is preserved. */
enum { PS2_SPRITE_BATCH_MAX = 256 };
static GSPRIMUVPOINT sprite_batch[PS2_SPRITE_BATCH_MAX * 2];
static unsigned sprite_batch_count;
static GSTEXTURE *sprite_batch_tex;

/* Textured triangles (rotated sprites, C2Di geometry) are queued the same
 * way. Only one of the two batches is non-empty at any time: adding to one
 * flushes the other, so the submission order matches the draw order. */
enum { PS2_TRI_BATCH_MAX = 170 };
static GSPRIMUVPOINT tri_batch[PS2_TRI_BATCH_MAX * 3];
static unsigned tri_batch_count;
static GSTEXTURE *tri_batch_tex;

static void flush_sprite_list(void)
{
    if (!sprite_batch_count) return;
    DIAG_COUNT(DC_SPRITE_FLUSH);
    if (gpu && sprite_batch_tex) {
        request_texture(sprite_batch_tex);
        gskit_prim_list_sprite_texture_uv_3d(gpu, sprite_batch_tex,
            (int)(sprite_batch_count * 2), sprite_batch);
    }
    sprite_batch_count = 0;
    sprite_batch_tex = NULL;
}

static void flush_tri_list(void)
{
    if (!tri_batch_count) return;
    DIAG_COUNT(DC_TRI_FLUSH);
    if (gpu && tri_batch_tex) {
        request_texture(tri_batch_tex);
        gsKit_prim_list_triangle_goraud_texture_uv_3d(gpu, tri_batch_tex,
            (int)(tri_batch_count * 3), tri_batch);
    }
    tri_batch_count = 0;
    tri_batch_tex = NULL;
}

static void flush_sprite_batch(void)
{
    flush_sprite_list();
    flush_tri_list();
}

/* The three vertex slots of the next triangle of `tex`. */
static inline GSPRIMUVPOINT *tri_batch_reserve(GSTEXTURE *tex)
{
    flush_sprite_list();
    if (tri_batch_tex != tex || tri_batch_count >= PS2_TRI_BATCH_MAX) {
        flush_tri_list();
        tri_batch_tex = tex;
    }
    return &tri_batch[tri_batch_count++ * 3];
}

static void tri_batch_add(GSTEXTURE *tex,
                          float x0, float y0, float u0, float v0, u64 c0,
                          float x1, float y1, float u1, float v1, u64 c1,
                          float x2, float y2, float u2, float v2, u64 c2)
{
    GSPRIMUVPOINT *v = tri_batch_reserve(tex);
    v[0].rgbaq = rgbaq_to_RGBAQ(c0);
    v[0].uv = vertex_to_UV(tex, u0, v0);
    v[0].xyz2 = vertex_to_XYZ2(gpu, x0, y0, 1);
    v[1].rgbaq = rgbaq_to_RGBAQ(c1);
    v[1].uv = vertex_to_UV(tex, u1, v1);
    v[1].xyz2 = vertex_to_XYZ2(gpu, x1, y1, 1);
    v[2].rgbaq = rgbaq_to_RGBAQ(c2);
    v[2].uv = vertex_to_UV(tex, u2, v2);
    v[2].xyz2 = vertex_to_XYZ2(gpu, x2, y2, 1);
}

static void sprite_batch_add(GSTEXTURE *tex, float x0, float y0, float u0, float v0,
                             float x1, float y1, float u1, float v1, u64 color)
{
    flush_tri_list();
    if (sprite_batch_tex != tex || sprite_batch_count >= PS2_SPRITE_BATCH_MAX) {
        flush_sprite_list();
        sprite_batch_tex = tex;
    }
    GSPRIMUVPOINT *v = &sprite_batch[sprite_batch_count++ * 2];
    v[0].rgbaq = rgbaq_to_RGBAQ(color);
    v[0].uv = vertex_to_UV(tex, u0, v0);
    v[0].xyz2 = vertex_to_XYZ2(gpu, x0, y0, 1);
    v[1].rgbaq = v[0].rgbaq;
    v[1].uv = vertex_to_UV(tex, u1, v1);
    v[1].xyz2 = vertex_to_XYZ2(gpu, x1, y1, 1);
}

static bool clip_empty;
/* Active scissor box in GS pixels. Images entirely outside it are dropped
 * before they reach a batch, so they cost no packet and no texture bind. */
static float cull_x0, cull_y0, cull_x1 = PS2_SCREEN_W, cull_y1 = PS2_SCREEN_H;

static inline bool outside_scissor(float min_x, float min_y, float max_x, float max_y)
{
    return max_x <= cull_x0 || min_x >= cull_x1 || max_y <= cull_y0 || min_y >= cull_y1;
}

static void apply_scissor(void)
{
    flush_sprite_batch();
    if (!gpu) return;
    int x0 = 0, y0 = 0, x1 = PS2_SCREEN_W, y1 = PS2_SCREEN_H;
    if (current_target && current_target->screen == GFX_TOP) {
        y0 = 0;
        y1 = PS2_SCREEN_H;
    }
    if (scissor_enabled && current_target) {
        float ax, ay, bx, by;
        logical_to_top((float)sc_x0, (float)sc_y0, &ax, &ay);
        logical_to_top((float)sc_x1, (float)sc_y1, &bx, &by);
        int sx0 = (int)floorf(GDPS2_PhysicalX(ax)), sy0 = (int)floorf(GDPS2_PhysicalY(ay));
        int sx1 = (int)ceilf(GDPS2_PhysicalX(bx)), sy1 = (int)ceilf(GDPS2_PhysicalY(by));
        if (sx0 > x0) x0 = sx0;
        if (sx1 < x1) x1 = sx1;
        if (sy0 > y0) y0 = sy0;
        if (sy1 < y1) y1 = sy1;
    }
    x0 = clamp_i(x0, 0, PS2_SCREEN_W - 1);
    y0 = clamp_i(y0, 0, PS2_SCREEN_H - 1);
    x1 = clamp_i(x1, 0, PS2_SCREEN_W);
    y1 = clamp_i(y1, 0, PS2_SCREEN_H);
    clip_empty = x1 <= x0 || y1 <= y0 ||
                 (current_target && (current_target->screen < 0 ||
                                     current_target->side == GFX_RIGHT));
    cull_x0 = (float)x0; cull_y0 = (float)y0;
    cull_x1 = (float)x1; cull_y1 = (float)y1;
    gsKit_set_scissor(gpu,
        GS_SETREG_SCISSOR(x0, clip_empty ? x0 : x1-1,
                          y0, clip_empty ? y0 : y1-1));
}

static int read_word(const u8 **cursor, const u8 *end, u32 *v)
{
    if (*cursor > end || (size_t)(end - *cursor) < 4) return 0;
    memcpy(v, *cursor, 4);
    *cursor += 4;
    return 1;
}

static int read_half(const u8 **cursor, const u8 *end, u16 *v)
{
    if (*cursor > end || (size_t)(end - *cursor) < 2) return 0;
    memcpy(v, *cursor, 2);
    *cursor += 2;
    return 1;
}

void C2D_SpriteSheetFree(C2D_SpriteSheet sheet)
{
    if (!sheet) return;
    C2Di_FlushVtxBuf();
    /* GS allocations last for the demo's lifetime; VRAM is not a free list.
     * The game will need a real atlas cache when loading/unloading levels. */
    if (sheet->pages) {
        for (u32 i = 0; i < sheet->page_count; i++) {
#ifdef PS2_DIAG_BIND_DEDUP
            if (last_manager_bind == &sheet->pages[i].texture)
                last_manager_bind = NULL;
#endif
#ifdef PS2_DIAG_FRAME_TIMING
            diag_shadow_forget(&sheet->pages[i].texture);
#endif
            if (texture_streaming && gpu && sheet->pages[i].texture.Mem)
                gsKit_TexManager_free(gpu, &sheet->pages[i].texture);
            free(sheet->pages[i].texture.Mem);
            free(sheet->pages[i].texture.Clut);
        }
    }
    free(sheet->pages);
    free(sheet->subtex);
    free(sheet->page_of);
    free(sheet);
}

C2D_SpriteSheet C2D_SpriteSheetLoadFromMem(const void *data, size_t size)
{
    const u8 *cursor = (const u8 *)data, *end;
    C2D_SpriteSheet sheet = NULL;
    const char *reason = "invalid header";
    int problem_index = -1;
    if (!data || !gpu || size < 12 || size > 64u * 1024u * 1024u) {
        printf("GDPS2 ATLAS: invalid input size=%lu gs=%d\n",
               (unsigned long)size, gpu != NULL);
        return NULL;
    }
    end = cursor + size;
    if (memcmp(cursor, "P2H1", 4)) {
        printf("GDPS2 ATLAS: invalid magic (expected P2H1)\n");
        return NULL;
    }
    cursor += 4;
    sheet = calloc(1, sizeof(*sheet));
    if (!sheet) {
        printf("GDPS2 ATLAS: cannot allocate sheet descriptor\n");
        return NULL;
    }
    if (!read_word(&cursor, end, &sheet->image_count) ||
        !read_word(&cursor, end, &sheet->page_count) ||
        sheet->image_count > 65535 || sheet->page_count == 0 ||
        sheet->page_count > 128) goto fail;

    reason = "cannot allocate sprite metadata";
    sheet->pages = calloc(sheet->page_count, sizeof(Ps2Page));
    sheet->subtex = calloc(sheet->image_count, sizeof(Tex3DS_SubTexture));
    sheet->page_of = calloc(sheet->image_count, sizeof(u16));
    if (!sheet->pages || !sheet->subtex || !sheet->page_of) goto fail;

    for (u32 i = 0; i < sheet->page_count; i++) {
        problem_index = (int)i;
        reason = "invalid or truncated texture page";
        Ps2Page *page = &sheet->pages[i];
        GSTEXTURE *tex = &page->texture;
        u16 w, h;
        size_t bytes;
        if (!read_half(&cursor, end, &w) || !read_half(&cursor, end, &h) ||
            (size_t)(end - cursor) < 4 || cursor[0] != 2 ||
            !w || !h || w > 512 || h > 512 || (w & (w-1)) || (h & (h-1))) goto fail;
        cursor += 4;
        bytes = (size_t)w * h;
        if ((size_t)(end - cursor) < 1024 + bytes) goto fail;

        tex->Clut = memalign(64, 1024);
        tex->Mem = memalign(64, bytes);
        reason = "EE RAM allocation for page/CLUT failed";
        if (!tex->Clut || !tex->Mem) goto fail;
        memcpy(tex->Clut, cursor, 1024);
        cursor += 1024;
        memcpy(tex->Mem, cursor, bytes);
        cursor += bytes;
        tex->Width = w;
        tex->Height = h;
        tex->PSM = GS_PSM_T8;
        tex->ClutPSM = GS_PSM_CT32;
        tex->ClutStorageMode = GS_CLUT_STORAGE_CSM1;
        tex->Filter = GS_FILTER_LINEAR;
        tex->TBW = w / 64;
        if (!texture_streaming) {
            reason = "GS VRAM reservation failed";
            tex->Vram = gsKit_vram_alloc(gpu, gsKit_texture_size(w, h, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
            tex->VramClut = gsKit_vram_alloc(gpu, gsKit_texture_size(16, 16, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
            if (tex->Vram == GSKIT_ALLOC_ERROR || tex->VramClut == GSKIT_ALLOC_ERROR) goto fail;
        } else {
            /* TexManager allocates/recycles VRAM for this page on demand;
             * only its EE-side palette and texels remain resident here. */
            tex->Vram = 0;
            tex->VramClut = 0;
            tex->Delayed = GS_SETTING_ON;
        }

        page->compat.data = tex->Mem;
        page->compat.clut = tex->Clut;
        page->compat.width = w;
        page->compat.height = h;
        page->compat.filter = GPU_LINEAR;
        if (!texture_streaming) {
            FlushCache(0);
            gsKit_texture_upload(gpu, tex);
        }
    }

    for (u32 i = 0; i < sheet->image_count; i++) {
        problem_index = (int)i;
        reason = "invalid/truncated sprite metadata or UV bounds";
        u16 page, x, y, w, h, reserved;
        Tex3DS_SubTexture *sub = &sheet->subtex[i];
        if (!read_half(&cursor, end, &page) || !read_half(&cursor, end, &x) ||
            !read_half(&cursor, end, &y) || !read_half(&cursor, end, &w) ||
            !read_half(&cursor, end, &h) || !read_half(&cursor, end, &reserved) ||
            page >= sheet->page_count ||
            x + (u32)w > sheet->pages[page].compat.width ||
            y + (u32)h > sheet->pages[page].compat.height) goto fail;
        sheet->page_of[i] = page;
        sub->width = w;
        sub->height = h;
        sub->left = (float)x / sheet->pages[page].compat.width;
        sub->right = (float)(x + w) / sheet->pages[page].compat.width;
        sub->top = 1.0f - (float)y / sheet->pages[page].compat.height;
        sub->bottom = 1.0f - (float)(y + h) / sheet->pages[page].compat.height;
    }
    return sheet;
fail:
    printf("GDPS2 ATLAS: decode failed reason=%s index=%d bytes=%lu sprites=%lu pages=%lu\n",
           reason, problem_index, (unsigned long)size,
           (unsigned long)(sheet ? sheet->image_count : 0),
           (unsigned long)(sheet ? sheet->page_count : 0));
    C2D_SpriteSheetFree(sheet);
    return NULL;
}

C2D_SpriteSheet C2D_SpriteSheetLoad(const char *filename)
{
    char path[256];
    FILE *file;
    long length = -1;
    void *bytes;
    C2D_SpriteSheet sheet;
    if (!filename) { puts("GDPS2 ATLAS: NULL filename"); return NULL; }
    if (snprintf(path, sizeof(path), "%s", filename) >= (int)sizeof(path)) {
        printf("GDPS2 ATLAS: path too long: %s\n", filename);
        return NULL;
    }
    char *dot = strrchr(path, '.');
    if (dot && strcmp(dot, ".t3x") == 0) {
        if ((size_t)(dot - path) + sizeof(".p2sheet") > sizeof(path)) {
            printf("GDPS2 ATLAS: mapped path too long: %s\n", filename);
            return NULL;
        }
        strcpy(dot, ".p2sheet");
    }
    file = fopen(path, "rb");
    if (!file) {
        printf("GDPS2 ATLAS: open failed file=%s mapped=%s errno=%d\n",
               filename, path, errno);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 12 ||
        length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        printf("GDPS2 ATLAS: seek or size failed path=%s length=%ld errno=%d\n",
               path, length, errno);
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (!bytes) {
        printf("GDPS2 ATLAS: cannot allocate read buffer path=%s bytes=%ld\n", path, length);
        fclose(file);
        return NULL;
    }
    size_t got = fread(bytes, 1, (size_t)length, file);
    if (got != (size_t)length) {
        printf("GDPS2 ATLAS: truncated read path=%s expected=%ld got=%lu errno=%d\n",
               path, length, (unsigned long)got, errno);
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    sheet = C2D_SpriteSheetLoadFromMem(bytes, (size_t)length);
#ifdef PS2_DIAG_FRAME_TIMING
    if (sheet) {
        const char *base = strrchr(filename, '/');
        base = base ? base + 1 : filename;
        for (u32 i = 0; i < sheet->page_count; i++) {
            unsigned slot = diag_name_count;
            for (unsigned k = 0; k < diag_name_count; k++)
                if (diag_names[k].tex == &sheet->pages[i].texture) slot = k;
            if (slot == diag_name_count) {
                if (diag_name_count >= DIAG_NAMES_MAX) break;
                ++diag_name_count;
            }
            diag_names[slot].tex = &sheet->pages[i].texture;
            snprintf(diag_names[slot].name, sizeof(diag_names[slot].name), "%.*s#%lu",
                     (int)strcspn(base, "."), base, (unsigned long)i);
        }
    }
#endif
    if (sheet)
        printf("GDPS2 ATLAS: OK %s -> %s bytes=%ld sprites=%lu pages=%lu streaming=%d\n",
               filename, path, length, (unsigned long)sheet->image_count,
               (unsigned long)sheet->page_count, texture_streaming ? 1 : 0);
    else
        printf("GDPS2 ATLAS: decode failure file=%s mapped=%s bytes=%ld\n",
               filename, path, length);
    free(bytes);
    return sheet;
}

size_t C2D_SpriteSheetCount(C2D_SpriteSheet sheet)
{
    return sheet ? sheet->image_count : 0;
}

C2D_Image C2D_SpriteSheetGetImage(C2D_SpriteSheet sheet, size_t index)
{
    static const Tex3DS_SubTexture blank = { 1, 1, 0, 1, 0, 0 };
    C2D_Image image = { NULL, &blank };
    if (sheet && index < sheet->image_count) {
        image.tex = &sheet->pages[sheet->page_of[index]].compat;
        image.subtex = &sheet->subtex[index];
    }
    return image;
}

bool C2D_Init(size_t maxObjects)
{
    (void)maxObjects;
    memset(&__C2Di_Context, 0, sizeof(__C2Di_Context));
    __C2Di_Context.vtxBuf = ps2_vertices;
    __C2Di_Context.idxBuf = ps2_indices;
    __C2Di_Context.vtxBufSize = PS2_C2DI_MAX_VERTICES;
    __C2Di_Context.idxBufSize = PS2_C2DI_MAX_INDICES;
    __C2Di_Context.flags = C2DiF_Active | C2DiF_DirtyAny;
    C2D_SetTintMode(C2D_TintSolid);
    C2D_ViewReset();
    return gpu != NULL;
}
void C2D_Fini(void) { C2Di_FlushVtxBuf(); }
/* This is also called after set_scissor() from source/utils/gfx.c, so it
 * must not reset the active camera transformation. The GS draws immediately
 * into its command queue; there is no Citro2D vertex batch to prepare. */
void C2D_Prepare(void) { __C2Di_Context.flags |= C2DiF_Active; }
void C2D_Flush(void) { C2Di_FlushVtxBuf(); }
void C2D_SceneSize(u32 width, u32 height, bool tilt)
{
    /* The PS2 stage is always 400x240. The game uses this API for 3DS
     * projections, so leave it as a no-op until zoom/wide-mode is ported. */
    (void)width; (void)height; (void)tilt;
}

void C2D_SceneTarget(C3D_RenderTarget *target)
{
    C2Di_FlushVtxBuf();
    current_target = target;
    scissor_enabled = false;
    clip_empty = false;
    if (frame_active) apply_scissor();
}

void C2D_TargetClear(C3D_RenderTarget *target, u32 color)
{
    C2Di_FlushVtxBuf();
    if (!frame_active || !gpu || !target || target->side == GFX_RIGHT)
        return;
    /* Both 3DS screen targets share ONE physical GS framebuffer on PS2.
     * C3D_FrameBegin already clears that buffer once. Clearing again when
     * entering the other target destroys menus that draw BOTTOM then TOP:
     * only the later TOP dots and final GS hover outline remain visible.
     * Keep both target clears as no-ops; this is a merged renderer, not two
     * independent framebuffers. Offscreen targets are not implemented. */
    (void)color;
}

void C2D_Fade(u32 color) { frame_fade = color; }

C3D_RenderTarget *C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side)
{
    C3D_RenderTarget *target = C3D_RenderTargetCreate(240,
        screen == GFX_TOP ? 400 : 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (target) C3D_RenderTargetSetOutput(target, screen, side, 0);
    return target;
}

bool C3D_Init(size_t cmdBufSize)
{
    (void)cmdBufSize;
    /* gsKit_init_global / gsKit_init_screen are owned by the platform. */
    return gpu != NULL;
}

void C3D_Fini(void) { frame_active = false; }

static u32 present_vcount;
u32 PS2_PresentVcount(void) { return present_vcount; }

bool C3D_FrameBegin(u8 flags)
{
    (void)flags;
    if (!gpu || frame_active) return false;
    frame_active = true;
#ifdef PS2_DIAG_BIND_DEDUP
    last_manager_bind = NULL;
#endif
#ifdef PS2_DIAG_FRAME_TIMING
    previous_requested_tex = NULL;
#endif
    current_target = NULL;
    scissor_enabled = false;
    clip_empty = false;
    apply_scissor();
    gsKit_clear(gpu, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
    C2D_ViewReset();
    C2D_Prepare();
    return true;
}

void C3D_FrameEnd(u8 flags)
{
    (void)flags;
    if (!gpu || !frame_active) return;
#ifdef PS2_DIAG_FRAME_TIMING
    u64 profile_t0 = GetTimerSystemTime();
#endif
    C2Di_FlushVtxBuf();
    current_target = NULL;
    scissor_enabled = false;
    clip_empty = false;
    apply_scissor();
    /* PSP-inspired controller focus: subtle golden outline around the
     * selected button, drawn after all UI and only outside active gameplay.
     * Coordinates use the same centered 4:3 projection as normal sprites. */
    float nx0, ny0, nx1, ny1;
    if (PS2_NavFocusRect(&nx0, &ny0, &nx1, &ny1)) {
        float x0 = GDPS2_PhysicalX(nx0), y0 = GDPS2_PhysicalY(ny0);
        float x1 = GDPS2_PhysicalX(nx1), y1 = GDPS2_PhysicalY(ny1);
        const u64 yellow = GS_SETREG_RGBAQ(125, 112, 25, 75, 0);
        gsKit_prim_sprite(gpu, x0, y0, x1, y0 + 2, 1, yellow);
        gsKit_prim_sprite(gpu, x0, y1 - 2, x1, y1, 1, yellow);
        gsKit_prim_sprite(gpu, x0, y0, x0 + 2, y1, 1, yellow);
        gsKit_prim_sprite(gpu, x1 - 2, y0, x1, y1, 1, yellow);
    }
    float cursor_x, cursor_y;
    if (PS2_NavCursorPoint(&cursor_x, &cursor_y)) {
        float x = GDPS2_PhysicalX(cursor_x);
        float y = GDPS2_PhysicalY(cursor_y);
        const u64 pointer_color = GS_SETREG_RGBAQ(125, 125, 125, 100, 0);
        gsKit_prim_sprite(gpu, x - 4, y, x + 5, y + 1, 1, pointer_color);
        gsKit_prim_sprite(gpu, x, y - 4, x + 1, y + 5, 1, pointer_color);
    }
    if (frame_fade >> 24) {
        u8 alpha = (u8)((((frame_fade >> 24) & 255) * 128 + 127) / 255);
        gsKit_prim_sprite(gpu, 0, 0, PS2_SCREEN_W, PS2_SCREEN_H, 1,
            GS_SETREG_RGBAQ(frame_fade & 255, (frame_fade >> 8) & 255,
                           (frame_fade >> 16) & 255, alpha, 0));
    }
    #ifdef PS2_DIAG_FRAME_TIMING
    u64 profile_t1 = GetTimerSystemTime();
    #endif
    gsKit_queue_exec(gpu);
    #ifdef PS2_DIAG_FRAME_TIMING
    u64 profile_t2 = GetTimerSystemTime();
    #endif
    gsKit_sync_flip(gpu);
    present_vcount = PS2_VblankCount();
    #ifdef PS2_DIAG_FRAME_TIMING
    u64 profile_t3 = GetTimerSystemTime();
    /* A 60-FPS average can hide visible repeated frames. Measure the
     * *presented* vblank intervals rather than deducing them from EE time.
     * Keep the histogram diagnostic-only: release has zero extra work. */
    static u32 diag_previous_present_vcount;
    static unsigned diag_present_gap1, diag_present_gap2, diag_present_gap3plus;
    static u32 diag_present_max_gap;
    if (diag_previous_present_vcount) {
        const u32 present_gap = present_vcount - diag_previous_present_vcount;
        if (present_gap == 1) ++diag_present_gap1;
        else if (present_gap == 2) ++diag_present_gap2;
        else if (present_gap >= 3 && present_gap < 60) ++diag_present_gap3plus;
        if (present_gap > diag_present_max_gap && present_gap < 60)
            diag_present_max_gap = present_gap;
    }
    diag_previous_present_vcount = present_vcount;
    #endif
    if (texture_streaming) gsKit_TexManager_nextFrame(gpu);
    #ifdef PS2_DIAG_FRAME_TIMING
    u64 profile_t4 = GetTimerSystemTime();
    static unsigned sample_count, max_bind_calls;
    static unsigned long total_bind_calls;
    static unsigned long total_same_tex;
    static unsigned long total_misses, total_upload_bytes, total_unique;
    static unsigned max_misses, max_upload_bytes, max_unique;
    static float total_bind_ms, max_bind_ms, total_image_ms;
    static unsigned long total_moves;
    static unsigned max_moves;
    {
        const float bind_ms = (float)frame_bind_ticks / ((float)kBUSCLK / 1000.0f);
        total_bind_ms += bind_ms;
        if (bind_ms > max_bind_ms) max_bind_ms = bind_ms;
        total_moves += frame_vram_moves;
        total_image_ms += (float)frame_image_ticks / ((float)kBUSCLK / 1000.0f);
        frame_image_ticks = 0;
        if (frame_vram_moves > max_moves) max_moves = frame_vram_moves;
        frame_bind_ticks = 0;
        frame_vram_moves = 0;
    }
    static unsigned long dc_total[DC_COUNT];
    static u32 dc_max[DC_COUNT];
    for (int k = 0; k < DC_COUNT; k++) {
        dc_total[k] += diag_dc[k];
        if (diag_dc[k] > dc_max[k]) dc_max[k] = diag_dc[k];
        diag_dc[k] = 0;
    }
    static float total_flush_ms, total_queue_ms, total_flip_ms, total_tex_ms;
    static float max_flip_ms, max_tex_ms;
    const float ticks_per_ms = (float)kBUSCLK / 1000.0f;
    const float flush_ms = (float)(profile_t1 - profile_t0) / ticks_per_ms;
    const float queue_ms = (float)(profile_t2 - profile_t1) / ticks_per_ms;
    const float flip_ms = (float)(profile_t3 - profile_t2) / ticks_per_ms;
    const float tex_ms = (float)(profile_t4 - profile_t3) / ticks_per_ms;
    ++sample_count;
    total_flush_ms += flush_ms;
    total_queue_ms += queue_ms;
    total_flip_ms += flip_ms;
    total_tex_ms += tex_ms;
    total_bind_calls += frame_bind_requests;
    total_same_tex += frame_consecutive_same_tex;
    if (frame_bind_requests > max_bind_calls) max_bind_calls = frame_bind_requests;
    if (flip_ms > max_flip_ms) max_flip_ms = flip_ms;
    if (tex_ms > max_tex_ms) max_tex_ms = tex_ms;
    total_misses += frame_shadow_misses;
    total_upload_bytes += frame_shadow_bytes;
    total_unique += frame_unique_pages;
    if (frame_shadow_misses > max_misses) max_misses = frame_shadow_misses;
    if (frame_shadow_bytes > max_upload_bytes) max_upload_bytes = frame_shadow_bytes;
    if (frame_unique_pages > max_unique) max_unique = frame_unique_pages;
    if (sample_count >= 120) {
        printf("GDPS2 FT PAGES (last frame, binds):");
        for (unsigned k = 0; k < frame_unique_pages; k++)
            printf(" %s:%u", diag_page_name(diag_seen[k]), (unsigned)diag_seen_binds[k]);
        printf("\n");
    }
    frame_shadow_misses = frame_shadow_bytes = frame_unique_pages = 0;
    if (sample_count >= 120) {
        printf("GDPS2 FT VBLANK: gap1=%u gap2=%u gap3plus=%u gap_max=%lu (120 presented frames; first frame omitted)\n",
            diag_present_gap1, diag_present_gap2, diag_present_gap3plus,
            (unsigned long)diag_present_max_gap);
        diag_present_gap1 = diag_present_gap2 = diag_present_gap3plus = 0;
        diag_present_max_gap = 0;
        printf("GDPS2 FT GS: frames=120 flush_avg=%.2fms queue_avg=%.2fms flip_avg=%.2fms flip_max=%.2fms tex_avg=%.2fms tex_max=%.2fms binds_avg=%lu binds_max=%u same_tex_avg=%lu (calls, not VRAM transfers)\n",
            total_flush_ms/120.0f, total_queue_ms/120.0f, total_flip_ms/120.0f,
            max_flip_ms, total_tex_ms/120.0f, max_tex_ms,
            total_bind_calls/120u, max_bind_calls, total_same_tex/120u);
        printf("GDPS2 FT VRAM: budget=%luKB pages_avg=%lu pages_max=%u est_uploads_avg=%lu est_uploads_max=%u est_upload_kb_avg=%lu est_upload_kb_max=%u (shadow LRU)\n",
            (unsigned long)(diag_vram_budget / 1024u), total_unique/120u, max_unique,
            total_misses/120u, max_misses, total_upload_bytes/120u/1024u,
            max_upload_bytes/1024u);
        printf("GDPS2 FT IMG: draw_image_ms_avg=%.2f\n", total_image_ms/120.0f);
        total_image_ms = 0;
        printf("GDPS2 FT BIND: bind_ms_avg=%.2f bind_ms_max=%.2f vram_moves_avg=%lu vram_moves_max=%u\n",
            total_bind_ms/120.0f, max_bind_ms, total_moves/120u, max_moves);
        printf("GDPS2 FT DRAW (avg/max): images=%lu/%lu rotated=%lu/%lu culled=%lu/%lu sprite_lists=%lu/%lu tri_lists=%lu/%lu blend=%lu/%lu c2di_tris=%lu/%lu\n",
            dc_total[0]/120u, (unsigned long)dc_max[0], dc_total[1]/120u, (unsigned long)dc_max[1],
            dc_total[2]/120u, (unsigned long)dc_max[2], dc_total[3]/120u, (unsigned long)dc_max[3],
            dc_total[4]/120u, (unsigned long)dc_max[4], dc_total[5]/120u, (unsigned long)dc_max[5],
            dc_total[6]/120u, (unsigned long)dc_max[6]);
        for (int k = 0; k < DC_COUNT; k++) dc_total[k] = dc_max[k] = 0;
        total_bind_ms = max_bind_ms = 0;
        total_moves = 0;
        max_moves = 0;
        total_misses = total_upload_bytes = total_unique = 0;
        max_misses = max_upload_bytes = max_unique = 0;
        sample_count = max_bind_calls = 0;
        total_bind_calls = 0;
        total_same_tex = 0;
        total_flush_ms = total_queue_ms = total_flip_ms = total_tex_ms = 0;
        max_flip_ms = max_tex_ms = 0;
    }
    #endif
    if (texture_streaming) {
        ++frame_count;
        if (frame_count <= 3)
            printf("GDPS2 GS: submitted frame=%lu bind_requests=%lu (not unique transfers)\n",
                   (unsigned long)frame_count, (unsigned long)frame_bind_requests);
        frame_bind_requests = 0;
#ifdef PS2_DIAG_FRAME_TIMING
        frame_consecutive_same_tex = 0;
#endif
    }
    frame_active = false;
}

void C3D_AlphaBlend(GPU_BLENDEQUATION color_eq, GPU_BLENDEQUATION alpha_eq,
                    GPU_BLENDFACTOR src, GPU_BLENDFACTOR dst,
                    GPU_BLENDFACTOR src_alpha, GPU_BLENDFACTOR dst_alpha)
{
    (void)color_eq; (void)alpha_eq; (void)src_alpha; (void)dst_alpha;
    /* GS ALPHA=(A-B)*C+D.  Source-over is (Cs-Cd)*As+Cd; the old
     * GS_BLEND_BACK2FRONT value 0x01 computes (Cd-Cs)*As+Cs instead,
     * making fully opaque UI pixels disappear while only edges remained.
     * PABE=0 makes blending work for every fragment including <0x80. */
    flush_sprite_batch();
    DIAG_COUNT(DC_BLEND);
    if (!gpu) return;
    if (src == GPU_SRC_ALPHA && dst == GPU_ONE_MINUS_SRC_ALPHA)
        gsKit_set_primalpha(gpu, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    else if (src == GPU_SRC_ALPHA && dst == GPU_ONE)
        gsKit_set_primalpha(gpu, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
    else if (src == GPU_ONE && dst == GPU_ONE)
        gsKit_set_primalpha(gpu, GS_SETREG_ALPHA(0, 2, 2, 1, 128), 0);
}

void C3D_SetScissor(GPU_SCISSORMODE mode,
                    u32 left, u32 top, u32 right, u32 bottom)
{
    C2Di_FlushVtxBuf();
    if (!current_target || mode == GPU_SCISSOR_DISABLE) {
        scissor_enabled = false;
    } else {
        /* utils/gfx.c rotates the x/y rectangle for the 3DS framebuffer. */
        const int w = current_target->width, h = current_target->height;
        sc_x0 = clamp_i(w - (int)bottom, 0, w);
        sc_x1 = clamp_i(w - (int)top, 0, w);
        sc_y0 = clamp_i(h - (int)right, 0, h);
        sc_y1 = clamp_i(h - (int)left, 0, h);
        scissor_enabled = true;
    }
    if (frame_active) apply_scissor();
}

C3D_RenderTarget *C3D_RenderTargetCreate(int width, int height,
                                        GPU_COLORBUF colorFmt, GPU_DEPTHBUF depthFmt)
{
    (void)colorFmt; (void)depthFmt;
    if (width <= 0 || height <= 0) return NULL;
    C3D_RenderTarget *target = calloc(1, sizeof(*target));
    if (!target) return NULL;
    /* The 3DS framebuffer is rotated; callers supply 240x400 / 240x320. */
    target->screen = -1;
    target->width = height > 400 ? 400 : height;
    target->height = width > 240 ? 240 : width;
    return target;
}

void C3D_RenderTargetDelete(C3D_RenderTarget *target)
{
    if (!target) return;
    C2Di_FlushVtxBuf();
    if (current_target == target) {
        current_target = NULL;
        if (frame_active) apply_scissor();
    }
    free(target);
}

void C3D_RenderTargetSetOutput(C3D_RenderTarget *target, gfxScreen_t screen,
                               gfx3dSide_t side, u32 transferFlags)
{
    (void)transferFlags;
    if (!target) return;
    target->screen = screen;
    target->side = side;
    target->width = screen == GFX_TOP ? 400 : 320;
    target->height = 240;
}

float C3D_GetProcessingTime(void) { return 0.f; }
float C3D_GetDrawingTime(void) { return 0.f; }
float C3D_GetCmdBufUsage(void) { return 0.f; }

C3D_TexEnv *C3D_GetTexEnv(int id)
{
    return &tex_envs[id >= 0 && id < 6 ? id : 0];
}
void C3D_TexEnvInit(C3D_TexEnv *env) { if (env) memset(env, 0, sizeof(*env)); }
void C3D_TexEnvSrc(C3D_TexEnv *env, C3D_TexEnvMode mode, int s1, int s2, int s3)
{ (void)env; (void)mode; (void)s1; (void)s2; (void)s3; }
void C3D_TexEnvFunc(C3D_TexEnv *env, C3D_TexEnvMode mode, int func)
{ (void)env; (void)mode; (void)func; }

void C2D_SetTintMode(C2D_TintMode mode)
{
    C2Di_FlushVtxBuf();
    current_tint_mode = mode;
    u32 bits = mode == C2D_TintMult ? C2DiF_Mode_ImageMult :
               mode == C2D_TintLuma ? C2DiF_Mode_ImageLuma : C2DiF_Mode_ImageSolid;
    __C2Di_Context.flags = (__C2Di_Context.flags & ~C2DiF_TintMode_Mask) |
        (bits << (C2DiF_TintMode_Shift - C2DiF_Mode_Shift));
}
void C2D_ViewReset(void)
{
    C2Di_FlushVtxBuf();
    view = (Affine){ 1, 0, 0, 1, 0, 0 };
}
void C2D_ViewTranslate(float x, float y)
{
    C2Di_FlushVtxBuf();
    view.tx += view.a * x + view.b * y;
    view.ty += view.c * x + view.d * y;
}
void C2D_ViewScale(float x, float y)
{
    C2Di_FlushVtxBuf();
    view.a *= x; view.b *= y;
    view.c *= x; view.d *= y;
}
void C2D_ViewRotate(float angle)
{
    C2Di_FlushVtxBuf();
    float c = cosf(angle), s = sinf(angle);
    Affine v = view;
    view.a = v.a*c + v.b*s; view.b = v.b*c - v.a*s;
    view.c = v.c*c + v.d*s; view.d = v.d*c - v.c*s;
}
void C2D_ViewShear(float x, float y)
{
    C2Di_FlushVtxBuf();
    Affine v = view;
    view.a = v.a + v.b*y; view.b = v.a*x + v.b;
    view.c = v.c + v.d*y; view.d = v.c*x + v.d;
}
void C2D_ViewSave(C3D_Mtx *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->r[0].x = view.a; m->r[0].y = view.b; m->r[0].w = view.tx;
    m->r[1].x = view.c; m->r[1].y = view.d; m->r[1].w = view.ty;
    m->r[2].z = 1.f;
    m->r[3].w = 1.f;
}
void C2D_ViewRestore(const C3D_Mtx *m)
{
    if (!m) return;
    C2Di_FlushVtxBuf();
    view.a = m->r[0].x; view.b = m->r[0].y; view.tx = m->r[0].w;
    view.c = m->r[1].x; view.d = m->r[1].y; view.ty = m->r[1].w;
}

static u64 tint_to_gs(const C2D_ImageTint *tint)
{
    u32 color = 0xFFFFFFFF;
    float k = 0;
    if (tint) {
        color = tint->corners[0].color;
        k = tint->corners[0].blend;
    }
    if (current_tint_mode == C2D_TintSolid) k = 1.0f;
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    u8 r = (u8)(255 - (255 - (color & 255)) * k);
    u8 g = (u8)(255 - (255 - ((color >> 8) & 255)) * k);
    u8 b = (u8)(255 - (255 - ((color >> 16) & 255)) * k);
    u8 a = (u8)((((color >> 24) & 255) * 128 + 127) / 255);
    return GS_SETREG_RGBAQ(r >> 1, g >> 1, b >> 1, a, 0);
}

static void map_xy(float x, float y, float *out_x, float *out_y)
{
    float tx = view.a * x + view.b * y + view.tx;
    float ty = view.c * x + view.d * y + view.ty;
    logical_to_top(tx, ty, &tx, &ty);
    *out_x = GDPS2_PhysicalX(tx);
    *out_y = GDPS2_PhysicalY(ty);
}

typedef float ps2_v4 __attribute__((vector_size(16), aligned(16)));
typedef int ps2_v4i __attribute__((vector_size(16), aligned(16)));

/* Logical (view-space) coordinates to GS pixels as one affine map:
 * X = g0*x + g1*y + g2, Y = g3*x + g4*y + g5. Same steps as map_xy. */
static void physical_affine(float g[6])
{
    float k = 1.f, ox = 0.f, oy = 0.f;
    if (current_target && current_target->screen == GFX_BOTTOM) {
        k = merge_scale;
        ox = merge_x;
        oy = merge_y;
    }
    const float sx = (float)GDPS2_FRAME_W / GDPS2_VISIBLE_W;
    const float sy = (float)GDPS2_FRAME_H / GDPS2_SOURCE_H;
    g[0] = sx * k * view.a;
    g[1] = sx * k * view.b;
    g[2] = sx * (ox + k * view.tx - GDPS2_CROP_X);
    g[3] = sy * k * view.c;
    g[4] = sy * k * view.d;
    g[5] = sy * (oy + k * view.ty);
}

/* Four corners: X = mx.x*dx + mx.y*dy + mx.z (Y likewise with my), as
 * floats for culling and as 12.4 fixed point for the GS, plus the UVs in
 * 12.4. On the EE this runs in VU0 macro mode: two FMAC chains and four
 * VFTOI4. Only the main thread draws, so VU0 state is not shared. */
static inline void corners_to_gs(const ps2_v4 *dx, const ps2_v4 *dy,
                                 const ps2_v4 *mx, const ps2_v4 *my,
                                 const ps2_v4 *cu, const ps2_v4 *cv,
                                 ps2_v4 *fx, ps2_v4 *fy,
                                 ps2_v4i *ix, ps2_v4i *iy, ps2_v4i *iu, ps2_v4i *iv)
{
#ifndef PS2_NO_VU0
    static const ps2_v4 ones = { 1.f, 1.f, 1.f, 1.f };
    __asm__ __volatile__(
        "lqc2         $vf1, 0(%[dx])\n"
        "lqc2         $vf2, 0(%[dy])\n"
        "lqc2         $vf3, 0(%[mx])\n"
        "lqc2         $vf4, 0(%[my])\n"
        "lqc2         $vf5, 0(%[one])\n"
        "lqc2         $vf8, 0(%[cu])\n"
        "lqc2         $vf9, 0(%[cv])\n"
        "vmulax.xyzw  $ACC, $vf1, $vf3x\n"
        "vmadday.xyzw $ACC, $vf2, $vf3y\n"
        "vmaddz.xyzw  $vf6, $vf5, $vf3z\n"
        "vmulax.xyzw  $ACC, $vf1, $vf4x\n"
        "vmadday.xyzw $ACC, $vf2, $vf4y\n"
        "vmaddz.xyzw  $vf7, $vf5, $vf4z\n"
        "sqc2         $vf6, 0(%[fx])\n"
        "sqc2         $vf7, 0(%[fy])\n"
        "vftoi4.xyzw  $vf6, $vf6\n"
        "vftoi4.xyzw  $vf7, $vf7\n"
        "vftoi4.xyzw  $vf8, $vf8\n"
        "vftoi4.xyzw  $vf9, $vf9\n"
        "sqc2         $vf6, 0(%[ix])\n"
        "sqc2         $vf7, 0(%[iy])\n"
        "sqc2         $vf8, 0(%[iu])\n"
        "sqc2         $vf9, 0(%[iv])\n"
        :
        : [dx] "r"(dx), [dy] "r"(dy), [mx] "r"(mx), [my] "r"(my),
          [one] "r"(&ones), [cu] "r"(cu), [cv] "r"(cv),
          [fx] "r"(fx), [fy] "r"(fy), [ix] "r"(ix), [iy] "r"(iy),
          [iu] "r"(iu), [iv] "r"(iv)
        : "memory");
#else
    for (int i = 0; i < 4; i++) {
        (*fx)[i] = (*mx)[0] * (*dx)[i] + (*mx)[1] * (*dy)[i] + (*mx)[2];
        (*fy)[i] = (*my)[0] * (*dx)[i] + (*my)[1] * (*dy)[i] + (*my)[2];
        (*ix)[i] = (int)((*fx)[i] * 16.0f);
        (*iy)[i] = (int)((*fy)[i] * 16.0f);
        (*iu)[i] = (int)((*cu)[i] * 16.0f);
        (*iv)[i] = (int)((*cv)[i] * 16.0f);
    }
#endif
}

/* The clamps of gsKit's vertex_to_XYZ2 / vertex_to_UV, on 12.4 values. */
static inline int clamp_gs_xy(int v)
{
    return v < 0 ? 0 : v >= 4096 * 16 ? 4096 * 16 - 1 : v;
}

static inline int clamp_gs_uv(int v, int max)
{
    if (v < 0) v = 0;
    if (v > max) v = max;
    return v >= 1024 * 16 ? 1024 * 16 - 1 : v;
}

static bool draw_image(C2D_Image img, const C2D_DrawParams *params,
                       const C2D_ImageTint *tint);

#ifdef PS2_DIAG_FRAME_TIMING
bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams *params,
                   const C2D_ImageTint *tint)
{
    const u64 t0 = GetTimerSystemTime();
    bool drawn = draw_image(img, params, tint);
    frame_image_ticks += GetTimerSystemTime() - t0;
    return drawn;
}
#else
bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams *params,
                   const C2D_ImageTint *tint)
{
    return draw_image(img, params, tint);
}
#endif

static bool draw_image(C2D_Image img, const C2D_DrawParams *params,
                       const C2D_ImageTint *tint)
{
    if (__C2Di_Context.idxBufPos || __C2Di_Context.vtxBufPos) C2Di_FlushVtxBuf();
    if (!gpu || clip_empty || !img.tex || !img.subtex || !params) return false;
    Ps2Page *page = (Ps2Page *)((u8 *)img.tex - offsetof(Ps2Page, compat));
    float width = fabsf(params->pos.w), height = fabsf(params->pos.h);
    float cx = params->center.x, cy = params->center.y;
    float left = img.subtex->left * page->texture.Width;
    float right = img.subtex->right * page->texture.Width;
    float top = (1.0f-img.subtex->top) * page->texture.Height;
    float bottom = (1.0f-img.subtex->bottom) * page->texture.Height;
    if (params->pos.w < 0) { float t=left; left=right; right=t; }
    if (params->pos.h < 0) { float t=top; top=bottom; bottom=t; }
    const u64 tint_color = tint_to_gs(tint);
    /* Most level tiles/decoration are not rotated. Avoid two trigonometric
     * calls and four transformed corners for the GS native sprite case.
     * This is algebraically the angle=0 subset of the old quad path. */
    if (params->angle == 0.0f && view.b == 0.0f && view.c == 0.0f) {
        float x0, y0, x3, y3;
        map_xy(-cx + params->pos.x, -cy + params->pos.y, &x0, &y0);
        /* Preserve the original (width - center) + position grouping to
         * minimize float rounding drift at large scrolling coordinates. */
        map_xy((width - cx) + params->pos.x, (height - cy) + params->pos.y,
               &x3, &y3);
        if (x0 <= x3 && y0 <= y3) {
            DIAG_COUNT(DC_IMAGES);
            if (outside_scissor(x0, y0, x3, y3)) { DIAG_COUNT(DC_CULLED); return true; }
            sprite_batch_add(&page->texture, x0, y0, left, top,
                             x3, y3, right, bottom, tint_color);
            return true;
        }
    }
    float c, s;
    PS2_SinCos(params->angle, &s, &c);
    /* The view, merged-bottom and 4:3 mappings form one affine map; fold
     * the sprite's rotation and position into it, then transform the four
     * corners and convert them to GS 12.4 fixed point together. */
    float g[6];
    physical_affine(g);
    const float px = params->pos.x, py = params->pos.y;
    const ps2_v4 dx = { -cx, width - cx, -cx, width - cx };
    const ps2_v4 dy = { -cy, -cy, height - cy, height - cy };
    const ps2_v4 mx = { g[0]*c + g[1]*s, g[1]*c - g[0]*s, g[0]*px + g[1]*py + g[2], 0.f };
    const ps2_v4 my = { g[3]*c + g[4]*s, g[4]*c - g[3]*s, g[3]*px + g[4]*py + g[5], 0.f };
    const ps2_v4 cu = { left, right, left, right };
    const ps2_v4 cv = { top, top, bottom, bottom };
    ps2_v4 fx, fy;
    ps2_v4i ix, iy, iu, iv;
    corners_to_gs(&dx, &dy, &mx, &my, &cu, &cv, &fx, &fy, &ix, &iy, &iu, &iv);
    {
        float min_x = fx[0], max_x = fx[0], min_y = fy[0], max_y = fy[0];
        for (int i = 1; i < 4; i++) {
            if (fx[i] < min_x) min_x = fx[i];
            if (fx[i] > max_x) max_x = fx[i];
            if (fy[i] < min_y) min_y = fy[i];
            if (fy[i] > max_y) max_y = fy[i];
        }
        DIAG_COUNT(DC_IMAGES);
        DIAG_COUNT(DC_ROTATED);
        if (outside_scissor(min_x, min_y, max_x, max_y)) { DIAG_COUNT(DC_CULLED); return true; }
    }
    GSTEXTURE *tex = &page->texture;
    const int umax = tex->Width * 16, vmax = tex->Height * 16;
    for (int i = 0; i < 4; i++) {
        ix[i] = clamp_gs_xy(ix[i] + gpu->OffsetX);
        iy[i] = clamp_gs_xy(iy[i] + gpu->OffsetY);
        iu[i] = clamp_gs_uv(iu[i], umax);
        iv[i] = clamp_gs_uv(iv[i], vmax);
    }
    /* In the GS backend a rotated quad must be made of two *separate*
     * textured triangles. The old quad primitive was giving a missing
     * diagonal half on rotating cube icons (two overlaid icon layers
     * made this especially apparent). The source quad's corner order is
     * TL, TR, BL, BR; split along TL->BR with matching UVs. */
    static const u8 order[6] = { 0, 1, 3, 0, 3, 2 };
    const gs_rgbaq color = rgbaq_to_RGBAQ(tint_color);
    for (int t = 0; t < 2; t++) {
        GSPRIMUVPOINT *out = tri_batch_reserve(tex);
        for (int k = 0; k < 3; k++) {
            const int i = order[t * 3 + k];
            out[k].rgbaq = color;
            out[k].uv.coord.uv = (u64)(u32)iu[i] | ((u64)(u32)iv[i] << 16);
            out[k].uv.tag = GS_UV;
            out[k].xyz2.xyz.xyz = (u64)(u32)ix[i] | ((u64)(u32)iy[i] << 16) | (1ull << 32);
            out[k].xyz2.tag = GS_XYZ2;
        }
    }
    return true;
}

static u64 solid_color_to_gs(u32 color)
{
    return GS_SETREG_RGBAQ(color & 255, (color >> 8) & 255,
        (color >> 16) & 255, (((color >> 24) & 255) * 128 + 127) / 255, 0);
}

bool C2D_DrawTriangle(float x0, float y0, u32 c0,
                      float x1, float y1, u32 c1,
                      float x2, float y2, u32 c2, float depth)
{
    C2Di_FlushVtxBuf();
    (void)depth;
    if (!gpu || clip_empty) return false;
    float ax,ay,bx,by,cx,cy;
    map_xy(x0,y0,&ax,&ay);
    map_xy(x1,y1,&bx,&by);
    map_xy(x2,y2,&cx,&cy);
    gsKit_prim_triangle_gouraud_3d(gpu, ax,ay,1, bx,by,1, cx,cy,1,
                                   solid_color_to_gs(c0), solid_color_to_gs(c1),
                                   solid_color_to_gs(c2));
    return true;
}

bool C2D_DrawLine(float x0, float y0, u32 c0,
                  float x1, float y1, u32 c1, float thickness, float depth)
{
    float dx = x1 - x0, dy = y1 - y0;
    float length = sqrtf(dx*dx + dy*dy);
    if (!gpu || length <= 0.f || thickness <= 0.f) return false;
    /* A pair of triangles maintains thickness under GS's 2D renderer. */
    float nx = -dy * thickness / (2.f * length);
    float ny =  dx * thickness / (2.f * length);
    C2D_DrawTriangle(x0+nx, y0+ny, c0,
                     x0-nx, y0-ny, c0, x1+nx, y1+ny, c1, depth);
    C2D_DrawTriangle(x1+nx, y1+ny, c1,
                     x0-nx, y0-ny, c0, x1-nx, y1-ny, c1, depth);
    return true;
}

bool C2D_DrawRectangle(float x, float y, float z, float w, float h,
                       u32 c0, u32 c1, u32 c2, u32 c3)
{
    C2Di_FlushVtxBuf();
    float ax,ay,bx,by,cx,cy,dx,dy;
    (void)z;
    if (!gpu || clip_empty) return false;
    map_xy(x,y,&ax,&ay);
    map_xy(x+w,y+h,&dx,&dy);
    if (c0 == c1 && c0 == c2 && c0 == c3 && view.b == 0.f && view.c == 0.f) {
        gsKit_prim_sprite(gpu, ax,ay,dx,dy,1,solid_color_to_gs(c0));
        return true;
    }
    map_xy(x+w,y,&bx,&by);
    map_xy(x,y+h,&cx,&cy);
    /* Citro2D colors are TL, TR, BL, BR. GS quads use winding TL,TR,BR,BL. */
    gsKit_prim_quad_gouraud_3d(gpu,
        ax,ay,1, bx,by,1, dx,dy,1, cx,cy,1,
        solid_color_to_gs(c0), solid_color_to_gs(c1),
        solid_color_to_gs(c3), solid_color_to_gs(c2));
    return true;
}

bool C2D_DrawEllipse(float x, float y, float z, float w, float h,
                     u32 c0, u32 c1, u32 c2, u32 c3)
{
    (void)c1; (void)c2; (void)c3;
    if (!gpu || clip_empty || w == 0.f || h == 0.f) return false;
    const int segments = 24;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    for (int i = 0; i < segments; i++) {
        float start = (float)(2.0 * M_PI) * i / segments;
        float end = (float)(2.0 * M_PI) * (i+1) / segments;
        C2D_DrawTriangle(cx,cy,c0,
            cx+cosf(start)*w*0.5f, cy+sinf(start)*h*0.5f,c0,
            cx+cosf(end)*w*0.5f, cy+sinf(end)*h*0.5f,c0,z);
    }
    return true;
}

void C3D_TexSetFilter(C3D_Tex *tex, GPU_TEXTURE_FILTER_PARAM mag, GPU_TEXTURE_FILTER_PARAM min)
{
    (void)min;
    flush_sprite_batch();
    if (tex) {
        Ps2Page *page = (Ps2Page *)((u8 *)tex - offsetof(Ps2Page, compat));
        page->texture.Filter = (mag == GPU_LINEAR) ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
    }
}

/* Source's C2D_DrawTriangleUV requires these private Citro2D entrypoints.
 * Triangles are queued until a state change, preserving original draw order.
 * The PS2 GS supports per-vertex textured Gouraud colors directly. */
static u64 internal_texture_color(u32 color)
{
    /* With GS texture modulate, a component of 0x80 gives unity. */
    return GS_SETREG_RGBAQ(((color & 255) + 1) >> 1,
        ((((color >> 8) & 255) + 1) >> 1),
        ((((color >> 16) & 255) + 1) >> 1),
        ((((color >> 24) & 255) * 128 + 127) / 255), 0);
}

static void flush_vtx_triangles(void)
{
    C2Di_Context *ctx = &__C2Di_Context;
    if (gpu && !clip_empty) {
        bool textured = batched_tex &&
            (batched_mode == C2DiF_Mode_ImageSolid ||
             batched_mode == C2DiF_Mode_ImageMult ||
             batched_mode == C2DiF_Mode_ImageLuma ||
             batched_mode == C2DiF_Mode_ImageAdd ||
             batched_mode == C2DiF_Mode_ImageSub ||
             batched_mode == C2DiF_Mode_ImageOMAdd ||
             batched_mode == C2DiF_Mode_ImageOMSub);
        GSTEXTURE *texture = NULL;
        if (textured) {
            Ps2Page *page = (Ps2Page *)((u8 *)batched_tex - offsetof(Ps2Page, compat));
            texture = &page->texture;
        }
        for (size_t i=0; i + 2 < ctx->idxBufPos; i += 3) {
            u16 a = ctx->idxBuf[i], b = ctx->idxBuf[i+1], c = ctx->idxBuf[i+2];
            if (a >= ctx->vtxBufPos || b >= ctx->vtxBufPos || c >= ctx->vtxBufPos)
                continue;
            const C2Di_Vertex *v0 = &ctx->vtxBuf[a];
            const C2Di_Vertex *v1 = &ctx->vtxBuf[b];
            const C2Di_Vertex *v2 = &ctx->vtxBuf[c];
            DIAG_COUNT(DC_VTX_TRIS);
            if (texture) {
                tri_batch_add(texture,
                    v0->pos[0],v0->pos[1],v0->texcoord[0],v0->texcoord[1],
                    internal_texture_color(v0->color),
                    v1->pos[0],v1->pos[1],v1->texcoord[0],v1->texcoord[1],
                    internal_texture_color(v1->color),
                    v2->pos[0],v2->pos[1],v2->texcoord[0],v2->texcoord[1],
                    internal_texture_color(v2->color));
            } else {
                gsKit_prim_triangle_gouraud_3d(gpu,
                    v0->pos[0],v0->pos[1],1,
                    v1->pos[0],v1->pos[1],1,
                    v2->pos[0],v2->pos[1],1,
                    solid_color_to_gs(v0->color),
                    solid_color_to_gs(v1->color),
                    solid_color_to_gs(v2->color));
            }
        }
    }
    ctx->idxBufPos = ctx->idxBufLastPos = ctx->vtxBufPos = 0;
    batched_tex = NULL;
}

void C2Di_FlushVtxBuf(void)
{
    flush_sprite_batch();
    flush_vtx_triangles();
    /* Callers emit direct gsKit primitives after this; nothing may stay queued. */
    flush_tri_list();
}

void C2Di_Update(void)
{
    C2Di_Context *ctx = &__C2Di_Context;
    if (ctx->flags & (C2DiF_DirtyTex | C2DiF_DirtyMode)) C2Di_FlushVtxBuf();
    ctx->flags &= ~C2DiF_DirtyAny;
}

void C2Di_CalcQuad(C2Di_Quad *quad, const C2D_DrawParams *params)
{
    if (!quad || !params) return;
    float w = fabsf(params->pos.w), h = fabsf(params->pos.h);
    float x[] = {-params->center.x, w-params->center.x,
                 -params->center.x, w-params->center.x};
    float y[] = {-params->center.y, -params->center.y,
                 h-params->center.y, h-params->center.y};
    float *corners[] = {quad->topLeft, quad->topRight,
                        quad->botLeft, quad->botRight};
    if (params->angle == 0.0f) {
        for (int i=0; i<4; ++i) {
            corners[i][0] = params->pos.x + x[i];
            corners[i][1] = params->pos.y + y[i];
        }
        return;
    }
    float c = cosf(params->angle), s = sinf(params->angle);
    for (int i=0;i<4;i++) {
        corners[i][0] = params->pos.x + x[i]*c - y[i]*s;
        corners[i][1] = params->pos.y + x[i]*s + y[i]*c;
    }
}

void C2Di_AppendTri(void)
{
    C2Di_Context *ctx = &__C2Di_Context;
    if (!(ctx->flags & C2DiF_Active)) return;
    if (!C2Di_CheckBufSpace(ctx, 3, 3)) C2Di_FlushVtxBuf();
    if (!C2Di_CheckBufSpace(ctx, 3, 3)) return;
    if (ctx->idxBufPos == 0) {
        batched_tex = ctx->curTex;
        batched_mode = ctx->flags & C2DiF_Mode_Mask;
    }
    u16 base = (u16)ctx->vtxBufPos;
    ctx->idxBuf[ctx->idxBufPos++] = base;
    ctx->idxBuf[ctx->idxBufPos++] = base+1;
    ctx->idxBuf[ctx->idxBufPos++] = base+2;
}

void C2Di_AppendQuad(void)
{
    C2Di_Context *ctx = &__C2Di_Context;
    if (!(ctx->flags & C2DiF_Active)) return;
    if (!C2Di_CheckBufSpace(ctx, 6, 4)) C2Di_FlushVtxBuf();
    if (!C2Di_CheckBufSpace(ctx, 6, 4)) return;
    if (ctx->idxBufPos == 0) {
        batched_tex = ctx->curTex;
        batched_mode = ctx->flags & C2DiF_Mode_Mask;
    }
    u16 base = (u16)ctx->vtxBufPos;
    const u16 indices[] = {base,base+1,base+2,base+2,base+1,base+3};
    memcpy(ctx->idxBuf+ctx->idxBufPos, indices, sizeof(indices));
    ctx->idxBufPos += 6;
}

void C2Di_AppendVtx(float x, float y, float z, float u, float v,
                    float ptx, float pty, u32 color)
{
    (void)z; (void)ptx;
    C2Di_Context *ctx = &__C2Di_Context;
    if (ctx->vtxBufPos >= ctx->vtxBufSize) return;
    C2Di_Vertex *out = &ctx->vtxBuf[ctx->vtxBufPos++];
    map_xy(x, y, &out->pos[0], &out->pos[1]);
    out->pos[2] = 0;
    out->texcoord[0] = ctx->curTex ? u*ctx->curTex->width : 0;
    out->texcoord[1] = ctx->curTex ? (1.f-v)*ctx->curTex->height : 0;
    out->ptcoord[0] = ptx;
    out->ptcoord[1] = pty;
    if (ctx->curTex && pty < 1.f) {
        float k = pty < 0.f ? 0.f : pty;
        u32 rr = (u32)(255.f - (255.f - (color&255))*k);
        u32 gg = (u32)(255.f - (255.f - ((color>>8)&255))*k);
        u32 bb = (u32)(255.f - (255.f - ((color>>16)&255))*k);
        color = (color&0xff000000) | rr | (gg<<8) | (bb<<16);
    }
    out->color = color;
}
