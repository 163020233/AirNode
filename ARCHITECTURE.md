# AirNode 项目架构文档

## 一、系统架构概览

```
┌─────────────────────────────────────────────────────────────────────┐
│                       Android App (Java 悬浮窗)                      │
│  Package: com.qjkj.overlayapp                                       │
├──────────────────────┬──────────────────────┬───────────────────────┤
│   ShoutManager       │   ServoManager       │   Stm32Transport      │
│   (喊话器模块)        │   (舵机控制模块)       │   (喊话器 TCP 传输)   │
│   TCP :9527          │   TCP :13550         │                       │
│   UDP :8999(音频)    │   独立 Socket         │                       │
├──────────┼───────────┴──────────┼──────────┴──────────┼────────────┤
│          ▼                     ▼                     ▼             │
│    ShoutFloatingService — 悬浮窗 UI 管理 (Service)                  │
│       ├─ 折叠小球 + 菜单面板 + 喊话面板 + 舵机面板                   │
│       └─ 多面板导航: COLLAPSED ↔ MENU ↔ SHOUT/SERVO               │
└──────────────────────┬──────────────────────────────────────────────┘
                       │ TCP/IP
                       ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    STM32F407ZGT6 + W5500 中控                        │
│  CubeMX Project: AirNode (CMake / Keil MDK)                        │
├─────────────────────────────────────────────────────────────────────┤
│  FreeRTOS (CMSIS_V2)                                                │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐                │
│  │  NetTask     │  │  RouterTask  │  │  ServoTask  │                │
│  │  Priority:   │  │  Priority:   │  │  Priority:  │                │
│  │  High        │  │  AboveNormal │  │  Normal     │                │
│  │  4KB Stack   │  │  4KB Stack   │  │  2KB Stack  │                │
│  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘                │
│         │                 │                  │                       │
│         │           NetQueue                 │                       │
│         │           (16×64B)                 │                       │
│         ├────────────────▶▌                  │                       │
│         │                                  │                       │
│         │          SendQueue                │                       │
│         │          (8×256B)                 │                       │
│         ◀──────▌───────                    │                       │
│                                            │  ServoQueue            │
│                                            │  (8×16B)               │
│                                         ◀──▌──────                 │
├─────────────────────────────────────────────────────────────────────┤
│  外设驱动                                                           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐               │
│  │  W5500   │ │  TIM2    │ │  USART1  │ │  GPIO    │               │
│  │  SPI1    │ │  CH1→PA15│ │  PA9/PA10│ │  PA4 CS  │               │
│  │  10.5Mbps│ │  PWM 50Hz│ │  115200  │ │  PC4 RST │               │
│  │  DMA ON  │ │  0°~180° │ │  8N1     │ │  PB0 INT │               │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘               │
└─────────────────────────────────────────────────────────────────────┘
```

## 二、硬件引脚分配

| 外设 | 引脚 | 功能 | 复用 |
|:----|:----|:-----|:-----|
| **W5500** | PA5 | SPI1_SCK | AF5 |
| | PA6 | SPI1_MISO | AF5 |
| | PA7 | SPI1_MOSI | AF5 |
| | PA4 | CS（片选） | GPIO_OUT |
| | PC4 | RST（复位） | GPIO_OUT |
| | PB0 | INT（中断，EXTI0） | EXT_IN |
| **舵机** | PA15 | TIM2_CH1 PWM 50Hz | AF1 |
| **串口** | PA9 | USART1_TX | AF7 |
| | PA10 | USART1_RX | AF7 |
| **调试** | PA13 | SWDIO | — |
| | PA14 | SWCLK | — |

## 三、通信协议

### 3.1 App → STM32 指令格式

```json
{"command":"servo_set","cseq":"1","ch":0,"angle":90}\r\n\r\n
```

| 字段 | 类型 | 说明 |
|:----|:----|:------|
| `command` | String | 指令名: `servo_set` / `servo_get` |
| `cseq` | String | 序号，响应时带回用于匹配 |
| `ch` | Int | 通道号 (0~N) |
| `angle` | Int | 角度值 (0~180) |

### 3.2 STM32 → App 响应格式

```json
{"command":"servo_set","code":"200","cseq":"1","msg":"ok"}\r\n\r\n
```

| 字段 | 类型 | 说明 |
|:----|:----|:------|
| `code` | String | `200`=成功, `400`=未知指令 |
| `msg` | String | 状态描述 |

## 四、软件模块说明

### 4.1 Android 端

| 模块 | 文件 | 功能 |
|:----|:-----|:------|
| **悬浮窗服务** | `ShoutFloatingService.java` | 系统 Service，管理悬浮窗 UI 和面板导航 |
| **舵机控制** | `ServoManager.java` | 独立 TCP 连接管理 STM32，JSON 指令收发 |
| **喊话器控制** | `ShoutManager.java` | 喊话器 TCP 指令 + UDP 音频推流 |
| **TCP 传输** | `Stm32Transport.java` | 单例 TCP 连接（喊话器用） |
| **主入口** | `MainActivity.java` | 权限检查 + 启动 Service |
| **布局文件** | `layout_floating_full.xml` | 悬浮窗完整布局（4个面板） |

### 4.2 STM32 端

| 模块 | 文件 | 功能 |
|:----|:-----|:------|
| **主程序** | `main.c` | 时钟配置 168MHz + 外设初始化 + 启动信息 |
| **RTOS 任务** | `freertos.c` | 3个 FreeRTOS 任务 + 队列管理 |
| **W5500 驱动** | `w5500.c/.h` | SPI 驱动 + TCP Server 收发 |
| **舵机 PWM** | `servo.c/.h` | TIM2_PWM 控制 0°~180° |
| **JSON 解析** | `simple_json.c/.h` | 轻量级 JSON key-value 提取 |
| **日志管理** | `debug_log.h` | 统一日志输出 (4级: E/W/I/D) |
| **定时器** | `tim.c/.h` | TIM2 初始化 (CubeMX 生成) |
| **串口** | `usart.c/.h` | USART1 初始化 (CubeMX 生成) |
| **中断处理** | `stm32f4xx_it.c` | 系统中断服务（CubeMX 生成） |

## 五、FreeRTOS 任务

| 任务 | 优先级 | 栈大小 | 功能 |
|:----|:-----:|:------:|:-----|
| **NetTask** | High | 4KB | W5500 初始化 + TCP 收发 + 数据入/出队 |
| **RouterTask** | AboveNormal | 4KB | 从 NetQueue 取数据 → JSON 解析 → 分发 |
| **ServoTask** | Normal | 2KB | 从 ServoQueue 取角度 → PWM 输出 |

## 六、数据流

### 舵机控制完整链路

```
App 舵机面板
  │ 松手发送: {"command":"servo_set","ch":0,"angle":90}
  ▼
ServoManager (Android)
  │ TCP :13550 → W5500
  ▼
NetTask (STM32)
  │ w5500_tcp_recv() → 入 NetQueue
  ▼
RouterTask
  │ json_get → command="servo_set" → angle → ServoQueue
  │ 回响应 → SendQueue → NetTask → W5500 → App
  ▼
ServoTask
  │ servo_set_angle(90) → 改 TIM2 CCR
  ▼
PA15 → PWM 50Hz → 舵机转到 90°
```

## 七、日志系统

| 级别 | 宏 | 用途 | 启动 | 指令 | 收发调试 |
|:---:|:---|:-----|:----:|:----:|:--------:|
| ERROR | `LOG_ERROR` | 致命错误 | ✅ | ✅ | ✅ |
| WARN | `LOG_WARN` | 警告 | ✅ | ✅ | ✅ |
| **INFO** | `LOG_INFO` | **正常信息（默认）** | **✅** | **✅** | ❌ |
| DEBUG | `LOG_DEBUG` | 调试详情 | ❌ | ❌ | ✅ |

## 八、开发工具链

| 环节 | 工具 | 说明 |
|:----|:-----|:------|
| 芯片配置 | STM32CubeMX | 引脚分配 + 时钟 + 外设 |
| 代码编辑 | CLion / Keil MDK | CLion 编辑 + CMake 编译 / Keil 编译 |
| 编译 | GCC arm-none-eabi | CLion CMake + Ninja |
| 烧录 | OpenOCD / Keil LOAD | CMSIS-DAP / Mini HSDAP |
| 调试 | GDB (CLion) / Keil Debug | SWD: PA13/PA14 |
