/**
 * @file    simple_json.c
 * @brief   极简 JSON key-value 解析器（不用 cJSON）
 *
 * 支持场景:
 *   {"cmd":"servo","angle":90}
 *   {"cmd":"servo","angle":-45}
 *
 * 不支持嵌套对象/数组，只做一层 key-value 提取。
 */

#include "simple_json.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief 跳过空白字符
 */
static const char* skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/**
 * @brief 查找 key 并返回 value 的起始位置（指向值首字符）
 */
static const char* find_value(const char *json, const char *key)
{
    if (!json || !key) return NULL;

    int keylen = strlen(key);

    /* 在 json 中查找 "key": */
    /* 逐字符搜索，效率不高但足够应付小数据 */
    while (*json)
    {
        /* 找双引号 */
        if (*json != '"') { json++; continue; }

        json++;  // 跳过左引号

        /* 比较 key */
        if (strncmp(json, key, keylen) == 0 && json[keylen] == '"')
        {
            json += keylen;  // 跳过 key
            if (*json != '"') return NULL;  // 语法检查
            json++;  // 跳过右引号

            json = skip_space(json);
            if (*json != ':') return NULL;
            json++;  // 跳过冒号
            json = skip_space(json);

            return json;  // 指向 value 首字符
        }
        else
        {
            /* 不是目标 key，跳到下一个双引号 */
            json = strchr(json, '"');
            if (!json) return NULL;
            json++;  // 跳过右引号
        }
    }
    return NULL;
}

int json_get_string(const char *json, const char *key, char *value, uint8_t maxlen)
{
    if (!value || maxlen == 0) return -4;

    const char *v = find_value(json, key);
    if (!v) return -1;

    /* 检查是否是字符串（以双引号开头） */
    if (*v != '"') return -3;

    v++;  // 跳过左引号

    int i = 0;
    while (*v && *v != '"' && i < maxlen - 1)
    {
        /* 处理转义符 */
        if (*v == '\\')
        {
            v++;
            if (!*v) break;
            /* 常用的转义 */
            switch (*v) {
                case 'n': value[i++] = '\n'; break;
                case 't': value[i++] = '\t'; break;
                case 'r': value[i++] = '\r'; break;
                case '\\': value[i++] = '\\'; break;
                case '"':  value[i++] = '"'; break;
                default:   value[i++] = *v; break;
            }
            v++;
        }
        else
        {
            value[i++] = *v++;
        }
    }

    if (i >= maxlen - 1) return -2;   // 缓冲不足
    if (*v != '"') return -3;          // 没找到结束引号

    value[i] = '\0';
    return 0;
}

int json_get_int(const char *json, const char *key, int *value)
{
    if (!value) return -1;

    const char *v = find_value(json, key);
    if (!v) return -1;

    /* 尝试解析整数（可能带负号） */
    int sign = 1;
    if (*v == '-') { sign = -1; v++; }
    else if (*v == '+') { v++; }

    if (*v < '0' || *v > '9') return -2;  // 不是数字

    int num = 0;
    while (*v >= '0' && *v <= '9')
    {
        num = num * 10 + (*v - '0');
        v++;
    }

    *value = num * sign;
    return 0;
}
