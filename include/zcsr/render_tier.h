#ifndef ZCSR_RENDER_TIER_H
#define ZCSR_RENDER_TIER_H
/* render_tier.h — compile-time assembly tiers for the instanced renderer.
 *
 * Three independent tier axes (FIX_PLAN 裁决 5/6/7/8). Each axis resolves
 * through its own compile-time `#ifndef` default; no axis implies another, and
 * there are NO profile bundles, NO per-frame bool parameters on the hot path.
 *
 *   Layout axis : FULL (168 B, per-instance recolor/ramp/glow/...) /
 *                 POS_UV (32 B, center + size + UV only) /
 *                 COMPACT (36 B, pos/uv + packed rot/alpha).
 *   Sort axis   : NONE (stage not compiled) / KEY (4 B key, stable radix) /
 *                 FULL (painter order, caller comparator).
 *   Color axis : LINEAR_HDR (RGBA16F linear frame + one LUT encode pass) /
 *                 DIRECT_SRGB (write default FB directly, no encode pass).
 *
 * This header is GL-free: VAO attribute slots are described by location /
 * element count / byte offset; the GL scalar type and divisor are bound at
 * VAO setup in the renderer module (phase 2 wiring).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Layout tier (裁决 6)
 * ======================================================================== */
/* Preprocessor literals for the #if tier gates below (enum constants are not
 * visible to the preprocessor and must not be used in #if expressions). */
#define ZCSR_LAYOUT_FULL    0
#define ZCSR_LAYOUT_POS_UV  1
#define ZCSR_LAYOUT_COMPACT 2

typedef enum {
    ZCSR_INSTANCE_LAYOUT_FULL = 0,   /* 168 B general record      */
    ZCSR_INSTANCE_LAYOUT_POS_UV = 1,/* 32 B center/size/UV only  */
    ZCSR_INSTANCE_LAYOUT_COMPACT = 2/* 36 B pos/uv + packed rot/alpha */
} zcsr_instance_layout;

#define ZCSR_INSTANCE_STRIDE_FULL    168u
#define ZCSR_INSTANCE_STRIDE_POS_UV   32u
#define ZCSR_INSTANCE_STRIDE_COMPACT  36u

/* Compact 32 B entry: center(2f) + size(2f) + uv(4f). POD, GL-free. */
typedef struct {
    float cx, cy;          /* center position (px)   offset  0 */
    float w, h;            /* size (px)              offset  8 */
    float u0, v0, u1, v1;  /* atlas UV sub-rect      offset 16 */
} zcsr_instance_pos_uv;

/* COMPACT 36 B entry: the 32 B POS_UV prefix unchanged, one packed uint32 at
 * offset 32. Pack layout (little-endian uint32):
 *   bits  0..15  rotation  normalized short, one full circle = 65536
 *   bits 16..23  alpha     8-bit, 0..255
 *   bits 24..31  reserved  zero on write, ignored on read
 * POD, GL-free. The encoding/decoding math lives in the renderer module; this
 * header only fixes the byte offsets and bit fields. */
#define ZCSR_COMPACT_PACK_ROT_MASK    0xFFFFu
#define ZCSR_COMPACT_PACK_ALPHA_SHIFT 16u
#define ZCSR_COMPACT_PACK_RES_SHIFT   24u
#define ZCSR_COMPACT_PACK_ALPHA_MASK  0xFFu
#define ZCSR_COMPACT_ROT_STEPS         65536.0f
#define ZCSR_COMPACT_ALPHA_STEPS       255.0f

typedef struct {
    float cx, cy;          /* center position (px)   offset  0 */
    float w, h;            /* size (px)              offset  8 */
    float u0, v0, u1, v1;  /* atlas UV sub-rect      offset 16 */
    uint32_t pack;         /* rot16 | alpha8<<16 | res8<<24  offset 32 */
} zcsr_instance_compact;

/* ===========================================================================
 * Sort pipeline-stage tier (裁决 7)
 * ======================================================================== */
#define ZCSR_SORT_TIER_NONE 0
#define ZCSR_SORT_TIER_KEY  1
#define ZCSR_SORT_TIER_FULL 2

typedef enum {
    ZCSR_SORT_NONE = 0,  /* sort stage compiled out; identity order */
    ZCSR_SORT_KEY,       /* stable 32-bit key radix/counting sort   */
    ZCSR_SORT_FULL       /* painter order, caller comparator        */
} zcsr_sort_tier;

/* ===========================================================================
 * Color pipeline tier (裁决 8)
 * ======================================================================== */
#define ZCSR_COLOR_OPT_LINEAR_HDR   0
#define ZCSR_COLOR_OPT_DIRECT_SRGB  1

typedef enum {
    ZCSR_COLOR_TIER_LINEAR_HDR = 0,  /* linear RGBA16F frame + LUT encode */
    ZCSR_COLOR_TIER_DIRECT_SRGB = 1  /* direct default-FB write, no encode */
} zcsr_color_tier;

/* ===========================================================================
 * Three independent compile-time axes. Each axis has its own `#ifndef`
 * default; no axis implies another, and there is no profile bundle that
 * selects several axes at once. An explicit -D<axis> definition on the
 * command line wins over the default below. Everything resolves through
 * compile-time `#if` — there is no per-frame runtime table.
 *
 *   ZCSR_INSTANCE_LAYOUT : 0 = FULL (168 B record), 2 = COMPACT (36 B record).
 *                          Default FULL. (POS_UV = 1 remains an assembly-time
 *                          layout value; it is not exposed through the CMake
 *                          cache string options.)
 *   ZCSR_COLOR_TIER     : 0 = LINEAR_HDR (linear frame + one LUT encode),
 *                          1 = DIRECT_SRGB (write default FB directly).
 *                          Default LINEAR_HDR.
 *   ZCSR_SORT_TIER      : 1 = KEY (stable 32-bit key radix sort),
 *                          0 = NONE (sort stage compiled out). Default KEY.
 * ======================================================================== */
#ifndef ZCSR_INSTANCE_LAYOUT
#define ZCSR_INSTANCE_LAYOUT ZCSR_LAYOUT_FULL
#endif

#ifndef ZCSR_COLOR_TIER
#define ZCSR_COLOR_TIER ZCSR_COLOR_OPT_LINEAR_HDR
#endif

#ifndef ZCSR_SORT_TIER
#define ZCSR_SORT_TIER ZCSR_SORT_TIER_KEY
#endif

/* Persistent mapped ring stays off by default; it remains an
 * independent opt-in tier for explicit assembly overrides. */
#ifndef ZCSR_INST_PERSISTENT_RING
#define ZCSR_INST_PERSISTENT_RING 0
#endif

/* ===========================================================================
 * VAO attribute slot descriptor (GL-free)
 * ======================================================================== */
/* GL scalar tokens kept GL-free: mapped to GL_FLOAT / GL_UNSIGNED_INT at the
 * VAO setup in the renderer module. */
#define ZCSR_ATTR_FLOAT 0
#define ZCSR_ATTR_UINT  1

typedef struct {
    int    location; /* vertex attribute location (shader `layout(location=)`) */
    int    count;    /* 1..4 elements per vertex instance */
    size_t offset;   /* byte offset inside the tier stride */
    int    type;     /* ZCSR_ATTR_FLOAT / ZCSR_ATTR_UINT */
} zcsr_tier_attr;

/* POS_UV (32 B) attribute table: loc 1 center, loc 2 size, loc 3 uv. */
#define ZCSR_POS_UV_ATTR_COUNT 3
static const zcsr_tier_attr zcsr_pos_uv_attrs[ZCSR_POS_UV_ATTR_COUNT] = {
    { 1, 2,  0, ZCSR_ATTR_FLOAT }, /* i_center cx,cy */
    { 2, 2,  8, ZCSR_ATTR_FLOAT }, /* i_size   w,h    */
    { 3, 4, 16, ZCSR_ATTR_FLOAT }  /* i_uv     u0,v0,u1,v1 */
};

/* FULL (168 B) attribute table. Mirrors the instanced VAO setup in
 * modules/glrender/glrender_instanced.c (phase 2 binds these slots). */
#define ZCSR_FULL_ATTR_COUNT 13
static const zcsr_tier_attr zcsr_full_attrs[ZCSR_FULL_ATTR_COUNT] = {
    {  1, 2,   0, ZCSR_ATTR_FLOAT }, /* i_center  cx,cy            */
    {  2, 2,   8, ZCSR_ATTR_FLOAT }, /* i_size    w,h              */
    {  3, 4,  16, ZCSR_ATTR_FLOAT }, /* i_uv      u0,v0,u1,v1      */
    {  4, 4,  32, ZCSR_ATTR_FLOAT }, /* i_color   r,g,b,a          */
    {  5, 1,  48, ZCSR_ATTR_FLOAT }, /* i_rotation                 */
    {  6, 1,  52, ZCSR_ATTR_FLOAT }, /* i_amount                   */
    {  7, 1,  56, ZCSR_ATTR_FLOAT }, /* i_mode                     */
    {  8, 4,  60, ZCSR_ATTR_FLOAT }, /* i_recolor hue family       */
    {  9, 4,  76, ZCSR_ATTR_FLOAT }, /* i_ramp row/amount/mode/luma*/
    { 10, 4,  92, ZCSR_ATTR_FLOAT }, /* i_glow                     */
    { 11, 4, 108, ZCSR_ATTR_FLOAT }, /* i_chroma sat/value         */
    { 12, 4, 124, ZCSR_ATTR_FLOAT }, /* i_emiss                    */
    { 13, 4, 140, ZCSR_ATTR_FLOAT }  /* i_rim                      */
};

/* COMPACT (36 B) attribute table: loc 1 center, loc 2 size, loc 3 uv,
 * loc 4 packed rotation/alpha/reserved uint32. */
#define ZCSR_COMPACT_ATTR_COUNT 4
static const zcsr_tier_attr zcsr_compact_attrs[ZCSR_COMPACT_ATTR_COUNT] = {
    { 1, 2,  0, ZCSR_ATTR_FLOAT }, /* i_center cx,cy           */
    { 2, 2,  8, ZCSR_ATTR_FLOAT }, /* i_size   w,h            */
    { 3, 4, 16, ZCSR_ATTR_FLOAT }, /* i_uv     u0,v0,u1,v1    */
    { 4, 1, 32, ZCSR_ATTR_UINT  }  /* i_pack  rot16|alpha8|res8 */
};

/* Stride for a layout tier. */
static inline size_t zcsr_tier_stride(zcsr_instance_layout layout) {
    if (layout == ZCSR_INSTANCE_LAYOUT_POS_UV)  return ZCSR_INSTANCE_STRIDE_POS_UV;
    if (layout == ZCSR_INSTANCE_LAYOUT_COMPACT) return ZCSR_INSTANCE_STRIDE_COMPACT;
    return ZCSR_INSTANCE_STRIDE_FULL;
}

/* ===========================================================================
 * Compile-time constraints
 * ======================================================================== */
#define ZCSR_TIER_ASSERT(cond, tag) \
    typedef char zcsr_tier_assert_##tag[(cond) ? 1 : -1]

/* POS_UV compact entry: 32 B, prefix offsets fixed. */
ZCSR_TIER_ASSERT(sizeof(zcsr_instance_pos_uv) == ZCSR_INSTANCE_STRIDE_POS_UV, pos_uv_stride);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_pos_uv, cx) == 0,  pos_uv_cx);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_pos_uv, w)  == 8,  pos_uv_w);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_pos_uv, u0) == 16, pos_uv_u0);

/* COMPACT 36 B entry: 32 B prefix unchanged, packed uint32 at offset 32. */
ZCSR_TIER_ASSERT(sizeof(zcsr_instance_compact) == ZCSR_INSTANCE_STRIDE_COMPACT, compact_stride);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_compact, cx)   == 0,  compact_cx);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_compact, w)    == 8,  compact_w);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_compact, u0)    == 16, compact_u0);
ZCSR_TIER_ASSERT(offsetof(zcsr_instance_compact, pack) == 32, compact_pack);
/* Hard 36 B stride contract: the active tier's stride constant must match the
 * compiled layout (fires in every TU that includes this header). */
#if ZCSR_INSTANCE_LAYOUT == ZCSR_LAYOUT_COMPACT
ZCSR_TIER_ASSERT(ZCSR_INSTANCE_STRIDE_COMPACT == 36u, compact_36_bytes);
#endif

/* FULL record checks are opt-in: a TU that includes zcsr/gl_render.h first
 * may define ZCSR_INSTANCE_LAYOUT_ASSERT before including this header to
 * activate them. They stay out of the GL-free sort/color TUs, and out of
 * gl_render.h's own early include of color_pipeline.h (where the record type
 * is not declared yet). */
#ifdef ZCSR_INSTANCE_LAYOUT_ASSERT
ZCSR_TIER_ASSERT(sizeof(zcsr_gl_instance) == ZCSR_INSTANCE_STRIDE_FULL, full_stride);
ZCSR_TIER_ASSERT(offsetof(zcsr_gl_instance, cx)  == 0,  full_cx);
ZCSR_TIER_ASSERT(offsetof(zcsr_gl_instance, w)  == 8,  full_w);
ZCSR_TIER_ASSERT(offsetof(zcsr_gl_instance, u0)  == 16, full_u0);
ZCSR_TIER_ASSERT(offsetof(zcsr_gl_instance, v1) + sizeof(float) ==
                 ZCSR_INSTANCE_STRIDE_POS_UV, full_prefix32);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_RENDER_TIER_H */
