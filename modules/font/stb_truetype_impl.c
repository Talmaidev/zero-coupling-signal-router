/* stb_truetype implementation translation unit.
 * Uses zcsr's bump allocator instead of malloc/free. */
#include <stddef.h>

extern void* zcsr_stbtt_malloc(size_t, void*);
extern void  zcsr_stbtt_free(void*, void*);

#define STBTT_malloc(x, u) zcsr_stbtt_malloc((x), (u))
#define STBTT_free(x, u)   zcsr_stbtt_free((x), (u))
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"
