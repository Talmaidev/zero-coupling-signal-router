#ifndef ZCSR_INSTANCE_SORT_H
#define ZCSR_INSTANCE_SORT_H
/* Pipeline-stage instance sort (裁决 7). Pure CPU, no GL, no heap.
 *
 * The stage operates on 4 B keys and a 4 B index order array only. The
 * renderer's wide instance record is never touched by sort scratch (裁决 4:
 * the second pass touches just the 4 B index array, not the full record).
 *
 * Tier selection is compile-time / assembly-time (裁决 5): there is no
 * per-frame bool parameter and no runtime feature flag in the hot path.
 *
 * The implementation is header-inline so the stage can be compiled straight
 * into any renderer translation unit without a separate static-lib link
 * dependency; the matching modules/instance_sort TU is the module registration
 * placeholder and probe host.
 *
 *   ZCSR_SORT_NONE  — the sort stage is not compiled at all (identity order).
 *   ZCSR_SORT_KEY   — stable LSD radix sort over a 32-bit key.
 *   ZCSR_SORT_FULL  — stable painter order through a caller comparator.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "zcsr/render_tier.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ZCSR_SORT_TIER
#define ZCSR_SORT_TIER ZCSR_SORT_TIER_KEY
#endif

/* Stable index sort over 32-bit keys.
 *
 * `order` receives the permutation: after the call, instances are visited in
 * the order order[0], order[1], ... . `scratch` is caller-owned, `count`
 * entries of uint32. Ties keep the ascending original index (stable).
 * Returns `count`; 0 on invalid arguments.
 */
static inline size_t zcsr_sort_key_order(const uint32_t* keys, uint32_t* order,
                                         uint32_t* scratch, size_t count) {
    size_t i, shift;
    uint32_t* src;
    uint32_t* dst;

    if (!keys || !order || !scratch) return 0u;
    if (count == 0u) return 0u;
    for (i = 0u; i < count; ++i) order[i] = (uint32_t)i;
    if (count < 2u) return count;

    src = order;
    dst = scratch;
    for (shift = 0u; shift < 32; shift += 8) {
        uint32_t hist[256];
        uint32_t total;
        int b;
        for (b = 0; b < 256; ++b) hist[b] = 0u;
        for (i = 0u; i < count; ++i) ++hist[(keys[src[i]] >> shift) & 0xFFu];
        total = 0u;
        for (b = 0; b < 256; ++b) {
            uint32_t c = hist[b];
            hist[b] = total;
            total += c;
        }
        for (i = 0u; i < count; ++i) {
            uint32_t k = keys[src[i]];
            dst[hist[(k >> shift) & 0xFFu]++] = src[i];
        }
        { uint32_t* tmp = src; src = dst; dst = tmp; }
    }
    if (src != order) memcpy(order, src, count * sizeof(uint32_t));
    return count;
}

/* Painter comparator for the FULL tier: returns non-zero when instance `a`
 * must be drawn before instance `b`. `ud` is caller state (e.g. the instance
 * array and the z extractor). */
typedef int (*zcsr_index_less_fn)(uint32_t a, uint32_t b, void* ud);

static inline void zcsr_sort_merge_index(uint32_t* order, uint32_t* scratch,
                                         size_t lo, size_t mid, size_t hi,
                                         zcsr_index_less_fn less, void* ud) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (less(order[j], order[i], ud)) {
            scratch[k++] = order[j++];
        } else {
            scratch[k++] = order[i++]; /* left first: stable on ties */
        }
    }
    while (i < mid) scratch[k++] = order[i++];
    while (j < hi) scratch[k++] = order[j++];
    memcpy(order + lo, scratch + lo, (hi - lo) * sizeof(uint32_t));
}

static inline void zcsr_sort_merge_rec(uint32_t* order, uint32_t* scratch,
                                       size_t lo, size_t hi,
                                       zcsr_index_less_fn less, void* ud) {
    size_t mid;
    if (hi - lo < 2u) return;
    mid = lo + (hi - lo) / 2u;
    zcsr_sort_merge_rec(order, scratch, lo, mid, less, ud);
    zcsr_sort_merge_rec(order, scratch, mid, hi, less, ud);
    zcsr_sort_merge_index(order, scratch, lo, mid, hi, less, ud);
}

/* Stable painter-order index sort for the FULL tier. */
static inline size_t zcsr_sort_index_stable(uint32_t* order, uint32_t* scratch,
                                            size_t count,
                                            zcsr_index_less_fn less, void* ud) {
    size_t i;
    if (!order || !scratch || !less || count == 0u) return 0u;
    for (i = 0u; i < count; ++i) order[i] = (uint32_t)i;
    if (count < 2u) return count;
    zcsr_sort_merge_rec(order, scratch, 0u, count, less, ud);
    return count;
}

/* Stage entry, dispatched by the compile-time ZCSR_SORT_TIER macro. */
static inline size_t zcsr_sort_instance_order(const uint32_t* keys, uint32_t* order,
                                             uint32_t* scratch, size_t count) {
#if ZCSR_SORT_TIER == ZCSR_SORT_TIER_NONE
    /* Stage compiled out: identity order, no sort code in this TU's body. */
    size_t i;
    (void)keys;
    (void)scratch;
    if (!order || count == 0u) return 0u;
    for (i = 0u; i < count; ++i) order[i] = (uint32_t)i;
    return count;
#elif ZCSR_SORT_TIER == ZCSR_SORT_TIER_KEY
    return zcsr_sort_key_order(keys, order, scratch, count);
#else
    /* ZCSR_SORT_FULL: the caller composes the painter composite key
     * (texture high bits, far z descending low bits) or uses
     * zcsr_sort_index_stable with a painter comparator directly. */
    return zcsr_sort_key_order(keys, order, scratch, count);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_INSTANCE_SORT_H */
