/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    debug_log.h
  * @brief   统一日志管理模块
  *
  * 使用方法:
  *   #include "debug_log.h"
  *   LOG_INFO("W5500 initialized");
  *   LOG_DEBUG("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  *   LOG_WARN("Connection timeout");
  *   LOG_ERROR("SPI communication failed");
  *
  * 日志级别控制 (在 FreeRTOSConfig.h 或编译选项中定义):
  *   LOG_LEVEL_NONE    0  - 关闭所有日志
  *   LOG_LEVEL_ERROR   1  - 仅错误
  *   LOG_LEVEL_WARN    2  - 错误+警告
  *   LOG_LEVEL_INFO    3  - 错误+警告+信息 (默认)
  *   LOG_LEVEL_DEBUG   4  - 全部
  *
  * 接线: USART1 (PA9/TX, PA10/RX) @115200
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __DEBUG_LOG_H__
#define __DEBUG_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <stdint.h>

/* ======================== 日志级别 ======================== */

#define LOG_LEVEL_NONE    0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4

/* 默认日志级别: INFO（可在编译选项中覆盖） */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
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

/* 不带标签的纯字符串输出（用于打印启动 Banner 等固定格式） */
#define LOG_RAW(str, len)  HAL_UART_Transmit(&huart1, (uint8_t*)(str), (len), 100)

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

#endif /* __DEBUG_LOG_H__ */
