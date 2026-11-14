#include "zcsr/assets.h"
#include "zcsr/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_asset_base[ZCSR_ASSET_PATH_CAP];

bool zcsr_assets_set_base_dir(const char* dir) {
    if (!dir || !dir[0]) {
        g_asset_base[0] = '\0';
        return true;
    }
    if (strlen(dir) >= sizeof(g_asset_base)) return false;
    snprintf(g_asset_base, sizeof(g_asset_base), "%s", dir);
    return true;
}

static bool is_absolute(const char* p) {
    if (!p || !p[0]) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    if (p[1] == ':' && (p[2] == '/' || p[2] == '\\')) return true;   /* Windows drive */
    return false;
}

bool zcsr_assets_resolve(const char* relative, char* out, int cap) {
    const char* base;
    int need;
    if (!relative || !relative[0] || !out || cap <= 1) return false;
    if (is_absolute(relative)) {
        if ((int)strlen(relative) >= cap) return false;
        snprintf(out, (size_t)cap, "%s", relative);
        return true;
    }
    base = g_asset_base[0] ? g_asset_base : getenv("ZCSR_ASSET_DIR");
    if (!base || !base[0]) base = ".";
    need = (int)(strlen(base) + 1 + strlen(relative) + 1);
    if (need > cap) return false;
    snprintf(out, (size_t)cap, "%s/%s", base, relative);
    return true;
}

static bool read_file(const char* path, unsigned char** data, size_t* len) {
    FILE* f = fopen(path, "rb");
    long sz;
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    *data = (unsigned char*)malloc((size_t)sz);
    if (!*data) { fclose(f); return false; }
    if (fread(*data, 1, (size_t)sz, f) != (size_t)sz) {
        free(*data);
        *data = NULL;
        fclose(f);
        return false;
    }
    fclose(f);
    *len = (size_t)sz;
    return true;
}

bool zcsr_assets_texture(zcsr_gl_renderer* r, const char* relative,
                         zcsr_gl_texture* out_tex, int* out_w, int* out_h) {
    char path[ZCSR_ASSET_PATH_CAP];
    unsigned char* encoded = NULL;
    size_t encoded_len = 0;
    void* arena = NULL;
    size_t arena_bytes;
    zcsr_image img = { 0, 0, NULL };
    zcsr_gl_texture tex;
    int attempt;

    if (!r || !out_tex) return false;
    if (!zcsr_assets_resolve(relative, path, (int)sizeof(path))) return false;
    if (!read_file(path, &encoded, &encoded_len)) return false;

    /* image 的缓冲要同时装解码 scratch 和 w*h*4 输出，且不暴露尺寸；
     * 先按 8x 文件大小 + 4MiB 试，失败翻倍重试（最多 5 次）。 */
    arena_bytes = encoded_len * 8u + (4u << 20);
    for (attempt = 0; attempt < 5; ++attempt) {
        arena = malloc(arena_bytes);
        if (!arena) break;
        img = zcsr_image_decode(encoded, encoded_len, arena, arena_bytes);
        if (img.rgba && img.w > 0 && img.h > 0) break;
        free(arena);
        arena = NULL;
        arena_bytes *= 2u;
    }
    free(encoded);
    if (!img.rgba) return false;

    tex = zcsr_gl_texture_create(r, img.rgba, img.w, img.h);
    free(arena);
    if (!tex) return false;
    *out_tex = tex;
    if (out_w) *out_w = img.w;
    if (out_h) *out_h = img.h;
    return true;
}

bool zcsr_assets_font(zcsr_gl_renderer* r, const char* relative, int pixel_height,
                      void* buffer, size_t bytes) {
    char path[ZCSR_ASSET_PATH_CAP];
    if (!r || !buffer || bytes == 0) return false;
    if (!zcsr_assets_resolve(relative, path, (int)sizeof(path))) return false;
    return zcsr_gl_font_from_file(r, path, pixel_height, buffer, bytes);
}

bool zcsr_assets_sound(zcsr_mixer* m, const char* id, const char* relative) {
    char path[ZCSR_ASSET_PATH_CAP];
    if (!m || !id || !id[0]) return false;
    if (!zcsr_assets_resolve(relative, path, (int)sizeof(path))) return false;
    return zcsr_mix_register_wav(m, id, path);
}
