# ESP32-S3 HMI 项目

基于 ESP32-S3 的智能语音交互设备，集成 AI 语音对话和笔记录音功能。

## 项目简介

本项目是一款便携式 AI 语音助手设备，主要功能包括：

- **AI 语音对话** - 通过 WebSocket 连接云端 AI 服务，实现实时语音交互
- **笔记录音** - 录制语音并上传云端，自动生成文字笔记
- **触摸屏界面** - 360x360 圆形 LCD 显示屏，支持触摸操作
- **音频播放** - 支持 MP3/WAV 格式音频播放

## 硬件平台

| 组件 | 型号 | 说明 |
|------|------|------|
| MCU | ESP32-S3 | 双核 240MHz，8MB PSRAM |
| 显示屏 | ST77916 | 360x360 圆形 LCD，QSPI |
| 触摸屏 | CST816 | I2C 电容触摸 |
| 音频输出 | PCM5101 | I2S DAC |
| 麦克风 | INMP441 | I2S 数字麦克风 |
| RTC | PCF85063 | I2C 实时时钟 |
| IO扩展 | TCA9554PWR | I2C GPIO 扩展 |

## 项目结构

采用分层架构，所有文件名使用 snake_case 命名：

```
main/
├── main.cpp                    # 程序入口
├── app_config.h                # 全局配置
│
├── drivers/                    # 驱动层
│   ├── i2c/                    # I2C 总线
│   │   └── i2c_driver.c/h
│   ├── gpio/                   # GPIO 扩展
│   │   └── tca9554.c/h
│   ├── display/                # 显示驱动
│   │   ├── st77916.c/h
│   │   └── esp_lcd_st77916/
│   ├── touch/                  # 触摸驱动
│   │   ├── cst816.c/h
│   │   └── esp_lcd_touch/
│   ├── audio/                  # 音频驱动
│   │   ├── pcm5101.c/h         # DAC
│   │   ├── mic_driver.c/h      # 麦克风
│   │   └── audio_processor.cc/h
│   ├── power/                  # 电源管理
│   │   └── bat_driver.c/h
│   ├── rtc/                    # RTC
│   │   └── pcf85063.c/h
│   └── storage/                # 存储
│       ├── sd_card.c/h
│       └── file_system.c/h
│
├── services/                   # 服务层
│   ├── wifi/                   # WiFi
│   │   └── wifi_service.c/h
│   ├── http/                   # HTTP 客户端
│   │   └── http_client.c/h
│   ├── ai/                     # AI 语音
│   │   └── ai_service.cc/h
│   └── note/                   # 笔记录音
│       ├── note_service.c/h
│       └── audio_recorder.c/h
│
├── ui/                         # UI 层
│   ├── lvgl_port/              # LVGL 移植
│   │   └── lvgl_driver.c/h
│   └── eez_ui/                 # EEZ UI
│
└── utils/                      # 工具
    └── utils.c/h
```

## 快速开始

### 环境要求

- ESP-IDF v5.x
- Python 3.8+

### 编译烧录

```bash
# 设置 ESP-IDF 环境
. $IDF_PATH/export.sh

# 配置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录并监控
idf.py -p /dev/ttyUSB0 flash monitor
```

### 配置修改

编辑 `main/app_config.h`：

```c
// WiFi 配置
static const wifi_config_item_t wifi_configs[] = {
    {"your_ssid", "your_password"},
};

// API 配置
#define CG_TOKEN "your_api_token"
#define CG_API_URL "https://your-api-server.com"
#define CG_AI_URL "wss://your-ai-server.com/api"
```

## 主要功能

### AI 语音对话

```c
#include "ai_service.h"

cg_ai_service_init();     // 初始化
cg_ai_service_start();    // 开始对话
cg_ai_service_stop();     // 停止对话
```

### 笔记录音

```c
#include "note_service.h"

start_note_recording();   // 开始录音
stop_note_recording();    // 停止并生成笔记
```

## 技术特点

- **双核并行** - 音频处理与 UI 渲染分核运行
- **PSRAM 支持** - 大缓冲区使用 8MB 外部 PSRAM
- **VAD 检测** - 基于 ESP-SR 的语音活动检测
- **Opus 编解码** - 高效音频压缩传输
- **自动重连** - WiFi 断线自动重连

## 文档

详细技术文档请参阅 [main/README.md](main/README.md)

## 许可证

[待定]
