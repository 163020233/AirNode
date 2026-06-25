/**
* @file    servo.c
 * @brief   舵机 PWM 控制 (F407: TIM2_CH1 → 通道 0, TIM2_CH2 → 通道 1)
 */

#include "servo.h"
#include "main.h"
#include "tim.h"
#include "debug_log.h"

extern TIM_HandleTypeDef htim2; // 声明引用 TIM2 句柄

/**
 * @brief 启动双通道 PWM 输出
 */
void servo_init(void)
{
    LOG_INFO(TAG_SERVO, "servo_init() called");

    /* 启动 TIM2 CH1 (对应通道 0) PWM 输出 */
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
    {
        LOG_ERROR(TAG_SERVO, "servo_init: CH1 PWM Start FAILED!");
    }
    else
    {
        LOG_INFO(TAG_SERVO, "servo_init: CH1 PWM started OK");
    }

    /* 启动 TIM2 CH2 (对应通道 1) PWM 输出 */
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) != HAL_OK)
    {
        LOG_ERROR(TAG_SERVO, "servo_init: CH2 PWM Start FAILED!");
    }
    else
    {
        LOG_INFO(TAG_SERVO, "servo_init: CH2 PWM started OK");
    }
}

/**
 * @brief 设置舵机角度 (0° ~ 180°)，用于通道 0 传统控制
 */
void servo_set_angle(int angle)
{
    /* 限幅 */
    if (angle < SERVO_MIN_ANGLE) angle = SERVO_MIN_ANGLE;
    if (angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;

    /* 直接整数映射: CCR = 25 + angle * 100 / 180 */
    uint32_t ccr = 25 + (uint32_t)angle * 100 / 180;

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, ccr);
}

/**
 * @brief 通过原始微秒 (us) 脉宽设置指定通道的 PWM (500us ~ 2500us)
 * @param ch 通道号 (0 代表 CH1, 1 代表 CH2)
 * @param pwm_us 微秒值 (1000~2000，例如 1500)
 */
void servo_set_pwm_us(uint8_t ch, uint16_t pwm_us)
{
    /* 标准舵机脉宽安全保护限制 */
    if (pwm_us < 500)  pwm_us = 500;
    if (pwm_us > 2500) pwm_us = 2500;

    /* 计算定时器 CCR 值 (50kHz 频率下，1 tick = 20微秒) */
    uint32_t ccr = (uint32_t)pwm_us / 20;

    if (ch == 0)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, ccr);
        // 修复：显式强转为 (int)，消除编译器 -Wformat 警告
        LOG_DEBUG(TAG_SERVO, "CH0 PWM set to %d us (CCR=%d)", (int)pwm_us, (int)ccr);
    }
    else if (ch == 1)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, ccr);
        // 修复：显式强转为 (int)，消除编译器 -Wformat 警告
        LOG_DEBUG(TAG_SERVO, "CH1 PWM set to %d us (CCR=%d)", (int)pwm_us, (int)ccr);
    }
}