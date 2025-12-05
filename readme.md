# ESP32-S3 HMI 项目

基于 ESP32-S3 的触摸屏人机界面（HMI）项目，集成了显示屏、触摸屏、音频播放、语音识别、无线通信等功能。

## 项目简介

本项目是一个功能完整的嵌入式 HMI 系统，使用 ESP32-S3 作为主控芯片，支持：
- 1.85 英寸触摸屏显示
- LVGL 图形界面
- 音频播放（MP3/WAV）
- 语音识别
- WiFi/蓝牙通信
- SD 卡存储
- 实时时钟（RTC）
- 电池电压监测

## 硬件平台

- **主控芯片**: ESP32-S3
- **显示屏**: ST77916 (1.85 英寸)
- **触摸屏**: CST816
- **音频 DAC**: PCM5101
- **RTC**: PCF85063
- **I/O 扩展**: TCA9554PWR

## 项目结构

```
esp32-HMI/
├── main/                          # 主程序代码目录
│   ├── main.cpp                   # 程序入口文件
│   ├── app_config.h               # 应用程序配置文件
│   ├── CMakeLists.txt             # 主程序构建配置
│   │
│   ├── Audio_Driver/              # 音频驱动
│   │   ├── PCM5101.c/.h          # PCM5101 DAC 驱动
│   │
│   ├── BAT_Driver/                # 电池驱动
│   │   ├── BAT_Driver.c/.h       # 电池电压检测驱动
│   │
│   ├── EXIO/                      # I/O 扩展驱动
│   │   ├── TCA9554PWR.c/.h       # TCA9554PWR I/O 扩展芯片驱动
│   │
│   ├── I2C_Driver/                # I2C 总线驱动
│   │   ├── I2C_Driver.c/.h       # I2C 通信驱动
│   │
│   ├── LCD_Driver/                # 显示屏驱动
│   │   ├── ST77916.c/.h          # ST77916 显示屏驱动
│   │   └── esp_lcd_st77916/      # ESP-IDF LCD 接口封装
│   │
│   ├── Touch_Driver/              # 触摸屏驱动
│   │   ├── CST816.c/.h           # CST816 触摸屏驱动
│   │   └── esp_lcd_touch/        # ESP-IDF 触摸接口封装
│   │
│   ├── LVGL_Driver/               # LVGL 图形库驱动
│   │   ├── LVGL_Driver.c/.h      # LVGL 初始化和驱动封装
│   │
│   ├── LVGL_UI/                   # LVGL 用户界面
│   │   ├── LVGL_Example.c/.h     # LVGL 示例界面
│   │   └── LVGL_Music.c/.h       # 音乐播放界面
│   │
│   ├── eez_ui/                    # EEZ-Flow UI 框架
│   │   ├── ui.c/.h                # UI 主文件
│   │   ├── screens.c/.h           # 界面定义
│   │   ├── styles.c/.h            # 样式定义
│   │   ├── fonts.h                # 字体定义
│   │   ├── images.c/.h            # 图片资源
│   │   ├── vars.h                 # 变量定义
│   │   ├── actions.h              # 动作定义
│   │   └── eez-flow.cpp/.h       # EEZ-Flow 框架
│   │
│   ├── MIC_Driver/                # 麦克风驱动
│   │   ├── MIC_Speech.c/.h       # 麦克风语音识别驱动
│   │
│   ├── PCF85063/                  # RTC 驱动
│   │   ├── PCF85063.c/.h         # PCF85063 实时时钟驱动
│   │
│   ├── SD_Card/                   # SD 卡驱动
│   │   ├── SD_MMC.c/.h           # SD/MMC 卡驱动
│   │
│   └── Wireless/                  # 无线通信驱动
│       ├── Wireless.c/.h         # WiFi/蓝牙通信驱动
│
├── components/                     # 第三方组件（本地管理）
│   ├── chmorgan__esp-audio-player/ # 音频播放器组件
│   ├── chmorgan__esp-libhelix-mp3/ # Helix MP3 解码库
│   ├── espressif__esp-dsp/        # ESP 数字信号处理库
│   ├── espressif__esp-sr/         # ESP 语音识别库
│   └── lvgl__lvgl/                # LVGL 图形库
│
├── managed_components/             # 自动管理的组件
│   └── lvgl__lvgl/                # ESP-IDF 组件管理器管理的组件
│
├── build/                          # 构建输出目录（编译生成）
│
├── CMakeLists.txt                  # 项目主 CMake 配置
├── sdkconfig                       # ESP-IDF 配置文件
├── sdkconfig.defaults              # ESP-IDF 默认配置
└── partitions.csv                  # 分区表定义
```

## 主要功能模块

### 1. 显示系统
- **LCD 驱动**: ST77916 显示屏驱动，支持 I2C 接口
- **触摸驱动**: CST816 触摸屏驱动，支持触摸事件处理
- **图形库**: LVGL 图形界面库，提供丰富的 UI 组件

### 2. 音频系统
- **音频播放**: 支持 MP3 和 WAV 格式音频播放
- **音频输出**: PCM5101 DAC 驱动
- **语音识别**: 集成 ESP-SR 语音识别库，支持语音命令

### 3. 存储系统
- **SD 卡**: 支持 SD/MMC 卡读写，用于存储音频文件等

### 4. 通信系统
- **WiFi**: 支持 WiFi 连接和通信
- **蓝牙**: 支持蓝牙通信（如需要）

### 5. 外设驱动
- **RTC**: PCF85063 实时时钟，提供时间功能
- **电池监测**: 电池电压检测和监测
- **I/O 扩展**: TCA9554PWR I/O 扩展芯片，扩展 GPIO 接口

### 6. UI 框架
- **LVGL**: 轻量级图形库，提供丰富的 UI 组件
- **EEZ-Flow**: UI 框架，用于构建复杂的用户界面

## 依赖组件

### ESP-IDF 组件
- `esp_lcd` - LCD 驱动框架
- `esp_audio` - 音频框架
- `esp_wifi` - WiFi 驱动
- `esp_sr` - 语音识别库

### 第三方组件
- **LVGL**: 图形界面库
- **esp-audio-player**: 音频播放器
- **libhelix-mp3**: MP3 解码库
- **esp-dsp**: 数字信号处理库
- **esp-sr**: 语音识别库

## 编译和烧录

### 环境要求
- ESP-IDF v5.0 或更高版本
- CMake 3.16 或更高版本
- Python 3.6 或更高版本

### 编译步骤

1. 设置 ESP-IDF 环境：
```bash
. $HOME/esp/esp-idf/export.sh
```

2. 配置项目：
```bash
idf.py menuconfig
```

3. 编译项目：
```bash
idf.py build
```

4. 烧录到设备：
```bash
idf.py flash
```

5. 查看串口输出：
```bash
idf.py monitor
```

### 一键编译和烧录
```bash
idf.py flash monitor
```

## 配置说明

### WiFi 配置
在 `main/app_config.h` 文件中配置 WiFi 连接信息：
```c
#define WIFI_SSID "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
```

### ESP-IDF 配置
使用 `idf.py menuconfig` 进入配置菜单，可以配置：
- 分区表
- WiFi 设置
- 音频编解码器
- 语音识别模型
- LVGL 配置
- 其他功能模块

## 程序流程

1. **初始化阶段** (`Driver_Init`):
   - 初始化 Flash
   - 初始化电池监测
   - 初始化 I2C 总线
   - 初始化 I/O 扩展
   - 初始化 RTC
   - 创建驱动任务循环

2. **驱动任务循环** (`Driver_Loop`):
   - 初始化无线通信
   - 循环执行：
     - RTC 时间更新
     - 电池电压读取
     - 延时 100ms

3. **主程序** (`app_main`):
   - 执行驱动初始化
   - 初始化 LCD 显示屏
   - 初始化音频系统
   - 初始化 LVGL 图形库
   - 初始化 UI 界面
   - 进入主循环：
     - UI 更新 (`ui_tick`)
     - LVGL 定时器处理 (`lv_timer_handler`)

## 开发说明

### 添加新功能模块
1. 在 `main/` 目录下创建新的驱动文件夹
2. 实现驱动代码（.c/.h 文件）
3. 在 `main/CMakeLists.txt` 中添加源文件和头文件路径
4. 在 `main/main.cpp` 中调用初始化函数

### UI 开发
- 使用 EEZ-Flow 框架进行 UI 设计
- UI 相关文件位于 `main/eez_ui/` 目录
- 样式和界面定义分别在 `styles.c` 和 `screens.c` 中

### 音频开发
- 音频文件存储在 SD 卡中
- 使用 `Play_Music()` 函数播放音频
- 支持 MP3 和 WAV 格式

## 注意事项

1. **内存管理**: ESP32-S3 内存有限，注意合理使用内存
2. **任务优先级**: 确保 LVGL 任务优先级设置合理
3. **I2C 冲突**: 注意 I2C 设备地址冲突问题
4. **电源管理**: 注意电池供电时的功耗优化

## 许可证

请查看各个组件的 LICENSE 文件了解具体的许可证信息。

## 更新日志

- 初始版本：支持基本的显示、触摸、音频播放功能

