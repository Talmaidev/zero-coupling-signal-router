/* zcsr/diagnostics.h — ZCSR 统一诊断门面
 *
 * 在现有 zcsr_log 之上封装：
 *   - 统一日志宏（自动带 file/line/function）
 *   - Assert / Verify / Panic
 *   - 错误传播辅助宏
 *
 * 不复制 Log 的线程同步、输出和格式化系统。
 */

#ifndef ZCSR_DIAGNOSTICS_H
#define ZCSR_DIAGNOSTICS_H

#include "zcsr/log.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 日志宏：自动记录 file / line / function
 * ============================================================
 *
 * 使用方式：
 *   ZCSR_LOG_INFO("resource", "Loaded %s", path);
 *   ZCSR_LOG_ERROR("ui", "Invalid node id %u", node_id);
 *
 * 编译期下限 ZCSR_LOG_COMPILE_LEVEL（0..5，默认 0=全部保留）：
 *   低于该级别的调用展开为 (void)0，格式串与实参都不求值，
 *   可从 Release 二进制中彻底剔除。默认 0 不改变现有行为。
 *
 * 偏差记录 DEV-001：##__VA_ARGS__ 为 GNU/MSVC 扩展，用于支持零实参调用；
 *   ISO C11 无 __VA_OPT__。该模式仅允许存在于本头文件，禁止扩散。
 */

#ifndef ZCSR_LOG_COMPILE_LEVEL
#define ZCSR_LOG_COMPILE_LEVEL 0
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 0
#define ZCSR_LOG_TRACE(module, fmt, ...) \
    zcsr_log_write(ZCSR_LOG_TRACE, module, "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ZCSR_LOG_TRACE(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 1
#define ZCSR_LOG_DEBUG(module, fmt, ...) \
    zcsr_log_write(ZCSR_LOG_DEBUG, module, "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ZCSR_LOG_DEBUG(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 2
#define ZCSR_LOG_INFO(module, fmt, ...) \
    zcsr_log_write(ZCSR_LOG_INFO, module, "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ZCSR_LOG_INFO(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 3
#define ZCSR_LOG_WARN(module, fmt, ...) \
    zcsr_log_write(ZCSR_LOG_WARN, module, "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ZCSR_LOG_WARN(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 4
#define ZCSR_LOG_ERROR(module, fmt, ...) \
    zcsr_log_write(ZCSR_LOG_ERROR, module, "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ZCSR_LOG_ERROR(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 5
#define ZCSR_LOG_FATAL(module, fmt, ...) \
    zcsr_log_write(ZCSR_LOG_FATAL, module, "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define ZCSR_LOG_FATAL(module, fmt, ...) ((void)0)
#endif

/* ============================================================
 * Assert：Debug 启用，Release 关闭
 * 用于程序员逻辑错误和内部不变量
 * ============================================================ */

#ifdef NDEBUG
  #define ZCSR_ASSERT(expr) ((void)0)
  #define ZCSR_ASSERT_MSG(expr, msg) ((void)0)
#else
  #define ZCSR_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            zcsr_log_write(ZCSR_LOG_FATAL, "assert", "ASSERT FAILED: %s [%s:%d]", #expr, __FILE__, __LINE__); \
            fflush(stderr); \
            abort(); \
        } \
    } while(0)

  #define ZCSR_ASSERT_MSG(expr, msg) \
    do { \
        if (!(expr)) { \
            zcsr_log_write(ZCSR_LOG_FATAL, "assert", "ASSERT FAILED: %s (%s) [%s:%d]", #expr, msg, __FILE__, __LINE__); \
            fflush(stderr); \
            abort(); \
        } \
    } while(0)
#endif

/* ============================================================
 * Verify：Debug / Release 都执行
 * 用于运行时检查，失败返回 false
 * ============================================================ */

#define ZCSR_VERIFY(expr) \
    ((expr) ? true : (ZCSR_LOG_ERROR("verify", "VERIFY FAILED: %s [%s:%d]", #expr, __FILE__, __LINE__), false))

/* 检查条件失败则记录日志并返回指定值 */
#define ZCSR_VERIFY_OR_RETURN(expr, retval) \
    do { \
        if (!(expr)) { \
            ZCSR_LOG_ERROR("verify", "VERIFY FAILED: %s [%s:%d]", #expr, __FILE__, __LINE__); \
            return (retval); \
        } \
    } while(0)

/* ============================================================
 * Panic：不可恢复的致命错误
 * 记录后终止程序
 * ============================================================ */

#define ZCSR_PANIC(msg) \
    do { \
        zcsr_log_write(ZCSR_LOG_FATAL, "panic", "PANIC: %s [%s:%d]", msg, __FILE__, __LINE__); \
        fflush(stderr); \
        abort(); \
    } while(0)

/* ============================================================
 * 错误传播辅助宏
 * ============================================================ */

/* 检查条件，失败则记录日志并返回指定值
 * 使用方式：
 *   ZCSR_CHECK_OR_RETURN(ptr != NULL, ZCSR_RESOURCE_ERROR_INVALID, "resource", "ptr is NULL");
 */
#define ZCSR_CHECK_OR_RETURN(condition, retval, module, msg) \
    do { \
        if (!(condition)) { \
            ZCSR_LOG_ERROR(module, "CHECK FAILED: %s - %s", #condition, msg); \
            return (retval); \
        } \
    } while(0)

/* 传播下层错误（注意：此宏假设成功值为 0）
 * 使用方式：
 *   zcsr_resource_result_t result = load_internal();
 *   if (result != ZCSR_RESOURCE_OK) return result;
 *
 * 为了跨编译器兼容，请直接写 if 判断，不要使用此宏
 */

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_DIAGNOSTICS_H */
