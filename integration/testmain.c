/* integration/testmain.c — minimal 2.5D debug slice (Geometry Proof v1 runtime).
 * World: X east, Y south, Z up, ground z=0. Projection = GAME SHEAR (contract C):
 *   screen_x = (x-cam)*ppu + w/2 ; screen_y = (y-cam)*ppu + h/2 - z*k*ppu
 * Scene: floor, ONE real-height receiving wall (vertical face, real 3D pixels),
 * ONE box footprint 2<=x<=3, -0.5<=y<=0.5, H=2, ONE movable 3D point light
 * (mouse = x/y, W/S = z). Shadows = per-pixel segment-vs-prism (ray_shadows).
 * Debug insets show the G-buffer Position and Normal channels directly.
 * The four Geometry Proof occlusion cases are checked host-side at init. */
#include "window.h"
#include "zcsr/gl_render.h"
#include "zcsr/gl_render_l2d_geo.h"
#include "zcsr/light2d.h"

#include <glad/glad.h>

#include <stdio.h>
#include <string.h>

extern void zcsr_window_get_mouse_position(int* x, int* y);

/* ---- world constants (Geometry Proof scene) ---- */
#define BOX_MIN_X 2.0f
#define BOX_MAX_X 3.0f
#define BOX_MIN_Y (-0.5f)
#define BOX_MAX_Y 0.5f
#define BOX_H 2.0f
#define WALL_Y (-2.0f)
#define WALL_H 3.0f
#define FLOOR_MIN_X (-2.0f)
#define FLOOR_MAX_X 10.0f
#define FLOOR_MIN_Y (-2.0f)
#define FLOOR_MAX_Y 2.0f
#define LIGHT_Z_DEFAULT 3.0f
#define LIGHT_RADIUS 14.0f
#define PPU_DEFAULT 96.0f
#define SHEAR_DEFAULT 0.5f

typedef struct {
    zcsr_light2d_ctx* ctx;
    zcsr_camera cam;
    unsigned int dbg_prog;  /* G-buffer channel inspector */
    unsigned int dbg_vao, dbg_vbo;
    int dbg_loc_pos, dbg_loc_nrm, dbg_loc_light, dbg_loc_dst;
    int quality;
    int last_cw, last_ch;
    int frame_index;
    int autoquit;
    int mouse_seen;
    int last_mouse_x, last_mouse_y;
    float light_z;
    zcsr_gl_texture rug_tex; /* programmatic 64x64 fixture, host-owned */
} light_state;

static unsigned char light2d_buf[64 * 1024];
static light_state g_light;

#define LIGHT_KEY_SPACE 0x20

/* ---- host-side mirror of the shader slab test (proof cross-check) ------- */
static int seg_blocked(float lx, float ly, float lz,
                       float px, float py, float pz)
{
    const float dx = px - lx;
    const float dy = py - ly;
    const float dz = pz - lz;
    float t0 = 1e-4f;
    float t1 = 1.0f - 1e-4f;
    float ta, tb;
    if (dx > -1e-6f && dx < 1e-6f) {
        if (lx < BOX_MIN_X || lx > BOX_MAX_X) return 0;
    } else {
        ta = (BOX_MIN_X - lx) / dx;
        tb = (BOX_MAX_X - lx) / dx;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (dy > -1e-6f && dy < 1e-6f) {
        if (ly < BOX_MIN_Y || ly > BOX_MAX_Y) return 0;
    } else {
        ta = (BOX_MIN_Y - ly) / dy;
        tb = (BOX_MAX_Y - ly) / dy;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (dz > -1e-6f && dz < 1e-6f) {
        if (lz < 0.0f || lz > BOX_H) return 0;
    } else {
        ta = (0.0f - lz) / dz;
        tb = (BOX_H - lz) / dz;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    return (t0 < t1) ? 1 : 0;
}

static void proof_check(void)
{
    struct { float lz, px; int want; const char* name; } cases[4] = {
        { 3.0f, 7.0f,  1, "L=(0,0,3) P=(7,0,0)  occluded" },
        { 3.0f, 10.0f, 0, "L=(0,0,3) P=(10,0,0) clear" },
        { 5.0f, 4.0f,  1, "L=(0,0,5) P=(4,0,0)  occluded" },
        { 5.0f, 7.0f,  0, "L=(0,0,5) P=(7,0,0)  clear" }
    };
    int i;
    int ok = 1;
    for (i = 0; i < 4; ++i) {
        const int got = seg_blocked(0.0f, 0.0f, cases[i].lz,
                                    cases[i].px, 0.0f, 0.0f);
        printf("[proof] %s: %s\n", cases[i].name,
               got == cases[i].want ? "PASS" : "FAIL");
        if (got != cases[i].want) ok = 0;
    }
    printf("[proof] tip(L_z=3)=x9 tip(L_z=5)=x5 (%s)\n",
           ok ? "all PASS" : "MISMATCH");
}

/* Q4 fixture: 64x64 RGBA, 8px transparent frame, four 24px quadrants
 * (TL red / TR green / BL blue / BR yellow). Image row 0 = v 0 = the
 * texture's top, matching the sprite pipeline's UV convention. */
static void make_rug_fixture(unsigned char* px)
{
    int x;
    int y;
    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 64; ++x) {
            unsigned char* p = px + (y * 64 + x) * 4u;
            const int in = (x >= 8 && x < 56 && y >= 8 && y < 56);
            if (!in) {
                p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;
            } else if (x < 32 && y < 32) {
                p[0] = 210; p[1] = 70; p[2] = 60; p[3] = 255;   /* TL red */
            } else if (x >= 32 && y < 32) {
                p[0] = 70; p[1] = 190; p[2] = 80; p[3] = 255;   /* TR green */
            } else if (x < 32) {
                p[0] = 60; p[1] = 90; p[2] = 210; p[3] = 255;   /* BL blue */
            } else {
                p[0] = 215; p[1] = 195; p[2] = 70; p[3] = 255;  /* BR yellow */
            }
        }
    }
}

/* G-buffer channel inspector: two inset quads sampling the Position and
 * Normal textures (units 5/6, still bound after the composite). */
static const char* dbg_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "  v_uv = a_pos * 0.5 + 0.5;\n"
    "}\n";

static const char* dbg_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_pos_tex;  /* unit 5 */\n"
    "uniform sampler2D u_nrm_tex;  /* unit 6 */\n"
    "uniform sampler2D u_light_tex; /* unit 8, HDR accumulation */\n"
    "uniform int u_which;          /* 0 pos, 1 normal, 2 light buffer */\n"
    "out vec4 o_color;\n"
    "void main(){\n"
    "  if (u_which == 0) {\n"
    "    vec3 p = texture(u_pos_tex, v_uv).xyz;\n"
    "    o_color = vec4(clamp(p.x * 0.0625 + 0.5, 0.0, 1.0),\n"
    "                   clamp(p.y * 0.125 + 0.5, 0.0, 1.0),\n"
    "                   clamp(p.z * 0.2, 0.0, 1.0), 1.0);\n"
    "  } else if (u_which == 1) {\n"
    "    vec3 n = texture(u_nrm_tex, v_uv).xyz * 2.0 - 1.0;\n"
    "    o_color = vec4(n * 0.5 + 0.5, 1.0);\n"
    "  } else {\n"
    "    vec3 l = texture(u_light_tex, v_uv).rgb;\n"
    "    o_color = vec4(clamp(l, 0.0, 1.0), 1.0);\n"
    "  }\n"
    "}\n";

static unsigned int compile_shader(unsigned int type, const char* src)
{
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
        fprintf(stderr, "shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static unsigned int link_program(const char* tag, const char* vs, const char* fs)
{
    unsigned int v = compile_shader(GL_VERTEX_SHADER, vs);
    unsigned int f = compile_shader(GL_FRAGMENT_SHADER, fs);
    unsigned int p;
    int ok = 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    p = glCreateProgram();
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
        fprintf(stderr, "%s link failed: %s\n", tag, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* Project one world point through the shared camera's game shear (host
 * mirror for the LOW debug path; the HIGH path projects inside l2d_geo). */
static void project(const light_state* s, int cw, int ch,
                    float wx, float wy, float wz, float* sx, float* sy)
{
    *sx = (wx - s->cam.center_x) * s->cam.px_per_unit + (float)cw * 0.5f;
    *sy = (wy - s->cam.center_y) * s->cam.px_per_unit + (float)ch * 0.5f
          - wz * s->cam.height_shear * s->cam.px_per_unit;
}

/* One world-space quad. HIGH: submitted through the PUBLIC l2d_geo API
 * (engine-owned MRT program; this file defines no G-buffer shader). LOW:
 * flat fill of the projected corner AABB — unlit debug shape only. Faces
 * are submitted ascending in the shear sort key q = k*(y-cy) + z by the
 * caller (floor -> wall -> box sides -> top). */
static void paint_quad(light_state* s, zcsr_gl_renderer* r, int cw, int ch,
                       int low,
                       float ax, float ay, float az,
                       float bx, float by, float bz,
                       float cx, float cy, float cz,
                       float dx, float dy, float dz,
                       float nx, float ny, float nz,
                       float red, float grn, float blu)
{
    const float corners[12] = {
        ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz
    };
    const float albedo[4] = { red, grn, blu, 1.0f };
    const float normal[3] = { nx, ny, nz };
    const float emissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (low) {
        float x0, y0, x1, y1, x2, y2, x3, y3;
        project(s, cw, ch, ax, ay, az, &x0, &y0);
        project(s, cw, ch, bx, by, bz, &x1, &y1);
        project(s, cw, ch, cx, cy, cz, &x2, &y2);
        project(s, cw, ch, dx, dy, dz, &x3, &y3);
        {
            float min_x = x0, max_x = x0, min_y = y0, max_y = y0;
            float xs[4]; float ys[4];
            int i;
            xs[0] = x0; xs[1] = x1; xs[2] = x2; xs[3] = x3;
            ys[0] = y0; ys[1] = y1; ys[2] = y2; ys[3] = y3;
            for (i = 1; i < 4; ++i) {
                if (xs[i] < min_x) min_x = xs[i];
                if (xs[i] > max_x) max_x = xs[i];
                if (ys[i] < min_y) min_y = ys[i];
                if (ys[i] > max_y) max_y = ys[i];
            }
            zcsr_gl_fill_rect(r, min_x, min_y, max_x - min_x, max_y - min_y,
                              red, grn, blu, 1.0f);
        }
        return;
    }
    (void)zcsr_gl_l2d_geo_face(r, corners, albedo, normal, emissive);
}

static void draw_scene(light_state* s, zcsr_gl_renderer* r, int cw, int ch,
                       int low, float lx, float ly, float lz)
{
    if (low) zcsr_gl_clear(r, 0.02f, 0.02f, 0.02f, 1.0f);
    else {
        /* one begin/end pair around all faces; ascending shear-key order */
        if (!zcsr_gl_l2d_geo_begin(r, &s->cam)) {
            fprintf(stderr, "FAIL: l2d_geo_begin\n");
            return;
        }
    }

    /* floor z=0, up normal */
    paint_quad(s, r, cw, ch, low,
               FLOOR_MIN_X, FLOOR_MIN_Y, 0.0f,
               FLOOR_MAX_X, FLOOR_MIN_Y, 0.0f,
               FLOOR_MAX_X, FLOOR_MAX_Y, 0.0f,
               FLOOR_MIN_X, FLOOR_MAX_Y, 0.0f,
               0.0f, 0.0f, 1.0f,
               0.45f, 0.47f, 0.52f);

    /* real receiving wall: vertical face at y=WALL_Y, z 0..WALL_H, faces +y */
    paint_quad(s, r, cw, ch, low,
               FLOOR_MIN_X, WALL_Y, 0.0f,
               FLOOR_MAX_X, WALL_Y, 0.0f,
               FLOOR_MAX_X, WALL_Y, WALL_H,
               FLOOR_MIN_X, WALL_Y, WALL_H,
               0.0f, 1.0f, 0.0f,
               0.38f, 0.36f, 0.34f);

    /* box: four sides then the top (painter's order) */
    paint_quad(s, r, cw, ch, low,
               BOX_MIN_X, BOX_MIN_Y, 0.0f, BOX_MAX_X, BOX_MIN_Y, 0.0f,
               BOX_MAX_X, BOX_MIN_Y, BOX_H, BOX_MIN_X, BOX_MIN_Y, BOX_H,
               0.0f, -1.0f, 0.0f, 0.30f, 0.30f, 0.34f);
    paint_quad(s, r, cw, ch, low,
               BOX_MIN_X, BOX_MIN_Y, 0.0f, BOX_MIN_X, BOX_MAX_Y, 0.0f,
               BOX_MIN_X, BOX_MAX_Y, BOX_H, BOX_MIN_X, BOX_MIN_Y, BOX_H,
               -1.0f, 0.0f, 0.0f, 0.38f, 0.38f, 0.42f);
    paint_quad(s, r, cw, ch, low,
               BOX_MAX_X, BOX_MIN_Y, 0.0f, BOX_MAX_X, BOX_MAX_Y, 0.0f,
               BOX_MAX_X, BOX_MAX_Y, BOX_H, BOX_MAX_X, BOX_MIN_Y, BOX_H,
               1.0f, 0.0f, 0.0f, 0.38f, 0.38f, 0.42f);
    paint_quad(s, r, cw, ch, low,
               BOX_MIN_X, BOX_MAX_Y, 0.0f, BOX_MAX_X, BOX_MAX_Y, 0.0f,
               BOX_MAX_X, BOX_MAX_Y, BOX_H, BOX_MIN_X, BOX_MAX_Y, BOX_H,
               0.0f, 1.0f, 0.0f, 0.52f, 0.52f, 0.56f);
    paint_quad(s, r, cw, ch, low,
               BOX_MIN_X, BOX_MIN_Y, BOX_H, BOX_MAX_X, BOX_MIN_Y, BOX_H,
               BOX_MAX_X, BOX_MAX_Y, BOX_H, BOX_MIN_X, BOX_MAX_Y, BOX_H,
               0.0f, 0.0f, 1.0f, 0.80f, 0.82f, 0.86f);

    /* Q4 textured rugs (HIGH only — the LOW debug path stays flat):
     * rug A maps the FULL fixture (four quadrants visible), rug B maps the
     * sub-rect UV (0.5,0.5)-(1,1) so its whole surface shows the fixture's
     * BR quadrant — same world size, different texture region proves the UV
     * path. The 8px transparent frame must DISCARD, leaving the floor's
     * earlier G-buffer write intact beneath both rugs. */
    if (!low && s->rug_tex != 0u) {
        const float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float emiss[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const float up[3] = { 0.0f, 0.0f, 1.0f };
        {
            const float ca[12] = { -1.6f, 0.4f, 0.0f, -0.6f, 0.4f, 0.0f,
                                   -0.6f, 1.4f, 0.0f, -1.6f, 1.4f, 0.0f };
            const float ua[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
            (void)zcsr_gl_l2d_geo_tex_face(r, s->rug_tex, ca, ua, tint, up, emiss);
        }
        {
            const float cb[12] = { 0.6f, 0.5f, 0.0f, 1.6f, 0.5f, 0.0f,
                                   1.6f, 1.5f, 0.0f, 0.6f, 1.5f, 0.0f };
            const float ub[8] = { 0.5f, 0.5f, 1.0f, 0.5f, 1.0f, 1.0f, 0.5f, 1.0f };
            (void)zcsr_gl_l2d_geo_tex_face(r, s->rug_tex, cb, ub, tint, up, emiss);
        }
    }

    /* flame marker: small quad AT the light's 3D position (shear shows z) */
    paint_quad(s, r, cw, ch, low,
               lx - 0.08f, ly - 0.08f, lz, lx + 0.08f, ly - 0.08f, lz,
               lx + 0.08f, ly + 0.08f, lz, lx - 0.08f, ly + 0.08f, lz,
               0.0f, 0.0f, 1.0f,
               0.9f, 0.7f, 0.3f);
    if (low) {
        zcsr_gl_flush(r);
    } else if (!zcsr_gl_l2d_geo_end(r)) {
        fprintf(stderr, "FAIL: l2d_geo_end\n");
    }
}

/* ---- module callbacks (window.h contract) ---------------------------------- */
static bool light_module_init(zcsr_gl_renderer* r, int cw, int ch, void* user)
{
    light_state* s = (light_state*)user;
    zcsr_light2d_options opt;
    zcsr_light2d_occluder occ;
    const float footprint[8] = {
        BOX_MIN_X, BOX_MIN_Y, BOX_MAX_X, BOX_MIN_Y,
        BOX_MAX_X, BOX_MAX_Y, BOX_MIN_X, BOX_MAX_Y
    };

    proof_check();

    s->ctx = zcsr_light2d_create(r,
                                 s->autoquit ? ZCSR_LIGHT_QUALITY_LOW : ZCSR_LIGHT_QUALITY_HIGH,
                                 ZCSR_LIGHT_QUALITY_HIGH,
                                 cw, ch, light2d_buf, sizeof(light2d_buf));
    if (!s->ctx) return false;

    s->dbg_prog = link_program("channel inspector", dbg_vs, dbg_fs);
    if (!s->dbg_prog) return false;
    s->dbg_loc_pos = glGetUniformLocation(s->dbg_prog, "u_pos_tex");
    s->dbg_loc_nrm = glGetUniformLocation(s->dbg_prog, "u_nrm_tex");
    s->dbg_loc_light = glGetUniformLocation(s->dbg_prog, "u_light_tex");
    s->dbg_loc_dst = glGetUniformLocation(s->dbg_prog, "u_which");
    glGenVertexArrays(1, &s->dbg_vao);
    glGenBuffers(1, &s->dbg_vbo);
    glBindVertexArray(s->dbg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->dbg_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    zcsr_camera_init(&s->cam, cw, ch, 4.0f, 0.0f, PPU_DEFAULT, SHEAR_DEFAULT);
    zcsr_light2d_set_camera(s->ctx, &s->cam);

    opt.shadows = true;
    opt.normals = true;
    opt.gain = 3.0f;
    opt.light_scale_pct = 50;
    opt.bloom = false;
    opt.ray_shadows = true; /* Geometry Proof semantics */
    if (!zcsr_light2d_set_options(s->ctx, &opt)) {
        fprintf(stderr, "FAIL: options rejected\n");
        return false;
    }

    occ.xy = footprint;
    occ.count = 4u;
    occ.height = BOX_H;
    occ.factor = 1.0f;
    occ.distance = 0.0f;
    occ.reverse = false;
    occ.self_shadow = false;
    occ.auto_disable = false;
    zcsr_light2d_clear_occluders(s->ctx);
    if (!zcsr_light2d_add_occluder(s->ctx, &occ)) {
        fprintf(stderr, "FAIL: occluder rejected\n");
        return false;
    }

    s->quality = s->autoquit ? (int)ZCSR_LIGHT_QUALITY_LOW : (int)ZCSR_LIGHT_QUALITY_HIGH;
    s->last_cw = cw;
    s->last_ch = ch;
    s->frame_index = 0;
    s->mouse_seen = 0;
    s->last_mouse_x = 0;
    s->last_mouse_y = 0;
    s->light_z = LIGHT_Z_DEFAULT;

    /* Q4 fixture texture (host-owned: created here, destroyed in shutdown) */
    {
        static unsigned char rug_px[64 * 64 * 4];
        make_rug_fixture(rug_px);
        s->rug_tex = zcsr_gl_texture_create(r, rug_px, 64, 64);
        if (s->rug_tex == 0u) {
            fprintf(stderr, "WARN: rug fixture texture creation failed\n");
        }
    }
    printf("[light] 2.5D slice init %dx%d (floor+wall+H2 box+3D light+rugs)\n", cw, ch);
    return true;
}

static void light_module_shutdown(zcsr_gl_renderer* r, void* user)
{
    light_state* s = (light_state*)user;
    (void)r;
    if (s->dbg_prog) glDeleteProgram(s->dbg_prog);
    if (s->dbg_vbo) glDeleteBuffers(1, &s->dbg_vbo);
    if (s->dbg_vao) glDeleteVertexArrays(1, &s->dbg_vao);
    if (s->rug_tex != 0u) zcsr_gl_texture_destroy(r, s->rug_tex);
    if (s->ctx) zcsr_light2d_destroy(s->ctx);
    s->ctx = NULL;
    printf("[light] module shutdown\n");
}

static void dbg_insets(light_state* s, zcsr_gl_renderer* r, int cw, int ch)
{
    const float w = 200.0f;
    const float h = 112.0f;
    const float x0 = 8.0f;
    const float y0 = (float)ch - h - 8.0f;
    const float verts[12] = { -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1 };
    GLint viewport[4];
    GLint blend_was = 0;
    zcsr_gl_l2d_target gb;
    int which;
    (void)cw;

    /* end_lighting popped the pipeline's saved bindings — re-bind the
     * G-buffer textures (units 4..8) before sampling the channels. */
    gb = zcsr_light2d_gbuffer_handle(s->ctx);
    if (gb == ZCSR_GL_L2D_TARGET_INVALID) return;
    if (!zcsr_gl_l2d_bind_target_textures(r, gb, 4)) return;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_BLEND, &blend_was);
    glUseProgram(s->dbg_prog);
    if (s->dbg_loc_pos >= 0) glUniform1i(s->dbg_loc_pos, 5);
    if (s->dbg_loc_nrm >= 0) glUniform1i(s->dbg_loc_nrm, 6);
    if (s->dbg_loc_light >= 0) glUniform1i(s->dbg_loc_light, 8);
    glDisable(GL_BLEND);
    glBindVertexArray(s->dbg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->dbg_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    for (which = 0; which < 3; ++which) {
        if (s->dbg_loc_dst >= 0) glUniform1i(s->dbg_loc_dst, which);
        /* which==2 shows the light buffer: bind_target_textures put the white
         * shadow placeholder on unit 8 — rebind the real accumulation texture */
        if (which == 2) (void)zcsr_gl_l2d_bind_light_texture(r, 8);
        glViewport((GLint)(x0 + (float)which * (w + 8.0f)), (GLint)y0,
                   (GLsizei)w, (GLsizei)h);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glViewport(viewport[0], viewport[1], (GLsizei)viewport[2], (GLsizei)viewport[3]);
    if (blend_was != 0) glEnable(GL_BLEND);
}

static void light_module_frame(zcsr_gl_renderer* r, int cw, int ch, void* user)
{
    light_state* s = (light_state*)user;
    zcsr_light light;
    float lx = 0.0f;
    float ly = 0.0f;
    float lz;
    int raw_mouse_x = 0;
    int raw_mouse_y = 0;
    const int frame_index = s->frame_index;

    if (s->ctx == NULL) return;
    lz = s->light_z;

    if (cw != s->last_cw || ch != s->last_ch) {
        zcsr_camera_set_viewport(&s->cam, cw, ch);
        if (zcsr_light2d_resize(s->ctx, cw, ch)) {
            s->last_cw = cw;
            s->last_ch = ch;
        }
    }

    zcsr_window_get_mouse_position(&raw_mouse_x, &raw_mouse_y);
    if (!s->autoquit) {
        if (frame_index == 0) {
            s->last_mouse_x = raw_mouse_x;
            s->last_mouse_y = raw_mouse_y;
        } else if (raw_mouse_x != s->last_mouse_x ||
                   raw_mouse_y != s->last_mouse_y) {
            s->mouse_seen = 1;
        }
        s->last_mouse_x = raw_mouse_x;
        s->last_mouse_y = raw_mouse_y;
        if (s->mouse_seen) {
            zcsr_camera_px_to_world(&s->cam, (float)raw_mouse_x, (float)raw_mouse_y,
                                    &lx, &ly);
        }
    }

    if (s->autoquit) {
        if (frame_index == 30) {
            printf("[light] autoquit frame 30: L=(%.2f,%.2f,%.2f)\n", lx, ly, lz);
        }
        if (frame_index == 60 && s->quality != (int)ZCSR_LIGHT_QUALITY_HIGH) {
            if (zcsr_light2d_set_quality(s->ctx, ZCSR_LIGHT_QUALITY_HIGH)) {
                s->quality = (int)ZCSR_LIGHT_QUALITY_HIGH;
                printf("[light] autoquit frame 60: quality -> HIGH\n");
            }
        }
    }

    memset(&light, 0, sizeof(light));
    light.x = lx;
    light.y = ly;
    light.height = lz;
    light.radius = LIGHT_RADIUS;
    light.decay = 0.5f;
    light.dir_x = 0.0f;
    light.dir_y = -1.0f;
    light.dir_z = 1.0f;
    light.type = ZCSR_LIGHT_POINT;
    light.r = 1.0f;
    light.g = 0.85f;
    light.b = 0.6f;
    light.a = 1.0f;

    if (!zcsr_light2d_begin_geometry(s->ctx)) {
        fprintf(stderr, "FAIL: begin_geometry\n");
        return;
    }
    draw_scene(s, r, cw, ch,
               s->quality == (int)ZCSR_LIGHT_QUALITY_LOW ? 1 : 0,
               lx, ly, lz);

    if (!zcsr_light2d_begin_lighting(s->ctx) ||
        !zcsr_light2d_add_light(s->ctx, &light) ||
        !zcsr_light2d_end_lighting(s->ctx)) {
        fprintf(stderr, "FAIL: lighting pass\n");
        return;
    }

    dbg_insets(s, r, cw, ch);

    {
        char status[160];
        snprintf(status, sizeof(status),
                 "L=(%.2f,%.2f,%.2f) %s | box x[2,3] y[-.5,.5] H=2 | tip x=%.1f | W/S:z mouse:xy",
                 lx, ly, lz,
                 s->quality == (int)ZCSR_LIGHT_QUALITY_LOW ? "LOW" : "HIGH",
                 3.0f * BOX_MAX_X / lz);
        zcsr_gl_fill_rect(r, 0.0f, 0.0f, (float)cw, 60.0f, 0.01f, 0.01f, 0.01f, 1.0f);
        zcsr_gl_flush(r);
        zcsr_gl_draw_text(r, status, 12.0f, 12.0f, 1.3f, 0.95f, 0.9f, 0.8f, 1.0f);
        zcsr_gl_draw_text(r, "insets: POSITION | NORMAL", 220.0f,
                          (float)ch - 130.0f, 1.0f, 0.7f, 0.7f, 0.7f, 1.0f);
    }

    ++s->frame_index;
}

static bool light_module_key(int key, void* user)
{
    light_state* s = (light_state*)user;
    if (s->ctx == NULL) return false;
    if (key == LIGHT_KEY_SPACE) {
        zcsr_light_quality next = (s->quality == (int)ZCSR_LIGHT_QUALITY_LOW)
            ? ZCSR_LIGHT_QUALITY_HIGH : ZCSR_LIGHT_QUALITY_LOW;
        if (zcsr_light2d_set_quality(s->ctx, next)) {
            s->quality = (int)next;
            printf("[light] quality -> %s\n",
                   s->quality == (int)ZCSR_LIGHT_QUALITY_LOW ? "LOW" : "HIGH");
        }
        return true;
    }
    if (key == 'W' || key == 'w') {
        s->light_z += 0.25f;
        if (s->light_z > 8.0f) s->light_z = 8.0f;
        printf("[light] light z -> %.2f (tip x=%.1f)\n", s->light_z,
               3.0f * BOX_MAX_X / s->light_z);
        return true;
    }
    if (key == 'S' || key == 's') {
        s->light_z -= 0.25f;
        if (s->light_z < 0.5f) s->light_z = 0.5f;
        printf("[light] light z -> %.2f (tip x=%.1f)\n", s->light_z,
               3.0f * BOX_MAX_X / s->light_z);
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    static const zcsr_window_module modules[1] = {
        {
            "LIGHT 2.5D",
            &g_light,
            light_module_init,
            light_module_shutdown,
            light_module_frame,
            light_module_key,
        },
    };
    memset(&g_light, 0, sizeof(g_light));
    g_light.quality = (int)ZCSR_LIGHT_QUALITY_HIGH;
    g_light.light_z = LIGHT_Z_DEFAULT;
    zcsr_camera_init(&g_light.cam, 0, 0, 4.0f, 0.0f, PPU_DEFAULT, SHEAR_DEFAULT);
    g_light.autoquit = (argc > 1 && argv[1] != NULL && strcmp(argv[1], "autoquit") == 0);
    return zcsr_window_main(modules, 1, argc, argv);
}
