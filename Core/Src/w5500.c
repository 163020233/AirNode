/**
 * @file    w5500.c
 * @brief   W5500 SPI 底层驱动 + TCP Server 模板
 *
 * 接线: CS→PA4, INT→PB0(EXTI), RST→PC4, SPI1→PA5/6/7
 * 网络: 192.168.101.20 / 255.255.255.0 / GW 192.168.101.1 :13550 (TCP Server)
 */

#include "w5500.h"
#include "spi.h"
#include <string.h>

/* ======================== W5500 寄存器地址 ======================= */

/* 通用寄存器 */
#define MR             0x0000  // 模式寄存器
#define GAR0           0x0001  // 网关
#define SUBR0          0x0005  // 子网掩码
#define SIPR0          0x000F  // 本机 IP
#define SHAR0          0x0009  // MAC
#define SN_MR          0x001C  // Socket n 模式
#define SN_CR          0x001E  // Socket n 命令
#define SN_IR          0x0020  // Socket n 中断
#define Sn_SR          0x0023  // Socket n 状态
#define Sn_PORT0       0x0024  // Socket n 端口号
#define Sn_DIPR0       0x002C  // Socket n 目标 IP (用不到, Server 只想等连接)
#define Sn_DPORT0      0x0034  // Socket n 目标端口
#define Sn_TX_FSR0     0x0028  // Socket n 发送缓冲空闲大小
#define Sn_TX_RD0      0x0022  // Socket n 发送读指针
#define Sn_TX_WR0      0x0024  // Socket n 发送写指针
#define Sn_RX_RSR0     0x0026  // Socket n 接收缓冲已用大小
#define Sn_RX_RD0      0x0028  // Socket n 接收读指针

/* 可变偏移: socket0 基址 = 0x0400, socket1 = 0x0500 ... */
#define SOCKET_BASE(n) (0x0400 + (n) * 0x0100)

/* Socket 模式 */
#define Sn_MR_TCP      0x21
#define Sn_MR_UDP      0x02

/* Socket 命令 */
#define CMD_OPEN       0x01
#define CMD_LISTEN     0x02
#define CMD_DISCON     0x08
#define CMD_CLOSE      0x10
#define CMD_SEND       0x20
#define CMD_RECV       0x40

/* Socket 状态值 */
#define SOCK_CLOSED    0x00
#define SOCK_INIT      0x13
#define SOCK_LISTEN    0x14
#define SOCK_ESTABLISHED 0x17
#define SOCK_CLOSE_WAIT 0x1C

/* ======================== SPI 读写封装 ======================= */

/* W5500 帧头格式:
 *   [0] 字节地址 [1] 字节地址 [2] 高4位:BSB(块选择)  低4位:模式
 *   模式: 0x00=读可变长, 0x40=写可变长, 0x01=读固定长度, 0x41=写固定长度
 */
#define VDM_R 0x00  // 可变长读
#define VDM_W 0x40  // 可变长写

static void w5500_spi_cs(uint8_t level)
{
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void w5500_write_reg(uint16_t reg, uint8_t data)
{
    uint8_t header[3];

    reg &= 0x7FFF;  // 确保地址有效

    header[0] = (reg >> 8) & 0xFF;
    header[1] = reg & 0xFF;
    header[2] = ((reg >> 3) & 0x1F) | VDM_W;  // BSB = reg[15:8]>>3... 简化处理，采用更可靠的方式

    /* 上面的 BSB 计算简化了，这里使用更通用的方式：
     * W5500 的 BSB = 块选择位, 对于 Socket 相关寄存器直接用 Socket 基址方式寻址。
     * 简化版本：我们直接用 16位统一地址模式。
     */
    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, 100);
    HAL_SPI_Transmit(W5500_SPI, &data, 1, 100);
    w5500_spi_cs(1);
}

static uint8_t w5500_read_reg(uint16_t reg)
{
    uint8_t header[3];
    uint8_t data = 0;

    header[0] = (reg >> 8) & 0xFF;
    header[1] = reg & 0xFF;
    header[2] = ((reg >> 8) & 0x0F) | VDM_R;  // BSB = reg[15:12] 读模式

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, 100);
    HAL_SPI_Receive(W5500_SPI, &data, 1, 100);
    w5500_spi_cs(1);
    return data;
}

static void w5500_write_buf(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t header[3];
    header[0] = (reg >> 8) & 0xFF;
    header[1] = reg & 0xFF;
    header[2] = ((reg >> 8) & 0x0F) | VDM_W;

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, 100);
    HAL_SPI_Transmit(W5500_SPI, buf, len, 100);
    w5500_spi_cs(1);
}

static void w5500_read_buf(uint16_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t header[3];
    header[0] = (reg >> 8) & 0xFF;
    header[1] = reg & 0xFF;
    header[2] = ((reg >> 8) & 0x0F) | VDM_R;

    w5500_spi_cs(0);
    HAL_SPI_Transmit(W5500_SPI, header, 3, 100);
    HAL_SPI_Receive(W5500_SPI, buf, len, 100);
    w5500_spi_cs(1);
}

/* ---- 简化版 Socket 寄存器函数 ---- */
#define SOCK_ADDR(n, offset) (SOCKET_BASE(n) + offset)

static uint8_t w5500_sock_read_reg(uint8_t sock, uint16_t offset)
{
    return w5500_read_reg(SOCK_ADDR(sock, offset));
}

static void w5500_sock_write_reg(uint8_t sock, uint16_t offset, uint8_t data)
{
    w5500_write_reg(SOCK_ADDR(sock, offset), data);
}

static void w5500_sock_write_buf(uint8_t sock, uint16_t offset, uint8_t *buf, uint16_t len)
{
    w5500_write_buf(SOCK_ADDR(sock, offset), buf, len);
}

static void w5500_sock_cmd(uint8_t sock, uint8_t cmd)
{
    w5500_sock_write_reg(sock, SN_CR, cmd);
    /* 等待命令执行完成 (CR 自动清零) */
    while (w5500_sock_read_reg(sock, SN_CR) != 0);
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
    w5500_write_reg(MR, 0x80);
    HAL_Delay(5);

    /* 验证 SPI 通信: 读取版本号 (W5500 版本寄存器 0x0039) */
    uint8_t ver = w5500_read_reg(0x0039);
    if (ver != 0x04) {
        /* ver != 0x04 可能 SPI 不通，工程模式中挂起调试用 */
        while (1);  // 串口号可接上调试
    }
}

/**
 * @brief 配置网络参数（IP、子网掩码、网关）
 */
void w5500_network_config(uint8_t *ip, uint8_t *mask, uint8_t *gw)
{
    uint8_t mac[6] = W5500_MAC_ADDR;

    w5500_write_buf(SHAR0, mac, 6);
    w5500_write_buf(GAR0, gw, 4);
    w5500_write_buf(SUBR0, mask, 4);
    w5500_write_buf(SIPR0, ip, 4);
}

/**
 * @brief 打开 TCP Server socket，开始监听
 * @param port 监听端口
 * @return 0=成功, -1=失败
 */
int w5500_tcp_server_open(uint16_t port)
{
    uint8_t sn = 0;  // 只用 socket 0

    /* 关闭可能残留的 socket */
    w5500_sock_cmd(sn, CMD_CLOSE);

    /* 配置为 TCP 模式 */
    w5500_sock_write_reg(sn, SN_MR, Sn_MR_TCP);

    /* 设置端口号 */
    uint8_t port_h = (port >> 8) & 0xFF, port_l = port & 0xFF;
    w5500_sock_write_buf(sn, Sn_PORT0, (uint8_t[]){port_h, port_l}, 2);

    /* OPEN */
    w5500_sock_cmd(sn, CMD_OPEN);

    /* 检查状态 */
    if (w5500_sock_read_reg(sn, Sn_SR) != SOCK_INIT) {
        return -1;  // 打开失败
    }

    /* LISTEN */
    w5500_sock_cmd(sn, CMD_LISTEN);

    return 0;
}

/**
 * @brief 检查 TCP 连接是否已建立
 * @return 1=客户端已连接, 0=未连接
 */
int w5500_tcp_established(void)
{
    uint8_t status = w5500_sock_read_reg(0, Sn_SR);
    return (status == SOCK_ESTABLISHED) ? 1 : 0;
}

/**
 * @brief 接收数据（非阻塞）
 * @param buf    接收缓冲区
 * @param maxlen 缓冲区最大长度
 * @return 接收的字节数, 0=无数据, -1=连接断开
 */
int w5500_tcp_recv(uint8_t *buf, uint16_t maxlen)
{
    uint8_t sn = 0;
    uint8_t status = w5500_sock_read_reg(sn, Sn_SR);

    if (status == SOCK_CLOSE_WAIT || status == SOCK_CLOSED) {
        return -1;  // 对端断开
    }
    if (status != SOCK_ESTABLISHED) {
        return 0;
    }

    /* 读取已接收数据大小 */
    uint16_t rx_size = w5500_sock_read_reg(sn, Sn_RX_RSR0) << 8;
    rx_size |= w5500_sock_read_reg(sn, Sn_RX_RSR0 + 1);

    if (rx_size == 0) return 0;

    uint16_t len = (rx_size < maxlen) ? rx_size : maxlen;

    /* 读取 Sn_RX_RD 指针 */
    uint16_t rx_rd = w5500_sock_read_reg(sn, Sn_RX_RD0) << 8;
    rx_rd |= w5500_sock_read_reg(sn, Sn_RX_RD0 + 1);

    /* 计算接收缓冲区的地址（偏移量） */
    /* W5500 Socket 0 的接收缓冲区在 BSB=0x0018，地址偏移 rx_rd % 0x2000 */
    /* 简化：直接读取接收缓冲区 */
    w5500_read_buf(0x1800 + (rx_rd & 0x1FFF), buf, len);

    /* 更新 Sn_RX_RD */
    rx_rd += len;
    w5500_sock_write_reg(sn, Sn_RX_RD0, (rx_rd >> 8) & 0xFF);
    w5500_sock_write_reg(sn, Sn_RX_RD0 + 1, rx_rd & 0xFF);

    /* 发送 RECV 命令确认接收完成 */
    w5500_sock_cmd(sn, CMD_RECV);

    return len;
}

/**
 * @brief 发送数据
 * @param buf 数据缓存
 * @param len 数据长度
 * @return 实际发送字节数, -1=发送失败
 */
int w5500_tcp_send(uint8_t *buf, uint16_t len)
{
    uint8_t sn = 0;

    if (w5500_sock_read_reg(sn, Sn_SR) != SOCK_ESTABLISHED) {
        return -1;
    }

    /* 检查发送缓冲空闲大小 */
    uint16_t free_size = w5500_sock_read_reg(sn, Sn_TX_FSR0) << 8;
    free_size |= w5500_sock_read_reg(sn, Sn_TX_FSR0 + 1);

    if (free_size < len) return -1;

    /* 读取 Sn_TX_WR 指针 */
    uint16_t tx_wr = w5500_sock_read_reg(sn, Sn_TX_WR0) << 8;
    tx_wr |= w5500_sock_read_reg(sn, Sn_TX_WR0 + 1);

    /* 写入发送缓冲区 */
    w5500_write_buf(0x1000 + (tx_wr & 0x1FFF), buf, len);

    /* 更新 Sn_TX_WR */
    tx_wr += len;
    w5500_sock_write_reg(sn, Sn_TX_WR0, (tx_wr >> 8) & 0xFF);
    w5500_sock_write_reg(sn, Sn_TX_WR0 + 1, tx_wr & 0xFF);

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
