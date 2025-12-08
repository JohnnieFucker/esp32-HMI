/**
 * @file app_config.h
 * @brief 应用程序全局配置
 *
 * 集中管理所有可配置参数：
 * - WiFi 网络配置
 * - 录音参数
 * - API 服务地址
 * - 超时设置
 *
 * @note 请在编译前根据实际情况修改配置值
 */

#pragma once

// ============================================================================
// WiFi 配置
// ============================================================================

/**
 * @brief WiFi 配置结构体
 */
typedef struct {
  const char *ssid;     ///< WiFi 网络名称
  const char *password; ///< WiFi 密码
} wifi_config_item_t;

/** WiFi 配置数量 */
#define WIFI_CONFIG_COUNT 2

/**
 * @brief WiFi 配置列表
 *
 * 系统会按顺序尝试连接，直到成功
 * @note 请修改为实际的 WiFi 配置
 */
static const wifi_config_item_t wifi_configs[WIFI_CONFIG_COUNT] = {
    {"AP-jianlong", "1122334455"}, // WiFi 1
    {"4F", "yuanhenglizhen"},      // WiFi 2
};

/** WiFi 单个网络连接超时时间（秒） */
#define WIFI_CONNECT_TIMEOUT_SEC 10

// ============================================================================
// 录音配置
// ============================================================================

/**
 * 每段录音时长（秒）
 * 录音完成后自动上传到服务器
 */
#define RECORD_DURATION_SEC 60

/**
 * 麦克风数字增益
 * - 1 = 原始音量
 * - 2 = 2倍放大
 * - 4 = 4倍放大（推荐）
 * - 8 = 8倍放大
 *
 * @note 增益过大会导致音频失真
 */
#define MIC_GAIN 4

// ============================================================================
// API 服务配置
// ============================================================================

/**
 * @brief 笔记服务配置
 * @note 请替换为实际的 API 地址和认证令牌
 */
#define CG_API_URL "https://vx.cgboiler.com"
#define API_UPLOAD "/v1/file/upload"
#define API_NOTE "/v1/note"

/** 用户 ID */
#define USER_ID "18602880272"

/**
 * @brief API 认证令牌 (JWT)
 * @warning 生产环境请妥善保管此令牌
 */
#define CG_TOKEN                                                               \
  "eyJ0eXBlIjoiSldUIiwiYWxnIjoiSFMyNTYifQ."                                    \
  "eyJjb21wYW55SWQiOiJmYmQ5NjU0ZS00ZDE5LTRkMjgtYjUyNS1lNzQ3N2E4YjI4YjkiLCJ1c2" \
  "VySWQiOiIxODYwMjg4MDI3MiIsInVzZXJOYW1lIjoi5p2O5bymIiwibG9naW5UeXBlIjoic21z" \
  "IiwicGxhdGZvcm0iOiJ3ZWIiLCJpYXQiOjE3NjM2MDAwMjMsImV4cCI6MTc3MTM3NjAyM30._"  \
  "eO1WGwXfFu0yrwGU54wP3s9Zjj44FvB2OQ9Sj975Zc"

// ============================================================================
// AI 语音服务配置
// ============================================================================

/** AI 语音服务 WebSocket 地址 */
#define CG_AI_URL "wss://audio-esp32.cgboiler.com/koi/v1/"

/**
 * 无语音输入自动断开时间（秒）
 * 超过此时间无语音输入，自动断开 WebSocket 连接
 */
#define CLOSE_CONNECTION_NO_VOICE_TIME 120
