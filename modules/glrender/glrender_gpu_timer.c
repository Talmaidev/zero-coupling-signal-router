/* glrender_gpu_timer.c — optional GPU elapsed-time query.
 *
 * Desktop OpenGL 3.3 exposes GL_TIME_ELAPSED in core. GLES3 does not; Android
 * uses GL_EXT_disjoint_timer_query through eglGetProcAddress. When the driver
 * lacks the extension, begin() returns false instead of pretending to measure.
 * Query objects are owned by the renderer's caller-provided buffer; no heap. */
#include "glrender_internal.h"

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <string.h>

#define ZCSR_GL_TIME_ELAPSED_EXT 0x88BF
#define ZCSR_GL_QUERY_RESULT_EXT 0x8866
#define ZCSR_GL_QUERY_RESULT_AVAILABLE_EXT 0x8867

typedef void (*zcsr_pfn_gen_queries)(GLsizei, GLuint*);
typedef void (*zcsr_pfn_begin_query)(GLenum, GLuint);
typedef void (*zcsr_pfn_end_query)(GLenum);
typedef void (*zcsr_pfn_get_query_uiv)(GLuint, GLenum, GLuint*);
typedef void (*zcsr_pfn_get_query_ui64v)(GLuint, GLenum, unsigned long long*);

static struct {
    bool checked;
    bool supported;
    zcsr_pfn_gen_queries gen;
    zcsr_pfn_begin_query begin;
    zcsr_pfn_end_query end;
    zcsr_pfn_get_query_uiv available;
    zcsr_pfn_get_query_ui64v result;
} s_timer_ext;

static void zcsr_gl_gpu_timer_load(void) {
    const char* ext;
    if (s_timer_ext.checked) return;
    s_timer_ext.checked = true;
    ext = glGetString(GL_EXTENSIONS);
    if (!ext || !strstr(ext, "GL_EXT_disjoint_timer_query")) {
        __android_log_print(ANDROID_LOG_INFO, "zcsr-gl",
            "GL_EXT_disjoint_timer_query not advertised; GPU timer disabled");
        return;
    }
    s_timer_ext.gen = (zcsr_pfn_gen_queries)eglGetProcAddress("glGenQueriesEXT");
    s_timer_ext.begin = (zcsr_pfn_begin_query)eglGetProcAddress("glBeginQueryEXT");
    s_timer_ext.end = (zcsr_pfn_end_query)eglGetProcAddress("glEndQueryEXT");
    s_timer_ext.available = (zcsr_pfn_get_query_uiv)eglGetProcAddress(
        "glGetQueryObjectuivEXT");
    s_timer_ext.result = (zcsr_pfn_get_query_ui64v)eglGetProcAddress(
        "glGetQueryObjectui64vEXT");
    s_timer_ext.supported =
        s_timer_ext.gen && s_timer_ext.begin && s_timer_ext.end &&
        s_timer_ext.available && s_timer_ext.result;
    __android_log_print(ANDROID_LOG_INFO, "zcsr-gl",
        "GL_EXT_disjoint_timer_query %s",
        s_timer_ext.supported ? "available" : "not exposed by driver");
}

static bool zcsr_gl_gpu_timer_ensure(zcsr_gl_renderer* r) {
    if (!r) return false;
    if (r->gpu_timer_queries[0] != 0 && r->gpu_timer_queries[1] != 0) return true;
    if (!s_timer_ext.supported) return false;
    s_timer_ext.gen(2, r->gpu_timer_queries);
    return r->gpu_timer_queries[0] != 0 && r->gpu_timer_queries[1] != 0;
}

bool zcsr_gl_gpu_timer_begin(zcsr_gl_renderer* r) {
    if (!r || r->gpu_timer_active) return false;
    if (!zcsr_gl_make_current(r)) return false;
    zcsr_gl_gpu_timer_load();
    if (!zcsr_gl_gpu_timer_ensure(r)) return false;
    s_timer_ext.begin(ZCSR_GL_TIME_ELAPSED_EXT,
                      r->gpu_timer_queries[r->gpu_timer_write]);
    r->gpu_timer_active = true;
    return true;
}

bool zcsr_gl_gpu_timer_end(zcsr_gl_renderer* r) {
    if (!r || !r->gpu_timer_active) return false;
    s_timer_ext.end(ZCSR_GL_TIME_ELAPSED_EXT);
    r->gpu_timer_active = false;
    r->gpu_timer_read = r->gpu_timer_write;
    r->gpu_timer_write ^= 1;
    r->gpu_timer_pending = true;
    return true;
}

bool zcsr_gl_gpu_timer_last_ms(zcsr_gl_renderer* r, float* out_ms) {
    GLuint available = 0;
    unsigned long long ns = 0;
    if (!r || !out_ms || !r->gpu_timer_pending) return false;
    if (!zcsr_gl_make_current(r)) return false;
    zcsr_gl_gpu_timer_load();
    if (!s_timer_ext.supported) return false;
    s_timer_ext.available(r->gpu_timer_queries[r->gpu_timer_read],
                          ZCSR_GL_QUERY_RESULT_AVAILABLE_EXT, &available);
    if (!available) return false;
    s_timer_ext.result(r->gpu_timer_queries[r->gpu_timer_read],
                       ZCSR_GL_QUERY_RESULT_EXT, &ns);
    r->gpu_timer_pending = false;
    *out_ms = (float)ns / 1000000.0f;
    return true;
}

void zcsr_gl_gpu_timer_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    if (r->gpu_timer_queries[0] != 0 || r->gpu_timer_queries[1] != 0) {
        glDeleteQueries(2, r->gpu_timer_queries);
        r->gpu_timer_queries[0] = 0;
        r->gpu_timer_queries[1] = 0;
    }
    r->gpu_timer_active = false;
    r->gpu_timer_pending = false;
    r->gpu_timer_write = 0;
    r->gpu_timer_read = 0;
}
#else
static bool zcsr_gl_gpu_timer_ensure(zcsr_gl_renderer* r) {
    if (!r) return false;
    if (r->gpu_timer_queries[0] != 0 && r->gpu_timer_queries[1] != 0) return true;
    glGenQueries(2, r->gpu_timer_queries);
    return r->gpu_timer_queries[0] != 0 && r->gpu_timer_queries[1] != 0;
}

bool zcsr_gl_gpu_timer_begin(zcsr_gl_renderer* r) {
    if (!r || r->gpu_timer_active) return false;
    if (!zcsr_gl_make_current(r)) return false;
    if (!zcsr_gl_gpu_timer_ensure(r)) return false;
    glBeginQuery(GL_TIME_ELAPSED, r->gpu_timer_queries[r->gpu_timer_write]);
    r->gpu_timer_active = true;
    return true;
}

bool zcsr_gl_gpu_timer_end(zcsr_gl_renderer* r) {
    if (!r || !r->gpu_timer_active) return false;
    glEndQuery(GL_TIME_ELAPSED);
    r->gpu_timer_active = false;
    r->gpu_timer_read = r->gpu_timer_write;
    r->gpu_timer_write ^= 1;
    r->gpu_timer_pending = true;
    return true;
}

bool zcsr_gl_gpu_timer_last_ms(zcsr_gl_renderer* r, float* out_ms) {
    GLint available = 0;
#if defined(_WIN32)
    unsigned long long ns = 0;
#else
    GLuint64 ns = 0;
#endif
    if (!r || !out_ms || !r->gpu_timer_pending) return false;
    if (!zcsr_gl_make_current(r)) return false;
    glGetQueryObjectiv(r->gpu_timer_queries[r->gpu_timer_read],
                       GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return false;
    glGetQueryObjectui64v(r->gpu_timer_queries[r->gpu_timer_read],
                          GL_QUERY_RESULT, &ns);
    r->gpu_timer_pending = false;
    /* Some desktop drivers return a sentinel/partial value when the query
     * result is not yet coherent even though AVAILABLE reports true. A frame
     * above one second is not a measurement; report unavailable instead of
     * feeding garbage into perf logs. */
    if (ns == 0 || ns > 1000000000ull) return false;
    *out_ms = (float)ns / 1000000.0f;
    return true;
}

void zcsr_gl_gpu_timer_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    if (r->gpu_timer_queries[0] != 0 || r->gpu_timer_queries[1] != 0) {
        glDeleteQueries(2, r->gpu_timer_queries);
        r->gpu_timer_queries[0] = 0;
        r->gpu_timer_queries[1] = 0;
    }
    r->gpu_timer_active = false;
    r->gpu_timer_pending = false;
    r->gpu_timer_write = 0;
    r->gpu_timer_read = 0;
}
#endif
