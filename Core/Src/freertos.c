/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
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
#include "usart.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NET_BUF_SIZE    512     // 网络接收缓冲区大小
#define SEND_BUF_SIZE   256     // 发送缓冲区大小
#define MSG_DELIMITER   "\r\n\r\n"  // 消息分隔符（Android 端用）

/* 串口日志宏（USART1, 115200） */
#define LOG(fmt, ...) do { \
    char _logbuf[128]; \
    int _n = snprintf(_logbuf, sizeof(_logbuf), "[RTOS] " fmt "\r\n", ##__VA_ARGS__); \
    if (_n > 0) HAL_UART_Transmit(&huart1, (uint8_t*)_logbuf, _n, 100); \
} while(0)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for NetTask */
osThreadId_t NetTaskHandle;
const osThreadAttr_t NetTask_attributes = {
  .name = "NetTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for RouterTask */
osThreadId_t RouterTaskHandle;
const osThreadAttr_t RouterTask_attributes = {
  .name = "RouterTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for ServoTask */
osThreadId_t ServoTaskHandle;
const osThreadAttr_t ServoTask_attributes = {
  .name = "ServoTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
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

/* USER CODE END FunctionPrototypes */

void StartNetTask(void *argument);
void StartRouterTask(void *argument);
void StartServoTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of NetQueue */
  NetQueueHandle = osMessageQueueNew (16, 64, &NetQueue_attributes);

  /* creation of ServoQueue */
  ServoQueueHandle = osMessageQueueNew (8, 16, &ServoQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* creation of SendQueue */
  SendQueueHandle = osMessageQueueNew (8, SEND_BUF_SIZE, &SendQueue_attributes);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of NetTask */
  NetTaskHandle = osThreadNew(StartNetTask, NULL, &NetTask_attributes);

  /* creation of RouterTask */
  RouterTaskHandle = osThreadNew(StartRouterTask, NULL, &RouterTask_attributes);

  /* creation of ServoTask */
  ServoTaskHandle = osThreadNew(StartServoTask, NULL, &ServoTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartNetTask */
/**
  * @brief  网络任务: 初始化 W5500 → 开 TCP Server → 循环收发
  *         收: 从 W5500 recv → 入 NetQueue
  *         发: 从 SendQueue 取 → 发回 W5500
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartNetTask */
void StartNetTask(void *argument)
{
  /* USER CODE BEGIN StartNetTask */
  uint8_t ip[4]   = W5500_IP_ADDR;
  uint8_t mask[4] = W5500_SUBNET_MASK;
  uint8_t gw[4]   = W5500_GATEWAY_ADDR;

  uint8_t net_buf[NET_BUF_SIZE];
  uint8_t send_buf[SEND_BUF_SIZE];

  /* Step 1: 初始化 W5500 */
  w5500_init();

  /* Step 2: 配置网络参数 */
  w5500_network_config(ip, mask, gw);

  /* 打印网络配置 */
  LOG("W5500 initialized");
  LOG("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  LOG("Mask: %d.%d.%d.%d", mask[0], mask[1], mask[2], mask[3]);
  LOG("GW: %d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);
  uint8_t mac[6] = W5500_MAC_ADDR;
  LOG("MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  LOG("Port: %d", W5500_TCP_PORT);

  /* Step 3: 打开 TCP Server */
  while (w5500_tcp_server_open(W5500_TCP_PORT) != 0)
  {
    /* 如果打开失败，等 1 秒重试 */
    osDelay(1000);
  }

  /* Step 4: 收发循环 */
  for(;;)
  {
    /* 等待建立连接 */
    if (!w5500_tcp_established())
    {
      osDelay(100);
      continue;
    }

    /* ---- 收数据 ---- */
    int len = w5500_tcp_recv(net_buf, NET_BUF_SIZE);
    if (len > 0)
    {
      /* 确保以 '\0' 结尾 */
      if (len < NET_BUF_SIZE) net_buf[len] = '\0';
      else net_buf[NET_BUF_SIZE - 1] = '\0';

      /* 塞入 NetQueue，交给 RouterTask 处理 */
      osMessageQueuePut(NetQueueHandle, net_buf, 0, 0);
    }
    else if (len == -1)
    {
      LOG("Client disconnected, re-listening");
      /* 连接断开，重新监听 */
      w5500_tcp_close();
      w5500_tcp_server_open(W5500_TCP_PORT);
      osDelay(100);
    }

    /* ---- 发数据（非阻塞取 SendQueue） ---- */
    if (osMessageQueueGet(SendQueueHandle, send_buf, NULL, 0) == osOK)
    {
      /* 如果发送失败（断开），忽略等下次连接 */
      w5500_tcp_send(send_buf, strlen((const char *)send_buf));
    }

    /* 无数据，等一会再查 */
    osDelay(10);
  }
  /* USER CODE END StartNetTask */
}

/* USER CODE BEGIN Header_StartRouterTask */
/**
* @brief 路由任务: 解析 Android App 发来的 JSON 指令
*
* Android 协议格式:
*   {"command":"servo_set","cseq":"1","ch":0,"angle":90}
*   以 \r\n\r\n 分隔（已由 NetTask 截断）
*
* 响应格式:
*   {"command":"servo_set","code":"200","cseq":"1","msg":"ok"}
*   通过 SendQueue 发回
*
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRouterTask */
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
    /* 从 NetQueue 取一条网络数据（阻塞等待） */
    osMessageQueueGet(NetQueueHandle, net_buf, NULL, osWaitForever);

    /* 确保以 '\0' 结尾 */
    net_buf[NET_BUF_SIZE - 1] = '\0';

    /* 提取 "cseq"（如果有，用于回响应时带回） */
    cseq[0] = '\0';
    json_get_string((const char *)net_buf, "cseq", cseq, sizeof(cseq));

    /* 解析 "command" 字段 */
    if (json_get_string((const char *)net_buf, "command", command, sizeof(command)) != 0)
    {
      continue;  /* 没有 command 字段，丢弃 */
    }

    /* ======================== 指令路由 ======================== */

    /* 串口日志：打印收到的指令 */
    LOG("RECV: %s", (const char *)net_buf);

    if (strcmp(command, "servo_set") == 0)
    {
      /* 舵机控制: {"command":"servo_set","ch":0,"angle":90} */
      channel = 0;
      angle = 90;

      json_get_int((const char *)net_buf, "ch", &channel);
      json_get_int((const char *)net_buf, "angle", &angle);

      /* 限幅 */
      if (angle < 0) angle = 0;
      if (angle > 180) angle = 180;

      LOG("servo_set: ch=%d, angle=%d", channel, angle);

      /* 目前只支持一个舵机，直接塞入 ServoQueue */
      osMessageQueuePut(ServoQueueHandle, &angle, 0, 0);

      /* 回响应给 Android App */
      LOG("Response sent");
      snprintf(resp, sizeof(resp),
               "{\"command\":\"servo_set\",\"code\":\"200\",\"cseq\":\"%s\",\"msg\":\"ok\"}"
               MSG_DELIMITER,
               cseq);
      osMessageQueuePut(SendQueueHandle, resp, 0, 0);
    }
    else if (strcmp(command, "servo_get") == 0)
    {
      /* 查询角度（预留） */
      LOG("servo_get");
      snprintf(resp, sizeof(resp),
               "{\"command\":\"servo_get\",\"code\":\"200\",\"cseq\":\"%s\",\"angle\":90}"
               MSG_DELIMITER,
               cseq);
      osMessageQueuePut(SendQueueHandle, resp, 0, 0);
    }
    else
    {
      /* 未知指令，回错误 */
      LOG("Unknown command: %s", command);
      snprintf(resp, sizeof(resp),
               "{\"command\":\"%s\",\"code\":\"400\",\"cseq\":\"%s\",\"msg\":\"unknown command\"}"
               MSG_DELIMITER,
               command, cseq);
      osMessageQueuePut(SendQueueHandle, resp, 0, 0);
    }
  }
  /* USER CODE END StartRouterTask */
}

/* USER CODE BEGIN Header_StartServoTask */
/**
* @brief 舵机任务: 从 ServoQueue 取角度值 → 输出 PWM 控制舵机
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartServoTask */
void StartServoTask(void *argument)
{
  /* USER CODE BEGIN StartServoTask */
  int angle;

  /* 初始化舵机 PWM */
  servo_init();

  for(;;)
  {
    /* 等待角度指令（阻塞） */
    osMessageQueueGet(ServoQueueHandle, &angle, NULL, osWaitForever);

    LOG("ServoTask: angle=%d", angle);

    /* 设置舵机角度 */
    servo_set_angle(angle);
  }
  /* USER CODE END StartServoTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

