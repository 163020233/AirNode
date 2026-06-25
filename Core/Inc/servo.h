#ifndef __SERVO_H
#define __SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SERVO_MIN_ANGLE   0
#define SERVO_MAX_ANGLE   180

void servo_init(void);
void servo_set_angle(int angle);

/* 新增：通过微秒脉宽设置指定通道 PWM 的 API 声明 */
void servo_set_pwm_us(uint8_t ch, uint16_t pwm_us);

#ifdef __cplusplus
}
#endif

#endif /* __SERVO_H */