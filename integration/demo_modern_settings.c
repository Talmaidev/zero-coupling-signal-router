/* integration/demo_modern_settings.c — 现代游戏设置面板演示.
 *
 * 展示 modern_settings_ui.h 用法:
 *   - 多标签页 (Video / Graphics / Audio)
 *   - 真正可展开下拉框 + 可拖拽滑块 + 开关
 *   - 鼠标 + 键盘双模式交互
 *   - 右侧信息面板
 *   - Apply 时实际调用 platform API (显示三模 / 分辨率 / 显示器 / 垂直同步)
 *
 * 操作:
 *   鼠标 — 点击标签页/下拉框/滑块/开关/按钮, 拖拽滑块
 *   键盘 — Up/Down 切换项, Left/Right 修改, Enter 展开下拉/切换,
 *          Tab 切到底部按钮, ESC 取消退出
 */
#include "modern_settings_ui.h"
#include "zcsr/gl_render.h"
#include "zcsr/platform.h"
#include "zcsr/input.h"
#include "zcsr/input_ext.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static void zcsr_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <unistd.h>
static void zcsr_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#endif

#define WIN_W 1280
#define WIN_H 720

/* ================================================================== */
/*  Video tab items                                                    */
/* ================================================================== */
enum {
    V_DISPLAY = 0,
    V_MODE,
    V_RESOLUTION,
    V_REFRESH,
    V_VSYNC,
    V_FRAMELIMIT,
    V_BRIGHTNESS,
    V_SEP_COMBAT,
    V_FOV,
    V_SHARPNESS,
    V_COUNT,
};

static const char* mode_opts[]    = { "Windowed", "Borderless", "Fullscreen" };
static const char* res_opts[]     = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" };
static const char* refresh_opts[] = { "60Hz", "75Hz", "120Hz", "144Hz", "165Hz", "240Hz" };
static const char* fps_opts[]     = { "30", "60", "120", "144", "Unlimited" };

static zcsr_ms_item g_video[V_COUNT];

/* ================================================================== */
/*  Graphics tab items                                                 */
/* ================================================================== */
enum {
    G_PRESET = 0,
    G_TEXTURE,
    G_SHADOW,
    G_AA,
    G_EFFECTS,
    G_POST,
    G_VIEWDIST,
    G_COUNT,
};

static const char* preset_opts[]  = { "Low", "Medium", "High", "Ultra" };
static const char* tex_opts[]     = { "Low", "Medium", "High", "Ultra" };
static const char* shadow_opts[]  = { "Off", "Low", "Medium", "High" };
static const char* aa_opts[]      = { "Off", "FXAA", "TAA", "MSAA 2x", "MSAA 4x" };
static const char* effect_opts[]  = { "Low", "Medium", "High" };
static const char* post_opts[]    = { "Off", "Low", "Medium", "High" };

static zcsr_ms_item g_graphics[G_COUNT];

/* ================================================================== */
/*  Audio tab items                                                    */
/* ================================================================== */
enum {
    A_MASTER = 0,
    A_MUSIC,
    A_SFX,
    A_VOICE,
    A_SEP_DEV,
    A_OUTPUT,
    A_INPUT,
    A_COUNT,
};

static const char* outdev_opts[] = { "Default", "Speakers", "Headphones" };
static const char* indev_opts[]  = { "Default", "Microphone", "Line In" };

static zcsr_ms_item g_audio[A_COUNT];

/* ================================================================== */
/*  Display enumeration                                                */
/* ================================================================== */
static char g_display_names[8][64];
static const char* g_display_opts[8];
static int g_display_count = 0;

static void init_display_list(void) {
    g_display_count = zcsr_display_count();
    if (g_display_count > 8) g_display_count = 8;
    for (int i = 0; i < g_display_count; i++) {
        zcsr_display_info info;
        if (zcsr_display_get(i, &info)) {
            snprintf(g_display_names[i], sizeof(g_display_names[i]),
                     "%s (%dx%d)", info.name, info.width, info.height);
        } else {
            snprintf(g_display_names[i], sizeof(g_display_names[i]), "Display %d", i);
        }
        g_display_opts[i] = g_display_names[i];
    }
}

/* ================================================================== */
/*  Global state                                                       */
/* ================================================================== */
static zcsr_surface*      g_surface   = NULL;
static zcsr_gl_renderer*  g_renderer  = NULL;
static zcsr_input*        g_input     = NULL;
static zcsr_input_ext*    g_inputext  = NULL;
static zcsr_ms*           g_settings  = NULL;

static int g_res_w = 1280, g_res_h = 720;

/* ================================================================== */
/*  Apply settings to actual window / renderer                         */
/* ================================================================== */
static void apply_settings(void) {
    if (!g_settings) return;

    /* --- Video tab --- */
    /* Display mode (三模) */
    int mode = g_video[V_MODE].value;
    zcsr_window_mode wm = ZCSR_WINDOW_WINDOWED;
    if (mode == 1) wm = ZCSR_WINDOW_BORDERLESS;
    else if (mode == 2) wm = ZCSR_WINDOW_FULLSCREEN;
    zcsr_surface_set_mode(g_surface, wm);
    printf("[apply] display mode: %s\n", mode_opts[mode]);

    /* Resolution (only meaningful in windowed mode) */
    if (mode == 0) {
        static const int rw[] = { 1280, 1600, 1920, 2560, 3840 };
        static const int rh[] = { 720, 900, 1080, 1440, 2160 };
        int ri = g_video[V_RESOLUTION].value;
        g_res_w = rw[ri]; g_res_h = rh[ri];
        zcsr_surface_set_client_size(g_surface, g_res_w, g_res_h);
        printf("[apply] resolution: %dx%d\n", g_res_w, g_res_h);
    }

    /* Display */
    int di = g_video[V_DISPLAY].value;
    if (di < g_display_count) {
        zcsr_display_info info;
        if (zcsr_display_get(di, &info)) {
            zcsr_surface_move_to_display(g_surface, info.id);
            printf("[apply] display: %s\n", info.name);
        }
    }

    /* V-Sync */
    zcsr_gl_set_vsync(g_renderer, g_video[V_VSYNC].toggle_value);
    printf("[apply] vsync: %s\n", g_video[V_VSYNC].toggle_value ? "On" : "Off");

    /* Resize settings panel to match new window client area.
     * GL viewport auto-updates per-frame via zcsr_gl_update_size();
     * the panel layout must be explicitly recomputed. */
    {
        int cw = 0, ch = 0;
        zcsr_surface_get_client_size(g_surface, &cw, &ch);
        if (cw > 0 && ch > 0) {
            g_res_w = cw; g_res_h = ch;
            zcsr_ms_set_bounds(g_settings, 20, 10, cw - 40, ch - 20);
            printf("[apply] panel resized to %dx%d\n", cw, ch);
        }
    }

    /* Print-only settings (rendering pipeline not implemented) */
    printf("[apply] frame limit: %s | brightness: %.0f | FOV: %.0f | sharpness: %.0f\n",
           fps_opts[g_video[V_FRAMELIMIT].value],
           g_video[V_BRIGHTNESS].slider_value,
           g_video[V_FOV].slider_value,
           g_video[V_SHARPNESS].slider_value);

    /* --- Graphics tab (print-only) --- */
    printf("[apply] graphics: preset=%s tex=%s shadow=%s aa=%s effects=%s post=%s viewdist=%.0f\n",
           preset_opts[g_graphics[G_PRESET].value],
           tex_opts[g_graphics[G_TEXTURE].value],
           shadow_opts[g_graphics[G_SHADOW].value],
           aa_opts[g_graphics[G_AA].value],
           effect_opts[g_graphics[G_EFFECTS].value],
           post_opts[g_graphics[G_POST].value],
           g_graphics[G_VIEWDIST].slider_value);

    /* --- Audio tab (print-only) --- */
    printf("[apply] audio: master=%.0f music=%.0f sfx=%.0f voice=%.0f out=%s in=%s\n",
           g_audio[A_MASTER].slider_value,
           g_audio[A_MUSIC].slider_value,
           g_audio[A_SFX].slider_value,
           g_audio[A_VOICE].slider_value,
           outdev_opts[g_audio[A_OUTPUT].value],
           indev_opts[g_audio[A_INPUT].value]);

    printf("[apply] ---\n");
}

/* ================================================================== */
/*  Reset to defaults                                                  */
/* ================================================================== */
static void reset_defaults(void) {
    /* Video */
    g_video[V_DISPLAY].value    = 0;
    g_video[V_MODE].value       = 1; /* Borderless */
    g_video[V_RESOLUTION].value = 2; /* 1080p */
    g_video[V_REFRESH].value    = 0;
    g_video[V_VSYNC].toggle_value   = true;
    g_video[V_FRAMELIMIT].value = 4; /* Unlimited */
    g_video[V_BRIGHTNESS].slider_value = 50.0f;
    g_video[V_FOV].slider_value     = 90.0f;
    g_video[V_SHARPNESS].slider_value = 50.0f;

    /* Graphics */
    g_graphics[G_PRESET].value  = 2; /* High */
    g_graphics[G_TEXTURE].value = 2;
    g_graphics[G_SHADOW].value  = 2;
    g_graphics[G_AA].value      = 1; /* FXAA */
    g_graphics[G_EFFECTS].value = 1;
    g_graphics[G_POST].value    = 1;
    g_graphics[G_VIEWDIST].slider_value = 70.0f;

    /* Audio */
    g_audio[A_MASTER].slider_value = 80.0f;
    g_audio[A_MUSIC].slider_value  = 60.0f;
    g_audio[A_SFX].slider_value    = 90.0f;
    g_audio[A_VOICE].slider_value  = 100.0f;
    g_audio[A_OUTPUT].value = 0;
    g_audio[A_INPUT].value  = 0;

    zcsr_ms_reset(g_settings);
    printf("[reset] defaults restored\n");
}

/* ================================================================== */
/*  Initialize all setting items                                       */
/* ================================================================== */
static void init_items(void) {
    memset(g_video, 0, sizeof(g_video));
    memset(g_graphics, 0, sizeof(g_graphics));
    memset(g_audio, 0, sizeof(g_audio));

    /* --- Video --- */
    g_video[V_DISPLAY] = (zcsr_ms_item){
        .label="Display", .description="Select the monitor for game output.",
        .type=ZCSR_MS_DROPDOWN, .options=g_display_opts, .option_count=g_display_count, .value=0 };
    g_video[V_MODE] = (zcsr_ms_item){
        .label="Display Mode", .description="Windowed: normal window. Borderless: undecorated window. Fullscreen: exclusive fullscreen on selected display.",
        .type=ZCSR_MS_DROPDOWN, .options=mode_opts, .option_count=3, .value=1 };
    g_video[V_RESOLUTION] = (zcsr_ms_item){
        .label="Resolution", .description="Set the window client resolution (applies in Windowed mode).",
        .type=ZCSR_MS_DROPDOWN, .options=res_opts, .option_count=5, .value=2 };
    g_video[V_REFRESH] = (zcsr_ms_item){
        .label="Refresh Rate", .description="Preferred display refresh rate.",
        .type=ZCSR_MS_DROPDOWN, .options=refresh_opts, .option_count=6, .value=0 };
    g_video[V_VSYNC] = (zcsr_ms_item){
        .label="V-Sync", .description="Synchronize frame rate with display refresh to eliminate tearing.",
        .type=ZCSR_MS_TOGGLE, .toggle_value=true };
    g_video[V_FRAMELIMIT] = (zcsr_ms_item){
        .label="Frame Rate Limit", .description="Cap the maximum frame rate. Unlimited removes the cap.",
        .type=ZCSR_MS_DROPDOWN, .options=fps_opts, .option_count=5, .value=4 };
    g_video[V_BRIGHTNESS] = (zcsr_ms_item){
        .label="Brightness", .description="Adjust overall screen brightness.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=50.0f };
    g_video[V_SEP_COMBAT] = (zcsr_ms_item){
        .label="--- Combat View ---", .type=ZCSR_MS_SEPARATOR };
    g_video[V_FOV] = (zcsr_ms_item){
        .label="Default FOV", .description="Field of view in degrees. Wider FOV shows more peripheral vision.",
        .type=ZCSR_MS_SLIDER, .min=60.0f,.max=120.0f,.step=1.0f,.slider_value=90.0f };
    g_video[V_SHARPNESS] = (zcsr_ms_item){
        .label="Sharpness", .description="Post-process sharpening filter strength.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=50.0f };

    /* --- Graphics --- */
    g_graphics[G_PRESET] = (zcsr_ms_item){
        .label="Quality Preset", .description="Global graphics quality preset. Customize individual settings below.",
        .type=ZCSR_MS_DROPDOWN, .options=preset_opts, .option_count=4, .value=2 };
    g_graphics[G_TEXTURE] = (zcsr_ms_item){
        .label="Texture Quality", .description="Texture resolution and filtering quality.",
        .type=ZCSR_MS_DROPDOWN, .options=tex_opts, .option_count=4, .value=2 };
    g_graphics[G_SHADOW] = (zcsr_ms_item){
        .label="Shadow Quality", .description="Shadow map resolution and filtering.",
        .type=ZCSR_MS_DROPDOWN, .options=shadow_opts, .option_count=4, .value=2 };
    g_graphics[G_AA] = (zcsr_ms_item){
        .label="Anti-Aliasing", .description="Smooths jagged edges. TAA offers best quality with motion blur.",
        .type=ZCSR_MS_DROPDOWN, .options=aa_opts, .option_count=5, .value=1 };
    g_graphics[G_EFFECTS] = (zcsr_ms_item){
        .label="Effects Quality", .description="Particle effects, explosions, and weather visual quality.",
        .type=ZCSR_MS_DROPDOWN, .options=effect_opts, .option_count=3, .value=1 };
    g_graphics[G_POST] = (zcsr_ms_item){
        .label="Post Processing", .description="Bloom, depth of field, color grading, and other post effects.",
        .type=ZCSR_MS_DROPDOWN, .options=post_opts, .option_count=4, .value=1 };
    g_graphics[G_VIEWDIST] = (zcsr_ms_item){
        .label="View Distance", .description="Maximum distance at which objects and terrain are rendered.",
        .type=ZCSR_MS_SLIDER, .min=10.0f,.max=100.0f,.step=1.0f,.slider_value=70.0f };

    /* --- Audio --- */
    g_audio[A_MASTER] = (zcsr_ms_item){
        .label="Master Volume", .description="Overall audio output volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=80.0f };
    g_audio[A_MUSIC] = (zcsr_ms_item){
        .label="Music Volume", .description="Background music volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=60.0f };
    g_audio[A_SFX] = (zcsr_ms_item){
        .label="SFX Volume", .description="Sound effects volume (gunshots, explosions, footsteps).",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=90.0f };
    g_audio[A_VOICE] = (zcsr_ms_item){
        .label="Voice Volume", .description="Voice chat and character dialogue volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=100.0f };
    g_audio[A_SEP_DEV] = (zcsr_ms_item){
        .label="--- Devices ---", .type=ZCSR_MS_SEPARATOR };
    g_audio[A_OUTPUT] = (zcsr_ms_item){
        .label="Output Device", .description="Audio output device for game sound.",
        .type=ZCSR_MS_DROPDOWN, .options=outdev_opts, .option_count=3, .value=0 };
    g_audio[A_INPUT] = (zcsr_ms_item){
        .label="Input Device", .description="Microphone / audio input device for voice chat.",
        .type=ZCSR_MS_DROPDOWN, .options=indev_opts, .option_count=3, .value=0 };
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

/* ---- high-resolution timing for startup diagnostics ---- */
#include <windows.h>
static LARGE_INTEGER t_freq;
static double t_mark_ms(LARGE_INTEGER* start) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ms = (double)(now.QuadPart - start->QuadPart) * 1000.0 / (double)t_freq.QuadPart;
    *start = now;
    return ms;
}

int main(void) {
    static unsigned char renderer_buf[8 * 1024 * 1024];
    static unsigned char input_buf[4 * 1024];
    static unsigned char inputext_buf[8 * 1024];
    static unsigned char settings_buf[8192];

    QueryPerformanceFrequency(&t_freq);
    LARGE_INTEGER t_total, t_step;
    QueryPerformanceCounter(&t_total);
    t_step = t_total;

    printf("=== zcsr Modern Settings Demo ===\n");

    init_display_list();
    init_items();
    printf("[boot] item init:       %.1f ms\n", t_mark_ms(&t_step));

    /* Create window */
    g_surface = zcsr_surface_create("zcsr modern settings",
                                    (zcsr_rect){ 100, 100, WIN_W, WIN_H });
    if (!g_surface) { printf("FAIL: no surface\n"); return 1; }
    printf("[boot] surface create:  %.1f ms\n", t_mark_ms(&t_step));

    g_renderer = zcsr_gl_create(g_surface, renderer_buf, sizeof renderer_buf);
    if (!g_renderer) { printf("FAIL: no renderer\n"); zcsr_surface_destroy(g_surface); return 1; }
    zcsr_gl_set_vsync(g_renderer, true);
    printf("[boot] gl create:       %.1f ms\n", t_mark_ms(&t_step));

    g_input = zcsr_input_init(input_buf, sizeof input_buf);
    if (!g_input) { printf("FAIL: no input\n"); goto cleanup; }
    printf("[boot] input init:      %.1f ms\n", t_mark_ms(&t_step));

    g_inputext = zcsr_input_ext_init(inputext_buf, sizeof inputext_buf);
    if (!g_inputext) { printf("FAIL: no input_ext\n"); goto cleanup; }
    printf("[boot] input_ext init:  %.1f ms\n", t_mark_ms(&t_step));

    /* audio init: not called in this demo (no audio subsystem).
     * Timing segment recorded as 0.0ms for completeness per DS requirement. */
    printf("[boot] audio init:      0.0 ms (not initialized in this demo)\n");

    /* Tabs */
    static zcsr_ms_tab tabs[3];
    tabs[0] = (zcsr_ms_tab){ .name="Video",    .items=g_video,    .count=V_COUNT };
    tabs[1] = (zcsr_ms_tab){ .name="Graphics", .items=g_graphics, .count=G_COUNT };
    tabs[2] = (zcsr_ms_tab){ .name="Audio",    .items=g_audio,    .count=A_COUNT };

    g_settings = zcsr_ms_create(settings_buf, sizeof settings_buf,
        &(zcsr_ms_config){
            .tabs        = tabs,
            .tab_count   = 3,
            .x           = 20,
            .y           = 10,
            .width       = WIN_W - 40,
            .height      = WIN_H - 20,
            .item_height = 34,
            .text_scale  = 1.4f,
        });
    if (!g_settings) { printf("FAIL: settings create\n"); goto cleanup; }
    printf("[boot] settings create: %.1f ms\n", t_mark_ms(&t_step));

    /* Adapt panel to the actual window size (borderless is full-screen,
     * not the compile-time WIN_W/WIN_H). Also syncs g_res_w/g_res_h. */
    {
        int cw = 0, ch = 0;
        zcsr_surface_get_client_size(g_surface, &cw, &ch);
        if (cw > 0 && ch > 0) {
            g_res_w = cw; g_res_h = ch;
            zcsr_ms_set_bounds(g_settings, 20, 10, cw - 40, ch - 20);
        }
    }

    /* Set draggable region: right side of the tab bar (past the tab buttons)
     * so borderless windows can be moved without interfering with UI clicks. */
    zcsr_surface_set_drag_region(g_surface, 900, 10, 360, 40);

    printf("Ready.\n");
    printf("  Mouse: click tabs / dropdowns / sliders / toggles / buttons\n");
    printf("  Keyboard: Up/Down navigate, Left/Right change, Enter open dropdown/toggle\n");
    printf("            Tab -> buttons, ESC = cancel & quit\n");
    printf("[boot] total to ready:  %.1f ms\n", t_mark_ms(&t_total));
    fflush(stdout);

    bool running = true;
    int frame_count = 0;
    LARGE_INTEGER t_first_frame;
    QueryPerformanceCounter(&t_first_frame);
    while (running) {
        /* CRITICAL: input_pump must come BEFORE surface_pump_events */
        zcsr_input_pump(g_input, g_surface);
        zcsr_input_ext_pump(g_inputext, g_surface);
        zcsr_surface_pump_events(g_surface);

        /* Mouse update (returns 1=apply, -1=cancel) */
        zcsr_mouse_state mst = zcsr_input_ext_mouse(g_inputext);
        int maction = zcsr_ms_update_mouse(g_settings, &mst);
        if (maction == 1) {
            zcsr_ms_apply(g_settings);
            apply_settings();
        } else if (maction == -1) {
            zcsr_ms_cancel(g_settings);
            printf("[settings] cancelled / quit\n");
            running = false;
        }

        /* Keyboard events */
        zcsr_key_event ev;
        while (zcsr_input_poll(g_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;
            int result = zcsr_ms_handle_key(g_settings, ev.key, ev.action);
            if (result == 1) {
                zcsr_ms_apply(g_settings);
                apply_settings();
            } else if (result == -1) {
                zcsr_ms_cancel(g_settings);
                printf("[settings] cancelled / quit\n");
                running = false;
            }
        }

        /* Render */
        zcsr_gl_begin(g_renderer);
        zcsr_gl_clear(g_renderer, 0.04f, 0.05f, 0.08f, 1.0f);

        /* subtle background grid */
        for (int gx = 0; gx < g_res_w; gx += 40) {
            zcsr_gl_fill_rect(g_renderer, (float)gx, 0.0f, 1.0f, (float)g_res_h,
                              0.06f, 0.08f, 0.12f, 0.3f);
        }
        for (int gy = 0; gy < g_res_h; gy += 40) {
            zcsr_gl_fill_rect(g_renderer, 0.0f, (float)gy, (float)g_res_w, 1.0f,
                              0.06f, 0.08f, 0.12f, 0.3f);
        }

        zcsr_ms_render(g_settings, g_renderer);

        /* bottom hint */
        zcsr_gl_draw_text(g_renderer,
            "Mouse: click/drag | Keyboard: Up/Down Left/Right Enter Tab ESC",
            30.0f, (float)(g_res_h - 18), 1.0f, 0.35f, 0.40f, 0.50f, 1.0f);

        zcsr_gl_flush(g_renderer);
        zcsr_gl_present(g_renderer);
        if (frame_count == 0) {
            double ms = t_mark_ms(&t_first_frame);
            printf("[boot] first frame present (window first visible): %.1f ms (after ready)\n", ms);
            fflush(stdout);
        }
        frame_count++;
        zcsr_sleep_ms(16);
    }

cleanup:
    if (g_settings) zcsr_ms_destroy(g_settings);
    if (g_inputext) zcsr_input_ext_shutdown(g_inputext);
    if (g_renderer) zcsr_gl_destroy(g_renderer);
    if (g_surface)  zcsr_surface_destroy(g_surface);
    printf("=== Modern settings demo ended ===\n");
    return 0;
}
