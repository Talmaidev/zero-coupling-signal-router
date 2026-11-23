#include "zcsr/scene_io.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#define ZCSR_SCENE_IO_LINE 1024
#define ZCSR_SCENE_IO_TEXT_LIMIT (64u * 1024u * 1024u)

static const char *const k_header_v1 = "ZCSRSCENE 1";
static const char *const k_header_v2 = "ZCSRSCENE 2";

#if defined(_WIN32)
static FILE *open_utf8(const char *utf8_path, const wchar_t *mode) {
    wchar_t wide_path[1024];
    int n;

    if (!utf8_path || !*utf8_path || !mode) return NULL;
    n = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wide_path,
                            (int)(sizeof(wide_path) / sizeof(wide_path[0])));
    if (n <= 0) return NULL;
    return _wfopen(wide_path, mode);
}
#else
static FILE *open_utf8(const char *utf8_path, const char *mode) {
    if (!utf8_path || !*utf8_path || !mode) return NULL;
    return fopen(utf8_path, mode);
}
#endif

typedef struct TextBuffer {
    char *data;
    size_t size;
    size_t cap;
} TextBuffer;

static bool text_reserve(TextBuffer *b, size_t extra) {
    size_t need;
    size_t cap;
    char *next;
    if (!b || extra > ZCSR_SCENE_IO_TEXT_LIMIT ||
        b->size > ZCSR_SCENE_IO_TEXT_LIMIT - extra) return false;
    need = b->size + extra;
    if (need <= b->cap) return true;
    cap = b->cap ? b->cap : 1024u;
    while (cap < need) {
        if (cap > ZCSR_SCENE_IO_TEXT_LIMIT / 2u) {
            cap = ZCSR_SCENE_IO_TEXT_LIMIT;
            break;
        }
        cap *= 2u;
    }
    next = (char *)realloc(b->data, cap);
    if (!next) return false;
    b->data = next;
    b->cap = cap;
    return true;
}

static bool text_append(TextBuffer *b, const char *s, size_t n) {
    if (!s || !text_reserve(b, n)) return false;
    memcpy(b->data + b->size, s, n);
    b->size += n;
    return true;
}

static bool text_append_line(TextBuffer *b, const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(line)) return false;
    return text_append(b, line, (size_t)n);
}

static bool finite_scene(const zcsr_scene *scene) {
    size_t i;
    if (!scene) return false;
    if (!isfinite(scene->camera.target_x) ||
        !isfinite(scene->camera.target_y) ||
        !isfinite(scene->camera.target_z) ||
        !isfinite(scene->camera.yaw_deg) ||
        !isfinite(scene->camera.pitch_deg) ||
        !isfinite(scene->camera.roll_deg) ||
        !isfinite(scene->camera.distance) ||
        !isfinite(scene->camera.ortho_size)) return false;
    for (i = 0; i < scene->count; ++i) {
        const zcsr_scene_entity *e = &scene->entities[i];
        size_t j;
        if (e->id == 0 || !isfinite(e->x) || !isfinite(e->y) ||
            !isfinite(e->z) || !isfinite(e->w) || !isfinite(e->h) ||
            !isfinite(e->rotation_deg) || !isfinite(e->pivot_x) ||
            !isfinite(e->pivot_y) || !isfinite(e->tint_r) ||
            !isfinite(e->tint_g) || !isfinite(e->tint_b) ||
            !isfinite(e->tint_a)) return false;
        for (j = 0; j < i; ++j)
            if (scene->entities[j].id == e->id) return false;
    }
    for (i = 0; i < scene->quad_count; ++i) {
        size_t c, j;
        if (scene->quads[i].id == 0 ||
            !zcsr_scene_get_const(scene, scene->quads[i].id)) return false;
        for (j = 0; j < i; ++j)
            if (scene->quads[j].id == scene->quads[i].id) return false;
        for (c = 0; c < 4; ++c) {
            if (!isfinite(scene->quads[i].corners[c].x) ||
                !isfinite(scene->quads[i].corners[c].y) ||
                !isfinite(scene->quads[i].corners[c].z)) return false;
        }
    }
    return true;
}

zcsr_scene_io_result zcsr_scene_io_serialize(const zcsr_scene *scene,
                                                        char **out_text,
                                                        size_t *out_size) {
    TextBuffer b = {0};
    size_t i;

    if (!scene || !out_text || !out_size)
        return ZCSR_SCENE_IO_INVALID_ARGUMENT;
    *out_text = NULL;
    *out_size = 0u;
    if (scene->count > ZCSR_SCENE_MAX_ENTITIES ||
        scene->quad_count > ZCSR_SCENE_MAX_ENTITIES)
        return ZCSR_SCENE_IO_CAPACITY;
    if (!finite_scene(scene)) return ZCSR_SCENE_IO_BAD_RECORD;

    if (!text_append_line(&b, "%s\n", k_header_v2) ||
        !text_append_line(&b,
            "camera %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %d\n",
            (double)scene->camera.target_x,
            (double)scene->camera.target_y,
            (double)scene->camera.target_z,
            (double)scene->camera.yaw_deg,
            (double)scene->camera.pitch_deg,
            (double)scene->camera.roll_deg,
            (double)scene->camera.distance,
            (double)scene->camera.ortho_size,
            scene->camera.view_w, scene->camera.view_h)) {
        free(b.data);
        return ZCSR_SCENE_IO_IO_ERROR;
    }

    for (i = 0; i < scene->count; ++i) {
        const zcsr_scene_entity *e = &scene->entities[i];
        if (!text_append_line(&b,
                "entity %llu %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g "
                "%d %d %.9g %.9g %.9g %.9g %u %d %u %llu %llu\n",
                (unsigned long long)e->id,
                (double)e->x, (double)e->y, (double)e->z,
                (double)e->w, (double)e->h,
                (double)e->rotation_deg, (double)e->pivot_x,
                (double)e->pivot_y, e->layer, e->order,
                (double)e->tint_r, (double)e->tint_g,
                (double)e->tint_b, (double)e->tint_a,
                (unsigned)e->texture, e->visible ? 1 : 0,
                (unsigned)e->kind,
                (unsigned long long)e->prefab_id,
                (unsigned long long)e->asset_id)) {
            free(b.data);
            return ZCSR_SCENE_IO_IO_ERROR;
        }
        if (zcsr_scene_has_quad(scene, e->id)) {
            zcsr_scene_point q[4];
            if (!zcsr_scene_entity_quad(scene, e->id, q)) {
                free(b.data);
                return ZCSR_SCENE_IO_BAD_RECORD;
            }
            if (!text_append_line(&b,
                    "quad %llu %.9g %.9g %.9g %.9g %.9g %.9g %.9g "
                    "%.9g %.9g %.9g %.9g %.9g\n",
                    (unsigned long long)e->id,
                    (double)q[0].x, (double)q[0].y, (double)q[0].z,
                    (double)q[1].x, (double)q[1].y, (double)q[1].z,
                    (double)q[2].x, (double)q[2].y, (double)q[2].z,
                    (double)q[3].x, (double)q[3].y, (double)q[3].z)) {
                free(b.data);
                return ZCSR_SCENE_IO_IO_ERROR;
            }
        }
    }

    if (!text_reserve(&b, 1u)) {
        free(b.data);
        return ZCSR_SCENE_IO_IO_ERROR;
    }
    b.data[b.size] = '\0';
    *out_text = b.data;
    *out_size = b.size;
    return ZCSR_SCENE_IO_OK;
}

#if defined(_WIN32)
static bool write_atomic_text(const char *utf8_path, const char *text, size_t size) {
    wchar_t dest_w[1024];
    wchar_t temp_w[1024];
    char temp_utf8[1024];
    FILE *f;
    bool ok;

    if (!utf8_path || !text || size == 0u) return false;
    if (strlen(utf8_path) >= sizeof(temp_utf8) - 5u) return false;
    snprintf(temp_utf8, sizeof(temp_utf8), "%s.tmp", utf8_path);
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, dest_w,
                            (int)(sizeof(dest_w) / sizeof(dest_w[0]))) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, temp_utf8, -1, temp_w,
                            (int)(sizeof(temp_w) / sizeof(temp_w[0]))) <= 0)
        return false;
    f = _wfopen(temp_w, L"wb");
    if (!f) return false;
    ok = fwrite(text, 1u, size, f) == size;
    if (ok) ok = fflush(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        _wremove(temp_w);
        return false;
    }
    if (!MoveFileExW(temp_w, dest_w,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        _wremove(temp_w);
        return false;
    }
    return true;
}
#else
static bool write_atomic_text(const char *utf8_path, const char *text, size_t size) {
    char temp_path[1024];
    FILE *f;
    bool ok;
    if (!utf8_path || !text || size == 0u) return false;
    if (strlen(utf8_path) >= sizeof(temp_path) - 5u) return false;
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", utf8_path);
    f = fopen(temp_path, "wb");
    if (!f) return false;
    ok = fwrite(text, 1u, size, f) == size;
    if (ok) ok = fflush(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return false;
    }
    if (rename(temp_path, utf8_path) != 0) {
        remove(temp_path);
        return false;
    }
    return true;
}
#endif

zcsr_scene_io_result zcsr_scene_io_save(const zcsr_scene *scene,
                                                   const char *utf8_path) {
    char *text = NULL;
    size_t size = 0u;
    zcsr_scene_io_result rc;
    bool written;

    if (!scene || !utf8_path || !*utf8_path)
        return ZCSR_SCENE_IO_INVALID_ARGUMENT;
    rc = zcsr_scene_io_serialize(scene, &text, &size);
    if (rc != ZCSR_SCENE_IO_OK) return rc;
    written = write_atomic_text(utf8_path, text, size);
    free(text);
    return written ? ZCSR_SCENE_IO_OK : ZCSR_SCENE_IO_IO_ERROR;
}

static void trim_line(char *line) {
    size_t n;
    if (!line) return;
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
}

static bool suffix_is_space(const char *line, int offset) {
    const char *p;
    if (!line || offset < 0) return false;
    p = line + offset;
    while (*p) {
        if (!isspace((unsigned char)*p)) return false;
        ++p;
    }
    return true;
}

static bool parse_camera(char *line, zcsr_scene *scene, bool v2) {
    float tx, ty, tz, yaw, pitch, roll, distance, ortho;
    int view_w = 1, view_h = 1;
    int used = 0;
    int fields;

    if (v2) {
        fields = sscanf(line,
                        "camera %f %f %f %f %f %f %f %f %d %d%n",
                        &tx, &ty, &tz, &yaw, &pitch, &roll,
                        &distance, &ortho, &view_w, &view_h, &used);
        if (fields != 10) return false;
    } else {
        fields = sscanf(line, "camera %f %f %f %f %f %f %f %f %d%n",
                        &tx, &ty, &tz, &yaw, &pitch, &roll,
                        &distance, &ortho, &view_w, &used);
        if (fields != 9) return false;
    }
    if (!suffix_is_space(line, used)) return false;
    if (!isfinite(tx) || !isfinite(ty) || !isfinite(tz) ||
        !isfinite(yaw) || !isfinite(pitch) || !isfinite(roll) ||
        !isfinite(distance) || !isfinite(ortho)) return false;
    scene->camera.target_x = tx;
    scene->camera.target_y = ty;
    scene->camera.target_z = tz;
    scene->camera.yaw_deg = yaw;
    scene->camera.pitch_deg = pitch;
    scene->camera.roll_deg = roll;
    scene->camera.distance = distance;
    scene->camera.ortho_size = ortho;
    scene->camera.view_w = view_w > 0 ? view_w : 1;
    scene->camera.view_h = view_h > 0 ? view_h : 1;
    return true;
}

static bool parse_entity(char *line, zcsr_scene *scene, bool v2) {
    unsigned long long id = 0;
    float x, y, z, w, h, rotation, pivot_x, pivot_y;
    float tint_r, tint_g, tint_b, tint_a;
    unsigned texture = 0;
    int layer = 0, order = 0, visible = 0;
    unsigned kind = 0;
    unsigned long long prefab_id = 0, asset_id = 0;
    int used = 0;
    int fields;
    zcsr_scene_entity e;
    zcsr_entity_id added;

    if (v2) {
        fields = sscanf(line,
            "entity %llu %f %f %f %f %f %f %f %f "
            "%d %d %f %f %f %f %u %d %u %llu %llu%n",
            &id, &x, &y, &z, &w, &h, &rotation, &pivot_x, &pivot_y,
            &layer, &order, &tint_r, &tint_g, &tint_b, &tint_a,
            &texture, &visible, &kind, &prefab_id, &asset_id, &used);
        if (fields != 20) return false;
    } else {
        fields = sscanf(line,
            "entity %llu %f %f %f %f %f %f %f %f "
            "%d %d %f %f %f %f %u %d%n",
            &id, &x, &y, &z, &w, &h, &rotation, &pivot_x, &pivot_y,
            &layer, &order, &tint_r, &tint_g, &tint_b, &tint_a,
            &texture, &visible, &used);
        if (fields != 17) return false;
    }
    if (!suffix_is_space(line, used)) return false;
    if (id == 0 || !isfinite(x) || !isfinite(y) || !isfinite(z) ||
        !isfinite(w) || !isfinite(h) || !isfinite(rotation) ||
        !isfinite(pivot_x) || !isfinite(pivot_y) ||
        !isfinite(tint_r) || !isfinite(tint_g) ||
        !isfinite(tint_b) || !isfinite(tint_a)) return false;
    if (zcsr_scene_get_const(scene, (zcsr_entity_id)id)) return false;

    memset(&e, 0, sizeof(e));
    e.id = (zcsr_entity_id)id;
    e.x = x;
    e.y = y;
    e.z = z;
    e.w = w;
    e.h = h;
    e.rotation_deg = rotation;
    e.pivot_x = pivot_x;
    e.pivot_y = pivot_y;
    e.layer = layer;
    e.order = order;
    e.tint_r = tint_r;
    e.tint_g = tint_g;
    e.tint_b = tint_b;
    e.tint_a = tint_a;
    e.texture = (uint32_t)texture;
    e.visible = visible != 0;
    e.kind = (uint32_t)kind;
    e.prefab_id = (uint64_t)prefab_id;
    e.asset_id = (uint64_t)asset_id;

    added = zcsr_scene_add(scene, &e);
    return added != 0 && added == e.id;
}

static bool parse_quad(char *line, zcsr_scene *scene) {
    unsigned long long id = 0;
    zcsr_scene_point q[4];
    int used = 0;
    if (sscanf(line,
               "quad %llu %f %f %f %f %f %f %f %f %f %f %f %f%n",
               &id, &q[0].x, &q[0].y, &q[0].z,
               &q[1].x, &q[1].y, &q[1].z,
               &q[2].x, &q[2].y, &q[2].z,
               &q[3].x, &q[3].y, &q[3].z, &used) != 13)
        return false;
    if (!suffix_is_space(line, used)) return false;
    if (id == 0) return false;
    if (!zcsr_scene_get_const(scene, (zcsr_entity_id)id)) return false;
    if (zcsr_scene_has_quad(scene, (zcsr_entity_id)id)) return false;
    return zcsr_scene_set_quad(scene, (zcsr_entity_id)id, q);
}

/* Parse the whole text into a temporary scene; on success copy into out. */
static zcsr_scene_io_result parse_scene_text(const char *text, size_t size,
                                                    zcsr_scene *out) {
    zcsr_scene *tmp;
    const char *p;
    const char *end;
    bool header_seen = false;
    bool v2 = false;
    bool camera_seen = false;
    zcsr_scene_io_result result = ZCSR_SCENE_IO_OK;

    if (!text || !out || size == 0u)
        return ZCSR_SCENE_IO_BAD_HEADER;

    tmp = (zcsr_scene *)malloc(sizeof(*tmp));
    if (!tmp) return ZCSR_SCENE_IO_IO_ERROR;
    zcsr_scene_init(tmp);

    p = text;
    end = text + size;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        char line[ZCSR_SCENE_IO_LINE];

        if (len >= sizeof(line)) {
            result = ZCSR_SCENE_IO_BAD_RECORD;
            break;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        trim_line(line);
        p = nl ? nl + 1 : end;

        if (!header_seen) {
            if (line[0] == '\0') continue;
            if (strcmp(line, k_header_v1) == 0) {
                v2 = false;
            } else if (strcmp(line, k_header_v2) == 0) {
                v2 = true;
            } else {
                result = ZCSR_SCENE_IO_BAD_HEADER;
                break;
            }
            header_seen = true;
            continue;
        }

        if (line[0] == '\0') continue;
        if (strncmp(line, "camera ", 7) == 0) {
            if (camera_seen || !parse_camera(line, tmp, v2)) {
                result = ZCSR_SCENE_IO_BAD_RECORD;
                break;
            }
            camera_seen = true;
        } else if (strncmp(line, "entity ", 7) == 0) {
            if (!parse_entity(line, tmp, v2)) {
                result = ZCSR_SCENE_IO_BAD_RECORD;
                break;
            }
        } else if (strncmp(line, "quad ", 5) == 0) {
            if (!parse_quad(line, tmp)) {
                result = ZCSR_SCENE_IO_BAD_RECORD;
                break;
            }
        } else {
            result = ZCSR_SCENE_IO_BAD_RECORD;
            break;
        }
    }

    if (result == ZCSR_SCENE_IO_OK && !header_seen)
        result = ZCSR_SCENE_IO_BAD_HEADER;
    if (result == ZCSR_SCENE_IO_OK && !camera_seen)
        result = ZCSR_SCENE_IO_BAD_RECORD;
    if (result == ZCSR_SCENE_IO_OK) *out = *tmp;
    free(tmp);
    return result;
}

zcsr_scene_io_result zcsr_scene_io_deserialize(const char *text,
                                                          size_t size,
                                                          zcsr_scene *scene) {
    if (!text || !scene || size == 0u)
        return ZCSR_SCENE_IO_INVALID_ARGUMENT;
    return parse_scene_text(text, size, scene);
}

static char *read_all_text(const char *path, size_t *out_size) {
    FILE *f;
    char *data = NULL;
    size_t size = 0u;
    size_t cap = 0u;

    if (!path || !*path || !out_size) return NULL;
#if defined(_WIN32)
    f = open_utf8(path, L"rb");
#else
    f = open_utf8(path, "rb");
#endif
    if (!f) return NULL;
    for (;;) {
        size_t got;
        if (size == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            char *next;
            if (next_cap > ZCSR_SCENE_IO_TEXT_LIMIT) next_cap = ZCSR_SCENE_IO_TEXT_LIMIT;
            if (next_cap <= cap) break;
            next = (char *)realloc(data, next_cap);
            if (!next) break;
            data = next;
            cap = next_cap;
        }
        got = fread(data + size, 1u, cap - size, f);
        size += got;
        if (got == 0u) break;
    }
    if (ferror(f) || size == 0u) {
        free(data);
        data = NULL;
        size = 0u;
    }
    fclose(f);
    *out_size = size;
    return data;
}

zcsr_scene_io_result zcsr_scene_io_load(zcsr_scene *scene,
                                                   const char *utf8_path) {
    char *text;
    size_t size = 0u;

    if (!scene || !utf8_path || !*utf8_path)
        return ZCSR_SCENE_IO_INVALID_ARGUMENT;
    text = read_all_text(utf8_path, &size);
    if (!text) return ZCSR_SCENE_IO_OPEN_FAILED;
    {
        zcsr_scene_io_result rc = parse_scene_text(text, size, scene);
        free(text);
        return rc;
    }
}

const char *zcsr_scene_io_result_name(
    zcsr_scene_io_result result) {
    switch (result) {
    case ZCSR_SCENE_IO_OK: return "ok";
    case ZCSR_SCENE_IO_INVALID_ARGUMENT: return "invalid argument";
    case ZCSR_SCENE_IO_OPEN_FAILED: return "open failed";
    case ZCSR_SCENE_IO_BAD_HEADER: return "bad header";
    case ZCSR_SCENE_IO_BAD_RECORD: return "bad record";
    case ZCSR_SCENE_IO_CAPACITY: return "capacity";
    case ZCSR_SCENE_IO_IO_ERROR: return "io error";
    default: return "unknown";
    }
}

