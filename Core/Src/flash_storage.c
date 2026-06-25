/**
 * @file    flash_storage.c
 * @brief   Flash 参数存储实现 (STM32F407ZGT6 - 终极修复版)
 */

#include "flash_storage.h"
#include "debug_log.h"
#include <string.h>

/* Flash 存储配置 (STM32F407ZGT6 Sector 11) */
#define FLASH_STORAGE_SECTOR      FLASH_SECTOR_11
#define FLASH_STORAGE_ADDR        0x080F0000
#define FLASH_STORAGE_MAGIC_32    0x5A5A1234      // 全新的 32 位魔术字

/* 缓存中的配置（上电时从 Flash 读取） */
static FlashChannelConfig g_configs[FLASH_CHANNEL_COUNT];
static uint8_t g_loaded = 0;

/* ======================== 内部函数 ======================== */

/**
 * @brief 计算校验和 (32位累加和)
 */
static uint32_t calc_checksum_32(const FlashChannelConfig *configs, uint8_t count)
{
    uint32_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += configs[i].closed_pwm;
        sum += configs[i].released_pwm;
    }
    return sum;
}

/**
 * @brief 从 Flash 读取原始配置 (按 32 位 Word 解析)
 */
static int read_from_flash(FlashChannelConfig *configs, uint8_t count)
{
    volatile uint32_t *ptr = (volatile uint32_t *)FLASH_STORAGE_ADDR;
    uint32_t magic = ptr[0];

    // 如果运行的是新代码，这里绝对会严格校验 32 位值 0x5A5A1234
    if (magic != FLASH_STORAGE_MAGIC_32) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        uint32_t val = ptr[1 + i];
        // 高 16 位为 closed_pwm，低 16 位为 released_pwm
        configs[i].closed_pwm   = (uint16_t)(val >> 16);
        configs[i].released_pwm = (uint16_t)(val & 0xFFFF);
    }

    uint32_t stored_cs = ptr[1 + count];
    uint32_t calc_cs = calc_checksum_32(configs, count);
    if (stored_cs != calc_cs) return -1;

    return 0;
}

/**
 * @brief 写入配置到 Flash
 */
static int write_to_flash(const FlashChannelConfig *configs, uint8_t count)
{
    uint32_t addr = FLASH_STORAGE_ADDR;
    uint32_t buf[2 + FLASH_CHANNEL_COUNT];

    buf[0] = FLASH_STORAGE_MAGIC_32;
    for (int i = 0; i < count; i++) {
        buf[1 + i] = ((uint32_t)configs[i].closed_pwm << 16) | configs[i].released_pwm;
    }
    buf[1 + count] = calc_checksum_32(configs, count);

    HAL_FLASH_Unlock();

    /* 核心安全步骤：清除 STM32F4 的所有 Flash 错误标志，防止硬件拒绝擦写 */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* 扇区擦除 */
    FLASH_EraseInitTypeDef erase;
    erase.TypeErase     = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange  = FLASH_VOLTAGE_RANGE_3;
    erase.Sector        = FLASH_STORAGE_SECTOR;
    erase.NbSectors     = 1;

    uint32_t sector_err;
    if (HAL_FLASHEx_Erase(&erase, &sector_err) != HAL_OK) {
        HAL_FLASH_Lock();
        LOG_ERROR(TAG_SYSTEM, "Flash erase failed on Sector 11");
        return -1;
    }

    /* 写入 32-bit Word */
    for (int i = 0; i < (2 + count); i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + (i * 4), buf[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            LOG_ERROR(TAG_SYSTEM, "Flash write failed at 0x%08lX", addr + (i * 4));
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

/* ======================== 公开 API ======================== */

void flash_load_defaults(void)
{
    LOG_INFO(TAG_SYSTEM, "========================================");
    LOG_INFO(TAG_SYSTEM, "===  Verification: 32-BIT CODE RUNNING ===");
    LOG_INFO(TAG_SYSTEM, "========================================");

    if (read_from_flash(g_configs, FLASH_CHANNEL_COUNT) == 0) {
        LOG_INFO(TAG_SYSTEM, "Flash config loaded OK");
        g_loaded = 1;
        for (int i = 0; i < FLASH_CHANNEL_COUNT; i++) {
            LOG_INFO(TAG_SYSTEM, "  CH%d: closed=%d, released=%d",
                i, g_configs[i].closed_pwm, g_configs[i].released_pwm);
        }
        return;
    }

    /* 校验失败，说明是初次运行新代码，自动初始化为默认参数并写入 */
    LOG_INFO(TAG_SYSTEM, "Flash config invalid, initializing to defaults...");
    for (int i = 0; i < FLASH_CHANNEL_COUNT; i++) {
        g_configs[i].closed_pwm   = DEFAULT_CLOSED_PWM;
        g_configs[i].released_pwm = DEFAULT_RELEASED_PWM;
    }

    /* 写入默认值到 Flash */
    if (write_to_flash(g_configs, FLASH_CHANNEL_COUNT) == 0) {
        LOG_INFO(TAG_SYSTEM, "Defaults successfully written to Flash!");
    } else {
        LOG_ERROR(TAG_SYSTEM, "Failed to write defaults to Flash!");
    }
    g_loaded = 1;
}

const FlashChannelConfig* flash_get_config(uint8_t ch)
{
    if (ch >= FLASH_CHANNEL_COUNT) return NULL;
    if (!g_loaded) flash_load_defaults();
    return &g_configs[ch];
}

int flash_set_config(uint8_t ch, uint16_t closed, uint16_t released)
{
    if (ch >= FLASH_CHANNEL_COUNT) return -1;
    if (!g_loaded) flash_load_defaults();

    g_configs[ch].closed_pwm = closed;
    g_configs[ch].released_pwm = released;

    if (write_to_flash(g_configs, FLASH_CHANNEL_COUNT) != 0) {
        return -1;
    }
    LOG_INFO(TAG_SYSTEM, "CH%d config saved: closed=%d, released=%d", ch, closed, released);
    return 0;
}