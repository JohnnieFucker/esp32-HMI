# ESP32-HMI 项目

基于 ESP32-S3 的智能语音交互设备，集成 AI 语音对话和笔记录音功能。

## 项目概述

本项目是一款便携式 AI 语音助手设备，主要功能包括：
- **AI 语音对话**：通过 WebSocket 连接云端 AI 服务，实现语音交互
- **笔记录音**：录制语音并上传到云端，自动生成文字笔记
- **圆形触摸屏**：360x360 分辨率 LCD 显示屏，支持触摸操作

## 硬件平台

| 组件 | 型号/规格 | 说明 |
|------|----------|------|
| MCU | ESP32-S3 | 双核 240MHz，8MB PSRAM |
| 显示屏 | ST77916 | 360x360 圆形 LCD，QSPI 接口 |
| 触摸屏 | CST816 | I2C 电容触摸 |
| 音频输出 | PCM5101 | I2S DAC |
| 麦克风 | INMP441 | I2S 数字麦克风 |
| RTC | PCF85063 | I2C 实时时钟 |
| IO扩展 | TCA9554PWR | I2C GPIO 扩展器 |

### GPIO 分配

```
I2C 总线:
  - SDA: GPIO_NUM_10
  - SCL: GPIO_NUM_11

音频输出 (I2S0):
  - BCLK: GPIO_NUM_48
  - LRCK: GPIO_NUM_38
  - DOUT: GPIO_NUM_47

麦克风输入 (I2S1):
  - BCLK: GPIO_NUM_15
  - LRCK: GPIO_NUM_2
  - DIN:  GPIO_NUM_39

LCD (QSPI):
  - SCK:   GPIO_NUM_40
  - DATA0: GPIO_NUM_45
  - DATA1: GPIO_NUM_46
  - DATA2: GPIO_NUM_7
  - DATA3: GPIO_NUM_6
  - CS:    GPIO_NUM_21

触摸屏 (I2C):
  - 共用 I2C 总线

电池 ADC:
  - GPIO_NUM_3 (ADC1_CH3)
```

## 软件架构

项目采用分层架构，按功能模块组织代码：

```
main/
├── main.cpp                 # 程序入口
├── app_config.h             # 全局配置
│
├── drivers/                 # 驱动层
│   ├── i2c/                 # I2C 总线
│   │   └── i2c_driver.c/h
│   ├── gpio/                # GPIO 扩展
│   │   └── tca9554.c/h
│   ├── display/             # 显示驱动
│   │   ├── st77916.c/h
│   │   └── esp_lcd_st77916/
│   ├── touch/               # 触摸驱动
│   │   ├── cst816.c/h
│   │   └── esp_lcd_touch/
│   ├── audio/               # 音频驱动
│   │   ├── pcm5101.c/h      # DAC
│   │   ├── mic_driver.c/h   # 麦克风
│   │   └── audio_processor.cc/h  # 音频处理
│   ├── power/               # 电源管理
│   │   └── bat_driver.c/h
│   ├── rtc/                 # 实时时钟
│   │   └── pcf85063.c/h
│   └── storage/             # 存储
│       ├── sd_card.c/h
│       └── file_system.c/h
│
├── services/                # 服务层
│   ├── wifi/                # WiFi 服务
│   │   └── wifi_service.c/h
│   ├── http/                # HTTP 客户端
│   │   └── http_client.c/h
│   ├── ai/                  # AI 语音服务
│   │   └── ai_service.cc/h
│   └── note/                # 笔记服务
│       ├── note_service.c/h
│       └── audio_recorder.c/h
│
├── ui/                      # UI 层
│   ├── lvgl_port/           # LVGL 移植
│   │   └── lvgl_driver.c/h
│   └── eez_ui/              # EEZ Studio UI
│
└── utils/                   # 工具函数
    └── utils.c/h
```

## 核心模块说明

### 1. 系统启动流程 (main.cpp)

```
app_main()
    ├── driver_init()           # 初始化基础驱动
    │   ├── Flash_Searching()   # 启动动画
    │   ├── BAT_Init()          # 电池ADC
    │   ├── I2C_Init()          # I2C总线
    │   ├── EXIO_Init()         # IO扩展器
    │   └── PCF85063_Init()     # RTC
    │
    ├── LCD_Init()              # LCD + 背光 + 触摸
    ├── Audio_Init()            # 音频播放器
    ├── LVGL_Init()             # LVGL图形库
    ├── ui_init()               # UI界面
    │
    └── Main Loop               # 主循环
        ├── ui_tick()           # UI业务逻辑
        └── lv_timer_handler()  # LVGL定时器
```

### 2. AI 语音服务 (services/ai/ai_service)

AI 语音对话的核心服务，实现与云端 AI 的实时语音交互。

**工作流程：**
```
cg_ai_service_start()
    ├── WebSocket 连接
    ├── 协议握手 (Hello)
    ├── 初始化 AudioProcessor (VAD)
    │
    └── 循环处理
        ├── mic_task (麦克风任务)
        │   ├── 读取 PCM 数据
        │   ├── VAD 语音检测
        │   ├── Opus 编码
        │   └── WebSocket 发送
        │
        └── audio_out_task (音频输出任务)
            ├── 接收 Opus 音频
            ├── Opus 解码
            └── I2S 播放
```

**状态机：**
```
IDLE → CONNECTING → CONNECTED → LISTENING ⇄ SPEAKING → IDLE
```

### 3. 笔记录音服务 (services/note/)

录制语音并上传到云端，自动生成笔记。

**工作流程：**
```
start_note_recording()
    ├── 生成 UUID
    ├── 初始化录音器
    │
    └── 并行任务
        ├── record_task  (录音)
        │   ├── 录制 WAV
        │   └── 加入上传队列
        │
        └── upload_task  (上传)
            └── HTTP POST

stop_note_recording()
    ├── 停止录音
    ├── 等待上传完成
    └── 生成笔记
```

### 4. 音频处理器 (drivers/audio/audio_processor)

基于 ESP-SR 的音频前端处理：
- **VAD** - 语音活动检测
- **NS** - 噪声抑制
- **AGC** - 自动增益控制

### 5. WiFi 管理 (services/wifi/)

支持多 WiFi 网络配置，在 `app_config.h` 中配置：
```c
static const wifi_config_item_t wifi_configs[] = {
    {"SSID1", "password1"},
    {"SSID2", "password2"},
};
```

## 配置说明

所有配置集中在 `app_config.h`：

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `wifi_configs[]` | WiFi 网络配置 | - |
| `WIFI_CONNECT_TIMEOUT_SEC` | WiFi 连接超时 | 10秒 |
| `RECORD_DURATION_SEC` | 每段录音时长 | 60秒 |
| `MIC_GAIN` | 麦克风增益 | 4 |
| `CG_TOKEN` | API 认证令牌 | - |
| `CG_API_URL` | 笔记服务 API | - |
| `CG_AI_URL` | AI 语音 WebSocket | - |
| `CLOSE_CONNECTION_NO_VOICE_TIME` | 无语音断开时间 | 120秒 |

## 编译与烧录

### 环境要求
- ESP-IDF v5.x
- Python 3.8+

### 编译步骤

```bash
# 设置 ESP-IDF 环境
. $IDF_PATH/export.sh

# 配置项目
idf.py set-target esp32s3
idf.py menuconfig

# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 监控
idf.py -p /dev/ttyUSB0 monitor
```

### menuconfig 关键配置

```
Component config → ESP PSRAM
    - Support for external RAM: [*]
    - Mode: Octal
    - Type: ESP-PSRAM64/128

Component config → ESP32S3-Specific
    - CPU frequency: 240 MHz

Component config → LWIP
    - Max sockets: 16
```

## 依赖组件

| 组件 | 用途 |
|------|------|
| lvgl | 图形界面 |
| esp-sr | 语音识别前端 |
| esp-opus | Opus 编解码 |
| esp_websocket_client | WebSocket |
| esp_http_client | HTTP 客户端 |

## 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 目录名 | snake_case | `drivers/audio/` |
| 文件名 | snake_case | `ai_service.cc` |
| C 函数 | 大写前缀 + 下划线 | `MIC_Init()` |
| C++ 类 | PascalCase | `AudioProcessor` |
| 全局变量 | g_ 前缀 | `g_state` |
| 常量/宏 | 全大写 | `MIC_SAMPLE_RATE` |

## 内存管理

| 内存类型 | 容量 | 用途 |
|----------|------|------|
| Internal SRAM | ~512KB | 关键数据、DMA |
| PSRAM | 8MB | 显存、音频缓冲区 |

```c
// 优先 PSRAM 分配
void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
if (!buf) {
    buf = malloc(size);  // 回退内部 RAM
}
```

## 故障排除

### WiFi 连接失败
1. 检查 `app_config.h` 配置
2. 确保 2.4GHz 网络
3. 检查信号强度

### 音频无声
1. 检查音量：`Volume_adjustment(vol)`
2. 确认 I2S 配置
3. 检查 PCM5101 供电

### AI 服务连接失败
1. 确认 WiFi 已连接
2. 检查 Token 配置
3. 查看串口日志

### 内存不足
1. 调用 `utils_print_memory_info()`
2. 确保大缓冲区用 PSRAM
3. 减少任务栈大小

## 许可证

[待定]
