/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    flash_storage.h
  * @brief   Flash 参数存储（抛投器配置）
  *
  * 存储布局 (STM32F407ZG, 1MB Flash, 最后1页 0x080F0000):
  *   偏移    内容
  *   0x0000  MAGIC = 0xA5A5 (2字节)
  *   0x0002  CH0 闭合 PWM (2字节)
  *   0x0004  CH0 投掷 PWM (2字节)
  *   0x0006  CH1 闭合 PWM (2字节)
  *   0x0008  CH1 投掷 PWM (2字节)
  *   0x000A  CHECKSUM (2字节, 所有数据累加)
  *
  * 接口:
  *   flash_load_defaults()     — 上电调用，加载配置到全局变量
  *   flash_get_config(ch)      — 获取通道配置
  *   flash_set_config(ch,c,r)  — 写入并保存到 Flash
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __FLASH_STORAGE_H__
#define __FLASH_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 通道数量 */
#define FLASH_CHANNEL_COUNT  2

/* 默认配置 */
#define DEFAULT_CLOSED_PWM   1500
#define DEFAULT_RELEASED_PWM 2000

/* 配置结构 */
typedef struct {
    uint16_t closed_pwm;    // 闭合时的 PWM 值
    uint16_t released_pwm;  // 投掷时的 PWM 值
} FlashChannelConfig;

/**
 * @brief 上电初始化: 从 Flash 读取配置，校验失败则用默认值
 */
void flash_load_defaults(void);

/**
 * @brief 获取通道配置
 * @param ch 通道号 (0~FLASH_CHANNEL_COUNT-1)
 * @return 配置结构体指针
 */
const FlashChannelConfig* flash_get_config(uint8_t ch);

/**
 * @brief 更新通道配置并写入 Flash
 * @param ch       通道号
 * @param closed   闭合 PWM
 * @param released 投掷 PWM
 * @return 0=成功, -1=写入失败
 */
int flash_set_config(uint8_t ch, uint16_t closed, uint16_t released);

#ifdef __cplusplus
}
#endif

#endif
