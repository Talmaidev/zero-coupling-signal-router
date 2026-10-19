/* demo_physics.c — Optional Box2D v3.1.1 physics demo.
 *
 * Build: cmake -S . -B build-box2d -DZCSR_ENABLE_BOX2D=ON && cmake --build build-box2d
 * Run:   ./build-box2d/demo_physics
 *
 * Shows: 800x600 window, static ground, dynamic boxes spawning every 0.5s,
 *        collision contact events printed to terminal. Close via any key or ~15s timeout.
 *
 * Zero zcsr API changes: this is an integration-level consumer that links zcsr modules
 * (platform/glrender/input) with the external box2d::box2d target. The zcsr modules
 * themselves know nothing about physics.
 */
#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/audio_mix.h"
#include "zcsr/signal.h"

#include <box2d/box2d.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- constants ---- */
#define WIN_W          800
#define WIN_H          600
#define PIXELS_PER_M   50.0f
#define MAX_BOXES      64
#define BOX_HALF       0.4f   /* 0.8m boxes */
#define SPAWN_INTERVAL 0.5f   /* seconds */
#define MAX_FRAMES     900    /* ~15s @60fps safety timeout */

/* ---- compile-time signal router: PHYSICS_CONTACT -> audiomix slot_play ---- */
#define PHYSICS_CONNS(X) \
    X("PHYSICS_CONTACT", zcsr_mix_slot_play)
ZCSR_DEFINE_ROUTER(physics, PHYSICS_CONNS)

/* ---- caller-provided statics (no heap) ---- */
static unsigned char g_renderer_buf[8 * 1024 * 1024];
static unsigned char g_input_buf[4 * 1024];
static b2BodyId      g_boxes[MAX_BOXES];
static int           g_box_count;

/* ---- physics world ---- */
static b2WorldId g_world;

static void spawn_box(void) {
    if (g_box_count >= MAX_BOXES) return;
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = (b2Vec2){ 8.0f + ((float)(g_box_count % 5) - 2.0f) * 0.3f, 2.0f };
    bd.fixedRotation = false;
    bd.name = "box";
    b2BodyId body = b2CreateBody(g_world, &bd);
    if (!b2Body_IsValid(body)) return;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 1.0f;
    sd.material.friction = 0.4f;
    sd.material.restitution = 0.15f;
    b2Polygon box = b2MakeBox(BOX_HALF, BOX_HALF);
    b2ShapeId shape = b2CreatePolygonShape(body, &sd, &box);
    if (!b2Shape_IsValid(shape)) return;

    g_boxes[g_box_count++] = body;
}

static void create_ground(void) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_staticBody;
    bd.position = (b2Vec2){ 8.0f, 11.5f };
    bd.name = "ground";
    b2BodyId ground = b2CreateBody(g_world, &bd);
    if (!b2Body_IsValid(ground)) return;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 0.0f;
    sd.material.friction = 0.6f;
    b2Polygon floor = b2MakeBox(8.0f, 0.5f);
    b2CreatePolygonShape(ground, &sd, &floor);
}

/* ---- meters -> screen pixels (Box2D y-down aligned with screen y-down, no flip) ---- */
static zcsr_rectf world_to_screen(b2Vec2 center, float half_w, float half_h) {
    float sx = (center.x - half_w) * PIXELS_PER_M;
    float sy = (center.y - half_h) * PIXELS_PER_M;
    return (zcsr_rectf){ sx, sy, half_w * 2.0f * PIXELS_PER_M, half_h * 2.0f * PIXELS_PER_M };
}

int main(void) {
    /* ---- window + renderer ---- */
    zcsr_surface* surface = zcsr_surface_create(
        "zcsr + Box2D v3.1.1 physics demo", (zcsr_rect){ 0, 0, WIN_W, WIN_H });
    if (!surface) {
        printf("SKIP: no window surface (headless)\n");
        return 0;
    }
    zcsr_gl_renderer* renderer = zcsr_gl_create(surface, g_renderer_buf, sizeof g_renderer_buf);
    if (!renderer) {
        printf("SKIP: GL renderer unavailable\n");
        zcsr_surface_destroy(surface);
        return 0;
    }
    zcsr_gl_set_vsync(renderer, true);

    zcsr_input* input = zcsr_input_init(g_input_buf, sizeof g_input_buf);

    /* ---- audiomix (for collision sound via signal router) ---- */
    zcsr_mixer* mixer = zcsr_mix_start();
    if (mixer) {
        /* click.wav lives in integration/; run from build-box2d/ so ../integration/ works */
        if (!zcsr_mix_register_wav(mixer, "click", "../integration/click.wav"))
            printf("[audiomix] WARN: failed to register click.wav (run from build-box2d/)\n");
        else
            printf("[audiomix] mixer started, click.wav registered\n");
    } else {
        printf("[audiomix] WARN: mixer unavailable (collision sounds disabled)\n");
    }

    /* ---- Box2D world ---- */
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = (b2Vec2){ 0.0f, 9.8f };
    wd.enableSleep = true;
    g_world = b2CreateWorld(&wd);
    if (!b2World_IsValid(g_world)) {
        printf("FAIL: b2World_Create returned invalid id\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(surface);
        return 1;
    }
    create_ground();
    printf("[Box2D] world created; ground at y=11.5m; boxes spawn every %.1fs\n", SPAWN_INTERVAL);

    /* ---- main loop ---- */
    float spawn_timer = 0.0f;
    const float dt = 1.0f / 60.0f;
    bool running = true;

    for (int frame = 0; running && frame < MAX_FRAMES; ++frame) {
        zcsr_surface_pump_events(surface);
        if (input) {
            zcsr_input_pump(input, surface);
            zcsr_key_event ev;
            while (zcsr_input_poll(input, &ev)) {
                if (ev.action == ZCSR_KEY_DOWN) {
                    printf("[input] key %u pressed — exiting\n", (unsigned)ev.key);
                    running = false;
                }
            }
        }

        /* spawn */
        spawn_timer += dt;
        if (spawn_timer >= SPAWN_INTERVAL) {
            spawn_timer = 0.0f;
            spawn_box();
        }

        /* step */
        b2World_Step(g_world, dt, 4);

        /* contact events: emit PHYSICS_CONTACT signal -> audiomix plays click */
        b2ContactEvents events = b2World_GetContactEvents(g_world);
        for (int i = 0; i < events.beginCount; ++i) {
            printf("[Box2D] contact begin: shapeA=%d shapeB=%d -> emit PHYSICS_CONTACT\n",
                   events.beginEvents[i].shapeIdA.index1,
                   events.beginEvents[i].shapeIdB.index1);
            physics_emit("PHYSICS_CONTACT", "click");
        }

        /* render */
        zcsr_gl_clear(renderer, 0.1f, 0.1f, 0.12f, 1.0f);
        zcsr_gl_begin(renderer);

        /* ground (y=11.5m, half_h=0.5m → sy=550px, h=50px) */
        zcsr_gl_fill_rect(renderer,
            0.0f, 550.0f,
            (float)WIN_W, 50.0f,
            0.3f, 0.3f, 0.35f, 1.0f);

        /* boxes */
        for (int i = 0; i < g_box_count; ++i) {
            if (!b2Body_IsValid(g_boxes[i])) continue;
            b2Vec2 pos = b2Body_GetPosition(g_boxes[i]);
            zcsr_rectf r = world_to_screen(pos, BOX_HALF, BOX_HALF);
            float hue = (float)(i % 5) / 5.0f;
            zcsr_gl_fill_rect(renderer, r.x, r.y, r.w, r.h,
                0.5f + hue * 0.4f, 0.4f, 0.7f - hue * 0.3f, 1.0f);
        }

        zcsr_gl_flush(renderer);
    }

    /* ---- cleanup ---- */
    if (mixer) zcsr_mix_stop(mixer);
    b2DestroyWorld(g_world);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(surface);
    printf("[Box2D] demo exited cleanly (%d boxes spawned)\n", g_box_count);
    return 0;
}
