/* Color pipeline contract test: default sRGB endpoints, LUT shape/monotonicity,
 * and a custom descriptor replacing the transfer curve without shader changes. */
#include "zcsr/color_pipeline.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static float identity(float v) { return v; }

int main(void) {
    const zcsr_color_pipeline_desc* def = zcsr_color_pipeline_default();
    uint8_t lut[ZCSR_COLOR_LUT_ENTRIES * 4];
    int i;
    float last = -1.0f;
    const zcsr_color_pipeline_desc custom = {
        "test.identity",
        identity,
        identity
    };

    assert(def != NULL);
    assert(def->stable_key != NULL);
    assert(fabsf(zcsr_color_pipeline_to_display(def, 0.0f) - 0.0f) < 1e-5f);
    assert(fabsf(zcsr_color_pipeline_to_display(def, 1.0f) - 1.0f) < 1e-5f);
    assert(fabsf(zcsr_color_pipeline_to_display(def, 0.5f) - 0.735356f) < 0.002f);
    assert(fabsf(zcsr_color_pipeline_to_linear(def, 0.5f) - 0.214041f) < 0.002f);

    assert(zcsr_color_pipeline_build_encode_lut(def, lut, ZCSR_COLOR_LUT_ENTRIES));
    for (i = 0; i < ZCSR_COLOR_LUT_ENTRIES; ++i) {
        float v = (float)lut[i * 4 + 0];
        assert(lut[i * 4 + 3] == 255u);
        assert(v + 0.5f >= last); /* 8-bit rounding may repeat one step */
        last = v;
    }
    assert(lut[0] == 0u);
    assert(lut[(ZCSR_COLOR_LUT_ENTRIES - 1) * 4] == 255u);

    assert(zcsr_color_pipeline_build_encode_lut(&custom, lut, ZCSR_COLOR_LUT_ENTRIES));
    assert(lut[0] == 0u);
    assert(lut[(ZCSR_COLOR_LUT_ENTRIES - 1) * 4] == 255u);
    i = ZCSR_COLOR_LUT_ENTRIES / 2;
    assert(abs((int)lut[i * 4] - 128) <= 1);

    puts("color_pipeline: PASS (default sRGB + replaceable identity LUT)");
    return 0;
}
