#ifndef ZCSR_COLOR_SHADER_SEGMENTS_H
#define ZCSR_COLOR_SHADER_SEGMENTS_H
/* color_shader_segments.h — GLSL stage segments owned by the color pipeline.
 *
 * Ruling 8: the instanced fragment shader's recolor / ramp / OKLab / chroma
 * bodies are assembled from these segments instead of being inlined in the
 * renderer module. Which segment set a build compiles in is decided by the
 * color tier (LINEAR_HDR / DIRECT_SRGB) at assembly time — never by a
 * per-frame bool on the hot path.
 *
 * These are opaque shader-source strings; the CPU side never parses them.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* sRGB display -> linear decoder shared by both tiers (input decode only;
 * this is not the frame encode pass). */
extern const char* zcsr_glsl_display_to_linear;

/* HSV pair: hue-family remap and saturation/value conditioning depend on it.
 * LINEAR_HDR only; DIRECT_SRGB does not compile this segment. */
extern const char* zcsr_glsl_hsv_pair;

/* OKLab pair: material-masked preserve-luminance ramp mode. LINEAR_HDR only. */
extern const char* zcsr_glsl_oklab_pair;

/* apply_recolor(): hue-family remap + 1D ramp + OKLab ramp + chroma/value
 * conditioning. Depends on the HSV and OKLab segments. LINEAR_HDR only. */
extern const char* zcsr_glsl_apply_recolor;

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_COLOR_SHADER_SEGMENTS_H */
