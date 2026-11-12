/* zcsr_resource.h — ZCSR 通用资源管理系统
 *
 * 概念区分：
 *   Asset    = 项目中的源文件（Editor 管理）
 *   Resource = 已加载的运行时资源（ZCSR Resource System 管理）
 *
 * 统一预处理 Pipeline：
 *   Source Asset -> Resolve Path -> Validate -> Identify Type -> Preprocess -> Generate Metadata -> Register -> Cache -> Consume
 */

#ifndef ZCSR_RESOURCE_H
#define ZCSR_RESOURCE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resource 类型 */
typedef enum zcsr_resource_type {
    ZCSR_RESOURCE_TYPE_UNKNOWN = 0,
    ZCSR_RESOURCE_TYPE_IMAGE,
    ZCSR_RESOURCE_TYPE_FONT,
    ZCSR_RESOURCE_TYPE_AUDIO,
    ZCSR_RESOURCE_TYPE_SCENE,
    ZCSR_RESOURCE_TYPE_MATERIAL,
    ZCSR_RESOURCE_TYPE_OTHER
} zcsr_resource_type_t;

/* Resource ID */
typedef uint32_t zcsr_resource_id_t;

/* Resource Handle */
typedef struct zcsr_resource_handle {
    zcsr_resource_id_t id;
    zcsr_resource_type_t type;
    uint32_t generation;  /* 用于检测失效句柄 */
} zcsr_resource_handle_t;

/* Resource Metadata */
typedef struct zcsr_resource_metadata {
    char path[256];
    zcsr_resource_type_t type;
    uint32_t size;
    uint32_t version;
} zcsr_resource_metadata_t;

/* 资源处理结果 */
typedef enum zcsr_resource_result {
    ZCSR_RESOURCE_OK = 0,
    ZCSR_RESOURCE_ERROR_NOT_FOUND,
    ZCSR_RESOURCE_ERROR_INVALID,
    ZCSR_RESOURCE_ERROR_UNSUPPORTED_TYPE,
    ZCSR_RESOURCE_ERROR_LOAD_FAILED,
    ZCSR_RESOURCE_ERROR_OUT_OF_MEMORY
} zcsr_resource_result_t;

/* 资源处理器回调 */
typedef zcsr_resource_result_t (*zcsr_resource_processor_fn)(
    const char* path,
    void** out_data,
    size_t* out_size
);

/* 初始化资源系统 */
zcsr_resource_result_t zcsr_resource_init(void);

/* 关闭资源系统 */
void zcsr_resource_shutdown(void);

/* 注册资源类型处理器 */
zcsr_resource_result_t zcsr_resource_register_processor(
    zcsr_resource_type_t type,
    zcsr_resource_processor_fn processor
);

/* 加载资源
 * 首次加载：创建资源，ref_count = 1
 * 重复加载同路径同类型：命中缓存，ref_count++
 * 每次成功 Load 对应一次 Unload */
zcsr_resource_handle_t zcsr_resource_load(
    const char* path,
    zcsr_resource_type_t type
);

/* 增加资源引用计数
 * 用于多个调用者共享同一资源 */
zcsr_resource_result_t zcsr_resource_retain(zcsr_resource_handle_t handle);

/* 减少资源引用计数
 * ref_count 归零时真正释放资源
 * 每次 Load / Retain 对应一次 Unload */
void zcsr_resource_unload(zcsr_resource_handle_t handle);

/* 获取资源统计信息 */
typedef struct zcsr_resource_stats {
    uint32_t active_count;
    uint32_t total_loads;
    uint32_t cache_hits;
    uint32_t load_failures;
    uint32_t total_bytes;
} zcsr_resource_stats_t;

void zcsr_resource_get_stats(zcsr_resource_stats_t* out_stats);

/* 查询资源元数据 */
zcsr_resource_result_t zcsr_resource_get_metadata(
    zcsr_resource_handle_t handle,
    zcsr_resource_metadata_t* out_metadata
);

/* 获取资源数据 */
void* zcsr_resource_get_data(zcsr_resource_handle_t handle);

/* 重载资源（Reimport） */
zcsr_resource_result_t zcsr_resource_reload(zcsr_resource_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_RESOURCE_H */
