/* file_posix.c — ZCSR File Service POSIX 后端实现
 *
 * 覆盖 Linux（SteamOS / Ubuntu / Kali）、Android 13、macOS 15。
 *
 * 平台守卫：本文件只在非 Win32 编译。Windows 由 file_win32.c 提供
 * 同名 zcsr_platform_file_ops 定义。CMake 用 GLOB 收集
 * modules/file_service 目录下的所有 .c 文件，所以每个后端文件必须自行守卫。
 *
 * 大文件：必须在任何系统头文件之前定义 _FILE_OFFSET_BITS=64，
 * 否则 32 位 Android 上 off_t 只有 32 位，>2 GiB 文件会 seek 崩坏。
 */
#if !defined(_WIN32)

#if !defined(_FILE_OFFSET_BITS)
#  define _FILE_OFFSET_BITS 64
#endif
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "zcsr/file_service.h"
#include "file_service_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* === 错误码映射 === */
static zcsr_file_result_t posix_err(int err) {
    switch (err) {
        case 0:         return ZCSR_FILE_OK;
        case ENOENT:
        case ENOTDIR:   return ZCSR_FILE_ERR_NOT_FOUND;
        case EACCES:
        case EPERM:     return ZCSR_FILE_ERR_ACCESS;
        case EEXIST:    return ZCSR_FILE_ERR_ALREADY_EXISTS;
        case ENOSPC:    return ZCSR_FILE_ERR_NO_SPACE;
        case EISDIR:    return ZCSR_FILE_ERR_IS_DIRECTORY;
        case ENOTEMPTY: return ZCSR_FILE_ERR_IO;
#ifdef ELOOP
        case ELOOP:     return ZCSR_FILE_ERR_IO;
#endif
        default:        return ZCSR_FILE_ERR_IO;
    }
}

/* === 修改时间归一化 ===
 * Linux/Android: struct stat::st_mtim (timespec)
 * macOS:         struct stat::st_mtimespec (timespec)
 * 二者字段名不同，必须分开取，否则 macOS 编不过。 */
static int64_t posix_mtime_ns(const struct stat* st) {
#if defined(__APPLE__)
    return (int64_t)st->st_mtimespec.tv_sec * 1000000000LL + (int64_t)st->st_mtimespec.tv_nsec;
#else
    return (int64_t)st->st_mtim.tv_sec * 1000000000LL + (int64_t)st->st_mtim.tv_nsec;
#endif
}

/* === stat === */
static zcsr_file_result_t posix_stat(const char* native_path, zcsr_file_stat_t* out_stat) {
    if (!native_path || !out_stat) return ZCSR_FILE_ERR_ARGUMENT;

    struct stat st;
    if (stat(native_path, &st) != 0) {
        return posix_err(errno);
    }

    if (S_ISDIR(st.st_mode)) {
        out_stat->kind = ZCSR_FILE_KIND_DIRECTORY;
        out_stat->size_bytes = 0;
    } else if (S_ISREG(st.st_mode)) {
        out_stat->kind = ZCSR_FILE_KIND_REGULAR;
        out_stat->size_bytes = (uint64_t)st.st_size;
    } else {
        out_stat->kind = ZCSR_FILE_KIND_UNKNOWN;
        out_stat->size_bytes = 0;
    }
    out_stat->modified_time_unix_ns = posix_mtime_ns(&st);
    return ZCSR_FILE_OK;
}

/* === directory iterator === */
typedef struct {
    DIR* dir;
    char path[1024];
} posix_dir_t;

static zcsr_file_result_t posix_dir_open(const char* native_path, void** out_dir) {
    if (!native_path || !out_dir) return ZCSR_FILE_ERR_ARGUMENT;

    DIR* d = opendir(native_path);
    if (!d) {
        return posix_err(errno);
    }

    posix_dir_t* dir = (posix_dir_t*)calloc(1, sizeof(posix_dir_t));
    if (!dir) {
        closedir(d);
        return ZCSR_FILE_ERR_IO;
    }
    dir->dir = d;
    size_t n = strlen(native_path);
    if (n >= sizeof(dir->path)) {
        closedir(d);
        free(dir);
        return ZCSR_FILE_ERR_ARGUMENT;
    }
    memcpy(dir->path, native_path, n + 1);

    *out_dir = dir;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_dir_next(void* dir_ptr, zcsr_dir_entry_t* out_entry,
                                         char* name_buf, size_t name_cap) {
    posix_dir_t* dir = (posix_dir_t*)dir_ptr;
    if (!dir || !dir->dir || !out_entry || !name_buf || name_cap == 0) {
        return ZCSR_FILE_ERR_ARGUMENT;
    }

    /* 跳过 . / ..；readdir 在结尾与错误时都返回 NULL，需 feof 语义区分，
     * POSIX 下用 errno 复位法判定真实结尾。 */
    for (;;) {
        errno = 0;
        struct dirent* ent = readdir(dir->dir);
        if (!ent) {
            return (errno == 0) ? ZCSR_FILE_END : posix_err(errno);
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        size_t n = strlen(ent->d_name);
        if (n >= name_cap) {
            return ZCSR_FILE_ERR_ARGUMENT;
        }
        memcpy(name_buf, ent->d_name, n + 1);
        out_entry->name = name_buf;

#if defined(__APPLE__)
        /* macOS 的 dirent 带 d_type；关闭时退回 stat。 */
        if (ent->d_type == DT_DIR) {
            out_entry->kind = ZCSR_FILE_KIND_DIRECTORY;
        } else if (ent->d_type == DT_REG || ent->d_type == DT_UNKNOWN) {
            out_entry->kind = ZCSR_FILE_KIND_REGULAR;
        } else {
            out_entry->kind = ZCSR_FILE_KIND_UNKNOWN;
        }
#else
        /* Android / Linux：bionic 与部分文件系统不保证 d_type 有效，统一 stat。 */
        (void)ent->d_type;
        out_entry->kind = ZCSR_FILE_KIND_UNKNOWN;
#endif

        /* 条目元数据统一走 stat（name 拼到目录路径后取）。 */
        {
            char full[2048];
            size_t pl = strlen(dir->path);
            size_t need = pl + 1 + n + 1;
            if (need <= sizeof(full)) {
                memcpy(full, dir->path, pl);
                full[pl] = '/';
                memcpy(full + pl + 1, name_buf, n + 1);

                struct stat st;
                if (stat(full, &st) == 0) {
                    out_entry->kind = S_ISDIR(st.st_mode)
                                      ? ZCSR_FILE_KIND_DIRECTORY
                                      : (S_ISREG(st.st_mode) ? ZCSR_FILE_KIND_REGULAR
                                                             : ZCSR_FILE_KIND_UNKNOWN);
                    out_entry->size_bytes = S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
                    out_entry->modified_time_unix_ns = posix_mtime_ns(&st);
                    return ZCSR_FILE_OK;
                }
                /* stat 失败（典型：符号链接断裂）→ 条目仍返回，元数据清零。 */
                out_entry->size_bytes = 0;
                out_entry->modified_time_unix_ns = 0;
                return ZCSR_FILE_OK;
            }
        }
        out_entry->size_bytes = 0;
        out_entry->modified_time_unix_ns = 0;
        return ZCSR_FILE_OK;
    }
}

static zcsr_file_result_t posix_dir_close(void* dir_ptr) {
    posix_dir_t* dir = (posix_dir_t*)dir_ptr;
    if (!dir) return ZCSR_FILE_ERR_ARGUMENT;
    if (dir->dir) closedir(dir->dir);
    free(dir);
    return ZCSR_FILE_OK;
}

/* === mkdir === */
static zcsr_file_result_t posix_mkdir(const char* native_path) {
    if (!native_path) return ZCSR_FILE_ERR_ARGUMENT;
    if (mkdir(native_path, 0755) != 0) {
        return posix_err(errno);
    }
    return ZCSR_FILE_OK;
}

/* === file I/O ===
 * 用 fd 而不是 FILE*：seek 语义与错误码都更直接，且不引入 stdio 缓冲。 */
typedef struct {
    int fd;
} posix_file_t;

static zcsr_file_result_t posix_open_read(const char* native_path, void** out_file) {
    if (!native_path || !out_file) return ZCSR_FILE_ERR_ARGUMENT;

    int fd = open(native_path, O_RDONLY);
    if (fd < 0) {
        return posix_err(errno);
    }

    posix_file_t* file = (posix_file_t*)calloc(1, sizeof(posix_file_t));
    if (!file) {
        close(fd);
        return ZCSR_FILE_ERR_IO;
    }
    file->fd = fd;
    *out_file = file;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_open_write(const char* native_path, void** out_file) {
    if (!native_path || !out_file) return ZCSR_FILE_ERR_ARGUMENT;

    int fd = open(native_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return posix_err(errno);
    }

    posix_file_t* file = (posix_file_t*)calloc(1, sizeof(posix_file_t));
    if (!file) {
        close(fd);
        return ZCSR_FILE_ERR_IO;
    }
    file->fd = fd;
    *out_file = file;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_read(void* file_ptr, void* buffer, size_t capacity,
                                     size_t* out_bytes_read) {
    posix_file_t* file = (posix_file_t*)file_ptr;
    if (!file || (!buffer && capacity > 0) || !out_bytes_read) {
        return ZCSR_FILE_ERR_ARGUMENT;
    }

    ssize_t n = read(file->fd, buffer, capacity);
    if (n < 0) {
        return posix_err(errno);
    }
    *out_bytes_read = (size_t)n;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_write(void* file_ptr, const void* data, size_t size,
                                      size_t* out_bytes_written) {
    posix_file_t* file = (posix_file_t*)file_ptr;
    if (!file || (!data && size > 0) || !out_bytes_written) {
        return ZCSR_FILE_ERR_ARGUMENT;
    }

    size_t done = 0;
    while (done < size) {
        ssize_t n = write(file->fd, (const char*)data + done, size - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            *out_bytes_written = done;
            return posix_err(errno);
        }
        done += (size_t)n;
    }
    *out_bytes_written = done;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_seek(void* file_ptr, int64_t offset, int origin,
                                     uint64_t* out_position) {
    posix_file_t* file = (posix_file_t*)file_ptr;
    if (!file) return ZCSR_FILE_ERR_ARGUMENT;

    /* whence 必须是 int（lseek 第三参数类型），不能用 off_t：
     * off_t 是 64 位宽，与 int 形参不匹配会触发 -Wconversion 告警。 */
    int base;
    switch (origin) {
        case 0: base = SEEK_SET; break;
        case 1: base = SEEK_CUR; break;
        case 2: base = SEEK_END; break;
        default: return ZCSR_FILE_ERR_ARGUMENT;
    }

    off_t pos = lseek(file->fd, (off_t)offset, base);
    if (pos < 0) {
        return posix_err(errno);
    }
    if (out_position) {
        *out_position = (uint64_t)pos;
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_close(void* file_ptr) {
    posix_file_t* file = (posix_file_t*)file_ptr;
    if (!file) return ZCSR_FILE_ERR_ARGUMENT;
    int rc = close(file->fd);
    free(file);
    return rc == 0 ? ZCSR_FILE_OK : posix_err(errno);
}

/* === file management === */
static zcsr_file_result_t posix_delete_file(const char* native_path) {
    if (!native_path) return ZCSR_FILE_ERR_ARGUMENT;
    if (unlink(native_path) != 0) {
        return posix_err(errno);
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_delete_dir(const char* native_path) {
    if (!native_path) return ZCSR_FILE_ERR_ARGUMENT;
    if (rmdir(native_path) != 0) {
        return posix_err(errno);
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t posix_rename(const char* from, const char* to) {
    if (!from || !to) return ZCSR_FILE_ERR_ARGUMENT;
    if (rename(from, to) != 0) {
        return posix_err(errno);
    }
    return ZCSR_FILE_OK;
}

/* POSIX 没有单次原子 copy，逐块读写。 */
static zcsr_file_result_t posix_copy(const char* from, const char* to) {
    if (!from || !to) return ZCSR_FILE_ERR_ARGUMENT;

    void* src = NULL;
    void* dst = NULL;
    zcsr_file_result_t rc = posix_open_read(from, &src);
    if (rc != ZCSR_FILE_OK) return rc;

    rc = posix_open_write(to, &dst);
    if (rc != ZCSR_FILE_OK) {
        posix_close(src);
        return rc;
    }

    unsigned char buf[64 * 1024];
    for (;;) {
        size_t got = 0;
        rc = posix_read(src, buf, sizeof(buf), &got);
        if (rc != ZCSR_FILE_OK) break;

        if (got == 0) { /* EOF */
            rc = ZCSR_FILE_OK;
            break;
        }

        size_t put = 0;
        rc = posix_write(dst, buf, got, &put);
        if (rc != ZCSR_FILE_OK) break;
        if (put != got) { rc = ZCSR_FILE_ERR_IO; break; }
    }

    (void)posix_close(src);
    (void)posix_close(dst);
    if (rc != ZCSR_FILE_OK) {
        (void)unlink(to); /* 复制失败不留半成品 */
    }
    return rc;
}

/* === 导出平台后端接口 === */
const zcsr_platform_file_ops_t zcsr_platform_file_ops = {
    .stat = posix_stat,
    .dir_open = posix_dir_open,
    .dir_next = posix_dir_next,
    .dir_close = posix_dir_close,
    .mkdir = posix_mkdir,
    .open_read = posix_open_read,
    .open_write = posix_open_write,
    .read = posix_read,
    .write = posix_write,
    .seek = posix_seek,
    .close = posix_close,
    .delete_file = posix_delete_file,
    .delete_dir = posix_delete_dir,
    .rename = posix_rename,
    .copy = posix_copy,
};

#endif /* !defined(_WIN32) */

#if defined(_WIN32)
/* Windows 构建时上面整段被排除，MSVC 会对空翻译单元报 C4206。
 * 放一个内部占位声明，保持 TU 非空且不导出任何符号。 */
typedef int zcsr_file_posix_unused_tu_;
#endif
