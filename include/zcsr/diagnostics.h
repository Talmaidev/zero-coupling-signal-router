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

#include <stdbool.h>
#ifdef __cplusplus
/* C++17 has no C11 _Atomic; rate-limited macros use std::atomic instead. */
#include <atomic>
#else
/* C11 <stdatomic.h> is not a C++17 header (GCC rejects its _Atomic typedefs;
 * MSVC only warns). */
#include <stdatomic.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep the C logger declaration inside this header's C-linkage region. This
 * matters when a C++ caller includes diagnostics.h directly and does not wrap
 * the include in its own extern "C" block. */
#include "zcsr/log.h"

/* ============================================================
 * 日志宏：自动记录 file / line / function
 * ============================================================
 *
 * 使用方式：
 *   ZCSR_LOG_INFO("resource", "Loaded %s", path);
 *   ZCSR_LOG_ERROR("ui", "Invalid node id %u", node_id);
 *
 * 编译期下限 ZCSR_LOG_COMPILE_LEVEL（0..5）：
 *   Debug 默认 0（全部保留）；Release（NDEBUG）默认 2（保留 INFO/WARN/ERROR/FATAL，
 *   剔除 TRACE/DEBUG）。低于该级别的调用展开为 (void)0，格式串与实参都不求值，
 *   可从 Release 二进制中彻底剔除。
 *
 * 偏差记录 DEV-001：##__VA_ARGS__ 为 GNU/MSVC/Clang 扩展，用于支持零实参调用；
 *   ISO C11 无 __VA_OPT__，无法在标准 C11 内同时支持“零实参 + 可变参数”。
 *   本仓库明确接受该扩展，并在 docs/DEVIATIONS.md 记录；其它编译器直接报错，
 *   不允许静默退化。
 */
#if !defined(__GNUC__) && !defined(__clang__) && !defined(_MSC_VER)
#error "ZCSR diagnostics.h requires ##__VA_ARGS__ support; see docs/DEVIATIONS.md (DEV-001)"
#endif

#ifndef ZCSR_LOG_COMPILE_LEVEL
#  ifdef NDEBUG
#    define ZCSR_LOG_COMPILE_LEVEL 2   /* Release: keep INFO/WARN/ERROR/FATAL, strip TRACE/DEBUG */
#  else
#    define ZCSR_LOG_COMPILE_LEVEL 0   /* Debug: keep every level */
#  endif
#endif

#if ZCSR_LOG_COMPILE_LEVEL < 0 || ZCSR_LOG_COMPILE_LEVEL > 5
#error "ZCSR_LOG_COMPILE_LEVEL must be in 0..5"
#endif

/* 单一展开点。`fmt` 必须是字符串字面量：文件/行号前缀依赖相邻字面量拼接。 */
#define ZCSR_LOG_EMIT(level_value, module_value, fmt, ...) \
    zcsr_log_write((level_value), (module_value), "[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

#if ZCSR_LOG_COMPILE_LEVEL <= 0
#define ZCSR_LOG_TRACE(module, fmt, ...) \
    ZCSR_LOG_EMIT(ZCSR_LOG_TRACE, module, fmt, ##__VA_ARGS__)
#else
#define ZCSR_LOG_TRACE(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 1
#define ZCSR_LOG_DEBUG(module, fmt, ...) \
    ZCSR_LOG_EMIT(ZCSR_LOG_DEBUG, module, fmt, ##__VA_ARGS__)
#else
#define ZCSR_LOG_DEBUG(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 2
#define ZCSR_LOG_INFO(module, fmt, ...) \
    ZCSR_LOG_EMIT(ZCSR_LOG_INFO, module, fmt, ##__VA_ARGS__)
#else
#define ZCSR_LOG_INFO(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 3
#define ZCSR_LOG_WARN(module, fmt, ...) \
    ZCSR_LOG_EMIT(ZCSR_LOG_WARN, module, fmt, ##__VA_ARGS__)
#else
#define ZCSR_LOG_WARN(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 4
#define ZCSR_LOG_ERROR(module, fmt, ...) \
    ZCSR_LOG_EMIT(ZCSR_LOG_ERROR, module, fmt, ##__VA_ARGS__)
#else
#define ZCSR_LOG_ERROR(module, fmt, ...) ((void)0)
#endif

#if ZCSR_LOG_COMPILE_LEVEL <= 5
#define ZCSR_LOG_FATAL(module, fmt, ...) \
    ZCSR_LOG_EMIT(ZCSR_LOG_FATAL, module, fmt, ##__VA_ARGS__)
#else
#define ZCSR_LOG_FATAL(module, fmt, ...) ((void)0)
#endif

/* ============================================================
 * 限流宏：每个调用点一个私有计数器，无堆分配
 * ============================================================
 *
 *   ZCSR_LOG_ONCE(level, module, fmt, ...)          仅第一次执行
 *   ZCSR_LOG_FIRST_N(level, module, n, fmt, ...)    前 n 次执行
 *   ZCSR_LOG_EVERY_N(level, module, n, fmt, ...)    第 1、1+n、1+2n… 次执行
 *
 * `n` 必须大于 0；非正数不输出。计数器是展开点内的 static 原子变量，
 * 每个调用点独立，跨调用保留。低于 ZCSR_LOG_COMPILE_LEVEL 时整个展开被
 * 编译期条件裁掉，不会产生计数器或参数求值。
 *
 * C11 用 <stdatomic.h> 的 _Atomic / atomic_*；C++17 用 <stdatomic.h> 不可用，
 * 这里改用 std::atomic 适配，保证同一组宏在两种语言下都能编译。
 */
#ifdef __cplusplus
#  define ZCSR_LOG_ATOMIC_TYPE(T) std::atomic<T>
#  define ZCSR_LOG_ATOMIC_EXCHANGE(p, v) \
       ((p)->exchange((v), std::memory_order_relaxed))
#  define ZCSR_LOG_ATOMIC_FETCH_ADD(p, v) \
       ((p)->fetch_add((v), std::memory_order_relaxed))
#  define ZCSR_LOG_ATOMIC_LOAD(p) \
       ((p)->load(std::memory_order_relaxed))
#  define ZCSR_LOG_ATOMIC_CAS(p, expected, desired) \
       ((p)->compare_exchange_strong(*(expected), (desired), \
                                     std::memory_order_relaxed))
#else
#  define ZCSR_LOG_ATOMIC_TYPE(T) _Atomic T
#  define ZCSR_LOG_ATOMIC_EXCHANGE(p, v) \
       atomic_exchange_explicit((p), (v), memory_order_relaxed)
#  define ZCSR_LOG_ATOMIC_FETCH_ADD(p, v) \
       atomic_fetch_add_explicit((p), (v), memory_order_relaxed)
#  define ZCSR_LOG_ATOMIC_LOAD(p) \
       atomic_load_explicit((p), memory_order_relaxed)
#  define ZCSR_LOG_ATOMIC_CAS(p, expected, desired) \
       atomic_compare_exchange_strong_explicit((p), (expected), (desired), \
                                                memory_order_relaxed, \
                                                memory_order_relaxed)
#endif

#define ZCSR_LOG_ONCE(level_value, module_value, fmt, ...) \
    do { \
        int zcsr_log_level_value = (int)(level_value); \
        if (zcsr_log_level_value >= ZCSR_LOG_COMPILE_LEVEL) { \
            static ZCSR_LOG_ATOMIC_TYPE(int) zcsr_log_once_flag = 0; \
            if (ZCSR_LOG_ATOMIC_EXCHANGE(&zcsr_log_once_flag, 1) == 0) { \
                ZCSR_LOG_EMIT((zcsr_log_level)zcsr_log_level_value, \
                              (module_value), fmt, ##__VA_ARGS__); \
            } \
        } \
    } while (0)

#define ZCSR_LOG_FIRST_N(level_value, module_value, n, fmt, ...) \
    do { \
        int zcsr_log_level_value = (int)(level_value); \
        if (zcsr_log_level_value >= ZCSR_LOG_COMPILE_LEVEL) { \
            static ZCSR_LOG_ATOMIC_TYPE(unsigned) zcsr_log_first_n_counter = 0; \
            unsigned zcsr_log_first_n_index = ZCSR_LOG_ATOMIC_FETCH_ADD( \
                &zcsr_log_first_n_counter, 1u); \
            int zcsr_log_first_n_limit = (int)(n); \
            if (zcsr_log_first_n_limit > 0 && \
                zcsr_log_first_n_index < (unsigned)zcsr_log_first_n_limit) { \
                ZCSR_LOG_EMIT((zcsr_log_level)zcsr_log_level_value, \
                              (module_value), fmt, ##__VA_ARGS__); \
            } \
        } \
    } while (0)

#define ZCSR_LOG_EVERY_N(level_value, module_value, n, fmt, ...) \
    do { \
        int zcsr_log_level_value = (int)(level_value); \
        if (zcsr_log_level_value >= ZCSR_LOG_COMPILE_LEVEL) { \
            static ZCSR_LOG_ATOMIC_TYPE(unsigned) zcsr_log_every_n_counter = 0; \
            unsigned zcsr_log_every_n_index = ZCSR_LOG_ATOMIC_FETCH_ADD( \
                &zcsr_log_every_n_counter, 1u); \
            int zcsr_log_every_n_step = (int)(n); \
            if (zcsr_log_every_n_step > 0 && \
                (zcsr_log_every_n_index % (unsigned)zcsr_log_every_n_step) == 0u) { \
                ZCSR_LOG_EMIT((zcsr_log_level)zcsr_log_level_value, \
                              (module_value), fmt, ##__VA_ARGS__); \
            } \
        } \
    } while (0)

/* 第 1 次执行，以及之后每隔 ms 毫秒执行一次；ms <= 0 不输出。
 * 时间源是 zcsr_log_now_ms()（单调时钟，不受系统时间回拨影响）。多线程同时
 * 命中时由 CAS 保证一个间隔内只有一个线程输出。 */
#define ZCSR_LOG_EVERY_MS(level_value, module_value, ms, fmt, ...) \
    do { \
        int zcsr_log_level_value = (int)(level_value); \
        long long zcsr_log_every_ms_interval = (long long)(ms); \
        if (zcsr_log_level_value >= ZCSR_LOG_COMPILE_LEVEL && \
            zcsr_log_every_ms_interval > 0) { \
            static ZCSR_LOG_ATOMIC_TYPE(uint64_t) zcsr_log_every_ms_last = 0; \
            uint64_t zcsr_log_every_ms_now = zcsr_log_now_ms(); \
            uint64_t zcsr_log_every_ms_prev = ZCSR_LOG_ATOMIC_LOAD( \
                &zcsr_log_every_ms_last); \
            if ((zcsr_log_every_ms_prev == 0 || \
                 zcsr_log_every_ms_now - zcsr_log_every_ms_prev >= \
                     (uint64_t)zcsr_log_every_ms_interval) && \
                ZCSR_LOG_ATOMIC_CAS(&zcsr_log_every_ms_last, \
                                    &zcsr_log_every_ms_prev, \
                                    zcsr_log_every_ms_now)) { \
                ZCSR_LOG_EMIT((zcsr_log_level)zcsr_log_level_value, \
                              (module_value), fmt, ##__VA_ARGS__); \
            } \
        } \
    } while (0)

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
