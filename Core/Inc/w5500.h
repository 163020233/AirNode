#ifndef __W5500_H__
#define __W5500_H__

#include "main.h"

/* ========================= 用户配置区 ======================== */

#define W5500_MAC_ADDR      {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}  // MAC 地址
#define W5500_IP_ADDR       {192, 168, 1, 20}                     // 本机 IP
#define W5500_SUBNET_MASK   {255, 255, 255, 0}                      // 子网掩码
#define W5500_GATEWAY_ADDR  {192, 168, 1, 1}                      // 网关
#define W5500_TCP_PORT      13550                                    // 监听端口

#define W5500_SPI           &hspi1
#define W5500_CS_PORT       GPIOA
#define W5500_CS_PIN        GPIO_PIN_4
#define W5500_RST_PORT      GPIOC
#define W5500_RST_PIN       GPIO_PIN_4

/* ============================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- W5500 基础函数 ---- */
void w5500_hard_reset(void);
void w5500_init(void);
void w5500_network_config(uint8_t *ip, uint8_t *mask, uint8_t *gw);

/* ---- TCP Server ---- */
int  w5500_tcp_server_open(uint16_t port);    // 返回 0=成功
int  w5500_tcp_established(void);             // 返回 1=已连接
int  w5500_tcp_recv(uint8_t *buf, uint16_t maxlen);  // 返回接收字节数, -1=无数据
int  w5500_tcp_send(uint8_t *buf, uint16_t len);     // 返回发送字节数
void w5500_tcp_close(void);

#ifdef __cplusplus
}
#endif

#endif
