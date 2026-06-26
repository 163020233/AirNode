/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "w5500.h"
#include "servo.h"
#include "simple_json.h"
#include "debug_log.h"
#include "flash_storage.h"
#include "tim.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NET_BUF_SIZE    512     // 网络接收缓冲区大小
#define SEND_BUF_SIZE   256     // 发送缓冲区大小
#define MSG_DELIMITER   "\r\n\r\n"  // 消息分隔符（Android 端用）
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 手动分配 48KB 堆空间，对齐到 8 字节 */
uint8_t ucHeap[49152] __attribute__((aligned(8)));
/* USER CODE END Variables */

/* Definitions for NetTask */
osThreadId_t NetTaskHandle;
const osThreadAttr_t NetTask_attributes = {
  .name = "NetTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for RouterTask */
osThreadId_t RouterTaskHandle;
const osThreadAttr_t RouterTask_attributes = {
  .name = "RouterTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for ServoTask */
osThreadId_t ServoTaskHandle;
const osThreadAttr_t ServoTask_attributes = {
  .name = "ServoTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for SerialTask */
osThreadId_t SerialTaskHandle;
const osThreadAttr_t SerialTask_attributes = {
  .name = "SerialTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for NetQueue */
osMessageQueueId_t NetQueueHandle;
const osMessageQueueAttr_t NetQueue_attributes = {
  .name = "NetQueue"
};
/* Definitions for ServoQueue */
osMessageQueueId_t ServoQueueHandle;
const osMessageQueueAttr_t ServoQueue_attributes = {
  .name = "ServoQueue"
};

/* USER CODE BEGIN Variables */
/* SendQueue: RouterTask → NetTask 响应队列 */
osMessageQueueId_t SendQueueHandle;
const osMessageQueueAttr_t SendQueue_attributes = {
  .name = "SendQueue"
};
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static int safe_json_get_int(const char *json, const char *key, int *val);
/* USER CODE BEGIN FunctionPrototypes */
static int safe_json_get_int(const char *json, const char *key, int *val);

/**
 * @brief 统一发送应答函数 (同时回复网口 TCP 和 串口 UART)
 */
// static void send_response(const char *msg)
// {
//   // 1. 发送给网络任务 (回传给 Android App)
//   osMessageQueuePut(SendQueueHandle, msg, 0, 0);
//
//   // 2. 同步发送回串口 (回传给 Python 配置工具)
//   HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100);
// }
/* USER CODE END FunctionPrototypes */
/* USER CODE END FunctionPrototypes */

void StartNetTask(void *argument);
void StartRouterTask(void *argument);
void StartServoTask(void *argument);
void StartSerialTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* Create the queue(s) */
  NetQueueHandle = osMessageQueueNew (16, 128, &NetQueue_attributes);
  ServoQueueHandle = osMessageQueueNew (8, 16, &ServoQueue_attributes);
  SendQueueHandle = osMessageQueueNew (8, SEND_BUF_SIZE, &SendQueue_attributes);

  /* Create the thread(s) */
  NetTaskHandle = osThreadNew(StartNetTask, NULL, &NetTask_attributes);
  if (NetTaskHandle == NULL) {
    LOG_INFO(TAG_RTOS, "ERROR: NetTask creation failed!");
  }

  RouterTaskHandle = osThreadNew(StartRouterTask, NULL, &RouterTask_attributes);
  if (RouterTaskHandle == NULL) {
    LOG_INFO(TAG_RTOS, "ERROR: RouterTask creation failed!");
  }

  ServoTaskHandle = osThreadNew(StartServoTask, NULL, &ServoTask_attributes);
  if (ServoTaskHandle == NULL) {
    LOG_INFO(TAG_RTOS, "ERROR: ServoTask creation failed! (Out of memory)");
  }

  SerialTaskHandle = osThreadNew(StartSerialTask, NULL, &SerialTask_attributes);
  if (SerialTaskHandle == NULL) {
    LOG_INFO(TAG_RTOS, "ERROR: SerialTask creation failed!");
  }
}

/* ======================== 内部安全解析函数 ======================== */
/**
 * @brief 安全提取 JSON 字符串中的整型数值 (完全独立，避开 simple_json 库缺陷)
 */
static int safe_json_get_int(const char *json, const char *key, int *val)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    // 精确检索带有双引号包裹的键名，例如 "closed":
    char *p = strstr(json, pattern);
    if (!p) {
        // 兼容没有引号的格式
        snprintf(pattern, sizeof(pattern), "%s:", key);
        p = strstr(json, pattern);
    }

    if (p) {
        p += strlen(pattern);
        // 跳过可能存在的空格或双引号
        while (*p == ' ' || *p == '"' || *p == ':') {
            p++;
        }
        *val = atoi(p);
        return 0; // 成功匹配并解析
    }
    return -1; // 未找到该键名
}

/* USER CODE BEGIN Header_StartNetTask */
void StartNetTask(void *argument)
{
  /* USER CODE BEGIN StartNetTask */
  uint8_t ip[4]   = W5500_IP_ADDR;
  uint8_t mask[4] = W5500_SUBNET_MASK;
  uint8_t gw[4]   = W5500_GATEWAY_ADDR;

  uint8_t net_buf[NET_BUF_SIZE];
  uint8_t send_buf[SEND_BUF_SIZE];

  w5500_init();
  w5500_network_config(ip, mask, gw);

  LOG_INFO(TAG_RTOS, "W5500 initialized");
  LOG_INFO(TAG_RTOS, "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  LOG_INFO(TAG_RTOS, "Mask: %d.%d.%d.%d", mask[0], mask[1], mask[2], mask[3]);
  LOG_INFO(TAG_RTOS, "GW: %d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);
  uint8_t mac[6] = W5500_MAC_ADDR;
  LOG_INFO(TAG_RTOS, "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  LOG_INFO(TAG_RTOS, "Port: %d", W5500_TCP_PORT);

  while (w5500_tcp_server_open(W5500_TCP_PORT) != 0)
  {
    osDelay(1000);
  }

  for(;;)
  {
    int len = w5500_tcp_recv(net_buf, NET_BUF_SIZE);

    if (len > 0)
    {
      if (len < NET_BUF_SIZE) net_buf[len] = '\0';
      else net_buf[NET_BUF_SIZE - 1] = '\0';
      osMessageQueuePut(NetQueueHandle, net_buf, 0, 0);
    }
    else if (len == -1)
    {
      // LOG_DEBUG(TAG_RTOS, "Client disconnected, re-listening...");
      w5500_tcp_close();
      w5500_tcp_server_open(W5500_TCP_PORT);
      osDelay(100);
    }

    if (w5500_tcp_established())
    {
      if (osMessageQueueGet(SendQueueHandle, send_buf, NULL, 0) == osOK)
      {
        w5500_tcp_send(send_buf, strlen((const char *)send_buf));
      }
    }
    else
    {
      uint8_t trash[SEND_BUF_SIZE];
      while (osMessageQueueGet(SendQueueHandle, trash, NULL, 0) == osOK);
    }

    osDelay(20);
  }
  /* USER CODE END StartNetTask */
}

/* USER CODE BEGIN Header_StartRouterTask */
void StartRouterTask(void *argument)
{
  /* USER CODE BEGIN StartRouterTask */
  uint8_t net_buf[NET_BUF_SIZE];
  char command[32];
  char cseq[16];
  char resp[SEND_BUF_SIZE];
  int channel;
  int angle;

  for(;;)
  {
    osMessageQueueGet(NetQueueHandle, net_buf, NULL, osWaitForever);
    net_buf[NET_BUF_SIZE - 1] = '\0';

    // LOG_INFO(TAG_RTOS, "RAW: %s", (const char *)net_buf);

    // 核心桥接选择：如果数据包以 '{' 开头，说明是安卓发来的网口 JSON 报文
    if (net_buf[0] == '{')
    {
      cseq[0] = '\0';
      json_get_string((const char *)net_buf, "cseq", cseq, sizeof(cseq));
      command[0] = '\0';
      if (json_get_string((const char *)net_buf, "command", command, sizeof(command)) != 0)
      {
        continue;
      }

      /* 1. 处理安卓 JSON 触发命令 */
      if (strcmp(command, "servo_trigger") == 0)
      {
        int ch = 0;
        char action[16] = "close";
        safe_json_get_int((const char *)net_buf, "ch", &ch);
        json_get_string((const char *)net_buf, "action", action, sizeof(action));

        if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
          const FlashChannelConfig *cfg = flash_get_config((uint8_t)ch);
          int target_pwm = (strcmp(action, "release") == 0) ? cfg->released_pwm : cfg->closed_pwm;
          servo_set_pwm_us((uint8_t)ch, (uint16_t)target_pwm);
          // snprintf(resp, sizeof(resp), "{\"command\":\"servo_trigger\",\"code\":\"200\",\"ch\":%d,\"action\":\"%s\",\"msg\":\"ok\"}" MSG_DELIMITER, ch, action);
          osMessageQueuePut(SendQueueHandle, resp, 0, 0); // 仅回发给网口
        }
      }
      /* 2. 处理安卓 传统角度控制 */
      else if (strcmp(command, "servo_set") == 0)
      {
        channel = 0; angle = 90;
        safe_json_get_int((const char *)net_buf, "ch", &channel);
        safe_json_get_int((const char *)net_buf, "angle", &angle);
        if (angle < 0) angle = 0;
        if (angle > 180) angle = 180;
        osMessageQueuePut(ServoQueueHandle, &angle, 0, 0);
        // snprintf(resp, sizeof(resp), "{\"command\":\"servo_set\",\"code\":\"200\",\"cseq\":\"%s\",\"msg\":\"ok\"}" MSG_DELIMITER, cseq);
        osMessageQueuePut(SendQueueHandle, resp, 0, 0);
      }
    }
    // 如果不以 '{' 开头，说明是 PC 上位机通过串口发来的极简 CSV 文本包
    else
    {
      char cmd_type = net_buf[0];
      int ch = 0;
      int val1 = 0;
      int val2 = 0;

      /* 1. 写入配置: W,ch,closed,released\r\n\r\n */
      if (cmd_type == 'W')
      {
        if (sscanf((const char*)net_buf, "W,%d,%d,%d", &ch, &val1, &val2) == 3)
        {
          if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
            if (flash_set_config((uint8_t)ch, (uint16_t)val1, (uint16_t)val2) == 0) {
              snprintf(resp, sizeof(resp), "W,%d,200\r\n\r\n", ch); // 成功响应
            } else {
              snprintf(resp, sizeof(resp), "W,%d,500\r\n\r\n", ch); // 写入失败
            }
          }
          HAL_UART_Transmit(&huart1, (uint8_t*)resp, strlen(resp), 100); // 串口直回
        }
      }
      /* 2. 读取配置: R,ch\r\n\r\n */
      else if (cmd_type == 'R')
      {
        if (sscanf((const char*)net_buf, "R,%d", &ch) == 1)
        {
          if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
            const FlashChannelConfig *cfg = flash_get_config((uint8_t)ch);
            snprintf(resp, sizeof(resp), "R,%d,200,%d,%d\r\n\r\n", ch, cfg->closed_pwm, cfg->released_pwm);
          } else {
            snprintf(resp, sizeof(resp), "R,%d,400,0,0\r\n\r\n", ch);
          }
          HAL_UART_Transmit(&huart1, (uint8_t*)resp, strlen(resp), 100);
        }
      }
      /* 3. 触发动作: T,ch,action_val\r\n\r\n  (action_val: 0为闭合, 1为投掷) */
      else if (cmd_type == 'T')
      {
        int action_val = 0;
        if (sscanf((const char*)net_buf, "T,%d,%d", &ch, &action_val) == 2)
        {
          if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
            const FlashChannelConfig *cfg = flash_get_config((uint8_t)ch);
            int target_pwm = (action_val == 1) ? cfg->released_pwm : cfg->closed_pwm;
            servo_set_pwm_us((uint8_t)ch, (uint16_t)target_pwm);
            snprintf(resp, sizeof(resp), "T,%d,200\r\n\r\n", ch);
          } else {
            snprintf(resp, sizeof(resp), "T,%d,400\r\n\r\n", ch);
          }
          HAL_UART_Transmit(&huart1, (uint8_t*)resp, strlen(resp), 100);
        }
      }
    }
  }
  /* USER CODE END StartRouterTask */
}
/* USER CODE BEGIN Header_StartServoTask */
void StartServoTask(void *argument)
{
  /* USER CODE BEGIN StartServoTask */
  int angle;
  // LOG_INFO(TAG_SERVO, "ServoTask started");
  servo_init();
  for(;;)
  {
    // LOG_INFO(TAG_SERVO, "Waiting for angle...");
    osMessageQueueGet(ServoQueueHandle, &angle, NULL, osWaitForever);
    servo_set_angle(angle);
    // LOG_INFO(TAG_SERVO, "Angle set done: %d", angle);
  }
  /* USER CODE END StartServoTask */
}

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN Application */
void StartSerialTask(void *argument)
{
  /* USER CODE BEGIN StartSerialTask */
  static uint8_t uart_buf[512];
  static uint16_t uart_len = 0;

  volatile uint32_t tmpreg = 0x00U;
  tmpreg = huart1.Instance->SR;
  tmpreg = huart1.Instance->DR;
  (void)tmpreg;

  for (;;)
  {
    uint8_t ch;
    if (HAL_UART_Receive(&huart1, &ch, 1, 5) == HAL_OK)
    {
      /* 核心首字节过滤：
            - 如果是 Android 网口 JSON，首字是 '{'
            - 如果是 PC 串口 CSV，首字是 'W'、'R' 或 'T'
            - 丢弃除此之外所有的前导电气噪声、零字节 \x00 等乱码 */
      if (uart_len == 0)
      {
        if (ch != '{' && ch != 'W' && ch != 'R' && ch != 'T')
        {
          continue;
        }
      }

      if (uart_len < sizeof(uart_buf) - 1)
      {
        uart_buf[uart_len++] = ch;
        if (uart_len >= 4 &&
            uart_buf[uart_len - 4] == '\r' &&
            uart_buf[uart_len - 3] == '\n' &&
            uart_buf[uart_len - 2] == '\r' &&
            uart_buf[uart_len - 1] == '\n')
        {
          uart_buf[uart_len] = '\0';
          osMessageQueuePut(NetQueueHandle, uart_buf, 0, 0);
          uart_len = 0;
        }
      }
      else
      {
        uart_len = 0;
      }
    }
    else
    {
      uart_len = 0;

      // 1. 获取硬件错误状态
      uint32_t err = HAL_UART_GetError(&huart1);

      // 2. 物理读取清除 ORE/FE 错误
      volatile uint32_t err_clear = huart1.Instance->SR;
      err_clear = huart1.Instance->DR;
      (void)err_clear;

      // 3. 根据错误类型实施差异化调度
      if (err == HAL_UART_ERROR_NONE)
      {
        /* 如果是正常的空闲超时（无错误），安全延时 5ms 释放 CPU */
        osDelay(5);
      }
      else
      {
        /* 如果是 ORE/FE 硬件错误，说明后续字符正在涌入，绝对不能延时！ */
        /* 延迟 0ms（让出当前时间片后立刻返回），以最高速度抢收后续字符，彻底打碎溢出死锁！ */
        osDelay(0);
      }
    }
  }
  /* USER CODE END StartSerialTask */
}
/* USER CODE END Application */