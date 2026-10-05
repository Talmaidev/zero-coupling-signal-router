/* State sort unit tests — opaque/translucent partition, opaque texture order, translucent
 * back-to-front z order. Pure logic, headless. Uses CHECK (runs in Release). */
#include "zcsr/gl_render.h"

#include <stdio.h>
#include <string.h>

#define N 20

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)

static zcsr_gl_instance make(uint32_t tex, float a, float z, float mode) {
    zcsr_gl_instance in;
    memset(&in, 0, sizeof in);
    in.cx = 0; in.cy = 0; in.w = 8; in.h = 8;
    in.u0 = 0; in.v0 = 0; in.u1 = 1; in.v1 = 1;
    in.r = 1; in.g = 1; in.b = 1; in.a = a;
    in.mode = mode; in.tex = tex; in.z = z;
    return in;
}

int main(void) {
    static zcsr_gl_instance items[N];
    static zcsr_gl_instance scratch[N];
    size_t opaque;

    /* 10 opaque (a=1, mode 0), 10 translucent (a=0.5), interleaved. */
    for (int i = 0; i < N; ++i) {
        int translucent = (i % 2) == 1;
        uint32_t tex = (uint32_t)(10 - i / 2); /* descending texture ids */
        float z = (float)i;                     /* increasing z */
        items[i] = make(tex, translucent ? 0.5f : 1.0f, z, 0.0f);
    }

    zcsr_sort_state(items, N, scratch, &opaque);
    CHECK(opaque == 10);

    /* first 10 are opaque, sorted by tex ascending */
    for (size_t i = 0; i < opaque; ++i) {
        CHECK(items[i].a == 1.0f);
        if (i > 0) CHECK(items[i].tex >= items[i - 1].tex);
    }

    /* last 10 are translucent, sorted by z descending (far first) */
    for (size_t i = opaque; i < N; ++i) {
        CHECK(items[i].a == 0.5f);
        if (i > opaque) CHECK(items[i].z <= items[i - 1].z);
    }

    /* MIX/ADD with a=1 is still translucent */
    {
        zcsr_gl_instance mix[2];
        mix[0] = make(1, 1.0f, 0.0f, 1.0f); /* MIX -> translucent */
        mix[1] = make(2, 1.0f, 0.0f, 0.0f); /* opaque */
        size_t op;
        zcsr_sort_state(mix, 2, scratch, &op);
        CHECK(op == 1);
        CHECK(mix[0].tex == 2); /* opaque one first */
    }

    printf("test_state: PASS (opaque=%zu, texture-asc / z-desc)\n", opaque);
    return 0;
}
