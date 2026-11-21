#include "zcsr/assets.h"
#include "zcsr/image.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
static AAssetManager* g_android_asset_manager;
#endif

static char g_asset_base[ZCSR_ASSET_PATH_CAP];

bool zcsr_assets_set_android_asset_manager(void* asset_manager) {
#if defined(__ANDROID__)
    g_android_asset_manager = (AAssetManager*)asset_manager;
    return g_android_asset_manager != NULL;
#else
    (void)asset_manager;
    return false;
#endif
}

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

#if defined(__ANDROID__)
/* APK assets are not filesystem paths: read them through AAssetManager first.
 * The resolved path may start with "./" (default base), which is not an APK
 * asset name, so strip it before opening. */
static bool read_android_asset_into(const char* path, unsigned char* data, size_t capacity,
                                    size_t* len) {
    AAsset* asset;
    const char* name = path;
    off_t size;
    size_t done = 0;
    if (!g_android_asset_manager || !path || path[0] == '/' || !data || capacity == 0) return false;
    if (name[0] == '.' && name[1] == '/') name += 2;
    asset = AAssetManager_open(g_android_asset_manager, name, AASSET_MODE_STREAMING);
    if (!asset) return false;
    size = AAsset_getLength(asset);
    if (size <= 0 || (size_t)size > capacity) {
        AAsset_close(asset);
        return false;
    }
    while (done < (size_t)size) {
        int chunk = (int)(((size_t)size - done) > (size_t)INT_MAX
                              ? (size_t)INT_MAX : ((size_t)size - done));
        int got = AAsset_read(asset, data + done, (int)chunk);
        if (got <= 0) {
            AAsset_close(asset);
            return false;
        }
        done += (size_t)got;
    }
    AAsset_close(asset);
    *len = done;
    return true;
}
#endif

static bool read_file_into(const char* path, unsigned char* data, size_t capacity,
                           size_t* len) {
    FILE* f;
    long sz;
#if defined(__ANDROID__)
    if (read_android_asset_into(path, data, capacity, len)) return true;
#endif
    f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    sz = ftell(f);
    if (sz <= 0 || data == NULL || capacity == 0 || (size_t)sz > capacity) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return false;
    }
    fclose(f);
    *len = (size_t)sz;
    return true;
}

bool zcsr_assets_read(const char* relative, void* out, size_t capacity, size_t* out_len) {
    char path[ZCSR_ASSET_PATH_CAP];
    size_t len = 0;
    if (!relative || !out || capacity == 0) return false;
    if (!zcsr_assets_resolve(relative, path, (int)sizeof(path))) return false;
    if (!read_file_into(path, (unsigned char*)out, capacity, &len)) return false;
    if (out_len) *out_len = len;
    return true;
}

bool zcsr_assets_texture_with_buffers(zcsr_gl_renderer* r, const char* relative,
                                    unsigned char* encoded, size_t encoded_bytes,
                                    void* arena, size_t arena_bytes,
                                    zcsr_gl_texture* out_tex, int* out_w, int* out_h) {
    size_t encoded_len = 0;
    zcsr_image img = { 0, 0, NULL };
    zcsr_gl_texture tex;

    if (!r || !out_tex || !encoded || encoded_bytes == 0 || !arena || arena_bytes == 0)
        return false;
    if (!zcsr_assets_read(relative, encoded, encoded_bytes, &encoded_len)) return false;

    img = zcsr_image_decode(encoded, encoded_len, arena, arena_bytes);
    if (!img.rgba || img.w <= 0 || img.h <= 0) return false;

    tex = zcsr_gl_texture_create(r, img.rgba, img.w, img.h);
    if (!tex) return false;
    *out_tex = tex;
    if (out_w) *out_w = img.w;
    if (out_h) *out_h = img.h;
    return true;
}

bool zcsr_assets_font(zcsr_gl_renderer* r, const char* relative, int pixel_height,
                      void* buffer, size_t bytes) {
    size_t ttf_len = 0;
    if (!r || !buffer || bytes == 0) return false;
    if (!zcsr_assets_read(relative, buffer, bytes, &ttf_len)) return false;
    return zcsr_gl_font_from_bytes(r, buffer, ttf_len, pixel_height, buffer, bytes);
}

bool zcsr_assets_sound(zcsr_mixer* m, const char* id, const char* relative) {
    char path[ZCSR_ASSET_PATH_CAP];
    if (!m || !id || !id[0]) return false;
    if (!zcsr_assets_resolve(relative, path, (int)sizeof(path))) return false;
    return zcsr_mix_register_wav(m, id, path);
}
