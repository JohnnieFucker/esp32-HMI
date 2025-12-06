#pragma once

/**
 * 应用程序全局配置文件
 * 在此文件中配置应用程序的各种参数
 */

// ==================== WiFi 配置 ====================
// WiFi 网络连接信息（支持多个 WiFi 网络，按顺序尝试连接）
typedef struct {
  const char *ssid;     // WiFi 名称
  const char *password; // WiFi 密码
} wifi_config_item_t;

// WiFi 配置数组大小
#define WIFI_CONFIG_COUNT 2

// WiFi 配置数组定义
static const wifi_config_item_t wifi_configs[WIFI_CONFIG_COUNT] = {
    {"AP-jianlong", "1122334455"}, // 第一个 WiFi 网络
    {"4F", "yuanhenglizhen"},      // 第二个 WiFi 网络
};

// WiFi 连接超时时间（秒）- 每个网络尝试连接的时间
#define WIFI_CONNECT_TIMEOUT_SEC 10

// ==================== 录音配置 ====================
// 录音时长（秒）- 每次录音的时长，录音完成后自动上传
#define RECORD_DURATION_SEC 60

// 麦克风增益（数字放大倍数）
// 1 = 原始音量, 2 = 2倍, 4 = 4倍, 8 = 8倍
// 如果录音声音太小，增加此值；如果失真，减小此值
#define MIC_GAIN 4

// ==================== 其他配置 ====================
// 可以在此处添加其他模块的配置参数
#define CG_TOKEN                                                               \
  "eyJ0eXBlIjoiSldUIiwiYWxnIjoiSFMyNTYifQ."                                    \
  "eyJjb21wYW55SWQiOiJmYmQ5NjU0ZS00ZDE5LTRkMjgtYjUyNS1lNzQ3N2E4YjI4YjkiLCJ1c2" \
  "VySWQiOiIxODYwMjg4MDI3MiIsInVzZXJOYW1lIjoi5p2O5bymIiwibG9naW5UeXBlIjoic21z" \
  "IiwicGxhdGZvcm0iOiJ3ZWIiLCJpYXQiOjE3NjM2MDAwMjMsImV4cCI6MTc3MTM3NjAyM30._"  \
  "eO1WGwXfFu0yrwGU54wP3s9Zjj44FvB2OQ9Sj975Zc"

#define USER_ID "18602880272"

#define CG_API_URL "https://vx.cgboiler.com"
#define API_UPLOAD "/v1/file/upload"
#define API_NOTE "/v1/note"