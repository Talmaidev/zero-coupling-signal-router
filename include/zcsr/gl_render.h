#ifndef ZCSR_GL_RENDER_H
#define ZCSR_GL_RENDER_H
/* Engine-ext Agent 1 — GPU rendering (OpenGL 3.3 Core). OPTIONAL renderer, beside the existing
 * software/overlay path (which stays unchanged). Implementer: modules/glrender (vendors glad;
 * glad used ONLY inside that module). Includes only shared contracts. No runtime heap: renderer
 * state lives in a caller buffer; GPU memory is the driver's. */
#include "platform.h"       /* zcsr_surface */
#include "sprite.h"         /* zcsr_rectf */
#include "color.h"          /* zcsr_color_mode */
#include "texture_atlas.h"  /* zcsr_atlas_region */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct zcsr_gl_renderer zcsr_gl_renderer; /* opaque; instance-owned */
typedef uint32_t zcsr_gl_texture;                 /* GPU texture handle; 0 = invalid */

/* Create a GL 3.3 Core context on the surface (Win32 wgl / X11 GLX / Cocoa NSOpenGL) and the
 * renderer state inside the caller buffer. NULL on failure / buffer too small.
 *
 * The renderer is large: a fixed vertex batch sized for the 10k-sprite cap dominates it (~2.3 MiB),
 * so give the buffer a few MiB (the bundled tests/benches use 8 MiB). The layout is opaque and is
 * never heap-allocated; an oversized static/arena buffer is the intended usage. */
zcsr_gl_renderer* zcsr_gl_create(zcsr_surface* surface, void* buffer, size_t bytes);
void              zcsr_gl_destroy(zcsr_gl_renderer*);
void              zcsr_gl_set_vsync(zcsr_gl_renderer*, bool on);
/* Swap front/back buffers (present). Call after draw_instances when targeting the window. */
void              zcsr_gl_present(zcsr_gl_renderer*);

/* Upload 8-bit RGBA pixels to a GPU texture. `linear` = linear filter (else nearest). */
zcsr_gl_texture   zcsr_gl_texture_create(zcsr_gl_renderer*, const uint8_t* rgba, int w, int h);
bool              zcsr_gl_texture_upload(zcsr_gl_renderer*, zcsr_gl_texture, const uint8_t* rgba, int w, int h);
void              zcsr_gl_texture_filter(zcsr_gl_renderer*, zcsr_gl_texture, bool linear);
void              zcsr_gl_texture_destroy(zcsr_gl_renderer*, zcsr_gl_texture);

/* One drawable sprite for the batch: GPU texture + dest rect (top-left origin) + rotation about
 * the rect center (radians) + RGBA color modulation (0..1).
 *
 * `r,g,b,a` is the single RGBA modulation color (full RGBA only, no per-channel control). `mode`
 * selects how it is applied (MULTIPLY/MIX/ADD); `amount` (0..1) is the blend strength for MIX/ADD.
 * A zero-initialized sprite has mode = ZCSR_MOD_MULTIPLY and amount = 0, reproducing the legacy
 * `texel * (r,g,b,a)` behavior exactly — existing callers are unaffected. MIX/ADD drive runtime
 * effects like combat flash-red and freeze-blue (a multiply alone can only darken). */
typedef struct {
    zcsr_gl_texture tex;
    zcsr_rectf      dst;
    float           rotation;
    float           r, g, b, a;
    zcsr_color_mode mode;   /* default 0 = MULTIPLY (legacy) */
    float           amount; /* blend strength 0..1 for MIX/ADD; ignored by MULTIPLY */
} zcsr_gl_sprite;

/* Batched draw. Target throughput: 200..10000 sprites within frame budget.
 * begin() clears the batch; submit() appends one; flush() draws all in one pass.
 * submit_batch() is the signal-friendly entry (render_submit_batch maps here). */
void zcsr_gl_begin(zcsr_gl_renderer*);
void zcsr_gl_submit(zcsr_gl_renderer*, const zcsr_gl_sprite*);
void zcsr_gl_flush(zcsr_gl_renderer*);
void zcsr_gl_submit_batch(zcsr_gl_renderer*, const zcsr_gl_sprite* sprites, size_t count);

/* Offscreen render-to-texture: set the draw target (0 = back buffer / window). */
void zcsr_gl_set_target(zcsr_gl_renderer*, zcsr_gl_texture target_or_zero);

/* Built-in bitmap-font text (scores / UI). Top-left origin, color 0..1. No external font loading. */
void zcsr_gl_draw_text(zcsr_gl_renderer*, const char* utf8, float x, float y, float scale,
                       float r, float g, float b, float a);

/* ---- Atlas sprites ---------------------------------------------------------------------------
 * A sprite that samples a sub-rectangle of a shared atlas texture. Many atlas sprites with the
 * same `atlas` texture differ only in `region` UV, so they merge into one draw call even though
 * they are logically distinct images. This is the post-atlas counterpart of zcsr_gl_sprite. */
typedef struct {
    zcsr_gl_texture   atlas;  /* the packed atlas texture (shared by many sprites) */
    zcsr_atlas_region region; /* UV sub-rectangle of this sprite's image */
    zcsr_rectf        dst;
    float             rotation;
    float r, g, b, a;
    zcsr_color_mode   mode;
    float             amount;
} zcsr_gl_atlas_sprite;

/* Batched draw for atlas sprites. Same begin/flush semantics as submit_batch; all sprites that
 * share one atlas texture are coalesced into a single glDrawArrays. */
void zcsr_gl_submit_atlas_batch(zcsr_gl_renderer*, const zcsr_gl_atlas_sprite* sprites, size_t count);

/* ---- Instanced rendering ----------------------------------------------------------------------
 * One draw call (glDrawElementsInstanced) expands a single unit quad into N instances. Only the
 * per-instance data is uploaded (60 bytes each), instead of 6 full vertices per sprite (216 bytes),
 * so both CPU vertex setup and GPU transfer shrink. The rotation/quad expansion moves into the
 * vertex shader. Best for large counts of same-texture sprites. */
typedef struct {
    float cx, cy;          /* center position (pixels) */
    float w, h;            /* size (pixels) */
    float u0, v0, u1, v1;  /* atlas UV sub-rectangle (use 0,0,1,1 for a full texture) */
    float r, g, b, a;      /* modulation color 0..1 */
    float rotation;        /* radians, about the center */
    float amount;          /* blend strength for MIX/ADD */
    float mode;            /* zcsr_color_mode as float */
    uint32_t tex;          /* CPU-only: texture handle, used for sorting into runs */
} zcsr_gl_instance;        /* 15 float attrs + 1 uint32 = 64 bytes; tex is not a shader attribute */

/* Draw `count` instances sharing one bound texture, in one instanced draw call. Instances are
 * drawn as-is (no sorting). For a batch spanning multiple textures, sort it first (see
 * zcsr_sort_instances) then call this once per texture run, or use submit_instance_batch. */
void zcsr_gl_draw_instances(zcsr_gl_renderer*, zcsr_gl_texture texture,
                            const zcsr_gl_instance* instances, size_t count);

/* Sort a writable instance array into texture runs (and blend mode within a texture) so that
 * drawing needs one state setup per run. Pure CPU, no heap: `temp` is caller scratch of at least
 * `count * sizeof(zcsr_gl_instance)` bytes. Returns the number of distinct texture runs. After
 * sorting, iterate runs with zcsr_instance_run to get each run's texture/start/count. */
size_t zcsr_sort_instances(zcsr_gl_instance* instances, size_t count, void* temp);

/* Describe one texture run produced by zcsr_sort_instances. `run_index` in 0..return-1.
 * Writes the texture and the [start,start+len) slice of the sorted array. */
void zcsr_instance_run(const zcsr_gl_instance* sorted, size_t count, size_t run_index,
                       zcsr_gl_texture* out_texture, size_t* out_start, size_t* out_len);

/* Convenience: sort and draw all instances (one instanced call per texture run). `temp` is
 * caller scratch (>= count * sizeof instance). */
void zcsr_gl_submit_instance_batch(zcsr_gl_renderer*, zcsr_gl_instance* instances, size_t count,
                                   void* temp, size_t temp_bytes);

#endif /* ZCSR_GL_RENDER_H */
