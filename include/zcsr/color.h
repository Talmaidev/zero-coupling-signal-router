#ifndef ZCSR_COLOR_H
#define ZCSR_COLOR_H
/* Engine-ext — shared color-modulation type for path A (glrender), the offline
 * image-processing path, and the legacy Light2D tint. The legacy tint is a
 * single RGBA modulation per `mode`; the newer `zcsr_color_transform` adds
 * selective hue remap, a material-masked chroma gradient map, chroma/value
 * conditioning, an alpha-rim layer, and an HDR emissive core.
 *
 * Path A's GPU shader now operates in linear light: DISPLAY textures are
 * decoded by the sRGB sampler, the display-domain tint is decoded by
 * `display_to_linear`, and `mode` is applied to those linear values before
 * writing the RGBA16F frame. `image_make_variant` stays DISPLAY-encoded and
 * quantizes to uint8 per pixel, so CPU/GPU results are not bit-exact.
 * Light2D's per-face tint is still a display-domain input decoded once at the
 * geometry boundary; its G-buffer/composite are linear.
 *
 * MULTIPLY is value 0 so a zero-initialized modulation reproduces the legacy
 * `texel * color` behavior — existing opaque sprites are unaffected. MIX/ADD
 * are needed because a pure multiply can only darken; "flash red" / "freeze
 * blue" must lighten/tint toward a color. */

typedef enum {
    ZCSR_MOD_MULTIPLY = 0, /* texel * color                  (default; legacy behavior)        */
    ZCSR_MOD_MIX,          /* lerp(texel, color, amount)      (tint toward color: flash/freeze) */
    ZCSR_MOD_ADD           /* texel + color * amount          (additive flash / glow)           */
} zcsr_color_mode;

/* Selective base-color transform, applied in the single linear working domain
 * after the existing MULTIPLY/MIX/ADD tint. It is deliberately separate from
 * zcsr_color_mode: the mode applies one RGBA modulation; this transform
 * rewrites a hue family while preserving saturation/value, or replaces RGB
 * with a sampled gradient ramp.
 *
 * `hue_amount == 0` disables the hue stage and `ramp_amount == 0` disables the
 * ramp stage, so a zero-initialized instance is unchanged. `glow_amount == 0`
 * disables the optional additive layer. */
typedef struct {
    /* Source hue centre in degrees [0,360). Pixels whose hue is within
     * `hue_width` degrees (feathered to 1.5x) are remapped. Only chromatic
     * pixels participate: near-neutral white/black are left untouched. */
    float hue_source;
    float hue_target;
    float hue_width;
    float hue_amount;   /* 0..1; 0 = off */

    /* Row in the renderer's shared 1D ramp atlas. The atlas is sampled by
     * linear luminance; each row is one gradient (shadow -> mid -> highlight).
     * `ramp_amount == 0` disables it.
     *
     * `ramp_mode` selects how the sampled row is applied:
     *   0 = replace RGB (legacy); the row's luminance overwrites source shading.
     *   1 = OKLab chroma map; keep source L and replace only a/b. This preserves
     *       the layered in-sprite gradient that a full RGB ramp flattens.
     * `ramp_luma` (0..1, mode 1 only) optionally blends source L toward the
     * row's L, so a material can lift overall brightness without discarding all
     * local shading. */
    float ramp_row;
    float ramp_amount;  /* 0..1; 0 = off */
    float ramp_mode;    /* 0 = RGB replace, 1 = OKLab chroma preserve-L */
    float ramp_luma;    /* 0..1; mode 1 only */

    /* Additive layer applied after hue/ramp, in display RGB, weighted by the
     * sprite's own alpha. This is the "hit +B" / fire-glow primitive; it does
     * not replace the texture, so shape and coverage stay intact. */
    float glow_r, glow_g, glow_b;
    float glow_amount;  /* 0..1; 0 = off */

    /* Chroma/value conditioning for low-saturation source art. A global hue
     * rotate preserves HSV saturation, so gray-lilac smoke stays gray-blue
     * when mapped to ice. These four controls rebuild the target chroma after
     * the selective hue stage and before the ramp stage.
     *
     * `sat_boost` pushes S toward 1 (0 = off), `sat_floor` clamps S from below
     * (0 = off), `value_gain` multiplies V by (1 + value_gain) (0 = off), and
     * `value_contrast` expands/compresses V around 0.5 (0 = off). Neutral
     * pixels (S <= 0.02) are excluded so white highlights stay white.
     * Zero-initialized values leave the legacy path unchanged. */
    float sat_boost;       /* 0..1 */
    float sat_floor;       /* 0..1 */
    float value_gain;      /* -1..1; 0 = off; negative compresses/darkens */
    float value_contrast;  /* -1..1; 0 = off */

    /* HDR emissive layer. The RGB colour is added unclamped, weighted by the
     * source texture's high-luminance core and alpha. The frame stays RGBA16F;
     * the final display encode applies one HDR tonemap before the sRGB LUT.
     * This is the "purple fire / black fire core" primitive, not a full-screen
     * additive flash. Zero amount = off. */
    float emiss_r, emiss_g, emiss_b;
    float emiss_amount;    /* 0..4; >1 is intentional HDR */

    /* Frost/energy rim layer. Adds an edge band derived from the alpha
     * silhouette, not a whole-sprite tint, so the interior shading survives.
     * Zero amount = off. */
    float rim_r, rim_g, rim_b;
    float rim_amount;      /* 0..2 */
} zcsr_color_transform;

#endif /* ZCSR_COLOR_H */
