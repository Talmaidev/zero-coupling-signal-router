#include "zcsr/color_pipeline.h"

#include <math.h>

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* sRGB transfer functions live here, in the algorithm module — never in a
 * shader source string. This is the current default descriptor; replacing it
 * is a matter of passing another descriptor to the renderer. */
static float srgb_to_display(float linear_value) {
    float v = clamp01(linear_value);
    if (v <= 0.0031308f) return 12.92f * v;
    return 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

static float srgb_to_linear(float display_value) {
    float v = clamp01(display_value);
    if (v <= 0.04045f) return v / 12.92f;
    return powf((v + 0.055f) / 1.055f, 2.4f);
}

static const zcsr_color_pipeline_desc g_default_pipeline = {
    "zcsr.srgb-linear",
    srgb_to_display,
    srgb_to_linear
};

const zcsr_color_pipeline_desc* zcsr_color_pipeline_default(void) {
    return &g_default_pipeline;
}

bool zcsr_color_pipeline_build_encode_lut(const zcsr_color_pipeline_desc* pipeline,
                                          uint8_t* rgba, int entries) {
    int i;
    if (pipeline == NULL || pipeline->to_display == NULL || rgba == NULL || entries <= 0) {
        return false;
    }
    for (i = 0; i < entries; ++i) {
        float linear = (entries == 1) ? 0.0f : (float)i / (float)(entries - 1);
        float encoded = clamp01(pipeline->to_display(linear));
        uint8_t value = (uint8_t)(encoded * 255.0f + 0.5f);
        rgba[i * 4 + 0] = value;
        rgba[i * 4 + 1] = value;
        rgba[i * 4 + 2] = value;
        rgba[i * 4 + 3] = 255u;
    }
    return true;
}

float zcsr_color_pipeline_to_display(const zcsr_color_pipeline_desc* pipeline,
                                     float linear_value) {
    if (pipeline == NULL || pipeline->to_display == NULL) return clamp01(linear_value);
    return clamp01(pipeline->to_display(linear_value));
}

float zcsr_color_pipeline_to_linear(const zcsr_color_pipeline_desc* pipeline,
                                    float display_value) {
    if (pipeline == NULL || pipeline->to_linear == NULL) return clamp01(display_value);
    return clamp01(pipeline->to_linear(display_value));
}
