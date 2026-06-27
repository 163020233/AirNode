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
  .stack_size = 256* 4,
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
static void send_response(const char *msg)
{
  // 1. 发送给网络任务 (回传给 Android App)
  osMessageQueuePut(SendQueueHandle, msg, 0, 0);

  // 2. 同步发送回串口 (回传给 Python 配置工具)
  HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100);
}
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
      LOG_DEBUG(TAG_RTOS, "Client disconnected, re-listening...");
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

    LOG_INFO(TAG_RTOS, "RAW: %s", (const char *)net_buf);

    cseq[0] = '\0';
    json_get_string((const char *)net_buf, "cseq", cseq, sizeof(cseq));

    command[0] = '\0';
    if (json_get_string((const char *)net_buf, "command", command, sizeof(command)) != 0)
    {
      LOG_DEBUG(TAG_RTOS, "No command found, skip");
      continue;
    }

    LOG_INFO(TAG_RTOS, "Command: %s", command);

    /* ======================== 指令路由 ======================== */

    /* ---- Flash 配置读取 ---- */
    if (strcmp(command, "config_read") == 0)
    {
      int ch = 0;
      safe_json_get_int((const char *)net_buf, "ch", &ch);

      if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
        const FlashChannelConfig *cfg = flash_get_config((uint8_t)ch);
        LOG_INFO(TAG_SYSTEM, "config_read: ch=%d", ch);
        snprintf(resp, sizeof(resp),
                 "{\"command\":\"config_read_response\",\"code\":\"200\",\"cseq\":\"%s\","
                 "\"ch\":%d,\"closed\":%d,\"released\":%d}"
                 MSG_DELIMITER,
                 cseq, ch, cfg->closed_pwm, cfg->released_pwm);
      } else {
        snprintf(resp, sizeof(resp),
                 "{\"command\":\"config_read_response\",\"code\":\"400\",\"cseq\":\"%s\",\"msg\":\"bad ch\"}"
                 MSG_DELIMITER, cseq);
      }
      send_response(resp); // 核心修复：使用统一应答
    }
    /* ---- Flash 配置写入 ---- */
    else if (strcmp(command, "config_write") == 0)
    {
      int ch = 0;
      int closed = 0;
      int released = 0;

      safe_json_get_int((const char *)net_buf, "ch", &ch);
      safe_json_get_int((const char *)net_buf, "closed", &closed);
      safe_json_get_int((const char *)net_buf, "released", &released);

      LOG_INFO(TAG_SYSTEM, "Parsed Config Write - CH%d: closed=%d, released=%d", ch, closed, released);

      if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
        if (flash_set_config((uint8_t)ch, (uint16_t)closed, (uint16_t)released) == 0) {
          snprintf(resp, sizeof(resp),
                   "{\"command\":\"config_write\",\"code\":\"200\",\"ch\":%d,\"cseq\":\"%s\",\"msg\":\"saved\"}"
                   MSG_DELIMITER, ch, cseq);
        } else {
          snprintf(resp, sizeof(resp),
                   "{\"command\":\"config_write\",\"code\":\"500\",\"ch\":%d,\"cseq\":\"%s\",\"msg\":\"flash error\"}"
                   MSG_DELIMITER, ch, cseq);
        }
      } else {
        snprintf(resp, sizeof(resp),
                 "{\"command\":\"config_write\",\"code\":\"400\",\"ch\":%d,\"cseq\":\"%s\",\"msg\":\"bad ch\"}"
                 MSG_DELIMITER, ch, cseq);
      }
      send_response(resp); // 核心修复：使用统一应答
    }
    /* ---- 抛投触发 ---- */
    else if (strcmp(command, "servo_trigger") == 0)
    {
      int ch = 0;
      char action[16] = "close";
      safe_json_get_int((const char *)net_buf, "ch", &ch);
      json_get_string((const char *)net_buf, "action", action, sizeof(action));

      if (ch >= 0 && ch < FLASH_CHANNEL_COUNT) {
        const FlashChannelConfig *cfg = flash_get_config((uint8_t)ch);
        int target_pwm = cfg->closed_pwm;
        if (strcmp(action, "release") == 0) target_pwm = cfg->released_pwm;

        LOG_INFO(TAG_SYSTEM, "trigger: ch=%d, action=%s, pwm=%d", ch, action, target_pwm);

        /* 直接设置 PWM，使用统一驱动接口 */
        servo_set_pwm_us((uint8_t)ch, (uint16_t)target_pwm);

        snprintf(resp, sizeof(resp),
                 "{\"command\":\"servo_trigger\",\"code\":\"200\",\"ch\":%d,\"action\":\"%s\",\"cseq\":\"%s\",\"msg\":\"ok\"}"
                 MSG_DELIMITER, ch, action, cseq);
      } else {
        snprintf(resp, sizeof(resp),
                 "{\"command\":\"servo_trigger\",\"code\":\"400\",\"ch\":%d,\"cseq\":\"%s\",\"msg\":\"bad ch\"}"
                 MSG_DELIMITER, ch, cseq);
      }
      send_response(resp); // 核心修复：使用统一应答
    }
    else if (strcmp(command, "servo_set") == 0)
    {
      channel = 0; angle = 90;
      safe_json_get_int((const char *)net_buf, "ch", &channel);
      safe_json_get_int((const char *)net_buf, "angle", &angle);
      if (angle < 0) angle = 0;
      if (angle > 180) angle = 180;
      LOG_INFO(TAG_RTOS, "servo_set: ch=%d, angle=%d", channel, angle);
      osMessageQueuePut(ServoQueueHandle, &angle, 0, 0);

      LOG_DEBUG(TAG_RTOS, "Response sent");
      snprintf(resp, sizeof(resp),
               "{\"command\":\"servo_set\",\"code\":\"200\",\"cseq\":\"%s\",\"msg\":\"ok\"}"
               MSG_DELIMITER, cseq);
      send_response(resp); // 核心修复：使用统一应答
    }
    else if (strcmp(command, "servo_get") == 0)
    {
      LOG_INFO(TAG_RTOS, "servo_get");
      snprintf(resp, sizeof(resp),
               "{\"command\":\"servo_get\",\"code\":\"200\",\"cseq\":\"%s\",\"angle\":90}"
               MSG_DELIMITER, cseq);
      send_response(resp); // 核心修复：使用统一应答
    }
    else
    {
      LOG_INFO(TAG_RTOS, "Unknown: %s", command);
      snprintf(resp, sizeof(resp),
               "{\"command\":\"%s\",\"code\":\"400\",\"cseq\":\"%s\",\"msg\":\"unknown\"}"
               MSG_DELIMITER, command, cseq);
      send_response(resp); // 核心修复：使用统一应答
    }
  }
  /* USER CODE END StartRouterTask */
}
/* USER CODE BEGIN Header_StartServoTask */
void StartServoTask(void *argument)
{
  /* USER CODE BEGIN StartServoTask */
  int angle;
  LOG_INFO(TAG_SERVO, "ServoTask started");
  servo_init();
  for(;;)
  {
    LOG_INFO(TAG_SERVO, "Waiting for angle...");
    osMessageQueueGet(ServoQueueHandle, &angle, NULL, osWaitForever);
    servo_set_angle(angle);
    LOG_INFO(TAG_SERVO, "Angle set done: %d", angle);
  }
  /* USER CODE END StartServoTask */
}

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN Application */

/* 串口接收缓冲区（中断+主循环共享）*/
static uint8_t s_uart_buf[512];
static uint16_t s_uart_len = 0;

/* 串口中断接收单字节缓存 */
uint8_t g_rx_byte;

/**
 * @brief 串口中断接收回调
 *        每收到1字节存入 s_uart_buf，检测到 \r\n\r\n 结束符后入 NetQueue
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    /* 注意！非DMA模式时 rx_byte 存的是当前收到的字节 */
    /* 实际 rx_byte 是 StartSerialTask 中定义的局部 static，但在中断里不可见 */
    /* 所以直接从 huart 实例获取最后接收的字节 */
    extern uint8_t g_rx_byte;
    uint8_t ch = g_rx_byte;

    if (s_uart_len < sizeof(s_uart_buf) - 1)
    {
      s_uart_buf[s_uart_len++] = ch;

      /* 检测 \r\n\r\n 分隔符 */
      if (s_uart_len >= 4 &&
          s_uart_buf[s_uart_len - 4] == '\r' &&
          s_uart_buf[s_uart_len - 3] == '\n' &&
          s_uart_buf[s_uart_len - 2] == '\r' &&
          s_uart_buf[s_uart_len - 1] == '\n')
      {
        s_uart_buf[s_uart_len] = '\0';
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        osMessageQueuePut(NetQueueHandle, s_uart_buf, 0, 0);
        s_uart_len = 0;
      }
    }
    else
    {
      s_uart_len = 0;
    }

    /* 重新开启下一字节中断接收 */
    HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);
  }
}

void StartSerialTask(void *argument)
{
  /* USER CODE BEGIN StartSerialTask */
  s_uart_len = 0;

  LOG_INFO(TAG_RTOS, "SerialTask started, enabling UART interrupt RX...");

  /* 使用全局变量 g_rx_byte 作为中断接收缓冲区 */
  g_rx_byte = 0;
  HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);

  for (;;)
  {
    osDelay(500);  // 降低打印频率，避免干扰
  }
  /* USER CODE END StartSerialTask */
}
/* USER CODE END Application */