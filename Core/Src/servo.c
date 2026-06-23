/**
 * @file    servo.c
 * @brief   舵机 PWM 控制 (TIM2_CH1 → PA15)
 *
 * ===== CubeMX 必须手动配置 (重要!) =====
 * 目前工程里没有 TIM2！你需要打开 AirNode.ioc：
 *   1. Pinout → Timers → TIM2
 *      - Channel1 → "PWM Generation CH1"
 *   2. Configuration → TIM2 → Parameter Settings:
 *      - Prescaler (PSC): 1680-1
 *      - Counter Mode: Up
 *      - Counter Period (ARR): 1000-1
 *      - Auto-reload preload: Enable
 *      - CH1 Pulse(CCR): 75 (初始90°)
 *   3. Pinout 界面 PA0 会自动变成 TIM2_CH1
 *   4. 点 GENERATE CODE 重新生成
 *   5. 工程里会多出 tim.c / tim.h，里面有 MX_TIM2_Init()
 *   6. 在 main.c 的 MX_USART1_UART_Init() 后面加上 MX_TIM2_Init()
 *
 * ===== 定时器计算 =====
 *   APB1 = 84MHz
 *   PSC=1679 (1680-1) → 84MHz/1680 = 50kHz
 *   ARR=999 (1000-1)  → 50kHz/1000 = 50Hz (周期20ms)
 *
 *   PWM 占空比（CCR 值）:
 *     0°   = 0.5ms  = 0.5ms×50kHz = 25  ticks
 *     90°  = 1.5ms  = 1.5ms×50kHz = 75  ticks
 *     180° = 2.5ms  = 2.5ms×50kHz = 125 ticks
 */

#include "servo.h"
#include "main.h"
#include "tim.h"
#include "debug_log.h"

/**
 * @brief 启动 PWM 输出
 *
 * 注意! 必须先 MX_TIM2_Init() 初始化定时器
 */
void servo_init(void)
{
    LOG_INFO(TAG_SERVO, "servo_init() called");
    /* 启动 TIM2 CH1 PWM 输出 */
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
    {
        LOG_INFO(TAG_SERVO, "servo_init: PWM Start FAILED!");
    }
    else
    {
        LOG_INFO(TAG_SERVO, "servo_init: PWM started OK");
    }
}

/**
 * @brief 设置舵机角度 (0° ~ 180°)
 *
 * 内部映射公式:
 *   角度 0 → CCR 25
 *   角度 90 → CCR 75
 *   角度 180 → CCR 125
 *
 * 用整数计算避免浮点，运行更快
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
