/* dr_flac implementation translation unit.
 *
 * The declarations are included from music.c; this TU compiles the decoder
 * body. The top-level header guard is undefined first so the body is not
 * skipped on the second include. Third-party warnings are suppressed only
 * within this include; see third_party/dr_flac_LICENSE.txt for provenance. */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#undef DR_FLAC_H
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4267 4310 4389 4456 4457 4701 4702 4189 4505 4100)
#endif
#include "third_party/dr_flac.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
