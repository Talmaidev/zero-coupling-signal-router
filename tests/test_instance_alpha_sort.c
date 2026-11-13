/* CPU-only contract test for alpha-aware translucent sorting.
 * Does not create a GL context; zcsr_sort_state is pure CPU. */
#include "zcsr/gl_render.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, name) do { \
    if (!(cond)) { printf("FAIL %s (line %d)\n", (name), __LINE__); ++failures; } \
} while (0)

static zcsr_gl_instance make_instance(uint32_t tex, float z, float a, bool has_alpha) {
    zcsr_gl_instance s;
    memset(&s, 0, sizeof(s));
    s.tex = tex;
    s.z = z;
    s.a = a;
    s.has_alpha = has_alpha;
    return s;
}

int main(void) {
    zcsr_gl_instance instances[3];
    zcsr_gl_instance temp[3];
    size_t opaque = 0;

    /* Opaque background, an alpha-textured sprite at z=1, and a normal
     * translucent sprite at z=2. Larger z is farther and must draw first. */
    instances[0] = make_instance(1u, 0.0f, 1.0f, false);
    instances[1] = make_instance(2u, 1.0f, 1.0f, true);
    instances[2] = make_instance(3u, 2.0f, 0.5f, false);

    (void)zcsr_sort_state(instances, 3u, temp, sizeof(temp), &opaque);

    CHECK(opaque == 1u, "only background is opaque");
    CHECK(instances[0].tex == 1u, "opaque background first");
    CHECK(instances[1].tex == 3u, "farthest translucent first");
    CHECK(instances[2].tex == 2u, "alpha-textured sprite is translucent");
    CHECK(instances[2].has_alpha && instances[2].z == 1.0f,
          "alpha sprite keeps its fields");

    {
        size_t out = 99u;
        CHECK(zcsr_sort_state(NULL, 1u, temp, sizeof(temp), &out) == 0u && out == 0u,
              "null instance array rejected");
        out = 99u;
        CHECK(zcsr_sort_state(instances, 1u, NULL, 0u, &out) == 0u && out == 0u,
              "null scratch rejected");
        out = 99u;
        CHECK(zcsr_sort_state(instances, 1u, temp, 0u, &out) == 0u && out == 0u,
              "undersized scratch rejected");
        out = 99u;
        CHECK(zcsr_sort_state(instances, 0u, temp, sizeof(temp), &out) == 0u && out == 0u,
              "empty array rejected");
    }

    if (failures == 0) {
        printf("test_instance_alpha_sort PASS\n");
        return 0;
    }
    printf("test_instance_alpha_sort FAIL (%d)\n", failures);
    return 1;
}
