#ifndef ZCSR_COLOR_PIPELINE_H
#define ZCSR_COLOR_PIPELINE_H

/* zcsr/color_pipeline.h — replaceable color-transfer algorithm contract.
 *
 * The engine default is one linear working space, encoded to the display
 * space only at the final composite. The transfer curve (today sRGB) is NOT
 * hard-coded in a shader: it is generated into a LUT by this module and the
 * shader samples that LUT. A future algorithm can replace the descriptor
 * without editing shader source.
 *
 * This module is pure CPU logic. It owns no GPU object, no window, no UIM
 * page. The renderer consumes it; the editor/runtime may also call it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of entries in the default 1D display-encode LUT. 1024 keeps the
 * dark end of the sRGB curve stable while remaining a 4 KiB RGBA8 texture. */
#define ZCSR_COLOR_LUT_ENTRIES 1024

typedef struct zcsr_color_pipeline_desc {
    /* Stable key for project/diagnostic identity; never a localized label. */
    const char* stable_key;
    /* linear-light [0,1] -> display-encoded [0,1]. */
    float (*to_display)(float linear_value);
    /* display-encoded [0,1] -> linear-light [0,1]. */
    float (*to_linear)(float display_value);
} zcsr_color_pipeline_desc;

/* The engine default: one linear working space, sRGB display encode/decode.
 * The descriptor has static lifetime; callers must not free it. */
const zcsr_color_pipeline_desc* zcsr_color_pipeline_default(void);

/* Build a horizontal RGBA8 encode LUT. `rgba` must hold entries*4 bytes.
 * RGB receive the encoded value, A is 255. Returns false on invalid input. */
bool zcsr_color_pipeline_build_encode_lut(const zcsr_color_pipeline_desc* pipeline,
                                          uint8_t* rgba, int entries);

/* Convenience CPU transfer helpers. */
float zcsr_color_pipeline_to_display(const zcsr_color_pipeline_desc* pipeline,
                                     float linear_value);
float zcsr_color_pipeline_to_linear(const zcsr_color_pipeline_desc* pipeline,
                                    float display_value);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_COLOR_PIPELINE_H */
