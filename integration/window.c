/* integration/window.c — 独立 window 骨架: the app shell (no main; testmain.c drives it).
 *
 * The shell is deliberately EMPTY by itself: a near-black screen with categorized
 * main-menu buttons (Delta Force style). Its own visual identity is dark slate +
 * amber accents — it does NOT borrow the settings panel's look. It owns:
 *   - a NORMAL OS window: taskbar icon, Alt-Tab switchable, never topmost, mouse
 *     cursor visible (the platform's default surface is a topmost fullscreen
 *     overlay; we switch to WINDOWED immediately after create)
 *   - one big button per registered test module (see window.h) + built-in
 *     SETTINGS + EXIT
 *   - the single main loop and full boot/teardown
 *   - the settings overlay machinery, entirely shell-internal: the panel is
 *     rendered into an offscreen texture (zcsr_gl_set_target) and composited
 *     back over the dimmed page at an ADJUSTABLE opacity (Interface tab), so
 *     plugged-in modules (testmain.c) know nothing about it
 *
 * Run with "autoquit" for the headless self-test (home -> module -> toggle ->
 * settings overlay -> close -> exit).
 */
#include "window.h"
#include "modern_settings_ui.h"
#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/input_ext.h"
#include "zcsr/window_event.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static void shell_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <unistd.h>
static void shell_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#endif

#define SHELL_W 1280
#define SHELL_H 720
#define SHELL_MAX_MODULES 4
#define KEY_ESCAPE 0x1B
#define KEY_SPACE  0x20

/* Offscreen texture cap for the settings panel (composite source). Windows larger
 * than this get a clamped panel. 1920x1080x4 = 8 MiB of zero-initialized BSS. */
#define SHELL_PANEL_TEX_MAX_W 1920
#define SHELL_PANEL_TEX_MAX_H 1080

/* Shell visual identity: near-black + slate + amber (distinct from the settings panel). */
static const float SHELL_BG[4]     = { 0.020f, 0.022f, 0.030f, 1.0f };
static const float SHELL_BTN[4]    = { 0.090f, 0.100f, 0.130f, 1.0f };
static const float SHELL_BTN_HOV[4]= { 0.160f, 0.150f, 0.110f, 1.0f };
static const float SHELL_ACCENT[4] = { 0.920f, 0.620f, 0.180f, 1.0f };
static const float SHELL_TEXT[4]   = { 0.850f, 0.860f, 0.880f, 1.0f };

/* ================================================================== */
/*  Settings items (complete demo set, Borderless restored)           */
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

/* Interface tab: shell-owned look-and-feel of the settings overlay itself. */
enum {
    I_OPACITY = 0, /* settings panel composite opacity (%) */
    I_DIM,         /* background dim under the overlay (%) */
    I_COUNT,
};

static zcsr_ms_item g_interface[I_COUNT];

static char g_display_names[8][64];
static const char* g_display_opts[8];
static int g_display_count = 0;

static void init_display_list(void) {
    int i;
    g_display_count = zcsr_display_count();
    if (g_display_count > 8) g_display_count = 8;
    for (i = 0; i < g_display_count; i++) {
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

static void init_items(void) {
    memset(g_video, 0, sizeof(g_video));
    memset(g_graphics, 0, sizeof(g_graphics));
    memset(g_audio, 0, sizeof(g_audio));
    memset(g_interface, 0, sizeof(g_interface));

    g_video[V_DISPLAY] = (zcsr_ms_item){
        .label="Display", .description="Select the monitor for output.",
        .type=ZCSR_MS_DROPDOWN, .options=g_display_opts, .option_count=g_display_count, .value=0 };
    g_video[V_MODE] = (zcsr_ms_item){
        .label="Display Mode", .description="Windowed: normal window (taskbar, Alt-Tab). Borderless: covers the whole monitor without a frame. Fullscreen: exclusive fullscreen.",
        .type=ZCSR_MS_DROPDOWN, .options=mode_opts, .option_count=3, .value=0 };
    g_video[V_RESOLUTION] = (zcsr_ms_item){
        .label="Resolution", .description="Window client resolution (applies in Windowed mode).",
        .type=ZCSR_MS_DROPDOWN, .options=res_opts, .option_count=5, .value=0 };
    g_video[V_REFRESH] = (zcsr_ms_item){
        .label="Refresh Rate", .description="Preferred display refresh rate.",
        .type=ZCSR_MS_DROPDOWN, .options=refresh_opts, .option_count=6, .value=0 };
    g_video[V_VSYNC] = (zcsr_ms_item){
        .label="V-Sync", .description="Synchronize the frame rate with the display.",
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
        .label="Default FOV", .description="Field of view in degrees.",
        .type=ZCSR_MS_SLIDER, .min=60.0f,.max=120.0f,.step=1.0f,.slider_value=90.0f };
    g_video[V_SHARPNESS] = (zcsr_ms_item){
        .label="Sharpness", .description="Post-process sharpening filter strength.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=50.0f };

    g_graphics[G_PRESET] = (zcsr_ms_item){
        .label="Quality Preset", .description="Global graphics quality preset.",
        .type=ZCSR_MS_DROPDOWN, .options=preset_opts, .option_count=4, .value=2 };
    g_graphics[G_TEXTURE] = (zcsr_ms_item){
        .label="Texture Quality", .description="Texture resolution and filtering quality.",
        .type=ZCSR_MS_DROPDOWN, .options=tex_opts, .option_count=4, .value=2 };
    g_graphics[G_SHADOW] = (zcsr_ms_item){
        .label="Shadow Quality", .description="Shadow map resolution and filtering.",
        .type=ZCSR_MS_DROPDOWN, .options=shadow_opts, .option_count=4, .value=2 };
    g_graphics[G_AA] = (zcsr_ms_item){
        .label="Anti-Aliasing", .description="Smooths jagged edges.",
        .type=ZCSR_MS_DROPDOWN, .options=aa_opts, .option_count=5, .value=1 };
    g_graphics[G_EFFECTS] = (zcsr_ms_item){
        .label="Effects Quality", .description="Particle effects and weather visual quality.",
        .type=ZCSR_MS_DROPDOWN, .options=effect_opts, .option_count=3, .value=1 };
    g_graphics[G_POST] = (zcsr_ms_item){
        .label="Post Processing", .description="Bloom, depth of field, color grading.",
        .type=ZCSR_MS_DROPDOWN, .options=post_opts, .option_count=4, .value=1 };
    g_graphics[G_VIEWDIST] = (zcsr_ms_item){
        .label="View Distance", .description="Maximum distance at which objects render.",
        .type=ZCSR_MS_SLIDER, .min=10.0f,.max=100.0f,.step=1.0f,.slider_value=70.0f };

    g_audio[A_MASTER] = (zcsr_ms_item){
        .label="Master Volume", .description="Overall audio output volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=80.0f };
    g_audio[A_MUSIC] = (zcsr_ms_item){
        .label="Music Volume", .description="Background music volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=60.0f };
    g_audio[A_SFX] = (zcsr_ms_item){
        .label="SFX Volume", .description="Sound effects volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=90.0f };
    g_audio[A_VOICE] = (zcsr_ms_item){
        .label="Voice Volume", .description="Voice chat and character dialogue volume.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=100.0f,.step=1.0f,.slider_value=100.0f };
    g_audio[A_SEP_DEV] = (zcsr_ms_item){
        .label="--- Devices ---", .type=ZCSR_MS_SEPARATOR };
    g_audio[A_OUTPUT] = (zcsr_ms_item){
        .label="Output Device", .description="Audio output device.",
        .type=ZCSR_MS_DROPDOWN, .options=outdev_opts, .option_count=3, .value=0 };
    g_audio[A_INPUT] = (zcsr_ms_item){
        .label="Input Device", .description="Microphone / audio input device.",
        .type=ZCSR_MS_DROPDOWN, .options=indev_opts, .option_count=3, .value=0 };

    g_interface[I_OPACITY] = (zcsr_ms_item){
        .label="Settings Opacity", .description="How opaque the settings overlay panel is over the page. Takes effect on Apply.",
        .type=ZCSR_MS_SLIDER, .min=30.0f,.max=100.0f,.step=5.0f,.slider_value=100.0f };
    g_interface[I_DIM] = (zcsr_ms_item){
        .label="Background Dim", .description="How dark the page behind the settings overlay is. Takes effect on Apply.",
        .type=ZCSR_MS_SLIDER, .min=0.0f,.max=90.0f,.step=1.0f,.slider_value=62.0f };
}

/* ================================================================== */
/*  Shell state                                                        */
/* ================================================================== */
static unsigned char renderer_buf[8 * 1024 * 1024];
static unsigned char input_buf[4 * 1024];
static unsigned char inputext_buf[8 * 1024];
static unsigned char settings_buf[8192];
static unsigned char panel_zero[SHELL_PANEL_TEX_MAX_W * SHELL_PANEL_TEX_MAX_H * 4];

static zcsr_surface*     w_surface  = NULL;
static zcsr_gl_renderer* w_renderer = NULL;
static zcsr_input*       w_input    = NULL;
static zcsr_input_ext*   w_inputext = NULL;
static zcsr_ms*          w_settings = NULL;

/* Mouse-position bridge for modules (read-only). Lets a module follow the cursor
 * without changing the zcsr_window_module callback ABI. */
static int g_zcsr_window_mouse_x = 0;
static int g_zcsr_window_mouse_y = 0;
void zcsr_window_get_mouse_position(int* x, int* y) {
    if (x) *x = g_zcsr_window_mouse_x;
    if (y) *y = g_zcsr_window_mouse_y;
}

/* Offscreen composite source for the settings panel (opaque content; the adjustable
 * transparency happens at composite time via sprite color alpha). */
static zcsr_gl_texture w_panel_tex = 0;
static int w_panel_tex_w = 0;
static int w_panel_tex_h = 0;

static const zcsr_window_module* w_modules = NULL;
static int w_module_count = 0;
static bool w_module_inited[SHELL_MAX_MODULES];

static bool w_running = true;
static int  w_active_module = -1;   /* -1 = home screen */
static bool w_settings_open = false;
static int  w_cw = SHELL_W, w_ch = SHELL_H;
static unsigned char w_prev_mouse_buttons = 0;
static bool w_autoquit = false;
static int  w_frame = 0;

/* Settings overlay geometry: panel occupies the main window (margins), clamped to
 * the offscreen texture cap. Same layout everywhere (render, mouse, resize). */
static void layout_settings_overlay(int* px, int* py, int* pw, int* ph) {
    int w = w_cw - 80;
    int h = w_ch - 100;
    if (w > SHELL_PANEL_TEX_MAX_W) w = SHELL_PANEL_TEX_MAX_W;
    if (h > SHELL_PANEL_TEX_MAX_H) h = SHELL_PANEL_TEX_MAX_H;
    if (w < 320) w = 320;
    if (h < 200) h = 200;
    *pw = w;
    *ph = h;
    *px = (w_cw - w) / 2;
    *py = (w_ch - h) / 2;
}

/* Home-screen big buttons: [0..module_count-1] modules, then SETTINGS, then EXIT. */
static int home_button_count(void) { return w_module_count + 2; }

static void home_button_rect(int index, float* x, float* y, float* bw, float* bh) {
    *x = 90.0f;
    *y = 150.0f + (float)index * 82.0f;
    *bw = 360.0f;
    *bh = 64.0f;
}

static bool point_in_rect(int px, int py, float x, float y, float bw, float bh) {
    float fx = (float)px;
    float fy = (float)py;
    return fx >= x && fx <= x + bw && fy >= y && fy <= y + bh;
}

/* Back button shown while a module page is active. */
static void home_back_rect(float* x, float* y, float* bw, float* bh) {
    *x = 12.0f;
    *y = 10.0f;
    *bw = 110.0f;
    *bh = 30.0f;
}

/* ================================================================== */
/*  Settings apply / reset / overlay open-close                        */
/* ================================================================== */
static void apply_settings(void) {
    int mode = g_video[V_MODE].value;
    zcsr_window_mode wm = ZCSR_WINDOW_WINDOWED;
    if (mode == 1) wm = ZCSR_WINDOW_BORDERLESS;
    else if (mode == 2) wm = ZCSR_WINDOW_FULLSCREEN;
    zcsr_surface_set_mode(w_surface, wm);
    printf("[apply] display mode: %s\n", mode_opts[mode]);

    if (mode == 0) {
        static const int rw[] = { 1280, 1600, 1920, 2560, 3840 };
        static const int rh[] = { 720, 900, 1080, 1440, 2160 };
        int ri = g_video[V_RESOLUTION].value;
        zcsr_surface_set_client_size(w_surface, rw[ri], rh[ri]);
        printf("[apply] resolution: %dx%d\n", rw[ri], rh[ri]);
    }

    {
        int di = g_video[V_DISPLAY].value;
        if (di < g_display_count) {
            zcsr_display_info info;
            if (zcsr_display_get(di, &info)) {
                zcsr_surface_move_to_display(w_surface, info.id);
                printf("[apply] display: %s\n", info.name);
            }
        }
    }

    zcsr_gl_set_vsync(w_renderer, g_video[V_VSYNC].toggle_value);
    printf("[apply] vsync: %s\n", g_video[V_VSYNC].toggle_value ? "On" : "Off");

    printf("[apply] interface: settings opacity %.0f%% dim %.0f%%\n",
           g_interface[I_OPACITY].slider_value, g_interface[I_DIM].slider_value);

    {
        int cw = 0, ch = 0;
        zcsr_surface_get_client_size(w_surface, &cw, &ch);
        if (cw > 0 && ch > 0) { w_cw = cw; w_ch = ch; }
        printf("[apply] client size now %dx%d\n", w_cw, w_ch);
    }

    printf("[apply] graphics/audio values applied (pipeline print-only)\n");
    printf("[apply] ---\n");
}

static void reset_defaults(void) {
    g_video[V_DISPLAY].value    = 0;
    g_video[V_MODE].value       = 0; /* Windowed */
    g_video[V_RESOLUTION].value = 0;
    g_video[V_REFRESH].value    = 0;
    g_video[V_VSYNC].toggle_value   = true;
    g_video[V_FRAMELIMIT].value = 4;
    g_video[V_BRIGHTNESS].slider_value = 50.0f;
    g_video[V_FOV].slider_value     = 90.0f;
    g_video[V_SHARPNESS].slider_value = 50.0f;

    g_graphics[G_PRESET].value  = 2;
    g_graphics[G_TEXTURE].value = 2;
    g_graphics[G_SHADOW].value  = 2;
    g_graphics[G_AA].value      = 1;
    g_graphics[G_EFFECTS].value = 1;
    g_graphics[G_POST].value    = 1;
    g_graphics[G_VIEWDIST].slider_value = 70.0f;

    g_audio[A_MASTER].slider_value = 80.0f;
    g_audio[A_MUSIC].slider_value  = 60.0f;
    g_audio[A_SFX].slider_value    = 90.0f;
    g_audio[A_VOICE].slider_value  = 100.0f;
    g_audio[A_OUTPUT].value = 0;
    g_audio[A_INPUT].value  = 0;

    g_interface[I_OPACITY].slider_value = 100.0f;
    g_interface[I_DIM].slider_value = 62.0f;

    zcsr_ms_reset(w_settings);
    printf("[reset] defaults restored\n");
}

/* Makes sure the offscreen panel texture exists at the overlay layout size. */
static void panel_tex_ensure(void) {
    int px, py, pw, ph;
    layout_settings_overlay(&px, &py, &pw, &ph);
    if (pw * ph * 4 > (int)sizeof(panel_zero)) return; /* layout guarantees the cap */
    if (w_panel_tex != 0 && w_panel_tex_w == pw && w_panel_tex_h == ph) return;
    if (w_panel_tex == 0) {
        w_panel_tex = zcsr_gl_texture_create(w_renderer, panel_zero, pw, ph);
        if (w_panel_tex != 0) zcsr_gl_texture_filter(w_renderer, w_panel_tex, true);
    } else if (!zcsr_gl_texture_upload(w_renderer, w_panel_tex, panel_zero, pw, ph)) {
        return;
    }
    if (w_panel_tex != 0) {
        w_panel_tex_w = pw;
        w_panel_tex_h = ph;
    }
}

static void open_settings_overlay(void) {
    int px, py, pw, ph;
    layout_settings_overlay(&px, &py, &pw, &ph);
    panel_tex_ensure();
    w_settings_open = true;
    printf("[shell] settings open\n");
}

static void close_settings(bool apply) {
    if (apply) {
        zcsr_ms_apply(w_settings);
        apply_settings();
    } else {
        zcsr_ms_cancel(w_settings);
    }
    w_settings_open = false;
    printf("[shell] settings closed (%s)\n", apply ? "applied" : "cancelled");
}

/* ================================================================== */
/*  Rendering: home screen / module chrome / settings overlay          */
/* ================================================================== */
static void draw_button(zcsr_gl_renderer* r, float x, float y, float bw, float bh,
                        const char* text, bool hovered) {
    zcsr_gl_fill_rect(r, x, y, bw, bh,
                      hovered ? SHELL_BTN_HOV[0] : SHELL_BTN[0],
                      hovered ? SHELL_BTN_HOV[1] : SHELL_BTN[1],
                      hovered ? SHELL_BTN_HOV[2] : SHELL_BTN[2], 1.0f);
    /* accent stripe on the left edge */
    zcsr_gl_fill_rect(r, x, y, 6.0f, bh,
                      SHELL_ACCENT[0], SHELL_ACCENT[1], SHELL_ACCENT[2], hovered ? 1.0f : 0.65f);
    zcsr_gl_draw_text(r, text, x + 26.0f, y + bh * 0.5f - 8.0f, 2.0f,
                      SHELL_TEXT[0], SHELL_TEXT[1], SHELL_TEXT[2], 1.0f);
}

static void render_home(zcsr_gl_renderer* r, int mx, int my) {
    int i;
    int total = home_button_count();
    zcsr_gl_draw_text(r, "ZCSR // INTEGRATION SHELL", 24.0f, 48.0f, 2.4f,
                      SHELL_ACCENT[0], SHELL_ACCENT[1], SHELL_ACCENT[2], 1.0f);
    zcsr_gl_draw_text(r, "window skeleton v0 (id pending)", 24.0f, 84.0f, 1.0f,
                      0.45f, 0.47f, 0.52f, 1.0f);

    for (i = 0; i < total; ++i) {
        float x, y, bw, bh;
        const char* text;
        bool hovered;
        home_button_rect(i, &x, &y, &bw, &bh);
        if (i < w_module_count) text = w_modules[i].title;
        else if (i == w_module_count) text = "SETTINGS";
        else text = "EXIT";
        hovered = w_settings_open ? false : point_in_rect(mx, my, x, y, bw, bh);
        draw_button(r, x, y, bw, bh, text, hovered);
    }

    zcsr_gl_draw_text(r,
        "click a category | S: settings | ESC: quit",
        24.0f, (float)w_ch - 26.0f, 1.0f, 0.42f, 0.44f, 0.48f, 1.0f);
}

static void render_module_chrome(zcsr_gl_renderer* r, int mx, int my, int module_index) {
    float x, y, bw, bh;
    bool hovered;
    home_back_rect(&x, &y, &bw, &bh);
    hovered = w_settings_open ? false : point_in_rect(mx, my, x, y, bw, bh);
    zcsr_gl_fill_rect(r, x, y, bw, bh,
                      hovered ? SHELL_BTN_HOV[0] : SHELL_BTN[0],
                      hovered ? SHELL_BTN_HOV[1] : SHELL_BTN[1],
                      hovered ? SHELL_BTN_HOV[2] : SHELL_BTN[2], 1.0f);
    zcsr_gl_draw_text(r, "<< HOME", x + 12.0f, y + 11.0f, 1.0f,
                      SHELL_TEXT[0], SHELL_TEXT[1], SHELL_TEXT[2], 1.0f);
    if (module_index >= 0 && module_index < w_module_count) {
        zcsr_gl_draw_text(r, w_modules[module_index].title, 140.0f, 14.0f, 1.6f,
                          SHELL_ACCENT[0], SHELL_ACCENT[1], SHELL_ACCENT[2], 1.0f);
    }
}

/* Renders the settings overlay over the (already queued) page content. The sprite
 * queue is one batch, so the page is flushed to the window FIRST; the panel is then
 * drawn into the offscreen texture (queue is empty at that point), and finally the
 * dim layer + composite sprite are queued for the caller's final flush. All public
 * renderer APIs, all shell-internal. */
static void render_settings_overlay(zcsr_gl_renderer* r) {
    int px, py, pw, ph;
    float dim, opacity;

    layout_settings_overlay(&px, &py, &pw, &ph);
    dim = g_interface[I_DIM].slider_value * 0.01f;
    opacity = g_interface[I_OPACITY].slider_value * 0.01f;

    if (w_panel_tex == 0 || w_panel_tex_w != pw || w_panel_tex_h != ph) {
        panel_tex_ensure();
    }

    if (w_panel_tex == 0) {
        /* fallback: flush the page, then render the panel directly (no translucency) */
        zcsr_gl_flush(r);
        zcsr_ms_set_bounds(w_settings, px, py, pw, ph);
        zcsr_ms_render(w_settings, r);
        return;
    }

    /* 1) page content -> window */
    zcsr_gl_flush(r);

    /* 2) dim layer over the page -> window */
    zcsr_gl_fill_rect(r, 0.0f, 0.0f, (float)w_cw, (float)w_ch, 0.0f, 0.0f, 0.0f, dim);
    zcsr_gl_flush(r);

    /* 3) panel -> offscreen texture (rendered from (0,0,pw,ph); queue is empty here) */
    zcsr_ms_set_bounds(w_settings, 0, 0, pw, ph);
    zcsr_gl_set_target(r, w_panel_tex);
    zcsr_gl_fill_rect(r, 0.0f, 0.0f, (float)pw, (float)ph, 0.0f, 0.0f, 0.0f, 1.0f);
    zcsr_ms_render(w_settings, r);
    zcsr_gl_flush(r);
    zcsr_gl_set_target(r, 0);

    /* 4) composite at the adjustable opacity (queued; caller flushes).
     * FBO round-trip flip: the panel was rendered into the texture with the
     * renderer's y-down projection, so the image's TOP row sits at texture v=1
     * while a sprite quad samples its top edge at v0 — flip v to draw upright. */
    zcsr_gl_queue_atlas_sprite(r, w_panel_tex,
                               (float)px, (float)py, (float)pw, (float)ph,
                               0.0f, 1.0f, 1.0f, 0.0f,
                               1.0f, 1.0f, 1.0f, opacity);
}

/* ================================================================== */
/*  Shell main loop                                                    */
/* ================================================================== */
int zcsr_window_main(const zcsr_window_module* modules, int module_count,
                     int argc, char** argv) {
    static zcsr_ms_tab tabs[4];
    int i;

    w_autoquit = (argc > 1 && argv != NULL && argv[1] != NULL &&
                  strcmp(argv[1], "autoquit") == 0);
    if (module_count > SHELL_MAX_MODULES) module_count = SHELL_MAX_MODULES;
    w_modules = modules;
    w_module_count = module_count;
    for (i = 0; i < module_count; ++i) w_module_inited[i] = false;

    printf("=== zcsr window shell ===\n");
    printf("black home screen + category buttons; SETTINGS built in; %d module(s)\n",
           module_count);

    init_display_list();
    init_items();

    w_surface = zcsr_surface_create("zcsr window shell",
                                    (zcsr_rect){ 80, 80, SHELL_W, SHELL_H });
    if (!w_surface) { fprintf(stderr, "FAIL: no surface\n"); return 1; }

    /* The platform default is a topmost fullscreen overlay (no taskbar, no Alt-Tab,
     * cursor feels lost). Switch to a NORMAL window immediately: taskbar icon,
     * Alt-Tab switchable, never topmost, visible mouse cursor. */
    if (!zcsr_surface_set_mode(w_surface, ZCSR_WINDOW_WINDOWED)) {
        fprintf(stderr, "WARN: windowed mode switch failed\n");
    }
    zcsr_surface_set_client_size(w_surface, SHELL_W, SHELL_H);

    w_renderer = zcsr_gl_create(w_surface, renderer_buf, sizeof(renderer_buf));
    if (!w_renderer) {
        fprintf(stderr, "FAIL: no renderer\n");
        zcsr_surface_destroy(w_surface);
        return 1;
    }
    zcsr_gl_set_vsync(w_renderer, true);

    w_input = zcsr_input_init(input_buf, sizeof(input_buf));
    if (!w_input) { fprintf(stderr, "FAIL: no input\n"); goto cleanup; }

    w_inputext = zcsr_input_ext_init(inputext_buf, sizeof(inputext_buf));
    if (!w_inputext) { fprintf(stderr, "FAIL: no input_ext\n"); goto cleanup; }

    zcsr_surface_get_client_size(w_surface, &w_cw, &w_ch);
    if (w_cw <= 0 || w_ch <= 0) { w_cw = SHELL_W; w_ch = SHELL_H; }

    tabs[0] = (zcsr_ms_tab){ .name="Video",     .items=g_video,     .count=V_COUNT };
    tabs[1] = (zcsr_ms_tab){ .name="Graphics",  .items=g_graphics,  .count=G_COUNT };
    tabs[2] = (zcsr_ms_tab){ .name="Audio",     .items=g_audio,     .count=A_COUNT };
    tabs[3] = (zcsr_ms_tab){ .name="Interface", .items=g_interface, .count=I_COUNT };
    w_settings = zcsr_ms_create(settings_buf, sizeof(settings_buf),
        &(zcsr_ms_config){
            .tabs        = tabs,
            .tab_count   = 4,
            .x           = 200,
            .y           = 90,
            .width       = 880,
            .height      = 540,
            .item_height = 34,
            .text_scale  = 1.4f,
        });
    if (!w_settings) { fprintf(stderr, "FAIL: settings create\n"); goto cleanup; }

    panel_tex_ensure();

    printf("Ready. window %dx%d, %d display(s)\n", w_cw, w_ch, g_display_count);
    fflush(stdout);

    while (w_running) {
        zcsr_key_event ev;
        zcsr_mouse_state mst;

        zcsr_input_pump(w_input, w_surface);
        zcsr_input_ext_pump(w_inputext, w_surface);
        zcsr_surface_pump_events(w_surface);
        if (zcsr_window_close_requested(w_surface)) w_running = false;

        {
            int cw = 0, ch = 0;
            zcsr_surface_get_client_size(w_surface, &cw, &ch);
            if (cw > 0 && ch > 0 && (cw != w_cw || ch != w_ch)) {
                w_cw = cw;
                w_ch = ch;
                if (w_settings_open) panel_tex_ensure(); /* re-layout the overlay */
            }
        }

        mst = zcsr_input_ext_mouse(w_inputext);
        g_zcsr_window_mouse_x = mst.x;
        g_zcsr_window_mouse_y = mst.y;

        /* ---- mouse routing ---- */
        if (w_settings_open) {
            /* panel logic runs in overlay-local coordinates (rendered from (0,0)) */
            int px, py, pw, ph;
            layout_settings_overlay(&px, &py, &pw, &ph);
            zcsr_mouse_state local = mst;
            local.x = mst.x - px;
            local.y = mst.y - py;
            {
                int maction = zcsr_ms_update_mouse(w_settings, &local);
                if (maction == 1) close_settings(true);
                else if (maction == -1) close_settings(false);
            }
        } else {
            bool clicked = (mst.buttons & ZCSR_MOUSE_LEFT) != 0 &&
                           (w_prev_mouse_buttons & ZCSR_MOUSE_LEFT) == 0;
            if (clicked) {
                if (w_active_module < 0) {
                    int total = home_button_count();
                    for (i = 0; i < total; ++i) {
                        float x, y, bw, bh;
                        home_button_rect(i, &x, &y, &bw, &bh);
                        if (point_in_rect(mst.x, mst.y, x, y, bw, bh)) {
                            if (i < w_module_count) {
                                w_active_module = i;
                                printf("[shell] enter module: %s\n", w_modules[i].title);
                            } else if (i == w_module_count) {
                                open_settings_overlay();
                            } else {
                                w_running = false;
                            }
                            break;
                        }
                    }
                } else {
                    float x, y, bw, bh;
                    home_back_rect(&x, &y, &bw, &bh);
                    if (point_in_rect(mst.x, mst.y, x, y, bw, bh)) {
                        printf("[shell] back to home\n");
                        w_active_module = -1;
                    }
                }
            }
        }
        w_prev_mouse_buttons = mst.buttons;

        /* ---- keyboard routing ---- */
        while (zcsr_input_poll(w_input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;
            if (w_settings_open) {
                if (ev.key == 'R' || ev.key == 'r') { reset_defaults(); continue; }
                {
                    int result = zcsr_ms_handle_key(w_settings, ev.key, ev.action);
                    if (result == 1) close_settings(true);
                    else if (result == -1) close_settings(false);
                }
                continue;
            }
            if (w_active_module < 0) {
                if (ev.key == KEY_ESCAPE) { w_running = false; continue; }
                if (ev.key == 'S' || ev.key == 's') { open_settings_overlay(); continue; }
            } else {
                bool consumed = false;
                if (w_modules[w_active_module].key != NULL) {
                    consumed = w_modules[w_active_module].key((int)ev.key,
                                                             w_modules[w_active_module].user);
                }
                if (!consumed && ev.key == KEY_ESCAPE) {
                    printf("[shell] back to home\n");
                    w_active_module = -1;
                }
            }
        }

        /* ---- autoquit self-test script ---- */
        if (w_autoquit && w_frame == 20) {
            w_active_module = 0;
            printf("autoquit: enter module 0\n");
        }
        if (w_autoquit && w_frame == 40) {
            if (w_modules[0].key != NULL) {
                (void)w_modules[0].key(KEY_SPACE, w_modules[0].user);
                printf("autoquit: sent SPACE to module\n");
            }
        }
        if (w_autoquit && w_frame == 60) open_settings_overlay();
        if (w_autoquit && w_frame == 90) close_settings(false);
        if (w_autoquit && w_frame >= 150) w_running = false;

        /* ---- render ---- */
        zcsr_gl_begin(w_renderer);
        zcsr_gl_clear(w_renderer, SHELL_BG[0], SHELL_BG[1], SHELL_BG[2], 1.0f);

        if (w_active_module < 0) {
            render_home(w_renderer, mst.x, mst.y);
        } else {
            if (!w_module_inited[w_active_module] &&
                w_modules[w_active_module].init != NULL) {
                if (!w_modules[w_active_module].init(w_renderer, w_cw, w_ch,
                                                    w_modules[w_active_module].user)) {
                    fprintf(stderr, "FAIL: module init\n");
                    w_running = false;
                    break;
                }
                w_module_inited[w_active_module] = true;
            }
            if (w_modules[w_active_module].frame != NULL) {
                w_modules[w_active_module].frame(w_renderer, w_cw, w_ch,
                                                 w_modules[w_active_module].user);
            }
            render_module_chrome(w_renderer, mst.x, mst.y, w_active_module);
        }

        if (w_settings_open) {
            render_settings_overlay(w_renderer);
        }

        zcsr_gl_flush(w_renderer);
        zcsr_gl_present(w_renderer);
        shell_sleep_ms(16);
        ++w_frame;
    }

cleanup:
    for (i = 0; i < w_module_count; ++i) {
        if (w_module_inited[i] && w_modules[i].shutdown != NULL) {
            w_modules[i].shutdown(w_renderer, w_modules[i].user);
        }
    }
    if (w_panel_tex != 0) zcsr_gl_texture_destroy(w_renderer, w_panel_tex);
    if (w_settings) zcsr_ms_destroy(w_settings);
    if (w_inputext) zcsr_input_ext_shutdown(w_inputext);
    if (w_renderer) zcsr_gl_destroy(w_renderer);
    if (w_surface) zcsr_surface_destroy(w_surface);
    printf("=== window shell ended (%d frames) ===\n", w_frame);
    return 0;
}
