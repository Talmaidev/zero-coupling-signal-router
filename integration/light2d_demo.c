/* light2d_demo.c — LOW (forward additive) vs HIGH (deferred G-buffer) comparison demo.
 *
 * SPACE toggles LOW/HIGH between frames, ESC or window close quits.
 *
 * LOW:  the scene is drawn to the window with the renderer between begin_geometry and
 *       begin_lighting; three colored lights (warm / cold / purple) are then added as
 *       radial-falloff quads on top of it.
 * HIGH: begin_geometry binds the four-attachment G-buffer. The renderer's sprite path
 *       cannot target it (its flush hardcodes the window FBO), so this demo stands in for
 *       the "caller geometry shaders" the contract asks for: a tiny GL program (built on
 *       the vendored glad function pointers zcsr_gl_create already loaded) paints a solid
 *       white rectangle into draw buffer 0 (Albedo). Position/Normal/Emissive keep their
 *       neutral clears, so the deferred N.L term reads its flat 0.45 base factor and the
 *       lights modulate only the white albedo region — black outside, as a deferred
 *       pipeline should.
 */
#include "zcsr/platform.h"
#include "zcsr/gl_render.h"
#include "zcsr/input.h"
#include "zcsr/window_event.h"
#include "zcsr/light2d.h"

#include <glad/glad.h> /* vendored loader; pointers are live after zcsr_gl_create */

#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
static void demo_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#  include <unistd.h>
static void demo_sleep_ms(int ms) { usleep((useconds_t)(ms * 1000)); }
#endif

#if defined(__ANDROID__)
#  define DEMO_GLSL_VERSION "#version 300 es\n"
#  define DEMO_GLSL_FS_PRECISION "precision highp float;\n"
#else
#  define DEMO_GLSL_VERSION "#version 330 core\n"
#  define DEMO_GLSL_FS_PRECISION ""
#endif

#define WIN_W 960
#define WIN_H 540
#define KEY_ESCAPE 0x1B
#define KEY_SPACE  0x20

static unsigned char renderer_buf[8 * 1024 * 1024];
static unsigned char input_buf[8 * 1024];
static unsigned char light2d_buf[64 * 1024];

/* ---- minimal caller geometry program: solid white rect, y-down pixel space.
 * Writes the four G-buffer outputs; the position attachment carries WORLD 3D
 * coordinates derived through the camera uniforms (default camera: origin
 * centered, 128 px per unit). ---------- */

static const char* demo_rect_vs =
    DEMO_GLSL_VERSION
    "layout(location=0) in vec2 a_pos;\n"
    "uniform vec2 u_view;\n"
    "void main(){\n"
    "  gl_Position = vec4((a_pos.x / u_view.x) * 2.0 - 1.0, 1.0 - (a_pos.y / u_view.y) * 2.0, 0.0, 1.0);\n"
    "}\n";
static const char* demo_rect_fs =
    DEMO_GLSL_VERSION
    DEMO_GLSL_FS_PRECISION
    "uniform vec2 u_view;\n"
    "uniform vec2 u_cam;\n"
    "uniform float u_ppu;\n"
    "layout(location=0) out vec4 o_albedo;\n"
    "layout(location=1) out vec4 o_position;\n"
    "layout(location=2) out vec4 o_normal;\n"
    "layout(location=3) out vec4 o_emissive;\n"
    "void main(){\n"
    "  o_albedo = vec4(1.0, 1.0, 1.0, 1.0);\n"
    "  vec2 frag_px = vec2(gl_FragCoord.x, u_view.y - gl_FragCoord.y);\n"
    "  o_position = vec4((frag_px - u_view * 0.5) / u_ppu + u_cam, 0.0, 1.0);\n"
    "  o_normal = vec4(0.5, 0.5, 0.0, 1.0);\n"
    "  o_emissive = vec4(0.0, 0.0, 0.0, 0.0);\n"
    "}\n";

static unsigned int demo_compile_shader(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    int ok = 0;
    if (!shader) return 0;
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        if (len < 0) len = 0;
        if (len >= (GLsizei)sizeof(log)) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        fprintf(stderr, "demo shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int demo_make_rect_pipeline(unsigned int* prog, unsigned int* vao, unsigned int* vbo,
                                   int* loc_view, int* loc_cam, int* loc_ppu) {
    unsigned int v = demo_compile_shader(GL_VERTEX_SHADER, demo_rect_vs);
    unsigned int f = demo_compile_shader(GL_FRAGMENT_SHADER, demo_rect_fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
    if (!p) {
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), &len, log);
        if (len < 0) len = 0;
        if (len >= (GLsizei)sizeof(log)) len = (GLsizei)sizeof(log) - 1;
        log[len] = '\0';
        fprintf(stderr, "demo program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    *prog = p;
    *loc_view = glGetUniformLocation(p, "u_view");
    *loc_cam = glGetUniformLocation(p, "u_cam");
    *loc_ppu = glGetUniformLocation(p, "u_ppu");
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);
    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return 1;
}

/* Paints a solid white rectangle into the G-buffer during the HIGH geometry
 * phase (all four outputs; world position via the default camera). Saves and
 * restores the VAO and ARRAY_BUFFER bindings it touches. */
static void demo_paint_albedo_rect(unsigned int prog, unsigned int vao, unsigned int vbo,
                                   int loc_view, int loc_cam, int loc_ppu,
                                   float x, float y, float w, float h) {
    float x1 = x + w;
    float y1 = y + h;
    const float verts[12] = {
        x,  y,  x1, y,  x,  y1,
        x,  y1, x1, y,  x1, y1
    };
    GLint vao_was = 0;
    GLint buf_was = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_was);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf_was);

    glUseProgram(prog);
    if (loc_view >= 0) glUniform2f(loc_view, (float)WIN_W, (float)WIN_H);
    if (loc_cam >= 0) glUniform2f(loc_cam, 0.0f, 0.0f);
    if (loc_ppu >= 0) glUniform1f(loc_ppu, 128.0f);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray((GLuint)vao_was);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buf_was);
}

int main(int argc, char** argv) {
    const int autoquit = (argc > 1 && argv[1] != NULL &&
                          strcmp(argv[1], "autoquit") == 0);
    zcsr_surface* window;
    zcsr_gl_renderer* renderer;
    zcsr_input* input;
    zcsr_light2d_ctx* lights;
    zcsr_light_quality quality = ZCSR_LIGHT_QUALITY_LOW;
    zcsr_light warm;
    zcsr_light cold;
    zcsr_light purple;
    unsigned int rect_prog = 0;
    unsigned int rect_vao = 0;
    unsigned int rect_vbo = 0;
    int rect_loc_view = -1;
    int rect_loc_cam = -1;
    int rect_loc_ppu = -1;
    bool running = true;
    int frame = 0;

    printf("zcsr light2d demo — SPACE toggles LOW/HIGH, ESC quits\n");

    if (sizeof(light2d_buf) <
        zcsr_light2d_buffer_bytes_required(ZCSR_LIGHT_QUALITY_HIGH)) {
        fprintf(stderr, "FAIL: light2d buffer too small\n");
        return 1;
    }

    window = zcsr_surface_create("zcsr light2d demo", (zcsr_rect){ 0, 0, WIN_W, WIN_H });
    if (!window) {
        fprintf(stderr, "FAIL: no window surface (headless)\n");
        return 1;
    }

    renderer = zcsr_gl_create(window, renderer_buf, sizeof(renderer_buf));
    if (!renderer) {
        fprintf(stderr, "FAIL: GL renderer unavailable\n");
        zcsr_surface_destroy(window);
        return 1;
    }
    zcsr_gl_set_vsync(renderer, false);

    input = zcsr_input_init(input_buf, sizeof(input_buf));
    if (!input) {
        fprintf(stderr, "FAIL: input init failed\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(window);
        return 1;
    }

    lights = zcsr_light2d_create(renderer, quality, ZCSR_LIGHT_QUALITY_HIGH,
                                 WIN_W, WIN_H, light2d_buf, sizeof(light2d_buf));
    if (!lights) {
        fprintf(stderr, "FAIL: light2d context create failed\n");
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(window);
        return 1;
    }

    if (!demo_make_rect_pipeline(&rect_prog, &rect_vao, &rect_vbo,
                                 &rect_loc_view, &rect_loc_cam, &rect_loc_ppu)) {
        fprintf(stderr, "FAIL: demo rect pipeline failed\n");
        zcsr_light2d_destroy(lights);
        zcsr_gl_destroy(renderer);
        zcsr_surface_destroy(window);
        return 1;
    }

    printf("Renderer + light2d ready. SPACE toggles quality, ESC quits.\n");

    /* Static per-light fields (type/direction) never change frame to frame. */
    memset(&warm, 0, sizeof(warm));
    memset(&cold, 0, sizeof(cold));
    memset(&purple, 0, sizeof(purple));
    warm.type = ZCSR_LIGHT_POINT;
    cold.type = ZCSR_LIGHT_POINT;
    purple.type = ZCSR_LIGHT_POINT;
    warm.dir_x = 0.0f; warm.dir_y = -1.0f; warm.dir_z = 1.0f;
    cold.dir_x = 0.0f; cold.dir_y = -1.0f; cold.dir_z = 1.0f;
    purple.dir_x = 0.0f; purple.dir_y = -1.0f; purple.dir_z = 1.0f;

    while (running) {
        zcsr_key_event ev;
        float t;

        zcsr_input_pump(input, window);
        zcsr_surface_pump_events(window);
        if (zcsr_window_close_requested(window)) running = false;
        while (zcsr_input_poll(input, &ev)) {
            if (ev.action != ZCSR_KEY_DOWN) continue;
            if (ev.key == KEY_ESCAPE) running = false;
            if (ev.key == KEY_SPACE) {
                zcsr_light_quality next = (quality == ZCSR_LIGHT_QUALITY_LOW)
                    ? ZCSR_LIGHT_QUALITY_HIGH
                    : ZCSR_LIGHT_QUALITY_LOW;
                if (zcsr_light2d_set_quality(lights, next)) {
                    quality = next;
                    printf("quality -> %s\n",
                           quality == ZCSR_LIGHT_QUALITY_LOW ? "LOW" : "HIGH");
                } else {
                    printf("set_quality rejected\n");
                }
            }
        }

        if (autoquit && frame == 60) {
            if (zcsr_light2d_set_quality(lights, ZCSR_LIGHT_QUALITY_HIGH)) {
                quality = ZCSR_LIGHT_QUALITY_HIGH;
                printf("autoquit: quality -> HIGH\n");
            } else {
                fprintf(stderr, "FAIL: autoquit quality toggle rejected\n");
            }
        }
        if (autoquit && frame >= 150) running = false;

        t = (float)frame * 0.016f;
        /* WORLD units through the default camera (origin centered, 128 px/u):
         * px -> world = (px - WIN/2) / 128. */
        warm.x = -1.875f + 0.86f * sinf(t * 0.60f);
        warm.y = -0.156f + 0.94f * cosf(t * 0.42f);
        warm.height = 0.47f;
        warm.radius = 1.8f;
        warm.decay = 1.6f;
        warm.r = 1.00f; warm.g = 0.68f; warm.b = 0.32f; warm.a = 1.0f;
        cold.x = 1.875f + 0.94f * cosf(t * 0.50f);
        cold.y = 0.0f + 0.86f * sinf(t * 0.66f);
        cold.height = 0.47f;
        cold.radius = 1.64f;
        cold.decay = 2.2f;
        cold.r = 0.34f; cold.g = 0.60f; cold.b = 1.00f; cold.a = 1.0f;
        purple.x = 0.0f + 1.95f * sinf(t * 0.30f + 1.7f);
        purple.y = -0.117f + 1.02f * cosf(t * 0.90f);
        purple.height = 0.47f;
        purple.radius = 1.33f;
        purple.decay = 1.2f;
        purple.r = 0.82f; purple.g = 0.30f; purple.b = 1.00f; purple.a = 1.0f;

        zcsr_gl_begin(renderer);
        if (!zcsr_light2d_begin_geometry(lights)) {
            fprintf(stderr, "FAIL: begin_geometry\n");
            running = false;
            break;
        }

        if (quality == ZCSR_LIGHT_QUALITY_LOW) {
            /* Scene straight to the window during the geometry phase; the additive pass
             * then layers the three lights on top of it. */
            zcsr_gl_clear(renderer, 0.03f, 0.03f, 0.06f, 1.0f);
            zcsr_gl_fill_rect(renderer, 0.0f, 0.0f, (float)WIN_W, (float)WIN_H,
                              0.09f, 0.09f, 0.13f, 1.0f);
            zcsr_gl_fill_rect(renderer, 80.0f, 60.0f, 800.0f, 420.0f,
                              0.16f, 0.16f, 0.20f, 1.0f);
            zcsr_gl_flush(renderer);
        } else {
            /* White rectangle -> all four G-buffer attachments (world position
             * via the default camera: origin centered, 128 px per unit). */
            demo_paint_albedo_rect(rect_prog, rect_vao, rect_vbo,
                                   rect_loc_view, rect_loc_cam, rect_loc_ppu,
                                   80.0f, 60.0f, 800.0f, 420.0f);
        }

        if (!zcsr_light2d_begin_lighting(lights) ||
            !zcsr_light2d_add_light(lights, &warm) ||
            !zcsr_light2d_add_light(lights, &cold) ||
            !zcsr_light2d_add_light(lights, &purple) ||
            !zcsr_light2d_end_lighting(lights)) {
            fprintf(stderr, "FAIL: lighting pass\n");
            running = false;
            break;
        }

        zcsr_gl_present(renderer);
        demo_sleep_ms(16);
        ++frame;
    }

    glDeleteProgram(rect_prog);
    glDeleteBuffers(1, &rect_vbo);
    glDeleteVertexArrays(1, &rect_vao);
    zcsr_light2d_destroy(lights);
    zcsr_gl_destroy(renderer);
    zcsr_surface_destroy(window);
    printf("done\n");
    return 0;
}
