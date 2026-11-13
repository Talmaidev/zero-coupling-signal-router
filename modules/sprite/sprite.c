/* modules/sprite/sprite.c — camera-aware GL sprite component.
 * Draws zcsr_sprite_instance entities through the existing glrender public
 * API only (no glrender internals touched). Zero heap, no module state. */
#include "zcsr/sprite.h"
#include "zcsr/gl_render.h"
#include "zcsr/gl_render_l2d_geo.h"
#include <math.h>

void zcsr_sprite_draw(zcsr_gl_renderer* r,
                      const zcsr_camera* cam,
                      const zcsr_transform* tr,
                      const zcsr_sprite_instance* sp)
{
    const zcsr_sprite_asset* asset;
    float px;
    float py;
    float pw;
    float ph;
    zcsr_gl_sprite s;

    if (r == NULL || cam == NULL || tr == NULL || sp == NULL) return;
    asset = sp->asset;
    if (asset == NULL || asset->tex == 0u) return;

    zcsr_camera_world_to_px(cam, tr->x, tr->y, 0.0f, &px, &py);
    pw = asset->w * tr->scale_x * cam->px_per_unit;
    ph = asset->h * tr->scale_y * cam->px_per_unit;

    if (asset->u0 == 0.0f && asset->v0 == 0.0f && asset->u1 == 1.0f && asset->v1 == 1.0f) {
        /* whole texture: the sprite path carries rotation about the center */
        s.tex = asset->tex;
        s.dst.x = px - pw * 0.5f;
        s.dst.y = py - ph * 0.5f;
        s.dst.w = pw;
        s.dst.h = ph;
        s.rotation = tr->rotation;
        s.r = sp->r;
        s.g = sp->g;
        s.b = sp->b;
        s.a = sp->a;
        s.mode = (zcsr_color_mode)0; /* MULTIPLY — the legacy tint behavior */
        s.amount = 0.0f;
        zcsr_gl_submit(r, &s);
    } else {
        /* atlas sub-rect: the atlas queue has no rotation channel */
        zcsr_gl_queue_atlas_sprite(r, asset->tex,
                                   px - pw * 0.5f, py - ph * 0.5f, pw, ph,
                                   asset->u0, asset->v0, asset->u1, asset->v1,
                                   sp->r, sp->g, sp->b, sp->a);
    }
}

/* Lit sprite: convert a transform + sprite_instance into a world-space textured
 * quad and submit it to the Light2D G-buffer. Caller MUST already be inside an
 * open geometry phase (after zcsr_gl_l2d_geo_begin, before geo_end).
 *
 * Contract (RENDER2D_CONTRACT §14.4):
 *   pivot = center (tr->x/tr->y is the sprite center, no pivot enum)
 *   size  = asset.w/h * scale_x/y (world units, NOT texels)
 *   corners: local rect (-hw,-hh),(hw,-hh),(hw,hh),(-hw,hh) rotated about center,
 *            then translated to (tr->x, tr->y, ground_z)
 *   uv    : per-corner (u0,v0),(u1,v0),(u1,v1),(u0,v1), passthrough, no flip
 *   normal: (0,0,1) for horizontal ground sprites (v1 limit)
 *   tint  : instance r,g,b,a (display-domain multiply)
 *   emissive: (0,0,0,0) v1 (per-sprite emissive is a future extension slot)
 */
void zcsr_sprite_draw_lit(zcsr_gl_renderer* r,
                          const zcsr_transform* tr,
                          const zcsr_sprite_instance* sp,
                          float ground_z)
{
    const zcsr_sprite_asset* asset;
    float hw, hh;
    float c, s;
    float lx0, ly0, lx1, ly1, lx2, ly2, lx3, ly3;
    float corners[12];
    float uv[8];
    float tint[4];
    const float normal[3] = { 0.0f, 0.0f, 1.0f };
    const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (r == NULL || tr == NULL || sp == NULL) return;
    asset = sp->asset;
    if (asset == NULL || asset->tex == 0u) return;

    hw = asset->w * tr->scale_x * 0.5f;
    hh = asset->h * tr->scale_y * 0.5f;

    c = cosf(tr->rotation);
    s = sinf(tr->rotation);

    /* local corners (centered at origin), then rotate, then translate */
    lx0 = -hw; ly0 = -hh;
    lx1 =  hw; ly1 = -hh;
    lx2 =  hw; ly2 =  hh;
    lx3 = -hw; ly3 =  hh;

    corners[0]  = lx0 * c - ly0 * s + tr->x;
    corners[1]  = lx0 * s + ly0 * c + tr->y;
    corners[2]  = ground_z;

    corners[3]  = lx1 * c - ly1 * s + tr->x;
    corners[4]  = lx1 * s + ly1 * c + tr->y;
    corners[5]  = ground_z;

    corners[6]  = lx2 * c - ly2 * s + tr->x;
    corners[7]  = lx2 * s + ly2 * c + tr->y;
    corners[8]  = ground_z;

    corners[9]  = lx3 * c - ly3 * s + tr->x;
    corners[10] = lx3 * s + ly3 * c + tr->y;
    corners[11] = ground_z;

    uv[0] = asset->u0; uv[1] = asset->v0;
    uv[2] = asset->u1; uv[3] = asset->v0;
    uv[4] = asset->u1; uv[5] = asset->v1;
    uv[6] = asset->u0; uv[7] = asset->v1;

    tint[0] = sp->r; tint[1] = sp->g; tint[2] = sp->b; tint[3] = sp->a;

    zcsr_gl_l2d_geo_tex_face(r, asset->tex, corners, uv, tint, normal, emissive);
}
