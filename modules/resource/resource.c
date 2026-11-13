/* zcsr_resource.c — ZCSR 通用资源管理系统实现 */

#include "zcsr/resource.h"
#include "zcsr/diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ZCSR_MAX_RESOURCES 256
#define ZCSR_MAX_PATH_LEN 255
#define ZCSR_MAX_REF_COUNT 1024

typedef struct zcsr_resource_entry {
    zcsr_resource_handle_t handle;
    zcsr_resource_metadata_t metadata;
    void* data;
    size_t data_size;
    int ref_count;
    int in_use;
} zcsr_resource_entry_t;

/* 全局状态 */
static zcsr_resource_entry_t g_resources[ZCSR_MAX_RESOURCES];
static uint32_t g_slot_generation[ZCSR_MAX_RESOURCES];  /* 每个槽位的 generation */
static zcsr_resource_processor_fn g_processors[ZCSR_RESOURCE_TYPE_OTHER + 1];
static int g_initialized = 0;

/* 统计信息 */
static zcsr_resource_stats_t g_stats;

/* 内部函数：根据 handle 解析到资源条目
 * handle.id = slot + 1，所以 slot = id - 1
 * 校验 generation 防止旧 handle 访问新资源 */
static zcsr_resource_entry_t* resource_resolve(zcsr_resource_handle_t handle) {
    if (!g_initialized) return NULL;
    if (handle.id == 0) return NULL;

    /* ID = slot + 1 */
    uint32_t slot = handle.id - 1;
    if (slot >= ZCSR_MAX_RESOURCES) return NULL;

    zcsr_resource_entry_t* entry = &g_resources[slot];
    if (!entry->in_use) return NULL;

    /* 校验 generation */
    if (entry->handle.generation != handle.generation) return NULL;

    return entry;
}

/* 内部函数：根据 path 和 type 查找已有资源 */
static zcsr_resource_entry_t* resource_find_by_path(const char* path, zcsr_resource_type_t type) {
    for (int i = 0; i < ZCSR_MAX_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].metadata.type == type) {
            if (strcmp(g_resources[i].metadata.path, path) == 0) {
                return &g_resources[i];
            }
        }
    }
    return NULL;
}

/* 内部函数：查找空闲槽位 */
static int resource_find_free_slot(void) {
    for (int i = 0; i < ZCSR_MAX_RESOURCES; i++) {
        if (!g_resources[i].in_use) {
            return i;
        }
    }
    return -1;
}

zcsr_resource_result_t zcsr_resource_init(void) {
    if (g_initialized) return ZCSR_RESOURCE_OK;

    memset(g_resources, 0, sizeof(g_resources));
    memset(g_slot_generation, 0, sizeof(g_slot_generation));
    memset(g_processors, 0, sizeof(g_processors));
    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = 1;

    ZCSR_LOG_INFO("resource", "Resource manager initialized");
    return ZCSR_RESOURCE_OK;
}

void zcsr_resource_shutdown(void) {
    if (!g_initialized) return;

    /* 卸载所有资源 */
    for (int i = 0; i < ZCSR_MAX_RESOURCES; i++) {
        if (g_resources[i].in_use) {
            if (g_resources[i].data) {
                free(g_resources[i].data);
                g_resources[i].data = NULL;
            }
            g_resources[i].in_use = 0;
            g_resources[i].ref_count = 0;
            g_slot_generation[i]++;  /* 递增 generation，让所有旧 handle 失效 */
        }
    }

    g_initialized = 0;
    ZCSR_LOG_INFO("resource", "Resource manager shutdown");
}

zcsr_resource_result_t zcsr_resource_register_processor(
    zcsr_resource_type_t type,
    zcsr_resource_processor_fn processor
) {
    if (!g_initialized) return ZCSR_RESOURCE_ERROR_INVALID;
    if (type < 0 || type > ZCSR_RESOURCE_TYPE_OTHER) return ZCSR_RESOURCE_ERROR_INVALID;

    g_processors[type] = processor;
    ZCSR_LOG_DEBUG("resource", "Registered processor for type %d", type);
    return ZCSR_RESOURCE_OK;
}

zcsr_resource_handle_t zcsr_resource_load(
    const char* path,
    zcsr_resource_type_t type
) {
    zcsr_resource_handle_t null_handle = {0};

    g_stats.total_loads++;

    if (!g_initialized) {
        ZCSR_LOG_ERROR("resource", "Load failed: not initialized");
        return null_handle;
    }
    if (!path || path[0] == '\0') {
        ZCSR_LOG_ERROR("resource", "Load failed: NULL or empty path");
        g_stats.load_failures++;
        return null_handle;
    }

    /* 检查路径长度 */
    size_t path_len = strlen(path);
    if (path_len > ZCSR_MAX_PATH_LEN) {
        ZCSR_LOG_ERROR("resource", "Load failed: path too long (%zu > %d)", path_len, ZCSR_MAX_PATH_LEN);
        g_stats.load_failures++;
        return null_handle;
    }

    /* 检查处理器是否存在 */
    if (!g_processors[type]) {
        ZCSR_LOG_ERROR("resource", "Load failed: no processor for type %d", type);
        g_stats.load_failures++;
        return null_handle;
    }

    /* 先查找已有资源（去重） */
    zcsr_resource_entry_t* existing = resource_find_by_path(path, type);
    if (existing) {
        /* 检查 ref_count 上限 */
        if (existing->ref_count >= ZCSR_MAX_REF_COUNT) {
            ZCSR_LOG_ERROR("resource", "Load cache hit failed: ref_count overflow (%d)", existing->ref_count);
            g_stats.load_failures++;
            return null_handle;
        }
        g_stats.cache_hits++;
        existing->ref_count++;
        ZCSR_LOG_DEBUG("resource", "Cache hit: %s (ref_count=%d)", path, existing->ref_count);
        return existing->handle;
    }

    /* 查找空闲槽位 */
    int slot = resource_find_free_slot();
    if (slot < 0) {
        ZCSR_LOG_ERROR("resource", "Load failed: no free slot (max %d)", ZCSR_MAX_RESOURCES);
        g_stats.load_failures++;
        return null_handle;
    }

    /* 调用处理器加载资源 */
    void* data = NULL;
    size_t data_size = 0;

    zcsr_resource_result_t result = g_processors[type](path, &data, &data_size);
    if (result != ZCSR_RESOURCE_OK) {
        ZCSR_LOG_ERROR("resource", "Processor failed for %s: error %d", path, result);
        g_stats.load_failures++;
        return null_handle;
    }

    /* 检查 data_size 是否适合存入 metadata.size (uint32_t) */
    if (data_size > UINT32_MAX) {
        ZCSR_LOG_ERROR("resource", "Load failed: data too large (%zu bytes)", data_size);
        free(data);
        g_stats.load_failures++;
        return null_handle;
    }

    /* 填充资源条目 */
    zcsr_resource_entry_t* entry = &g_resources[slot];

    /* ID = slot + 1，槽位复用时 generation 递增 */
    entry->handle.id = (uint32_t)(slot + 1);
    entry->handle.type = type;
    entry->handle.generation = g_slot_generation[slot] + 1;
    entry->metadata.type = type;
    entry->metadata.size = (uint32_t)data_size;
    entry->metadata.version = 1;

    /* 安全复制路径：metadata.path 是 char[256]，允许最长 255 字符 + NUL。
     * ZCSR_MAX_PATH_LEN 是“最大字符数”，不是数组长度。 */
    snprintf(entry->metadata.path, sizeof(entry->metadata.path), "%s", path);

    entry->data = data;
    entry->data_size = data_size;
    entry->ref_count = 1;
    entry->in_use = 1;

    g_stats.total_bytes += (uint32_t)data_size;

    ZCSR_LOG_INFO("resource", "Loaded: %s (id=%u, gen=%u, size=%zu)", path, entry->handle.id, entry->handle.generation, data_size);
    return entry->handle;
}

zcsr_resource_result_t zcsr_resource_retain(zcsr_resource_handle_t handle) {
    zcsr_resource_entry_t* entry = resource_resolve(handle);
    if (!entry) {
        ZCSR_LOG_WARN("resource", "Retain failed: invalid handle id=%u gen=%u", handle.id, handle.generation);
        return ZCSR_RESOURCE_ERROR_INVALID;
    }

    if (entry->ref_count >= ZCSR_MAX_REF_COUNT) {
        ZCSR_LOG_ERROR("resource", "Retain failed: ref_count overflow (%d)", entry->ref_count);
        return ZCSR_RESOURCE_ERROR_OUT_OF_MEMORY;
    }

    entry->ref_count++;
    return ZCSR_RESOURCE_OK;
}

void zcsr_resource_unload(zcsr_resource_handle_t handle) {
    zcsr_resource_entry_t* entry = resource_resolve(handle);
    if (!entry) {
        ZCSR_LOG_WARN("resource", "Unload failed: invalid handle id=%u gen=%u", handle.id, handle.generation);
        return;
    }

    if (entry->ref_count <= 0) {
        ZCSR_LOG_ERROR("resource", "Unload error: ref_count already 0 for id=%u", handle.id);
        return;
    }

    entry->ref_count--;

    if (entry->ref_count <= 0) {
        /* 真正释放资源 */
        if (entry->data) {
            free(entry->data);
            if (g_stats.total_bytes >= (uint32_t)entry->data_size) {
                g_stats.total_bytes -= (uint32_t)entry->data_size;
            } else {
                g_stats.total_bytes = 0;
            }
        }
        entry->data = NULL;
        entry->data_size = 0;
        entry->in_use = 0;
        entry->ref_count = 0;

        /* 递增槽位 generation，让旧 handle 失效 */
        uint32_t slot = handle.id - 1;
        g_slot_generation[slot]++;

        ZCSR_LOG_INFO("resource", "Unloaded: %s (id=%u)", entry->metadata.path, handle.id);
    }
}

zcsr_resource_result_t zcsr_resource_get_metadata(
    zcsr_resource_handle_t handle,
    zcsr_resource_metadata_t* out_metadata
) {
    if (!out_metadata) return ZCSR_RESOURCE_ERROR_INVALID;

    zcsr_resource_entry_t* entry = resource_resolve(handle);
    if (!entry) {
        return ZCSR_RESOURCE_ERROR_NOT_FOUND;
    }

    *out_metadata = entry->metadata;
    return ZCSR_RESOURCE_OK;
}

void* zcsr_resource_get_data(zcsr_resource_handle_t handle) {
    zcsr_resource_entry_t* entry = resource_resolve(handle);
    if (!entry) {
        return NULL;
    }
    return entry->data;
}

zcsr_resource_result_t zcsr_resource_reload(zcsr_resource_handle_t handle) {
    zcsr_resource_entry_t* entry = resource_resolve(handle);
    if (!entry) {
        return ZCSR_RESOURCE_ERROR_NOT_FOUND;
    }

    /* 事务安全：先加载新数据，成功后再交换 */
    void* new_data = NULL;
    size_t new_data_size = 0;

    zcsr_resource_result_t result = g_processors[entry->handle.type](
        entry->metadata.path, &new_data, &new_data_size
    );
    if (result != ZCSR_RESOURCE_OK) {
        ZCSR_LOG_ERROR("resource", "Reload failed for %s: error %d", entry->metadata.path, result);
        return result;
    }

    /* 检查新数据大小 */
    if (new_data_size > UINT32_MAX) {
        ZCSR_LOG_ERROR("resource", "Reload failed: new data too large");
        free(new_data);
        return ZCSR_RESOURCE_ERROR_OUT_OF_MEMORY;
    }

    /* 全部成功，交换新旧数据 */
    if (entry->data) {
        free(entry->data);
        if (g_stats.total_bytes >= (uint32_t)entry->data_size) {
            g_stats.total_bytes -= (uint32_t)entry->data_size;
        } else {
            g_stats.total_bytes = 0;
        }
    }

    entry->data = new_data;
    entry->data_size = new_data_size;
    entry->metadata.size = (uint32_t)new_data_size;
    entry->metadata.version++;
    entry->handle.generation++;
    g_stats.total_bytes += (uint32_t)new_data_size;

    ZCSR_LOG_INFO("resource", "Reloaded: %s (version=%u)", entry->metadata.path, entry->metadata.version);
    return ZCSR_RESOURCE_OK;
}

void zcsr_resource_get_stats(zcsr_resource_stats_t* out_stats) {
    if (!out_stats) return;
    *out_stats = g_stats;
    out_stats->active_count = 0;
    for (int i = 0; i < ZCSR_MAX_RESOURCES; i++) {
        if (g_resources[i].in_use) {
            out_stats->active_count++;
        }
    }
}
