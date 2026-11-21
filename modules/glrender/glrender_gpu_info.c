/* glrender_gpu_info.c — runtime GPU identity for the one supported GL path.
 *
 * Why this exists: Linux abandoned-laptop users overwhelmingly run an Intel
 * (iris/i965) or AMD (radeonsi) integrated GPU through Mesa. The engine must
 * select that path automatically at runtime; it must not require a per-vendor
 * build macro, nor an application call to a vendor-setup API. The context is
 * already generic GL 3.3 Core, so the only runtime work is to identify what
 * Mesa actually gave us and report it once. No renderer algorithm branches on
 * this classification.
 *
 * The string arrays are fixed-size, like the rest of zcsr_gl_renderer; this
 * translation unit performs no allocation.
 */
#include "glrender_internal.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>

#if defined(__ANDROID__)
#  define ZCSR_GL_GPU_LOG(...) __android_log_print(ANDROID_LOG_INFO, "zcsr-gl", __VA_ARGS__)
#else
#  define ZCSR_GL_GPU_LOG(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#endif

static void zcsr_gl_copy_string(char* dst, size_t cap, const char* src) {
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ASCII case-insensitive substring search; GL strings are ASCII in practice,
 * and avoiding strcasestr keeps this portable to MSVC and Android. */
static bool zcsr_gl_ci_contains(const char* haystack, const char* needle) {
    size_t n;
    if (!haystack || !needle || !*needle) return false;
    n = strlen(needle);
    for (; *haystack; ++haystack) {
        size_t i = 0;
        while (i < n && haystack[i] &&
               tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

static const char* zcsr_gl_gpu_vendor_name(zcsr_gl_gpu_vendor v) {
    switch (v) {
    case ZCSR_GL_GPU_INTEL: return "intel";
    case ZCSR_GL_GPU_AMD: return "amd";
    case ZCSR_GL_GPU_NVIDIA: return "nvidia";
    case ZCSR_GL_GPU_APPLE: return "apple";
    case ZCSR_GL_GPU_SOFTWARE: return "software";
    default: return "unknown";
    }
}

static const char* zcsr_gl_gpu_driver_name(zcsr_gl_gpu_driver d) {
    switch (d) {
    case ZCSR_GL_DRIVER_IRIS: return "iris";
    case ZCSR_GL_DRIVER_I965: return "i965";
    case ZCSR_GL_DRIVER_RADEONSI: return "radeonsi";
    case ZCSR_GL_DRIVER_ZINK: return "zink";
    case ZCSR_GL_DRIVER_LLVMPIPE: return "llvmpipe";
    case ZCSR_GL_DRIVER_NOUVEAU: return "nouveau";
    case ZCSR_GL_DRIVER_NVIDIA: return "nvidia";
    case ZCSR_GL_DRIVER_APPLE: return "apple";
    case ZCSR_GL_DRIVER_MESA: return "mesa";
    default: return "generic";
    }
}

static const char* zcsr_gl_backend_name(void) {
#if defined(__ANDROID__)
    return "egl-android";
#elif defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11)
    return "glx";
#elif defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)
    return "egl-wayland";
#elif defined(_WIN32)
    return "wgl";
#elif defined(__APPLE__)
    return "cgl";
#else
    return "headless";
#endif
}

void zcsr_gl_detect_gpu(zcsr_gl_renderer* r) {
    const char* vendor;
    const char* renderer;
    const char* version;

    if (!r) return;
    r->gpu_vendor = ZCSR_GL_GPU_UNKNOWN;
    r->gpu_driver = ZCSR_GL_DRIVER_GENERIC;
    r->gpu_software = false;
    r->gpu_probed = false;

    vendor = (const char*)glGetString(GL_VENDOR);
    renderer = (const char*)glGetString(GL_RENDERER);
    version = (const char*)glGetString(GL_VERSION);
    if (!vendor && !renderer) return;

    zcsr_gl_copy_string(r->gpu_vendor_string, sizeof(r->gpu_vendor_string), vendor);
    zcsr_gl_copy_string(r->gpu_renderer_string, sizeof(r->gpu_renderer_string), renderer);
    zcsr_gl_copy_string(r->gpu_version_string, sizeof(r->gpu_version_string), version);

    r->gpu_software = zcsr_gl_ci_contains(renderer, "llvmpipe") ||
                      zcsr_gl_ci_contains(renderer, "softpipe") ||
                      zcsr_gl_ci_contains(renderer, "swrast");

    if (zcsr_gl_ci_contains(renderer, "iris")) {
        r->gpu_driver = ZCSR_GL_DRIVER_IRIS;
    } else if (zcsr_gl_ci_contains(renderer, "i965")) {
        r->gpu_driver = ZCSR_GL_DRIVER_I965;
    } else if (zcsr_gl_ci_contains(renderer, "radeonsi")) {
        r->gpu_driver = ZCSR_GL_DRIVER_RADEONSI;
    } else if (zcsr_gl_ci_contains(renderer, "zink")) {
        r->gpu_driver = ZCSR_GL_DRIVER_ZINK;
    } else if (zcsr_gl_ci_contains(renderer, "llvmpipe")) {
        r->gpu_driver = ZCSR_GL_DRIVER_LLVMPIPE;
    } else if (zcsr_gl_ci_contains(renderer, "nouveau")) {
        r->gpu_driver = ZCSR_GL_DRIVER_NOUVEAU;
    } else if (zcsr_gl_ci_contains(renderer, "nvidia")) {
        r->gpu_driver = ZCSR_GL_DRIVER_NVIDIA;
    } else if (zcsr_gl_ci_contains(renderer, "apple")) {
        r->gpu_driver = ZCSR_GL_DRIVER_APPLE;
    } else if (zcsr_gl_ci_contains(renderer, "mesa") ||
               zcsr_gl_ci_contains(vendor, "mesa")) {
        r->gpu_driver = ZCSR_GL_DRIVER_MESA;
    }

    /* Vendor is inferred from the renderer first because Mesa often reports
     * vendor="Mesa" while the renderer names the real Intel/AMD GPU. */
    if (zcsr_gl_ci_contains(renderer, "intel") || zcsr_gl_ci_contains(vendor, "intel")) {
        r->gpu_vendor = ZCSR_GL_GPU_INTEL;
    } else if (zcsr_gl_ci_contains(renderer, "amd") ||
               zcsr_gl_ci_contains(renderer, "ati") ||
               zcsr_gl_ci_contains(renderer, "radeon") ||
               zcsr_gl_ci_contains(vendor, "amd") ||
               zcsr_gl_ci_contains(vendor, "ati")) {
        r->gpu_vendor = ZCSR_GL_GPU_AMD;
    } else if (zcsr_gl_ci_contains(renderer, "nvidia") ||
               zcsr_gl_ci_contains(renderer, "geforce") ||
               zcsr_gl_ci_contains(vendor, "nvidia")) {
        r->gpu_vendor = ZCSR_GL_GPU_NVIDIA;
    } else if (zcsr_gl_ci_contains(renderer, "apple") ||
               zcsr_gl_ci_contains(vendor, "apple")) {
        r->gpu_vendor = ZCSR_GL_GPU_APPLE;
    }
    if (r->gpu_software) r->gpu_vendor = ZCSR_GL_GPU_SOFTWARE;

    /* Mesa's GL_RENDERER usually names the hardware, not the DRI driver
     * (e.g. "Mesa Intel(R) UHD Graphics 630"). For diagnostics, an Intel/AMD
     * Mesa context is the iris/radeonsi path in practice; the render path is
     * still the same GL 3.3 Core code, so this only improves the startup line. */
    if (r->gpu_driver == ZCSR_GL_DRIVER_MESA) {
        if (r->gpu_vendor == ZCSR_GL_GPU_INTEL) r->gpu_driver = ZCSR_GL_DRIVER_IRIS;
        else if (r->gpu_vendor == ZCSR_GL_GPU_AMD) r->gpu_driver = ZCSR_GL_DRIVER_RADEONSI;
    }

    r->gpu_probed = true;
    ZCSR_GL_GPU_LOG("[gl] gpu: vendor=%s driver=%s backend=%s software=%s renderer=\"%s\" version=\"%s\"\n",
                    zcsr_gl_gpu_vendor_name(r->gpu_vendor),
                    zcsr_gl_gpu_driver_name(r->gpu_driver),
                    zcsr_gl_backend_name(),
                    r->gpu_software ? "yes" : "no",
                    r->gpu_renderer_string[0] ? r->gpu_renderer_string : "(null)",
                    r->gpu_version_string[0] ? r->gpu_version_string : "(null)");
}

/* Heuristic iGPU classification, used for diagnostics only. The engine must
 * work identically when this returns "unknown": the render path is GL 3.3 Core
 * regardless of vendor. */
static bool zcsr_gl_gpu_classify_integrated(zcsr_gl_gpu_vendor vendor,
                                            const char* renderer,
                                            bool* known) {
    *known = true;
    if (!renderer) { *known = false; return false; }
    switch (vendor) {
    case ZCSR_GL_GPU_INTEL:
        /* Intel Arc is a discrete product line; Iris Xe/UHD/HD are iGPUs. */
        if (zcsr_gl_ci_contains(renderer, "arc")) return false;
        return true;
    case ZCSR_GL_GPU_AMD:
        /* AMD APU marketing strings: Vega APUs, Renoir/Cezanne/Rembrandt/
         * Phoenix, and the generic "Radeon Graphics" used by Ryzen APUs.
         * "RX"/"Navi"/"Polaris"/"Vega 56/64" are discrete. */
        if (zcsr_gl_ci_contains(renderer, "rx ") ||
            zcsr_gl_ci_contains(renderer, "navi") ||
            zcsr_gl_ci_contains(renderer, "polaris") ||
            zcsr_gl_ci_contains(renderer, "ellesmere") ||
            zcsr_gl_ci_contains(renderer, "vega 56") ||
            zcsr_gl_ci_contains(renderer, "vega 64")) {
            return false;
        }
        if (zcsr_gl_ci_contains(renderer, "radeon graphics") ||
            zcsr_gl_ci_contains(renderer, "vega") ||
            zcsr_gl_ci_contains(renderer, "renoir") ||
            zcsr_gl_ci_contains(renderer, "cezanne") ||
            zcsr_gl_ci_contains(renderer, "rembrandt") ||
            zcsr_gl_ci_contains(renderer, "phoenix") ||
            zcsr_gl_ci_contains(renderer, "raven") ||
            zcsr_gl_ci_contains(renderer, "picasso") ||
            zcsr_gl_ci_contains(renderer, "bristol") ||
            zcsr_gl_ci_contains(renderer, "stoney") ||
            zcsr_gl_ci_contains(renderer, "lucienne")) {
            return true;
        }
        *known = false;
        return false;
    case ZCSR_GL_GPU_APPLE:
        if (zcsr_gl_ci_contains(renderer, "apple m")) return true;
        *known = false;
        return false;
    case ZCSR_GL_GPU_NVIDIA:
        return false; /* NVIDIA renderer is a discrete GPU in practice */
    case ZCSR_GL_GPU_SOFTWARE:
        return false;
    default:
        *known = false;
        return false;
    }
}

bool zcsr_gl_query_gpu_info(const zcsr_gl_renderer* r, zcsr_gl_gpu_info* out) {
    if (!r || !out || !r->gpu_probed) return false;
    memset(out, 0, sizeof *out);
    switch (r->gpu_vendor) {
    case ZCSR_GL_GPU_INTEL:    out->vendor = ZCSR_GPU_VENDOR_INTEL; break;
    case ZCSR_GL_GPU_AMD:      out->vendor = ZCSR_GPU_VENDOR_AMD; break;
    case ZCSR_GL_GPU_NVIDIA:   out->vendor = ZCSR_GPU_VENDOR_NVIDIA; break;
    case ZCSR_GL_GPU_APPLE:    out->vendor = ZCSR_GPU_VENDOR_APPLE; break;
    case ZCSR_GL_GPU_SOFTWARE: out->vendor = ZCSR_GPU_VENDOR_SOFTWARE; break;
    default:                   out->vendor = ZCSR_GPU_VENDOR_UNKNOWN; break;
    }
    switch (r->gpu_driver) {
    case ZCSR_GL_DRIVER_IRIS:     out->driver = ZCSR_GPU_DRIVER_IRIS; break;
    case ZCSR_GL_DRIVER_I965:     out->driver = ZCSR_GPU_DRIVER_I965; break;
    case ZCSR_GL_DRIVER_RADEONSI: out->driver = ZCSR_GPU_DRIVER_RADEONSI; break;
    case ZCSR_GL_DRIVER_ZINK:     out->driver = ZCSR_GPU_DRIVER_ZINK; break;
    case ZCSR_GL_DRIVER_LLVMPIPE: out->driver = ZCSR_GPU_DRIVER_LLVMPIPE; break;
    case ZCSR_GL_DRIVER_NOUVEAU:  out->driver = ZCSR_GPU_DRIVER_NOUVEAU; break;
    case ZCSR_GL_DRIVER_NVIDIA:   out->driver = ZCSR_GPU_DRIVER_NVIDIA; break;
    case ZCSR_GL_DRIVER_APPLE:    out->driver = ZCSR_GPU_DRIVER_APPLE; break;
    case ZCSR_GL_DRIVER_MESA:     out->driver = ZCSR_GPU_DRIVER_MESA; break;
    default:                      out->driver = ZCSR_GPU_DRIVER_GENERIC; break;
    }
    out->integrated_known = true;
    out->integrated = zcsr_gl_gpu_classify_integrated(
        r->gpu_vendor, r->gpu_renderer_string, &out->integrated_known);
    out->software = r->gpu_software;
    zcsr_gl_copy_string(out->vendor_string, sizeof out->vendor_string, r->gpu_vendor_string);
    zcsr_gl_copy_string(out->renderer_string, sizeof out->renderer_string, r->gpu_renderer_string);
    zcsr_gl_copy_string(out->version_string, sizeof out->version_string, r->gpu_version_string);
    zcsr_gl_copy_string(out->backend, sizeof out->backend, zcsr_gl_backend_name());
    return true;
}
