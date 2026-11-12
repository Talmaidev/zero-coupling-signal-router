/*
 * zcsr_log.h - 统一日志系统，参考 SDL2 日志风格
 * 分级：DEBUG / INFO / WARN / ERROR
 * 用法：ZCSR_LOG_INFO("render", "target switched to RTT %d", target);
 */
#ifndef ZCSR_LOG_H
#define ZCSR_LOG_H

#include <stdio.h>
#include <stdarg.h>

typedef enum {
    ZCSR_LOG_DEBUG = 0,
    ZCSR_LOG_INFO = 1,
    ZCSR_LOG_WARN = 2,
    ZCSR_LOG_ERROR = 3
} ZCSR_LogLevel;

/* 全局日志级别，低于这个级别的日志不会输出 */
static ZCSR_LogLevel g_zcsr_log_level = ZCSR_LOG_DEBUG;

static inline void zcsr_log_log(ZCSR_LogLevel level, const char *category, const char *fmt, ...) {
    if (level < g_zcsr_log_level) return;
    
    const char *level_str = "UNKNOWN";
    switch (level) {
        case ZCSR_LOG_DEBUG: level_str = "DEBUG"; break;
        case ZCSR_LOG_INFO:  level_str = "INFO";  break;
        case ZCSR_LOG_WARN:  level_str = "WARN";  break;
        case ZCSR_LOG_ERROR: level_str = "ERROR"; break;
    }
    
    printf("[%s][%s] ", level_str, category);
    
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    printf("\n");
}

#define ZCSR_LOG_DEBUG(cat, fmt, ...) zcsr_log_log(ZCSR_LOG_DEBUG, cat, fmt, ##__VA_ARGS__)
#define ZCSR_LOG_INFO(cat, fmt, ...)  zcsr_log_log(ZCSR_LOG_INFO,  cat, fmt, ##__VA_ARGS__)
#define ZCSR_LOG_WARN(cat, fmt, ...)  zcsr_log_log(ZCSR_LOG_WARN,  cat, fmt, ##__VA_ARGS__)
#define ZCSR_LOG_ERROR(cat, fmt, ...) zcsr_log_log(ZCSR_LOG_ERROR, cat, fmt, ##__VA_ARGS__)

#endif /* ZCSR_LOG_H */
