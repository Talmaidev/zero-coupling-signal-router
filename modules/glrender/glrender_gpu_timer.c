/* glrender_gpu_timer.c — optional GL_TIME_ELAPSED GPU timer.
 *
 * Desktop OpenGL 3.3 supports query objects. Android/GLES3 does not expose the
 * same core elapsed-time query in this renderer's minimal path, so the API
 * reports "unsupported" instead of pretending to measure. Query objects are
 * owned by the renderer's caller-provided buffer; no heap allocation. */
#include "glrender_internal.h"

#if defined(__ANDROID__)
bool zcsr_gl_gpu_timer_begin(zcsr_gl_renderer* r) {
    (void)r;
    return false;
}

bool zcsr_gl_gpu_timer_end(zcsr_gl_renderer* r) {
    (void)r;
    return false;
}

bool zcsr_gl_gpu_timer_last_ms(zcsr_gl_renderer* r, float* out_ms) {
    (void)r;
    (void)out_ms;
    return false;
}

void zcsr_gl_gpu_timer_destroy(zcsr_gl_renderer* r) {
    (void)r;
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
