/* zcsr_file_service.h — ZCSR 统一文件服务
 *
 * 职责：
 * - 统一文件操作 API
 * - VFS / Mount 虚拟路径
 * - 平台后端抽象
 *
 * 不负责：
 * - Asset Catalog
 * - Import Pipeline
 * - Resource Manager
 */
#ifndef ZCSR_FILE_SERVICE_H
#define ZCSR_FILE_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 错误码 */
typedef enum zcsr_file_result {
    ZCSR_FILE_OK = 0,
    ZCSR_FILE_END,
    ZCSR_FILE_ERR_ARGUMENT,
    ZCSR_FILE_ERR_NOT_FOUND,
    ZCSR_FILE_ERR_ALREADY_EXISTS,
    ZCSR_FILE_ERR_ACCESS,
    ZCSR_FILE_ERR_IO,
    ZCSR_FILE_ERR_NO_SPACE,
    ZCSR_FILE_ERR_NOT_DIRECTORY,
    ZCSR_FILE_ERR_IS_DIRECTORY,
    ZCSR_FILE_ERR_UNSUPPORTED,
    ZCSR_FILE_ERR_INTERNAL
} zcsr_file_result_t;

/* 文件类型 */
typedef enum zcsr_file_kind {
    ZCSR_FILE_KIND_UNKNOWN = 0,
    ZCSR_FILE_KIND_REGULAR = 1,
    ZCSR_FILE_KIND_DIRECTORY = 2
} zcsr_file_kind_t;

/* 文件属性 */
typedef struct zcsr_file_stat {
    zcsr_file_kind_t kind;
    uint64_t size_bytes;
    int64_t modified_time_unix_ns;
} zcsr_file_stat_t;

/* 目录条目 */
typedef struct zcsr_dir_entry {
    zcsr_file_kind_t kind;
    uint64_t size_bytes;
    int64_t modified_time_unix_ns;
    const char* name;
} zcsr_dir_entry_t;

/* 不透明句柄 */
typedef struct zcsr_file zcsr_file_t;
typedef struct zcsr_dir zcsr_dir_t;

/* Seek 起点 */
typedef enum zcsr_file_seek_origin {
    ZCSR_FILE_SEEK_BEGIN = 0,
    ZCSR_FILE_SEEK_CURRENT = 1,
    ZCSR_FILE_SEEK_END = 2
} zcsr_file_seek_origin_t;

/* Mount 描述符 */
typedef struct zcsr_mount {
    const char* scheme;     /* 如 "project", "engine", "user" */
    const char* native_path; /* 如 "C:/projects/mygame" */
    bool read_only;
} zcsr_mount_t;

/* 文件服务句柄 */
typedef struct zcsr_file_service zcsr_file_service_t;

/* === 生命周期 === */
zcsr_file_service_t* zcsr_file_service_create(void);
void zcsr_file_service_destroy(zcsr_file_service_t* fs);

/* === Mount 管理 === */
zcsr_file_result_t zcsr_file_service_mount(zcsr_file_service_t* fs,
                                            const char* scheme,
                                            const char* native_path,
                                            bool read_only);
zcsr_file_result_t zcsr_file_service_unmount(zcsr_file_service_t* fs,
                                               const char* scheme);

/* === 路径解析 === */
/* 把 URI (如 "project://textures/player.png") 解析成原生路径 */
zcsr_file_result_t zcsr_file_service_resolve(zcsr_file_service_t* fs,
                                               const char* uri,
                                               char* out_native,
                                               size_t out_capacity);

/* === 文件操作 === */
zcsr_file_result_t zcsr_file_stat(zcsr_file_service_t* fs,
                                   const char* uri,
                                   zcsr_file_stat_t* out_stat);

zcsr_file_result_t zcsr_file_exists(zcsr_file_service_t* fs,
                                     const char* uri,
                                     bool* out_exists);

/* === 目录操作 === */
zcsr_file_result_t zcsr_dir_open(zcsr_file_service_t* fs,
                                  const char* uri,
                                  zcsr_dir_t** out_dir);
zcsr_file_result_t zcsr_dir_next(zcsr_file_service_t* fs,
                                  zcsr_dir_t* dir,
                                  zcsr_dir_entry_t* out_entry);
zcsr_file_result_t zcsr_dir_close(zcsr_file_service_t* fs,
                                   zcsr_dir_t* dir);

zcsr_file_result_t zcsr_dir_create(zcsr_file_service_t* fs,
                                     const char* uri);
zcsr_file_result_t zcsr_dir_create_recursive(zcsr_file_service_t* fs,
                                              const char* uri);

/* === 文件读写 === */
zcsr_file_result_t zcsr_file_open_read(zcsr_file_service_t* fs,
                                        const char* uri,
                                        zcsr_file_t** out_file);
zcsr_file_result_t zcsr_file_open_write(zcsr_file_service_t* fs,
                                           const char* uri,
                                           zcsr_file_t** out_file);
zcsr_file_result_t zcsr_file_read(zcsr_file_service_t* fs,
                                   zcsr_file_t* file,
                                   void* buffer,
                                   size_t capacity,
                                   size_t* out_bytes_read);
zcsr_file_result_t zcsr_file_write(zcsr_file_service_t* fs,
                                     zcsr_file_t* file,
                                     const void* data,
                                     size_t size,
                                     size_t* out_bytes_written);
zcsr_file_result_t zcsr_file_seek(zcsr_file_service_t* fs,
                                   zcsr_file_t* file,
                                   int64_t offset,
                                   zcsr_file_seek_origin_t origin,
                                   uint64_t* out_position);
zcsr_file_result_t zcsr_file_close(zcsr_file_service_t* fs,
                                    zcsr_file_t* file);

/* === 文件管理 === */
zcsr_file_result_t zcsr_file_delete(zcsr_file_service_t* fs,
                                     const char* uri);
zcsr_file_result_t zcsr_dir_delete(zcsr_file_service_t* fs,
                                   const char* uri);
zcsr_file_result_t zcsr_file_rename(zcsr_file_service_t* fs,
                                      const char* from_uri,
                                      const char* to_uri);
zcsr_file_result_t zcsr_file_copy(zcsr_file_service_t* fs,
                                    const char* from_uri,
                                    const char* to_uri);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_FILE_SERVICE_H */
