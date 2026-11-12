/* file_win32.c — ZCSR File Service Win32 后端实现
 *
 * 平台守卫：本文件只在 Win32 编译。其他平台（Linux/macOS/Android）需要
 * 各自的 file_posix.c / file_cocoa.c / file_android.c 提供同名
 * zcsr_file_backend_ops 定义。CMake 用 GLOB 收集
 * modules/file_service/*.c，所以每个后端文件必须自行守卫。
 */
#if defined(_WIN32)

#include "zcsr/file_service.h"
#include "file_service_internal.h"
#include <windows.h>
#include <string.h>
#include <stdlib.h>

/* MSVC 没有 swprintf，用 _snwprintf 代替 */
#define swprintf _snwprintf

/* 把 UTF-8 路径转换成宽字符路径 */
static int utf8_to_wide(const char* utf8, wchar_t* wide, int wide_cap) {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_cap);
}

/* 把宽字符路径转换成 UTF-8 */
static int wide_to_utf8(const wchar_t* wide, char* utf8, int utf8_cap) {
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8_cap, NULL, NULL);
}

/* === stat === */
static zcsr_file_result_t win32_stat(const char* native_path, zcsr_file_stat_t* out_stat) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &data)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return ZCSR_FILE_ERR_NOT_FOUND;
        return ZCSR_FILE_ERR_IO;
    }

    out_stat->kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                     ? ZCSR_FILE_KIND_DIRECTORY : ZCSR_FILE_KIND_REGULAR;
    out_stat->size_bytes = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;

    /* FILETIME 转 Unix 时间戳（纳秒） */
    ULARGE_INTEGER ull;
    ull.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = data.ftLastWriteTime.dwHighDateTime;
    out_stat->modified_time_unix_ns = (int64_t)((ull.QuadPart - 116444736000000000ULL) * 100);

    return ZCSR_FILE_OK;
}

/* === directory iterator === */
typedef struct {
    HANDLE hFind;
    WIN32_FIND_DATAW find_data;
    bool first;
} win32_dir_t;

static zcsr_file_result_t win32_dir_open(const char* native_path, void** out_dir) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    /* 拼接 \* 表示通配符 */
    wchar_t pattern[1100];
    wcscpy(pattern, wpath);
    wcscat(pattern, L"\\*");

    win32_dir_t* dir = (win32_dir_t*)calloc(1, sizeof(win32_dir_t));
    dir->hFind = FindFirstFileW(pattern, &dir->find_data);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return ZCSR_FILE_ERR_NOT_FOUND;
    }
    dir->first = true;
    *out_dir = dir;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_dir_next(void* dir_ptr, zcsr_dir_entry_t* out_entry,
                                          char* name_buf, size_t name_cap) {
    win32_dir_t* dir = (win32_dir_t*)dir_ptr;

    if (dir->first) {
        dir->first = false;
    } else {
        if (!FindNextFileW(dir->hFind, &dir->find_data)) {
            return ZCSR_FILE_END;
        }
    }

    /* 跳过 . 和 .. */
    if (wcscmp(dir->find_data.cFileName, L".") == 0 ||
        wcscmp(dir->find_data.cFileName, L"..") == 0) {
        return win32_dir_next(dir_ptr, out_entry, name_buf, name_cap);
    }

    out_entry->kind = (dir->find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                      ? ZCSR_FILE_KIND_DIRECTORY : ZCSR_FILE_KIND_REGULAR;
    out_entry->size_bytes = ((uint64_t)dir->find_data.nFileSizeHigh << 32) | dir->find_data.nFileSizeLow;

    wide_to_utf8(dir->find_data.cFileName, name_buf, (int)name_cap);
    out_entry->name = name_buf;

    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_dir_close(void* dir_ptr) {
    win32_dir_t* dir = (win32_dir_t*)dir_ptr;
    FindClose(dir->hFind);
    free(dir);
    return ZCSR_FILE_OK;
}

/* === mkdir === */
static zcsr_file_result_t win32_mkdir(const char* native_path) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    if (!CreateDirectoryW(wpath, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) return ZCSR_FILE_ERR_ALREADY_EXISTS;
        return ZCSR_FILE_ERR_IO;
    }
    return ZCSR_FILE_OK;
}

/* === file I/O === */
typedef struct {
    HANDLE hFile;
} win32_file_t;

static zcsr_file_result_t win32_open_read(const char* native_path, void** out_file) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return ZCSR_FILE_ERR_NOT_FOUND;
    }

    win32_file_t* file = (win32_file_t*)calloc(1, sizeof(win32_file_t));
    file->hFile = h;
    *out_file = file;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_open_write(const char* native_path, void** out_file) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return ZCSR_FILE_ERR_IO;
    }

    win32_file_t* file = (win32_file_t*)calloc(1, sizeof(win32_file_t));
    file->hFile = h;
    *out_file = file;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_read(void* file_ptr, void* buffer, size_t capacity,
                                       size_t* out_bytes_read) {
    win32_file_t* file = (win32_file_t*)file_ptr;
    DWORD bytes_read = 0;
    if (!ReadFile(file->hFile, buffer, (DWORD)capacity, &bytes_read, NULL)) {
        return ZCSR_FILE_ERR_IO;
    }
    *out_bytes_read = bytes_read;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_write(void* file_ptr, const void* data, size_t size,
                                        size_t* out_bytes_written) {
    win32_file_t* file = (win32_file_t*)file_ptr;
    DWORD bytes_written = 0;
    if (!WriteFile(file->hFile, data, (DWORD)size, &bytes_written, NULL)) {
        return ZCSR_FILE_ERR_IO;
    }
    *out_bytes_written = bytes_written;
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_seek(void* file_ptr, int64_t offset, int origin,
                                      uint64_t* out_position) {
    win32_file_t* file = (win32_file_t*)file_ptr;
    DWORD move_method;
    switch (origin) {
        case 0: move_method = FILE_BEGIN; break;
        case 1: move_method = FILE_CURRENT; break;
        case 2: move_method = FILE_END; break;
        default: return ZCSR_FILE_ERR_ARGUMENT;
    }

    LARGE_INTEGER li;
    li.QuadPart = offset;
    DWORD new_pos = SetFilePointer(file->hFile, li.LowPart, &li.HighPart, move_method);
    if (new_pos == INVALID_SET_FILE_POINTER) {
        return ZCSR_FILE_ERR_IO;
    }

    if (out_position) {
        *out_position = ((uint64_t)li.HighPart << 32) | new_pos;
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_close(void* file_ptr) {
    win32_file_t* file = (win32_file_t*)file_ptr;
    CloseHandle(file->hFile);
    free(file);
    return ZCSR_FILE_OK;
}

/* === file management === */
static zcsr_file_result_t win32_delete_file(const char* native_path) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    if (!DeleteFileW(wpath)) {
        return ZCSR_FILE_ERR_IO;
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_delete_dir(const char* native_path) {
    wchar_t wpath[1024];
    if (utf8_to_wide(native_path, wpath, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    if (!RemoveDirectoryW(wpath)) {
        return ZCSR_FILE_ERR_IO;
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_rename(const char* from, const char* to) {
    wchar_t wfrom[1024], wto[1024];
    if (utf8_to_wide(from, wfrom, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;
    if (utf8_to_wide(to, wto, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    if (!MoveFileW(wfrom, wto)) {
        return ZCSR_FILE_ERR_IO;
    }
    return ZCSR_FILE_OK;
}

static zcsr_file_result_t win32_copy(const char* from, const char* to) {
    wchar_t wfrom[1024], wto[1024];
    if (utf8_to_wide(from, wfrom, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;
    if (utf8_to_wide(to, wto, 1024) == 0) return ZCSR_FILE_ERR_ARGUMENT;

    if (!CopyFileW(wfrom, wto, FALSE)) {
        return ZCSR_FILE_ERR_IO;
    }
    return ZCSR_FILE_OK;
}

/* === 导出平台后端接口 === */
const zcsr_platform_file_ops_t zcsr_platform_file_ops = {
    .stat = win32_stat,
    .dir_open = win32_dir_open,
    .dir_next = win32_dir_next,
    .dir_close = win32_dir_close,
    .mkdir = win32_mkdir,
    .open_read = win32_open_read,
    .open_write = win32_open_write,
    .read = win32_read,
    .write = win32_write,
    .seek = win32_seek,
    .close = win32_close,
    .delete_file = win32_delete_file,
    .delete_dir = win32_delete_dir,
    .rename = win32_rename,
    .copy = win32_copy,
};

#endif /* defined(_WIN32) */
