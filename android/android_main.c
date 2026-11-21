/* zcsr Android Phase 1B — EGL/GLES lifecycle + clear-frame proof. */
#include <android_native_app_glue.h>
#include <android/log.h>
#include <jni.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zcsr/platform.h"
#include "zcsr/clock.h"
#include "zcsr/assets.h"
#include "zcsr/gl_render.h"
#include "zcsr/audio_mix.h"
#include "zcsr/input_ext.h"
#include "zcsr/input_ext_android.h"
#include "zcsr/input_android.h"
#include "zcsr/input.h"

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
    int scene;            /* 0=batch, 1=atlas, 2=instanced, 3=text */
    uint64_t scene_tick;  /* ns timestamp of last scene switch */
    zcsr_input_ext* input_ext;
    union {
        max_align_t alignment;
        unsigned char bytes[4096];
    } input_storage;
    zcsr_input* keyboard;
    union {
        max_align_t alignment;
        unsigned char bytes[2048];
    } keyboard_storage;
    zcsr_pad_state last_logged_pads[ZCSR_PAD_MAX];
    bool pad_log_initialized;
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
    {
        bool timed = zcsr_gl_gpu_timer_begin(runtime->renderer);
        zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
        zcsr_gl_submit_batch(
            runtime->renderer, zcsr_test_sprites, ZCSR_TEST_SPRITE_COUNT);
        if (!zcsr_android_check_gl("sprite batch draw")) {
            LOGE("sprite batch generated GL error"); return false;
        }
        if (timed) (void)zcsr_gl_gpu_timer_end(runtime->renderer);
    }
    zcsr_gl_present(runtime->renderer);
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
    {
        bool timed = zcsr_gl_gpu_timer_begin(runtime->renderer);
        zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
        zcsr_gl_submit_atlas_batch(
            runtime->renderer, zcsr_test_atlas_sprites, ZCSR_TEST_SPRITE_COUNT);
        if (!zcsr_android_check_gl("atlas batch draw")) {
            LOGE("atlas draw generated GL error"); return false;
        }
        if (timed) (void)zcsr_gl_gpu_timer_end(runtime->renderer);
    }
    zcsr_gl_present(runtime->renderer);
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
    {
        bool timed = zcsr_gl_gpu_timer_begin(runtime->renderer);
        size_t submitted;
        uint64_t cpu_start;
        zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
        cpu_start = zcsr_clock_now_ns();
        submitted = zcsr_gl_submit_instance_batch(
            runtime->renderer, zcsr_test_instances, ZCSR_TEST_SPRITE_COUNT,
            zcsr_test_instance_scratch, sizeof(zcsr_test_instance_scratch));
        {
            uint64_t cpu_end = zcsr_clock_now_ns();
            LOGI("1C.2 CPU submit: %.1f us for %zu instances",
                 (double)(cpu_end - cpu_start) / 1000.0, submitted);
        }
        if (!zcsr_android_check_gl("instanced sprite draw")) {
            LOGE("instanced draw generated GL error"); return false;
        }
        if (timed) (void)zcsr_gl_gpu_timer_end(runtime->renderer);
    }
    zcsr_gl_present(runtime->renderer);
    LOGI("1C.2 instanced PASS: 202 instances submitted");
    return true;
}

static bool zcsr_android_draw_text_scene(
    zcsr_android_runtime* runtime, int width, int height) {
    if (!runtime || !runtime->renderer) return false;
    (void)width; (void)height;
    zcsr_android_drain_gl_errors();
    {
        bool timed = zcsr_gl_gpu_timer_begin(runtime->renderer);
        zcsr_gl_clear(runtime->renderer, 0.03f, 0.03f, 0.06f, 1.0f);
        zcsr_gl_draw_text(runtime->renderer, "Hello zcsr! 123",
                          40.0f, 60.0f, 5.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        zcsr_gl_draw_text(runtime->renderer, "The quick brown fox",
                          40.0f, 140.0f, 4.0f, 0.5f, 0.9f, 1.0f, 1.0f);
        zcsr_gl_draw_text(runtime->renderer, "jumps over 456 lazy dogs!",
                          40.0f, 200.0f, 4.0f, 1.0f, 0.8f, 0.3f, 1.0f);
        zcsr_gl_draw_text(runtime->renderer, "ABCDEFGHIJKLMNOP",
                          40.0f, 280.0f, 4.0f, 0.9f, 0.5f, 0.9f, 1.0f);
        zcsr_gl_draw_text(runtime->renderer, "QRSTUVWXYZ 0123456789",
                          40.0f, 340.0f, 4.0f, 0.9f, 0.5f, 0.9f, 1.0f);
        zcsr_gl_draw_text(runtime->renderer, "!@#$%^&*()_+-=[]{}|;:',.<>/?",
                          40.0f, 400.0f, 3.0f, 0.6f, 0.8f, 0.6f, 1.0f);
        zcsr_gl_flush(runtime->renderer);
        if (timed) (void)zcsr_gl_gpu_timer_end(runtime->renderer);
    }
    if (!zcsr_android_check_gl("text draw")) {
        LOGE("text draw generated GL error"); return false;
    }
    zcsr_gl_present(runtime->renderer);
    LOGI("1C.3 text PASS: 8x8 bitmap font rendered (6 lines)");
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

    {
        bool refresh_ok = zcsr_surface_set_refresh_hz(runtime->surface, 120);
        zcsr_display_info display = {0};
        LOGI("1C refresh 120Hz request: %s",
             refresh_ok ? "accepted by ANativeWindow" : "not available");
        if (zcsr_display_get(0, &display)) {
            LOGI("1C display reports: %dx%d, refresh=%dHz, dpi_scale=%.2f",
                 display.width, display.height, display.refresh_hz,
                 (double)display.dpi_scale);
        } else {
            LOGI("1C display reports: unavailable");
        }
    }

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

static int32_t zcsr_android_handle_input(
    struct android_app* state, AInputEvent* event) {
    zcsr_android_runtime* runtime;
    if (!state || !event) return 0;
    runtime = (zcsr_android_runtime*)state->userData;
    if (!runtime) return 0;
    {
        int ext_handled = runtime->input_ext
            ? zcsr_input_ext_android_event(runtime->input_ext, event) : 0;
        int key_handled = zcsr_input_android_event(runtime->keyboard, event);
        return ext_handled || key_handled;
    }
}

static void zcsr_android_focus_callback(
    zcsr_surface* surface,
    zcsr_focus_event event,
    void* user
) {
    zcsr_android_runtime* runtime = (zcsr_android_runtime*)user;
    (void)surface;
    if (event == ZCSR_FOCUS_LOST && runtime) {
        if (runtime->input_ext) zcsr_input_ext_android_reset(runtime->input_ext);
        zcsr_input_android_reset(runtime->keyboard);
    }
}

/* NativeActivity's fullscreen theme hides the status bar, but not the gestural
 * navigation bar. Apply the real immersive-sticky flags through the Activity's
 * decor view; re-apply on focus so a transient swipe does not leave the bar
 * permanently visible. */
static void zcsr_android_apply_immersive(struct android_app* state) {
    JNIEnv* env = NULL;
    bool attached = false;
    jobject activity;
    jclass activity_class = NULL;
    jmethodID get_window = NULL;
    jobject window = NULL;
    jclass window_class = NULL;
    jmethodID get_decor = NULL;
    jobject decor = NULL;
    jclass view_class = NULL;
    jmethodID set_visibility = NULL;
    jmethodID get_visibility = NULL;
    /* FULLSCREEN | HIDE_NAVIGATION | LAYOUT_STABLE |
     * LAYOUT_HIDE_NAVIGATION | LAYOUT_FULLSCREEN | IMMERSIVE_STICKY */
    const jint immersive_flags = 4 | 2 | 256 | 512 | 1024 | 4096;

    if (!state || !state->activity || !state->activity->vm) return;
    if ((*state->activity->vm)->GetEnv(state->activity->vm, (void**)&env,
                                       JNI_VERSION_1_6) != JNI_OK) {
        if ((*state->activity->vm)->AttachCurrentThread(
                state->activity->vm, &env, NULL) != JNI_OK) {
            LOGE("1A immersive: AttachCurrentThread failed");
            return;
        }
        attached = true;
    }
    activity = state->activity->clazz;
    activity_class = (*env)->GetObjectClass(env, activity);
    if (activity_class) {
        get_window = (*env)->GetMethodID(env, activity_class, "getWindow",
                                         "()Landroid/view/Window;");
        window = get_window
            ? (*env)->CallObjectMethod(env, activity, get_window) : NULL;
    }
    if (window) {
        window_class = (*env)->GetObjectClass(env, window);
        get_decor = (*env)->GetMethodID(env, window_class, "getDecorView",
                                        "()Landroid/view/View;");
        decor = get_decor
            ? (*env)->CallObjectMethod(env, window, get_decor) : NULL;
    }
    if (decor) {
        view_class = (*env)->GetObjectClass(env, decor);
        set_visibility = (*env)->GetMethodID(env, view_class,
                                             "setSystemUiVisibility", "(I)V");
        if (set_visibility) {
            jint actual = 0;
            bool call_failed;
            (*env)->CallVoidMethod(env, decor, set_visibility, immersive_flags);
            call_failed = (*env)->ExceptionCheck(env) != 0;
            if (call_failed) (*env)->ExceptionClear(env);
            get_visibility = (*env)->GetMethodID(env, view_class,
                                                 "getSystemUiVisibility", "()I");
            if (get_visibility) {
                actual = (*env)->CallIntMethod(env, decor, get_visibility);
            }
            if ((actual & immersive_flags) == immersive_flags) {
                LOGI("1A immersive: flags readback=0x%x (request=0x%x, "
                     "set call exception=%d)", (unsigned)actual,
                     (unsigned)immersive_flags, (int)call_failed);
            } else {
                LOGE("1A immersive: flags not applied (readback=0x%x, "
                     "request=0x%x, set call exception=%d)", (unsigned)actual,
                     (unsigned)immersive_flags, (int)call_failed);
            }
        }
    }
    if (decor) (*env)->DeleteLocalRef(env, decor);
    if (window) (*env)->DeleteLocalRef(env, window);
    if (view_class) (*env)->DeleteLocalRef(env, view_class);
    if (activity_class) (*env)->DeleteLocalRef(env, activity_class);
    if (window_class) (*env)->DeleteLocalRef(env, window_class);
    if (attached) (*state->activity->vm)->DetachCurrentThread(state->activity->vm);
}

static int zcsr_android_query_refresh_hz(struct android_app* state) {
    JNIEnv* env = NULL;
    bool attached = false;
    jobject activity;
    jclass activity_class = NULL;
    jmethodID get_wm = NULL;
    jobject wm = NULL;
    jclass wm_class = NULL;
    jmethodID get_display = NULL;
    jobject display = NULL;
    jclass display_class = NULL;
    jmethodID get_refresh = NULL;
    jfloat hz = 0.0f;

    if (!state || !state->activity || !state->activity->vm) return 0;
    if ((*state->activity->vm)->GetEnv(state->activity->vm, (void**)&env,
                                       JNI_VERSION_1_6) != JNI_OK) {
        if ((*state->activity->vm)->AttachCurrentThread(
                state->activity->vm, &env, NULL) != JNI_OK) {
            return 0;
        }
        attached = true;
    }
    activity = state->activity->clazz;
    activity_class = (*env)->GetObjectClass(env, activity);
    if (activity_class) {
        get_wm = (*env)->GetMethodID(env, activity_class, "getWindowManager",
                                     "()Landroid/view/WindowManager;");
        wm = get_wm ? (*env)->CallObjectMethod(env, activity, get_wm) : NULL;
    }
    if (wm) {
        wm_class = (*env)->GetObjectClass(env, wm);
        get_display = (*env)->GetMethodID(env, wm_class, "getDefaultDisplay",
                                          "()Landroid/view/Display;");
        display = get_display
            ? (*env)->CallObjectMethod(env, wm, get_display) : NULL;
    }
    if (display) {
        display_class = (*env)->GetObjectClass(env, display);
        get_refresh = (*env)->GetMethodID(env, display_class, "getRefreshRate",
                                          "()F");
        if (get_refresh) {
            hz = (*env)->CallFloatMethod(env, display, get_refresh);
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        hz = 0.0f;
    }
    if (display_class) (*env)->DeleteLocalRef(env, display_class);
    if (display) (*env)->DeleteLocalRef(env, display);
    if (wm_class) (*env)->DeleteLocalRef(env, wm_class);
    if (wm) (*env)->DeleteLocalRef(env, wm);
    if (activity_class) (*env)->DeleteLocalRef(env, activity_class);
    if (attached) (*state->activity->vm)->DetachCurrentThread(state->activity->vm);
    return (hz > 1.0f && hz < 1000.0f) ? (int)(hz + 0.5f) : 0;
}

static void zcsr_android_handle_cmd(struct android_app* state, int32_t cmd) {
    zcsr_android_runtime* runtime;

    if (!state) return;
    runtime = (zcsr_android_runtime*)state->userData;
    if (!runtime) return;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (state->window) {
            int mode_hz = zcsr_android_query_refresh_hz(state);
            if (mode_hz > 0) {
                zcsr_platform_android_set_display_refresh_hz(mode_hz);
                LOGI("1A display mode refresh from Java: %dHz", mode_hz);
            } else {
                LOGE("1A display mode refresh unavailable from Java Display");
            }
            zcsr_android_start_gl(runtime, state->window);
        }
        break;

    case APP_CMD_TERM_WINDOW:
        if (runtime->input_ext) {
            zcsr_input_ext_android_reset(runtime->input_ext);
        }
        zcsr_input_android_reset(runtime->keyboard);
        /* EGL surface/context must go away before ANativeWindow is detached. */
        zcsr_android_stop_gl(runtime);
        zcsr_surface_detach_native(runtime->surface);
        LOGI("surface detached");
        break;

    case APP_CMD_GAINED_FOCUS:
        zcsr_platform_android_dispatch_focus(ZCSR_FOCUS_GAINED);
        zcsr_android_apply_immersive(state);
        break;

    case APP_CMD_CONFIG_CHANGED:
        zcsr_platform_android_set_config(state->config);
        break;

    case APP_CMD_LOST_FOCUS:
        zcsr_platform_android_dispatch_focus(ZCSR_FOCUS_LOST);
        break;

    default:
        break;
    }
}

void android_main(struct android_app* state) {
    zcsr_rect bounds = {0, 0, 100, 100};
    zcsr_surface* surface;
    zcsr_android_runtime runtime = { 0 };

    LOGI("android boot ok");

    surface = zcsr_surface_create("zcsr_android", bounds);
    if (!surface) {
        LOGE("surface allocation failed");
        return;
    }

    runtime.surface = surface;
    runtime.audio_channel = -1;
    runtime.scene_tick = zcsr_clock_now_ns();
    zcsr_platform_android_set_config(state->config);
    /* Bind APK assets once; every later zcsr_assets_read/texture/font call
     * then resolves inside assets/ without an app-side per-file path. */
    zcsr_assets_set_android_asset_manager(state->activity->assetManager);

    state->userData = &runtime;
    state->onAppCmd = zcsr_android_handle_cmd;

    runtime.input_ext = zcsr_input_ext_init(
        runtime.input_storage.bytes,
        sizeof(runtime.input_storage.bytes));
    if (runtime.input_ext) {
        state->onInputEvent = zcsr_android_handle_input;
        zcsr_input_ext_android_bind_activity(runtime.input_ext, state->activity);
        zcsr_surface_set_focus_callback(
            runtime.surface,
            zcsr_android_focus_callback,
            &runtime
        );
        LOGI("1E input extension initialized (sizeof storage=%zu)",
             sizeof(runtime.input_storage.bytes));
        runtime.keyboard = zcsr_input_init(
            runtime.keyboard_storage.bytes,
            sizeof(runtime.keyboard_storage.bytes));
        if (runtime.keyboard) {
            LOGI("1E keyboard state initialized (sizeof storage=%zu)",
                 sizeof(runtime.keyboard_storage.bytes));
        } else {
            LOGE("1E keyboard state init failed; key state disabled");
        }
    } else {
        LOGE("1E input extension init failed; input disabled");
    }

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

        if (runtime.input_ext) {
            int i;
            char text[128];
            size_t text_n;
            zcsr_input_ext_pump(runtime.input_ext, runtime.surface);
            text_n = zcsr_input_ext_take_text(runtime.input_ext, text, sizeof text);
            if (text_n > 0u) {
                LOGI("1E text: %s", text);
            }
            {
                zcsr_key_event ke;
                if (zcsr_input_poll(runtime.keyboard, &ke)) {
                    LOGI("1E key: code=%u action=%s text=%d",
                         (unsigned)ke.key,
                         ke.action == ZCSR_KEY_DOWN ? "down" : "up",
                         (int)ke.text);
                }
            }
            {
                zcsr_touch_state t = zcsr_input_ext_touch(runtime.input_ext);
                if (t.pressed || t.released || t.dx != 0.0f || t.dy != 0.0f) {
                    LOGI("1E touch: x=%.1f y=%.1f dx=%.1f dy=%.1f active=%d pressed=%d released=%d",
                         t.x, t.y, t.dx, t.dy,
                         (int)t.active, (int)t.pressed, (int)t.released);
                }
            }
            for (i = 0; i < ZCSR_PAD_MAX; ++i) {
                zcsr_pad_state p = zcsr_input_ext_pad(runtime.input_ext, i);
                if (!runtime.pad_log_initialized ||
                    memcmp(&p, &runtime.last_logged_pads[i], sizeof(p)) != 0) {
                    if (p.connected) {
                        LOGI("1E pad[%d]: buttons=0x%04x lx=%.2f ly=%.2f rx=%.2f ry=%.2f lt=%.2f rt=%.2f",
                             i, p.buttons, p.lx, p.ly, p.rx, p.ry, p.lt, p.rt);
                    }
                    runtime.last_logged_pads[i] = p;
                }
            }
            runtime.pad_log_initialized = true;
        }

        if (runtime.mixer) {
            int finished[ZCSR_MIX_CHANNELS];
            size_t count = zcsr_mix_poll_finished(
                runtime.mixer, finished, ZCSR_MIX_CHANNELS);
            for (size_t i = 0; i < count; ++i) {
                LOGI("1D audio: audio_finished channel=%d", finished[i]);
            }
        }

        if (runtime.renderer) {
            float gpu_ms = 0.0f;
            if (zcsr_gl_gpu_timer_last_ms(runtime.renderer, &gpu_ms)) {
                LOGI("1C GPU timer: %.3f ms (GL_EXT_disjoint_timer_query)", gpu_ms);
            }
        }

        /* Scene rotation: every 2 s, re-query size and redraw the next scene. */
        if (runtime.renderer && runtime.test_texture) {
            uint64_t now = zcsr_clock_now_ns();
            if (now - runtime.scene_tick >= 2000000000ull) {
                int width = 0, height = 0;
                runtime.scene_tick = now;
                runtime.scene = (runtime.scene + 1) % 4; /* batch->atlas->instanced->text */
                if (zcsr_android_query_egl_size(&width, &height)) {
                    if (runtime.scene == 0)
                        (void)zcsr_android_draw_batch_scene(&runtime, width, height);
                    else if (runtime.scene == 1)
                        (void)zcsr_android_draw_atlas_scene(&runtime, width, height);
                    else if (runtime.scene == 2)
                        (void)zcsr_android_draw_instanced_scene(&runtime, width, height);
                    else
                        (void)zcsr_android_draw_text_scene(&runtime, width, height);
                }
            }
        }
    }

    /* Free GL/EGL resources while the attached native window is still valid. */
    zcsr_surface_set_focus_callback(runtime.surface, NULL, NULL);
    if (runtime.input_ext) {
        zcsr_input_ext_shutdown(runtime.input_ext);
        runtime.input_ext = 0;
    }
    state->onInputEvent = 0;
    runtime.keyboard = 0;
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
