/**
 * @file    w5500.c
 * @brief   W5500 SPI 底层驱动 + TCP Server 模板 (修正版)
 */

#include "w5500.h"
#include "spi.h"
#include "usart.h"
#include "debug_log.h"
#include <string.h>
#include <stdio.h>

/* ======================== W5500 BSB (区域块选择) ======================= */
#define W5500_BSB_COMMON          0x00  // 通用寄存器块
#define W5500_BSB_S_REG(n)        (uint8_t)(0x01 + ((n) << 2)) // Socket n 寄存器块
#define W5500_BSB_S_TX(n)         (uint8_t)(0x02 + ((n) << 2)) // Socket n 发送数据缓冲区
#define W5500_BSB_S_RX(n)         (uint8_t)(0x03 + ((n) << 2)) // Socket n 接收数据缓冲区

/* ======================== W5500 寄存器偏移 (Offset) ======================= */

/* 1. 通用寄存器偏移 (Block 0x00) */
#define MR             0x0000  // 模式寄存器
#define GAR0           0x0001  // 网关 (4字节)
#define SUBR0          0x0005  // 子网掩码 (4字节)
#define SHAR0          0x0009  // MAC 地址 (6字节)
#define SIPR0          0x000F  // 本机 IP (4字节)
#define VERSIONR       0x0039  // 芯片版本寄存器 (1字节)

/* 2. Socket 寄存器偏移 (在各 Socket 寄存器块中的相对偏移) */
#define W5500_Sn_MR          0x0000  // 模式
#define W5500_Sn_CR          0x0001  // 命令
#define W5500_Sn_IR          0x0002  // 中断
#define W5500_Sn_SR          0x0003  // 状态
#define W5500_Sn_PORT0       0x0004  // 端口号 (2字节)
#define W5500_Sn_DIPR0       0x000C  // 目标 IP (4字节)
#define W5500_Sn_DPORT0      0x0010  // 目标端口 (2字节)
#define W5500_Sn_TX_FSR0     0x0020  // 发送缓冲空闲大小 (2字节)
#define W5500_Sn_TX_RD0      0x0022  // 发送读指针 (2字节)
#define W5500_Sn_TX_WR0      0x0024  // 发送写指针 (2字节)
#define W5500_Sn_RX_RSR0     0x0026  // 接收缓冲已用大小 (2字节)
#define W5500_Sn_RX_RD0      0x0028  // 接收读指针 (2字节)
#define W5500_Sn_RX_WR0      0x002A  // 接收写指针 (2字节)

/* Socket 命令与状态 */
#define CMD_OPEN       0x01
#define CMD_LISTEN     0x02
#define CMD_DISCON     0x08
#define CMD_CLOSE      0x10
#define CMD_SEND       0x20
#define CMD_RECV       0x40

#define SOCK_CLOSED      0x00
#define SOCK_INIT        0x13
#define SOCK_LISTEN      0x14
#define SOCK_ESTABLISHED 0x17
#define SOCK_CLOSE_WAIT  0x1C

/* ======================== SPI 读写基础封装 ======================= */

static void w5500_spi_cs(uint8_t level)
{
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief 向指定 BSB 块和偏移地址写入 1 字节
 */
static void w5500_write_reg(uint8_t bsb, uint16_t offset, uint8_t data)
{
    uint8_t header[3];
    header[0] = (uint8_t)(offset >> 8);
    header[1] = (uint8_t)(offset & 0xFF);
    // Bit[2]=1 表示写操作, Bit[1:0]=00 表示可变长度模式
    header[2] = (uint8_t)((bsb << 3) | 0x04);

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, HAL_MAX_DELAY);
    HAL_SPI_Transmit(W5500_SPI, &data, 1, HAL_MAX_DELAY);
    w5500_spi_cs(1);
}

/**
 * @brief 从指定 BSB 块和偏移地址读取 1 字节
 */
static uint8_t w5500_read_reg(uint8_t bsb, uint16_t offset)
{
    uint8_t header[3];
    uint8_t data = 0;
    header[0] = (uint8_t)(offset >> 8);
    header[1] = (uint8_t)(offset & 0xFF);
    // Bit[2]=0 表示读操作, Bit[1:0]=00 表示可变长度模式
    header[2] = (uint8_t)((bsb << 3) | 0x00);

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, HAL_MAX_DELAY);
    HAL_SPI_Receive(W5500_SPI, &data, 1, HAL_MAX_DELAY);
    w5500_spi_cs(1);
    return data;
}

/**
 * @brief 向指定 BSB 块和偏移地址连续写入数据缓冲区
 */
static void w5500_write_buf(uint8_t bsb, uint16_t offset, uint8_t *buf, uint16_t len)
{
    if (len == 0) return;
    uint8_t header[3];
    header[0] = (uint8_t)(offset >> 8);
    header[1] = (uint8_t)(offset & 0xFF);
    header[2] = (uint8_t)((bsb << 3) | 0x04);

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, HAL_MAX_DELAY);
    HAL_SPI_Transmit(W5500_SPI, buf, len, HAL_MAX_DELAY);
    w5500_spi_cs(1);
}

/**
 * @brief 从指定 BSB 块和偏移地址连续读取数据缓冲区
 */
static void w5500_read_buf(uint8_t bsb, uint16_t offset, uint8_t *buf, uint16_t len)
{
    if (len == 0) return;
    uint8_t header[3];
    header[0] = (uint8_t)(offset >> 8);
    header[1] = (uint8_t)(offset & 0xFF);
    header[2] = (uint8_t)((bsb << 3) | 0x00);

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, HAL_MAX_DELAY);
    HAL_SPI_Receive(W5500_SPI, buf, len, HAL_MAX_DELAY);
    w5500_spi_cs(1);
}

/* ---- Socket 级别读写辅助函数 ---- */

static uint8_t w5500_sock_read_reg(uint8_t sock, uint16_t offset)
{
    return w5500_read_reg(W5500_BSB_S_REG(sock), offset);
}

static void w5500_sock_write_reg(uint8_t sock, uint16_t offset, uint8_t data)
{
    w5500_write_reg(W5500_BSB_S_REG(sock), offset, data);
}

//未来接口设置stm32IP地址
__attribute__((unused)) static void w5500_sock_write_buf(uint8_t sock, uint16_t offset, uint8_t *buf, uint16_t len)
{
    w5500_write_buf(W5500_BSB_S_REG(sock), offset, buf, len);
}

static void w5500_sock_cmd(uint8_t sock, uint8_t cmd)
{
    w5500_sock_write_reg(sock, W5500_Sn_CR, cmd);
    /* 等待命令执行完成 */
    while (w5500_sock_read_reg(sock, W5500_Sn_CR) != 0);
}

/* ======================== 公开 API ======================= */

/**
 * @brief 硬件复位 W5500
 */
void w5500_hard_reset(void)
{
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50);
}

/**
 * @brief W5500 初始化 SPI 接口 + 软复位
 */
void w5500_init(void)
{
    /* 硬件复位 */
    w5500_hard_reset();

    /* 软复位 */
    w5500_write_reg(W5500_BSB_COMMON, MR, 0x80);
    HAL_Delay(10);

    /* 验证 SPI 通信: 读取版本寄存器 (VERSIONR = 0x0039) */
    uint8_t ver = w5500_read_reg(W5500_BSB_COMMON, VERSIONR);
    LOG_INFO(TAG_W5500, "W5500 version: 0x%02X (expected 0x04)", ver);
    if (ver != 0x04) {
        LOG_INFO(TAG_W5500, "SPI communication FAILED!");
        while (1);
    }
    LOG_INFO(TAG_W5500, "W5500 SPI OK");
}

/**
 * @brief 配置网络参数（IP、子网掩码、网关）
 */
void w5500_network_config(uint8_t *ip, uint8_t *mask, uint8_t *gw)
{
    uint8_t mac[6] = W5500_MAC_ADDR;
    uint8_t buf[4];

    /* 配置物理 MAC、网关、掩码 */
    w5500_write_buf(W5500_BSB_COMMON, SHAR0, mac, 6);
    w5500_write_buf(W5500_BSB_COMMON, GAR0, gw, 4);
    w5500_write_buf(W5500_BSB_COMMON, SUBR0, mask, 4);

    HAL_Delay(1);

    /* 配置 IP */
    w5500_write_buf(W5500_BSB_COMMON, SIPR0, ip, 4);
    HAL_Delay(5);

    /* 回读校验以确认写入成功 */
    w5500_read_buf(W5500_BSB_COMMON, SIPR0, buf, 4);
    if (buf[0] == ip[0] && buf[1] == ip[1] && buf[2] == ip[2] && buf[3] == ip[3]) {
        LOG_INFO(TAG_W5500, "IP verify OK (%d.%d.%d.%d)", buf[0], buf[1], buf[2], buf[3]);
    } else {
        LOG_INFO(TAG_W5500, "IP MISMATCH! wrote %d.%d.%d.%d but read %d.%d.%d.%d",
            ip[0], ip[1], ip[2], ip[3], buf[0], buf[1], buf[2], buf[3]);
    }
}

/**
 * @brief 打开 TCP Server socket，开始监听
 */
int w5500_tcp_server_open(uint16_t port)
{
    uint8_t sn = 0;  // 示例中默认只用 socket 0

    /* 关闭可能残留的 socket */
    w5500_sock_cmd(sn, CMD_CLOSE);

    /* 配置为 TCP 模式 */
    w5500_sock_write_reg(sn, W5500_Sn_MR, 0x01); // 0x01 = TCP 模式

    /* 设置端口号 */
    uint8_t port_h = (uint8_t)(port >> 8);
    uint8_t port_l = (uint8_t)(port & 0xFF);
    w5500_sock_write_reg(sn, W5500_Sn_PORT0, port_h);
    w5500_sock_write_reg(sn, W5500_Sn_PORT0 + 1, port_l);

    /* OPEN */
    w5500_sock_cmd(sn, CMD_OPEN);

    /* 检查状态是否成功切换到 INIT */
    if (w5500_sock_read_reg(sn, W5500_Sn_SR) != SOCK_INIT) {
        return -1;
    }

    /* LISTEN */
    w5500_sock_cmd(sn, CMD_LISTEN);

    /* 确认状态进入 LISTEN */
    if (w5500_sock_read_reg(sn, W5500_Sn_SR) != SOCK_LISTEN) {
        return -1;
    }

    LOG_INFO(TAG_W5500, "TCP Server listening on port %d...", port);
    return 0;
}

/**
 * @brief 检查 TCP 连接是否已建立
 */
int w5500_tcp_established(void)
{
    uint8_t status = w5500_sock_read_reg(0, W5500_Sn_SR);
    return (status == SOCK_ESTABLISHED) ? 1 : 0;
}

/**
 * @brief 接收数据（非阻塞）
 */
/**
 * @brief 接收数据（非阻塞）
 */
int w5500_tcp_recv(uint8_t *buf, uint16_t maxlen)
{
    uint8_t sn = 0;
    uint8_t status = w5500_sock_read_reg(sn, W5500_Sn_SR);

    // 1. 检查连接是否断开
    if (status == SOCK_CLOSE_WAIT || status == SOCK_CLOSED) {
        LOG_INFO(TAG_W5500, "Disconnect event detected. Socket state: 0x%02X", status);
        return -1;
    }

    // 未建立连接时直接返回（不打印，防止刷屏）
    if (status != SOCK_ESTABLISHED) {
        return 0;
    }

    /* 读取已接收数据大小 */
    uint16_t rx_size = w5500_sock_read_reg(sn, W5500_Sn_RX_RSR0) << 8;
    rx_size |= w5500_sock_read_reg(sn, W5500_Sn_RX_RSR0 + 1);

    // 缓冲区无数据时直接返回（不打印，防止刷屏）
    if (rx_size == 0) return 0;

    // 2. 发现有数据，开始打印调试信息
    uint16_t len = (rx_size < maxlen) ? rx_size : maxlen;
    LOG_INFO(TAG_W5500, "Data arrived! Total bytes in queue: %d, reading: %d bytes (limit: %d)",
        rx_size, len, maxlen);

    /* 读取当前接收读指针 */
    uint16_t rx_rd = w5500_sock_read_reg(sn, W5500_Sn_RX_RD0) << 8;
    rx_rd |= w5500_sock_read_reg(sn, W5500_Sn_RX_RD0 + 1);

    uint16_t old_rd = rx_rd; // 备份旧指针用于日志打印

    /* 直接读取接收数据块 */
    w5500_read_buf(W5500_BSB_S_RX(sn), rx_rd, buf, len);

    /* 更新 RX 读指针值 */
    rx_rd += len;
    w5500_sock_write_reg(sn, W5500_Sn_RX_RD0, (uint8_t)(rx_rd >> 8));
    w5500_sock_write_reg(sn, W5500_Sn_RX_RD0 + 1, (uint8_t)(rx_rd & 0xFF));

    /* 执行 RECV 命令使更新生效 */
    w5500_sock_cmd(sn, CMD_RECV);

    // 3. 打印读指针的变化情况
    LOG_INFO(TAG_W5500, "RX pointer shifted: 0x%04X -> 0x%04X (CMD_RECV confirmed)", old_rd, rx_rd);

    return len;
}
/**
 * @brief 发送数据
 */
int w5500_tcp_send(uint8_t *buf, uint16_t len)
{
    uint8_t sn = 0;

    if (w5500_sock_read_reg(sn, W5500_Sn_SR) != SOCK_ESTABLISHED) {
        return -1;
    }

    /* 检查发送缓冲空闲大小 */
    uint16_t free_size = w5500_sock_read_reg(sn, W5500_Sn_TX_FSR0) << 8;
    free_size |= w5500_sock_read_reg(sn, W5500_Sn_TX_FSR0 + 1);

    if (free_size < len) return -1;

    /* 读取 Sn_TX_WR 指针 */
    uint16_t tx_wr = w5500_sock_read_reg(sn, W5500_Sn_TX_WR0) << 8;
    tx_wr |= w5500_sock_read_reg(sn, W5500_Sn_TX_WR0 + 1);

    /* 写入发送缓冲区 */
    w5500_write_buf(W5500_BSB_S_TX(sn), tx_wr, buf, len);

    /* 更新 Sn_TX_WR */
    tx_wr += len;
    w5500_sock_write_reg(sn, W5500_Sn_TX_WR0, (uint8_t)(tx_wr >> 8));
    w5500_sock_write_reg(sn, W5500_Sn_TX_WR0 + 1, (uint8_t)(tx_wr & 0xFF));

    /* SEND 命令 */
    w5500_sock_cmd(sn, CMD_SEND);

    return len;
}

/**
 * @brief 断开并关闭 socket
 */
void w5500_tcp_close(void)
{
    w5500_sock_cmd(0, CMD_DISCON);
    HAL_Delay(10);
    w5500_sock_cmd(0, CMD_CLOSE);
}