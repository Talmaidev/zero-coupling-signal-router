/* zcsr Android Phase 1B — EGL/GLES lifecycle + clear-frame proof. */
#include <android_native_app_glue.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zcsr/platform.h"
#include "zcsr/clock.h"
#include "zcsr/gl_render.h"
#include "zcsr/audio_mix.h"

#include <math.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "zcsr", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "zcsr", __VA_ARGS__))

#define ZCSR_GL_BUFFER_BYTES (8u * 1024u * 1024u)

#define ZCSR_TEST_TEXTURE_SIZE 16
#define ZCSR_TEST_GRID_COLS 10
#define ZCSR_TEST_GRID_ROWS 20
#define ZCSR_TEST_GRID_COUNT 200
#define ZCSR_TEST_SPRITE_COUNT 202

/* Aligned, fixed-size storage. No runtime heap allocation. */
static union {
    max_align_t alignment;
    unsigned char bytes[ZCSR_GL_BUFFER_BYTES];
} zcsr_renderer_storage;

static uint8_t zcsr_test_pixels[ZCSR_TEST_TEXTURE_SIZE * ZCSR_TEST_TEXTURE_SIZE * 4];
static zcsr_gl_sprite zcsr_test_sprites[ZCSR_TEST_SPRITE_COUNT];
static zcsr_gl_atlas_sprite zcsr_test_atlas_sprites[ZCSR_TEST_SPRITE_COUNT];
static zcsr_gl_instance zcsr_test_instances[ZCSR_TEST_SPRITE_COUNT];
static zcsr_gl_instance zcsr_test_instance_scratch[ZCSR_TEST_SPRITE_COUNT];

typedef struct {
    zcsr_surface* surface;
    zcsr_gl_renderer* renderer;
    zcsr_gl_texture test_texture;
    zcsr_mixer* mixer;
    int audio_channel;
    int scene;            /* 0=batch, 1=atlas, 2=instanced */
    uint64_t scene_tick;  /* ns timestamp of last scene switch */
} zcsr_android_runtime;

static void zcsr_android_stop_gl(zcsr_android_runtime* runtime) {
    if (!runtime) return;

    if (runtime->renderer && runtime->test_texture) {
        zcsr_gl_texture_destroy(runtime->renderer, runtime->test_texture);
        runtime->test_texture = 0;
    }

    if (runtime->renderer) {
        zcsr_gl_destroy(runtime->renderer);
        runtime->renderer = 0;
        LOGI("EGL renderer destroyed");
    }
}

static void zcsr_android_make_test_pattern(bool alternate) {
    int x, y;
    for (y = 0; y < ZCSR_TEST_TEXTURE_SIZE; ++y) {
        for (x = 0; x < ZCSR_TEST_TEXTURE_SIZE; ++x) {
            uint8_t r, g, b;
            size_t i = ((size_t)y * ZCSR_TEST_TEXTURE_SIZE + (size_t)x) * 4u;
            if (x == 0 || y == 0 ||
                x == ZCSR_TEST_TEXTURE_SIZE - 1 ||
                y == ZCSR_TEST_TEXTURE_SIZE - 1) {
                r = g = b = 255;
            } else if (x < 8 && y < 8) { r = 240; g = 35; b = 35; }
            else if (x >= 8 && y < 8) { r = 30; g = 220; b = 60; }
            else if (x < 8)            { r = 35; g = 90; b = 240; }
            else                       { r = 255; g = 210; b = 20; }
            if ((x >= 3 && x <= 4 && y >= 3 && y <= 8) ||
                (x >= 3 && x <= 8 && y >= 7 && y <= 8)) {
                r = g = b = 0;
            }
            if (alternate) { uint8_t t = r; r = b; b = t; }
            zcsr_test_pixels[i + 0] = r;
            zcsr_test_pixels[i + 1] = g;
            zcsr_test_pixels[i + 2] = b;
            zcsr_test_pixels[i + 3] = 255;
        }
    }
}

static void zcsr_android_drain_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) { }
}

static bool zcsr_android_check_gl(const char* operation) {
    bool ok = true;
    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR) {
        LOGE("%s generated GL error 0x%x", operation, (unsigned int)error);
        ok = false;
    }
    return ok;
}

static bool zcsr_android_query_egl_size(int* out_width, int* out_height) {
    EGLDisplay display = eglGetCurrentDisplay();
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    EGLint width = 0, height = 0;
    if (!out_width || !out_height ||
        display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return false;
    if (eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
        eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE ||
        width <= 0 || height <= 0) return false;
    *out_width = (int)width;
    *out_height = (int)height;
    return true;
}

static bool zcsr_android_create_test_texture(zcsr_android_runtime* runtime) {
    if (!runtime || !runtime->renderer) return false;
    zcsr_android_make_test_pattern(false);
    zcsr_android_drain_gl_errors();
    runtime->test_texture = zcsr_gl_texture_create(
        runtime->renderer, zcsr_test_pixels,
        ZCSR_TEST_TEXTURE_SIZE, ZCSR_TEST_TEXTURE_SIZE);
    if (!runtime->test_texture || !zcsr_android_check_gl("texture create")) {
        LOGE("texture create failed"); return false;
    }
    zcsr_android_make_test_pattern(true);
    zcsr_android_drain_gl_errors();
    if (!zcsr_gl_texture_upload(
            runtime->renderer, runtime->test_texture, zcsr_test_pixels,
            ZCSR_TEST_TEXTURE_SIZE, ZCSR_TEST_TEXTURE_SIZE)) {
        LOGE("texture upload returned false"); return false;
    }
    if (!zcsr_android_check_gl("texture upload")) {
        LOGE("texture upload generated GL error"); return false;
    }
    zcsr_gl_texture_filter(runtime->renderer, runtime->test_texture, false);
    if (!zcsr_android_check_gl("texture filter")) {
        LOGE("texture filter setup failed"); return false;
    }
    LOGI("1C.0 texture=%u upload=PASS filter=NEAREST",
         (unsigned int)runtime->test_texture);
    return true;
}

static void zcsr_android_fill_batch(
    zcsr_android_runtime* runtime, int width, int height) {
    int i;
    float cell_w = (float)width / (float)ZCSR_TEST_GRID_COLS;
    float cell_h = (float)height / (float)ZCSR_TEST_GRID_ROWS;
    float side_w = cell_w * 0.66f;
    float side_h = cell_h * 0.66f;
    float side = side_w < side_h ? side_w : side_h;
    float center_side = (float)width * 0.25f;
    float center_limit_h = (float)height * 0.12f;
    if (center_side > center_limit_h) center_side = center_limit_h;
    for (i = 0; i < ZCSR_TEST_GRID_COUNT; ++i) {
        int col = i % ZCSR_TEST_GRID_COLS;
        int row = i / ZCSR_TEST_GRID_COLS;
        zcsr_gl_sprite* s = &zcsr_test_sprites[i];
        *s = (zcsr_gl_sprite){0};
        s->tex = runtime->test_texture;
        s->dst = (zcsr_rectf){
            (float)col * cell_w + (cell_w - side) * 0.5f,
            (float)row * cell_h + (cell_h - side) * 0.5f,
            side, side
        };
        s->r = s->g = s->b = s->a = 1.0f;
        s->mode = ZCSR_MOD_MULTIPLY;
    }
    zcsr_test_sprites[200] = (zcsr_gl_sprite){0};
    zcsr_test_sprites[200].tex = runtime->test_texture;
    zcsr_test_sprites[200].dst = (zcsr_rectf){
        ((float)width - center_side) * 0.5f,
        ((float)height - center_side) * 0.5f,
        center_side, center_side
    };
    zcsr_test_sprites[200].r = 1.0f;
    zcsr_test_sprites[200].g = 1.0f;
    zcsr_test_sprites[200].b = 1.0f;
    zcsr_test_sprites[200].a = 1.0f;
    zcsr_test_sprites[200].mode = ZCSR_MOD_MULTIPLY;
    zcsr_test_sprites[201] = zcsr_test_sprites[200];
    zcsr_test_sprites[201].dst.x += center_side * 0.16f;
    zcsr_test_sprites[201].dst.y += center_side * 0.16f;
    zcsr_test_sprites[201].dst.w *= 0.68f;
    zcsr_test_sprites[201].dst.h *= 0.68f;
    zcsr_test_sprites[201].r = 1.0f;
    zcsr_test_sprites[201].g = 0.25f;
    zcsr_test_sprites[201].b = 0.25f;
    zcsr_test_sprites[201].a = 0.45f;
}

static bool zcsr_android_draw_batch_scene(
    zcsr_android_runtime* runtime, int width, int height) {
    if (!runtime || !runtime->renderer || !runtime->test_texture) return false;
    zcsr_android_fill_batch(runtime, width, height);
    zcsr_android_drain_gl_errors();
    zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
    zcsr_gl_submit_batch(
        runtime->renderer, zcsr_test_sprites, ZCSR_TEST_SPRITE_COUNT);
    if (!zcsr_android_check_gl("sprite batch draw")) {
        LOGE("sprite batch generated GL error"); return false;
    }
    LOGI("1C.0 sprite batch PASS: 200 grid + 2 center sprites");
    return true;
}

static void zcsr_android_fill_atlas(
    zcsr_android_runtime* runtime, int width, int height) {
    static const zcsr_atlas_region regions[4] = {
        { 0.0f, 0.0f, 0.5f, 0.5f },
        { 0.5f, 0.0f, 1.0f, 0.5f },
        { 0.0f, 0.5f, 0.5f, 1.0f },
        { 0.5f, 0.5f, 1.0f, 1.0f }
    };
    size_t i;
    zcsr_android_fill_batch(runtime, width, height);
    for (i = 0; i < ZCSR_TEST_SPRITE_COUNT; ++i) {
        const zcsr_gl_sprite* src = &zcsr_test_sprites[i];
        zcsr_gl_atlas_sprite* dst = &zcsr_test_atlas_sprites[i];
        *dst = (zcsr_gl_atlas_sprite){0};
        dst->atlas = runtime->test_texture;
        dst->dst = src->dst;
        dst->rotation = src->rotation;
        dst->r = src->r; dst->g = src->g; dst->b = src->b; dst->a = src->a;
        dst->mode = src->mode;
        dst->amount = src->amount;
        dst->region = (i < ZCSR_TEST_GRID_COUNT)
            ? regions[i % 4]
            : (zcsr_atlas_region){ 0.0f, 0.0f, 1.0f, 1.0f };
    }
}

static bool zcsr_android_draw_atlas_scene(
    zcsr_android_runtime* runtime, int width, int height) {
    if (!runtime || !runtime->renderer || !runtime->test_texture) return false;
    zcsr_android_fill_atlas(runtime, width, height);
    zcsr_android_drain_gl_errors();
    zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
    zcsr_gl_submit_atlas_batch(
        runtime->renderer, zcsr_test_atlas_sprites, ZCSR_TEST_SPRITE_COUNT);
    if (!zcsr_android_check_gl("atlas batch draw")) {
        LOGE("atlas draw generated GL error"); return false;
    }
    LOGI("1C.1 atlas PASS: four UV regions sampled");
    return true;
}

static void zcsr_android_fill_instances(
    zcsr_android_runtime* runtime, int width, int height) {
    size_t i;
    zcsr_android_fill_batch(runtime, width, height);
    for (i = 0; i < ZCSR_TEST_SPRITE_COUNT; ++i) {
        const zcsr_gl_sprite* src = &zcsr_test_sprites[i];
        zcsr_gl_instance* dst = &zcsr_test_instances[i];
        *dst = (zcsr_gl_instance){0};
        dst->cx = src->dst.x + src->dst.w * 0.5f;
        dst->cy = src->dst.y + src->dst.h * 0.5f;
        dst->w = src->dst.w;
        dst->h = src->dst.h;
        dst->u0 = 0.0f; dst->v0 = 0.0f;
        dst->u1 = 1.0f; dst->v1 = 1.0f;
        dst->r = src->r; dst->g = src->g; dst->b = src->b; dst->a = src->a;
        dst->rotation = src->rotation;
        dst->amount = src->amount;
        dst->mode = (float)src->mode;
        dst->tex = runtime->test_texture;
        dst->z = (float)i;
    }
}

static bool zcsr_android_draw_instanced_scene(
    zcsr_android_runtime* runtime, int width, int height) {
    if (!runtime || !runtime->renderer || !runtime->test_texture) return false;
    zcsr_android_fill_instances(runtime, width, height);
    zcsr_android_drain_gl_errors();
    zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
    zcsr_gl_submit_instance_batch(
        runtime->renderer, zcsr_test_instances, ZCSR_TEST_SPRITE_COUNT,
        zcsr_test_instance_scratch, sizeof(zcsr_test_instance_scratch));
    if (!zcsr_android_check_gl("instanced sprite draw")) {
        LOGE("instanced draw generated GL error"); return false;
    }
    LOGI("1C.2 instanced PASS: 202 instances submitted");
    return true;
}

static void zcsr_android_start_gl(
    zcsr_android_runtime* runtime, void* native_window) {
    const GLubyte* version;
    void* currently_bound;

    if (!runtime || !runtime->surface || !native_window) return;

    currently_bound = zcsr_surface_native_handle(runtime->surface);

    /* A duplicate INIT_WINDOW for the same live window is harmless. */
    if (runtime->renderer && currently_bound == native_window) return;

    /* If replacing a live window, destroy EGL before platform releases its reference. */
    zcsr_android_stop_gl(runtime);

    if (currently_bound != native_window) {
        if (!zcsr_surface_attach_native(runtime->surface, native_window)) {
            LOGE("surface attach failed");
            return;
        }
    }

    runtime->renderer = zcsr_gl_create(
        runtime->surface,
        zcsr_renderer_storage.bytes,
        sizeof(zcsr_renderer_storage.bytes));

    if (!runtime->renderer) {
        LOGE("zcsr_gl_create failed; inspect zcsr-gl EGL/shader logs");
        return;
    }

    version = glGetString(GL_VERSION);
    LOGI("GLES version: %s",
         version ? (const char*)version : "(null)");

    if (!glIsEnabled(GL_BLEND)) {
        LOGE("GL_BLEND not enabled after context creation");
        zcsr_android_stop_gl(runtime);
        return;
    }

    {
        int width = 0, height = 0;
        if (!zcsr_android_query_egl_size(&width, &height)) {
            LOGE("query egl surface size failed");
            zcsr_android_stop_gl(runtime);
            return;
        }
        if (!zcsr_android_create_test_texture(runtime)) {
            LOGE("test texture setup failed");
            zcsr_android_stop_gl(runtime);
            return;
        }
        if (!zcsr_android_draw_batch_scene(runtime, width, height)) {
            LOGE("batch scene draw failed");
            zcsr_android_stop_gl(runtime);
            return;
        }
    }
}

#define ZCSR_AUDIO_TEST_RATE 44100U
#define ZCSR_AUDIO_TEST_FRAMES 13230U /* 300 ms */

static bool zcsr_android_write_u16le(FILE* f, uint16_t value) {
    unsigned char b[2];
    b[0] = (unsigned char)(value & 0xffU);
    b[1] = (unsigned char)(value >> 8);
    return fwrite(b, 1, sizeof b, f) == sizeof b;
}

static bool zcsr_android_write_u32le(FILE* f, uint32_t value) {
    unsigned char b[4];
    b[0] = (unsigned char)(value & 0xffU);
    b[1] = (unsigned char)((value >> 8) & 0xffU);
    b[2] = (unsigned char)((value >> 16) & 0xffU);
    b[3] = (unsigned char)(value >> 24);
    return fwrite(b, 1, sizeof b, f) == sizeof b;
}

static bool zcsr_android_create_beep_wav(const char* path) {
    FILE* f;
    const uint32_t data_bytes = ZCSR_AUDIO_TEST_FRAMES * 4U;
    bool ok = true;

    if (!path) return false;
    f = fopen(path, "wb");
    if (!f) return false;

    ok = fwrite("RIFF", 1, 4, f) == 4 &&
         zcsr_android_write_u32le(f, 36U + data_bytes) &&
         fwrite("WAVEfmt ", 1, 8, f) == 8 &&
         zcsr_android_write_u32le(f, 16U) &&
         zcsr_android_write_u16le(f, 1U) &&       /* PCM */
         zcsr_android_write_u16le(f, 2U) &&       /* stereo */
         zcsr_android_write_u32le(f, ZCSR_AUDIO_TEST_RATE) &&
         zcsr_android_write_u32le(f, ZCSR_AUDIO_TEST_RATE * 4U) &&
         zcsr_android_write_u16le(f, 4U) &&       /* block align */
         zcsr_android_write_u16le(f, 16U) &&
         fwrite("data", 1, 4, f) == 4 &&
         zcsr_android_write_u32le(f, data_bytes);

    for (uint32_t i = 0; ok && i < ZCSR_AUDIO_TEST_FRAMES; ++i) {
        float envelope = 1.0f;
        float phase;
        int16_t sample;
        unsigned char b[2];

        if (i < 441U) envelope = (float)i / 441.0f;
        if (i >= ZCSR_AUDIO_TEST_FRAMES - 441U) {
            float fade = (float)(ZCSR_AUDIO_TEST_FRAMES - i) / 441.0f;
            if (fade < envelope) envelope = fade;
        }

        phase = 6.2831853071795864769f * 440.0f *
                (float)i / (float)ZCSR_AUDIO_TEST_RATE;
        sample = (int16_t)(sinf(phase) * 5000.0f * envelope);
        b[0] = (unsigned char)((uint16_t)sample & 0xffU);
        b[1] = (unsigned char)(((uint16_t)sample >> 8) & 0xffU);
        if (fwrite(b, 1, 2, f) != 2 || fwrite(b, 1, 2, f) != 2) {
            ok = false;
            break;
        }
    }

    if (fclose(f) != 0) ok = false;
    if (!ok) (void)remove(path);
    return ok;
}

static void zcsr_android_start_audio_demo(
    zcsr_android_runtime* runtime, struct android_app* state) {
    char path[512];
    int written;
    bool registered;

    if (!runtime || !state || !state->activity ||
        !state->activity->internalDataPath) {
        LOGE("1D audio: app-private path unavailable");
        return;
    }

    runtime->mixer = zcsr_mix_start();
    if (!runtime->mixer) {
        LOGE("1D audio: zcsr_mix_start failed (AAudio unavailable or stream open failed)");
        return;
    }

    written = snprintf(path, sizeof path, "%s/zcsr_1d_beep.wav",
                       state->activity->internalDataPath);
    if (written <= 0 || (size_t)written >= sizeof path) {
        LOGE("1D audio: WAV path too long");
        return;
    }

    if (!zcsr_android_create_beep_wav(path)) {
        LOGE("1D audio: failed to generate WAV");
        return;
    }

    registered = zcsr_mix_register_wav(runtime->mixer, "zcsr_1d_beep", path);
    (void)remove(path); /* WAV samples have been copied into the mixer pool. */
    if (!registered) {
        LOGE("1D audio: WAV registration failed");
        return;
    }

    runtime->audio_channel = zcsr_mix_play(
        runtime->mixer, "zcsr_1d_beep", 0.8f, false);
    if (runtime->audio_channel < 0) {
        LOGE("1D audio: play request failed");
        return;
    }

    LOGI("1D audio: WAV registered; play channel=%d", runtime->audio_channel);
}

static void zcsr_android_handle_cmd(struct android_app* state, int32_t cmd) {
    zcsr_android_runtime* runtime;

    if (!state) return;
    runtime = (zcsr_android_runtime*)state->userData;
    if (!runtime) return;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (state->window) {
            zcsr_android_start_gl(runtime, state->window);
        }
        break;

    case APP_CMD_TERM_WINDOW:
        /* EGL surface/context must go away before ANativeWindow is detached. */
        zcsr_android_stop_gl(runtime);
        zcsr_surface_detach_native(runtime->surface);
        LOGI("surface detached");
        break;

    default:
        break;
    }
}

void android_main(struct android_app* state) {
    zcsr_rect bounds = {0, 0, 100, 100};
    zcsr_surface* surface;
    zcsr_android_runtime runtime;

    LOGI("android boot ok");

    surface = zcsr_surface_create("zcsr_android", bounds);
    if (!surface) {
        LOGE("surface allocation failed");
        return;
    }

    runtime.surface = surface;
    runtime.renderer = 0;
    runtime.test_texture = 0;
    runtime.mixer = 0;
    runtime.audio_channel = -1;
    runtime.scene = 0;
    runtime.scene_tick = zcsr_clock_now_ns();

    state->userData = &runtime;
    state->onAppCmd = zcsr_android_handle_cmd;

    LOGI("surface_create=%p", (void*)surface);
    LOGI("clock_now_ns=%llu",
         (unsigned long long)zcsr_clock_now_ns());

    zcsr_android_start_audio_demo(&runtime, state);

    /* Cover the case where a window is already available. */
    if (state->window) {
        zcsr_android_handle_cmd(state, APP_CMD_INIT_WINDOW);
    }

    while (state->destroyRequested == 0) {
        struct android_poll_source* source = 0;
        int events = 0;

        int ident = ALooper_pollOnce(
            100, 0, &events, (void**)&source);

        if (ident == ALOOPER_POLL_ERROR) {
            LOGE("ALooper_pollOnce error; exiting loop");
            break;
        }

        if (source) {
            source->process(state, source);
        }

        if (runtime.mixer) {
            int finished[ZCSR_MIX_CHANNELS];
            size_t count = zcsr_mix_poll_finished(
                runtime.mixer, finished, ZCSR_MIX_CHANNELS);
            for (size_t i = 0; i < count; ++i) {
                LOGI("1D audio: audio_finished channel=%d", finished[i]);
            }
        }

        /* Scene rotation: every 2 s, re-query size and redraw the next scene. */
        if (runtime.renderer && runtime.test_texture) {
            uint64_t now = zcsr_clock_now_ns();
            if (now - runtime.scene_tick >= 2000000000ull) {
                int width = 0, height = 0;
                runtime.scene_tick = now;
                runtime.scene = (runtime.scene + 1) % 3; /* 1C.2: batch->atlas->instanced */
                if (zcsr_android_query_egl_size(&width, &height)) {
                    if (runtime.scene == 0)
                        (void)zcsr_android_draw_batch_scene(&runtime, width, height);
                    else if (runtime.scene == 1)
                        (void)zcsr_android_draw_atlas_scene(&runtime, width, height);
                    else
                        (void)zcsr_android_draw_instanced_scene(&runtime, width, height);
                }
            }
        }
    }

    /* Free GL/EGL resources while the attached native window is still valid. */
    zcsr_android_stop_gl(&runtime);

    if (runtime.mixer) {
        zcsr_mix_stop(runtime.mixer);
        runtime.mixer = 0;
        LOGI("1D audio: mixer stopped");
    }

    zcsr_surface_detach_native(surface);

    state->onAppCmd = 0;
    state->userData = 0;

    zcsr_surface_destroy(surface);
    LOGI("android_main exiting");
}
