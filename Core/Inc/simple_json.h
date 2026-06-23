#ifndef __SIMPLE_JSON_H__
#define __SIMPLE_JSON_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从 JSON 字符串中提取指定 key 的字符串值
 *
 * @param json   JSON 字符串（必须以 '\0' 结尾）
 * @param key    key 名称（如 "cmd"）
 * @param value  输出缓冲区，存放提取到的值
 * @param maxlen 输出缓冲区最大长度
 * @return 0=成功, -1=key没找到, -2=值太长, -3=格式错误, -4=value为空
 */
int json_get_string(const char *json, const char *key, char *value, uint8_t maxlen);

/**
 * @brief 从 JSON 字符串中提取指定 key 的整数值
 *
 * @param json   JSON 字符串
 * @param key    key 名称
 * @param value  输出整数指针
 * @return 0=成功, -1=key没找到, -2=格式错误
 */
int json_get_int(const char *json, const char *key, int *value);

#ifdef __cplusplus
}
#endif

#endif
