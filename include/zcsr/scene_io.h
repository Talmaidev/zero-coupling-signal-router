#ifndef ZCSR_SCENE_IO_H
#define ZCSR_SCENE_IO_H

/* Versioned text scene format shared by the editor and the runtime player.
 *
 * v2 shape:
 *   ZCSRSCENE 2
 *   camera <8 floats> <view_w int> <view_h int>
 *   entity <id> <8 floats> <layer int> <order int> <4 tint floats>
 *          <texture uint> <visible int> <kind uint> <prefab u64> <asset u64>
 *   quad   <id> <12 floats>
 *
 * v1 files remain loadable. `texture` is a runtime handle and is not the
 * stable asset identity; `asset_id` is. Saves are atomic. */

#include "zcsr/scene.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum zcsr_scene_io_result {
    ZCSR_SCENE_IO_OK = 0,
    ZCSR_SCENE_IO_INVALID_ARGUMENT,
    ZCSR_SCENE_IO_OPEN_FAILED,
    ZCSR_SCENE_IO_BAD_HEADER,
    ZCSR_SCENE_IO_BAD_RECORD,
    ZCSR_SCENE_IO_CAPACITY,
    ZCSR_SCENE_IO_IO_ERROR
} zcsr_scene_io_result;

zcsr_scene_io_result zcsr_scene_io_serialize(const zcsr_scene* scene,
                                             char** out_text, size_t* out_size);
zcsr_scene_io_result zcsr_scene_io_deserialize(const char* text, size_t size,
                                               zcsr_scene* scene);
zcsr_scene_io_result zcsr_scene_io_save(const zcsr_scene* scene,
                                        const char* utf8_path);
zcsr_scene_io_result zcsr_scene_io_load(zcsr_scene* scene,
                                        const char* utf8_path);
const char* zcsr_scene_io_result_name(zcsr_scene_io_result result);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_SCENE_IO_H */
