/* file_service.c — ZCSR 统一文件服务实现
 *
 * VFS / Mount 管理，路径解析
 */
#include "zcsr/file_service.h"
#include "zcsr/diagnostics.h"
#include "file_service_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ZCSR_MAX_MOUNTS 16
#define ZCSR_MAX_URI_LEN 512
#define ZCSR_MAX_PATH_LEN 256

typedef struct zcsr_mount_entry {
    char scheme[64];
    char native_path[ZCSR_MAX_PATH_LEN];
    bool read_only;
    bool used;
} zcsr_mount_entry_t;

struct zcsr_file_service {
    zcsr_mount_entry_t mounts[ZCSR_MAX_MOUNTS];
};

/* 内部：路径规范化 + 防止 .. 穿越 */
static void normalize_path(char* path) {
    if (!path || path[0] == '\0') return;

    /* 第一步：把所有反斜杠换成正斜杠 */
    for (char* p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* 第二步：去掉重复的斜杠 */
    char* src = path;
    char* dst = path;
    while (*src) {
        if (*src == '/' && src > path && *(src - 1) == '/') {
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    /* 第三步：处理 . 和 .. */
    /* 用栈的方式处理 */
    char* parts[128];
    int part_count = 0;

    char* token = strtok(path, "/");
    while (token && part_count < 128) {
        if (strcmp(token, ".") == 0) {
            /* 跳过 . */
        } else if (strcmp(token, "..") == 0) {
            if (part_count > 0) {
                part_count--;  /* 出栈 */
            }
            /* 否则就是越过根目录，忽略 */
        } else {
            parts[part_count++] = token;
        }
        token = strtok(NULL, "/");
    }

    /* 重新拼接 */
    char* p = path;
    for (int i = 0; i < part_count; i++) {
        *p++ = '/';
        size_t len = strlen(parts[i]);
        memcpy(p, parts[i], len);
        p += len;
    }
    *p = '\0';
}

/* 内部：把 URI 解析成原生路径 */
static zcsr_file_result_t resolve_uri(zcsr_file_service_t* fs,
                                      const char* uri,
                                      char* out_native,
                                      size_t out_capacity) {
    if (!fs || !uri || !out_native || out_capacity < 2) {
        ZCSR_LOG_ERROR("file_service", "Invalid arguments to resolve_uri");
        return ZCSR_FILE_ERR_ARGUMENT;
    }

    /* 查找 :// 分隔符 */
    const char* sep = strstr(uri, "://");
    if (!sep) {
        /* 不是 URI，直接当原生路径 */
        strncpy(out_native, uri, out_capacity - 1);
        out_native[out_capacity - 1] = '\0';
        normalize_path(out_native);
        ZCSR_LOG_DEBUG("file_service", "Resolved native path: %s", out_native);
        return ZCSR_FILE_OK;
    }

    /* 提取 scheme */
    char scheme[64];
    size_t scheme_len = sep - uri;
    if (scheme_len >= sizeof(scheme)) {
        ZCSR_LOG_ERROR("file_service", "URI scheme too long: %zu", scheme_len);
        return ZCSR_FILE_ERR_ARGUMENT;
    }
    memcpy(scheme, uri, scheme_len);
    scheme[scheme_len] = '\0';

    /* 查找 mount */
    for (int i = 0; i < ZCSR_MAX_MOUNTS; i++) {
        if (!fs->mounts[i].used) continue;
        if (strcmp(fs->mounts[i].scheme, scheme) == 0) {
            /* 拼接：mount_path + '/' + path_after_scheme */
            const char* path_part = sep + 3; /* 跳过 :// */
            snprintf(out_native, out_capacity, "%s/%s",
                     fs->mounts[i].native_path, path_part);
            normalize_path(out_native);
            ZCSR_LOG_DEBUG("file_service", "Resolved %s -> %s", uri, out_native);
            return ZCSR_FILE_OK;
        }
    }

    ZCSR_LOG_WARN("file_service", "No mount found for scheme: %s", scheme);
    return ZCSR_FILE_ERR_NOT_FOUND;
}

zcsr_file_service_t* zcsr_file_service_create(void) {
    zcsr_file_service_t* fs = (zcsr_file_service_t*)calloc(1, sizeof(zcsr_file_service_t));
    if (!fs) {
        ZCSR_LOG_ERROR("file_service", "Failed to allocate file service");
    }
    return fs;
}

void zcsr_file_service_destroy(zcsr_file_service_t* fs) {
    if (!fs) return;
    free(fs);
    ZCSR_LOG_INFO("file_service", "File service destroyed");
}

zcsr_file_result_t zcsr_file_service_mount(zcsr_file_service_t* fs,
                                            const char* scheme,
                                            const char* native_path,
                                            bool read_only) {
    if (!fs || !scheme || !native_path) {
        ZCSR_LOG_ERROR("file_service", "Invalid arguments to mount");
        return ZCSR_FILE_ERR_ARGUMENT;
    }

    /* 检查是否已经有相同 scheme 的 mount */
    for (int i = 0; i < ZCSR_MAX_MOUNTS; i++) {
        if (fs->mounts[i].used && strcmp(fs->mounts[i].scheme, scheme) == 0) {
            ZCSR_LOG_WARN("file_service", "Mount already exists for scheme: %s", scheme);
            return ZCSR_FILE_ERR_ALREADY_EXISTS;
        }
    }

    /* 查找空闲槽位 */
    for (int i = 0; i < ZCSR_MAX_MOUNTS; i++) {
        if (!fs->mounts[i].used) {
            strncpy(fs->mounts[i].scheme, scheme, sizeof(fs->mounts[i].scheme) - 1);
            fs->mounts[i].scheme[sizeof(fs->mounts[i].scheme) - 1] = '\0';
            strncpy(fs->mounts[i].native_path, native_path, sizeof(fs->mounts[i].native_path) - 1);
            fs->mounts[i].native_path[sizeof(fs->mounts[i].native_path) - 1] = '\0';
            fs->mounts[i].read_only = read_only;
            fs->mounts[i].used = true;
            ZCSR_LOG_INFO("file_service", "Mounted %s -> %s", scheme, native_path);
            return ZCSR_FILE_OK;
        }
    }

    ZCSR_LOG_ERROR("file_service", "No free mount slots (max %d)", ZCSR_MAX_MOUNTS);
    return ZCSR_FILE_ERR_INTERNAL;
}

zcsr_file_result_t zcsr_file_service_unmount(zcsr_file_service_t* fs,
                                               const char* scheme) {
    if (!fs || !scheme) {
        ZCSR_LOG_ERROR("file_service", "Invalid arguments to unmount");
        return ZCSR_FILE_ERR_ARGUMENT;
    }

    for (int i = 0; i < ZCSR_MAX_MOUNTS; i++) {
        if (fs->mounts[i].used && strcmp(fs->mounts[i].scheme, scheme) == 0) {
            fs->mounts[i].used = false;
            ZCSR_LOG_INFO("file_service", "Unmounted %s", scheme);
            return ZCSR_FILE_OK;
        }
    }

    ZCSR_LOG_WARN("file_service", "Mount not found for scheme: %s", scheme);
    return ZCSR_FILE_ERR_NOT_FOUND;
}

zcsr_file_result_t zcsr_file_service_resolve(zcsr_file_service_t* fs,
                                               const char* uri,
                                               char* out_native,
                                               size_t out_capacity) {
    return resolve_uri(fs, uri, out_native, out_capacity);
}

/* === 文件操作实现（桥接平台后端） === */

zcsr_file_result_t zcsr_file_stat(zcsr_file_service_t* fs,
                                   const char* uri,
                                   zcsr_file_stat_t* out_stat) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    return zcsr_platform_file_ops.stat(native, out_stat);
}

zcsr_file_result_t zcsr_file_exists(zcsr_file_service_t* fs,
                                     const char* uri,
                                     bool* out_exists) {
    zcsr_file_stat_t stat;
    zcsr_file_result_t rc = zcsr_file_stat(fs, uri, &stat);
    *out_exists = (rc == ZCSR_FILE_OK);
    return ZCSR_FILE_OK;
}

zcsr_file_result_t zcsr_dir_open(zcsr_file_service_t* fs,
                                  const char* uri,
                                  zcsr_dir_t** out_dir) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    void* dir = NULL;
    rc = zcsr_platform_file_ops.dir_open(native, &dir);
    *out_dir = (zcsr_dir_t*)dir;
    return rc;
}

zcsr_file_result_t zcsr_dir_next(zcsr_file_service_t* fs,
                                  zcsr_dir_t* dir,
                                  zcsr_dir_entry_t* out_entry) {
    (void)fs; /* Directory iteration is backend-global; fs is kept for API symmetry. */
    /* 注意：这是现有 API 的设计缺陷，使用静态缓冲区
     * 连续调用会覆盖上一次的结果，非线程安全
     * 未来应该改 API 让调用者提供缓冲区 */
    static char name_buf[256];
    memset(name_buf, 0, sizeof(name_buf));
    return zcsr_platform_file_ops.dir_next((void*)dir, out_entry, name_buf, sizeof(name_buf));
}

zcsr_file_result_t zcsr_dir_close(zcsr_file_service_t* fs,
                                   zcsr_dir_t* dir) {
    (void)fs; /* Backend handle is global; fs is kept for API symmetry. */
    return zcsr_platform_file_ops.dir_close((void*)dir);
}

zcsr_file_result_t zcsr_dir_create(zcsr_file_service_t* fs,
                                     const char* uri) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    return zcsr_platform_file_ops.mkdir(native);
}

zcsr_file_result_t zcsr_dir_create_recursive(zcsr_file_service_t* fs,
                                              const char* uri) {
    /* 修复：实现递归创建目录 */
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;

    /* 逐层创建 */
    char tmp[ZCSR_MAX_URI_LEN];
    strncpy(tmp, native, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    /* 跳过开头的斜杠 */
    char* p = tmp;
    while (*p == '/') p++;

    while (*p) {
        /* 找到下一个斜杠 */
        char* next = strchr(p, '/');
        if (next) {
            *next = '\0';
        }

        /* 创建这一级目录 */
        zcsr_file_result_t mkdir_rc = zcsr_platform_file_ops.mkdir(tmp);
        /* 如果已经存在，不算错误 */
        if (mkdir_rc != ZCSR_FILE_OK && mkdir_rc != ZCSR_FILE_ERR_ALREADY_EXISTS) {
            return mkdir_rc;
        }

        if (next) {
            *next = '/';  /* 恢复斜杠 */
            p = next + 1;
            while (*p == '/') p++;  /* 跳过多个斜杠 */
        } else {
            break;
        }
    }

    return ZCSR_FILE_OK;
}

zcsr_file_result_t zcsr_file_open_read(zcsr_file_service_t* fs,
                                        const char* uri,
                                        zcsr_file_t** out_file) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    void* file = NULL;
    rc = zcsr_platform_file_ops.open_read(native, &file);
    *out_file = (zcsr_file_t*)file;
    return rc;
}

zcsr_file_result_t zcsr_file_open_write(zcsr_file_service_t* fs,
                                           const char* uri,
                                           zcsr_file_t** out_file) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    void* file = NULL;
    rc = zcsr_platform_file_ops.open_write(native, &file);
    *out_file = (zcsr_file_t*)file;
    return rc;
}

zcsr_file_result_t zcsr_file_read(zcsr_file_service_t* fs,
                                   zcsr_file_t* file,
                                   void* buffer,
                                   size_t capacity,
                                   size_t* out_bytes_read) {
    (void)fs; /* Backend handle is global; fs is kept for API symmetry. */
    return zcsr_platform_file_ops.read((void*)file, buffer, capacity, out_bytes_read);
}

zcsr_file_result_t zcsr_file_write(zcsr_file_service_t* fs,
                                     zcsr_file_t* file,
                                     const void* data,
                                     size_t size,
                                     size_t* out_bytes_written) {
    (void)fs; /* Backend handle is global; fs is kept for API symmetry. */
    return zcsr_platform_file_ops.write((void*)file, data, size, out_bytes_written);
}

zcsr_file_result_t zcsr_file_seek(zcsr_file_service_t* fs,
                                   zcsr_file_t* file,
                                   int64_t offset,
                                   zcsr_file_seek_origin_t origin,
                                   uint64_t* out_position) {
    (void)fs; /* Backend handle is global; fs is kept for API symmetry. */
    return zcsr_platform_file_ops.seek((void*)file, offset, (int)origin, out_position);
}

zcsr_file_result_t zcsr_file_close(zcsr_file_service_t* fs,
                                    zcsr_file_t* file) {
    (void)fs; /* Backend handle is global; fs is kept for API symmetry. */
    return zcsr_platform_file_ops.close((void*)file);
}

zcsr_file_result_t zcsr_file_delete(zcsr_file_service_t* fs,
                                     const char* uri) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    return zcsr_platform_file_ops.delete_file(native);
}

zcsr_file_result_t zcsr_dir_delete(zcsr_file_service_t* fs,
                                   const char* uri) {
    char native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, uri, native, sizeof(native));
    if (rc != ZCSR_FILE_OK) return rc;
    return zcsr_platform_file_ops.delete_dir(native);
}

zcsr_file_result_t zcsr_file_rename(zcsr_file_service_t* fs,
                                      const char* from_uri,
                                      const char* to_uri) {
    char from_native[ZCSR_MAX_URI_LEN];
    char to_native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, from_uri, from_native, sizeof(from_native));
    if (rc != ZCSR_FILE_OK) return rc;
    rc = resolve_uri(fs, to_uri, to_native, sizeof(to_native));
    if (rc != ZCSR_FILE_OK) return rc;
    return zcsr_platform_file_ops.rename(from_native, to_native);
}

zcsr_file_result_t zcsr_file_copy(zcsr_file_service_t* fs,
                                    const char* from_uri,
                                    const char* to_uri) {
    char from_native[ZCSR_MAX_URI_LEN];
    char to_native[ZCSR_MAX_URI_LEN];
    zcsr_file_result_t rc = resolve_uri(fs, from_uri, from_native, sizeof(from_native));
    if (rc != ZCSR_FILE_OK) return rc;
    rc = resolve_uri(fs, to_uri, to_native, sizeof(to_native));
    if (rc != ZCSR_FILE_OK) return rc;
    return zcsr_platform_file_ops.copy(from_native, to_native);
}
