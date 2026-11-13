/* file_service_internal.h — File Service 内部接口（不公开） */
#ifndef ZCSR_FILE_SERVICE_INTERNAL_H
#define ZCSR_FILE_SERVICE_INTERNAL_H

#include "zcsr/file_service.h"

/* 平台后端函数指针类型 */
typedef zcsr_file_result_t (*pfn_stat)(const char* native_path, zcsr_file_stat_t* out_stat);
typedef zcsr_file_result_t (*pfn_dir_open)(const char* native_path, void** out_dir);
typedef zcsr_file_result_t (*pfn_dir_next)(void* dir, zcsr_dir_entry_t* out_entry, char* name_buf, size_t name_cap);
typedef zcsr_file_result_t (*pfn_dir_close)(void* dir);
typedef zcsr_file_result_t (*pfn_mkdir)(const char* native_path);
typedef zcsr_file_result_t (*pfn_open_read)(const char* native_path, void** out_file);
typedef zcsr_file_result_t (*pfn_open_write)(const char* native_path, void** out_file);
typedef zcsr_file_result_t (*pfn_read)(void* file, void* buffer, size_t capacity, size_t* out_bytes_read);
typedef zcsr_file_result_t (*pfn_write)(void* file, const void* data, size_t size, size_t* out_bytes_written);
typedef zcsr_file_result_t (*pfn_seek)(void* file, int64_t offset, int origin, uint64_t* out_position);
typedef zcsr_file_result_t (*pfn_close)(void* file);
typedef zcsr_file_result_t (*pfn_delete_file)(const char* native_path);
typedef zcsr_file_result_t (*pfn_delete_dir)(const char* native_path);
typedef zcsr_file_result_t (*pfn_rename)(const char* from, const char* to);
typedef zcsr_file_result_t (*pfn_copy)(const char* from, const char* to);

/* 平台后端接口表 */
typedef struct zcsr_platform_file_ops {
    pfn_stat stat;
    pfn_dir_open dir_open;
    pfn_dir_next dir_next;
    pfn_dir_close dir_close;
    pfn_mkdir mkdir;
    pfn_open_read open_read;
    pfn_open_write open_write;
    pfn_read read;
    pfn_write write;
    pfn_seek seek;
    pfn_close close;
    pfn_delete_file delete_file;
    pfn_delete_dir delete_dir;
    pfn_rename rename;
    pfn_copy copy;
} zcsr_platform_file_ops_t;

/* 在平台后端文件中定义（每个文件自带平台守卫，CMake 以 GLOB 全量收集）：
 *   Win32          -> file_win32.c
 *   Linux/Android/macOS -> file_posix.c */
extern const zcsr_platform_file_ops_t zcsr_platform_file_ops;

#endif /* ZCSR_FILE_SERVICE_INTERNAL_H */
