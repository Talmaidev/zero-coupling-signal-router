/* modules/sprite/sprite.c — camera-aware GL sprite component.
 * Draws zcsr_sprite_instance entities through the existing glrender public
 * API only (no glrender internals touched). Zero heap, no module state. */
#include "zcsr/sprite.h"
#include "zcsr/gl_render.h"

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
