#ifndef __SERVO_H__
#define __SERVO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 舵机角度范围（SG90 / MG996R 标准） */
#define SERVO_MIN_ANGLE    0
#define SERVO_MAX_ANGLE    180
#define SERVO_MID_ANGLE    90

/**
 * @brief 初始化舵机 PWM（TIM2_CH1, PA0）
 *        需要在 CubeMX 中配置 TIM2: 频率 50Hz (20ms 周期)
 *        定时器时钟 84MHz (APB1), PSC=16800-1, ARR=100-1 → 50Hz
 *        PWM 脉宽: 0°=0.5ms(2.5%), 90°=1.5ms(7.5%), 180°=2.5ms(12.5%)
 *        对应 CCR 值: 0°=2.5, 90°=7.5, 180°=12.5
 */
void servo_init(void);

/**
 * @brief 设置舵机角度（0~180°）
 *
 * @param angle 角度值，范围 0-180
 *              会自动限幅
 */
void servo_set_angle(int angle);

#ifdef __cplusplus
}
#endif

#endif
