#ifndef __DEBUG_LOG_H__
#define __DEBUG_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <stdint.h>

/* ======================== 调试模式开关 ======================== */

/*
 * DEBUG_MODE 宏定义:
 * 在编译选项中 -DDEBUG_MODE=1 开启调试模式（全日志）
 * 不定义或定义为0则为发布模式（仅ERROR/WARN）
 *
 * CubeMX/CLion 的 Debug 配置已自动定义了 DEBUG 宏。
 * 用 #ifdef DEBUG 来检测当前是否为调试构建。
 */

#ifndef DEBUG_MODE
#define DEBUG_MODE 1
#endif

/* ======================== 日志级别 ======================== */

#define LOG_LEVEL_NONE    0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4

/* 调试模式: LOG_LEVEL_DEBUG, 发布模式: LOG_LEVEL_WARN */
#if DEBUG_MODE
#define LOG_LEVEL LOG_LEVEL_DEBUG
#else
#define LOG_LEVEL LOG_LEVEL_WARN
#endif

/* ======================== 日志宏 ======================== */

#define LOG_TAG(level_char, tag, fmt, ...) do { \
    char _lobuf[192]; \
    int _n = snprintf(_lobuf, sizeof(_lobuf), \
        "[" level_char "][" tag "] " fmt "\r\n", \
        ##__VA_ARGS__); \
    if (_n > 0) HAL_UART_Transmit(&huart1, (uint8_t*)_lobuf, _n, 100); \
} while(0)

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(tag, ...)    LOG_TAG("E", tag, ##__VA_ARGS__)
#else
#define LOG_ERROR(tag, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(tag, ...)     LOG_TAG("W", tag, ##__VA_ARGS__)
#else
#define LOG_WARN(tag, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(tag, ...)     LOG_TAG("I", tag, ##__VA_ARGS__)
#else
#define LOG_INFO(tag, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(tag, ...)    LOG_TAG("D", tag, ##__VA_ARGS__)
#else
#define LOG_DEBUG(tag, ...)
#endif

#define LOG_RAW(str, len)  HAL_UART_Transmit(&huart1, (uint8_t*)(str), (len), 100)

/* ======================== 调试断言 ======================== */

/* 调试模式下才启用断言检查 */
#if DEBUG_MODE
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        LOG_ERROR(TAG_SYSTEM, "ASSERT FAILED: %s  (%s)", msg, #cond); \
        Error_Handler(); \
    } \
} while(0)
#else
#define ASSERT(cond, msg)
#endif

/* ======================== 预定义标签 ======================== */

#define TAG_INIT    "INIT"
#define TAG_W5500   "W5500"
#define TAG_SERVO   "SERVO"
#define TAG_UART    "UART"
#define TAG_RTOS    "RTOS"
#define TAG_MAIN    "MAIN"
#define TAG_SYSTEM  "SYS"

#ifdef __cplusplus
}
#endif

#endif
