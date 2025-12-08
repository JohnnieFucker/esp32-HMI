/**
 * @file CgAiService.h
 * @brief AI 语音对话服务接口
 *
 * 提供与云端 AI 服务的语音交互功能：
 * - WebSocket 实时连接
 * - Opus 音频编解码
 * - VAD 语音活动检测
 * - 自动超时断开
 *
 * 使用流程：
 * 1. cg_ai_service_init() - 初始化服务
 * 2. cg_ai_service_start() - 开始对话
 * 3. cg_ai_service_stop() - 结束对话
 * 4. cg_ai_service_deinit() - 释放资源
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief AI 服务状态枚举
 *
 * 状态流转：
 * IDLE → CONNECTING → CONNECTED → LISTENING → SENDING → SPEAKING → LISTENING
 *                                     ↑_______________________________↓
 *
 * VAD 控制：
 * - LISTENING: VAD 开启，检测语音
 * - SENDING:   VAD 关闭，发送音频数据
 * - SPEAKING:  VAD 关闭，播放 AI 回复
 */
typedef enum {
  CG_AI_STATE_IDLE = 0,   // 空闲状态
  CG_AI_STATE_CONNECTING, // 正在连接 WebSocket
  CG_AI_STATE_CONNECTED,  // 已连接，等待初始化
  CG_AI_STATE_LISTENING,  // 聆听中（VAD 开启）
  CG_AI_STATE_SENDING,    // 发送中（VAD 关闭，正在发送音频）
  CG_AI_STATE_SPEAKING,   // 播放中（VAD 关闭，播放 AI 回复）
  CG_AI_STATE_ERROR       // 错误状态
} cg_ai_state_t;

/**
 * AI 服务状态变化回调
 */
typedef void (*cg_ai_state_callback_t)(cg_ai_state_t new_state,
                                       void *user_data);

/**
 * 初始化 AI 语音服务
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t cg_ai_service_init(void);

/**
 * 反初始化 AI 语音服务
 */
void cg_ai_service_deinit(void);

/**
 * 启动 AI 对话
 * 连接到 CG_AI_URL WebSocket 服务器，开始监听麦克风
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t cg_ai_service_start(void);

/**
 * 停止 AI 对话
 * 断开 WebSocket 连接，停止麦克风
 */
void cg_ai_service_stop(void);

/**
 * 获取当前 AI 服务状态
 * @return 当前状态
 */
cg_ai_state_t cg_ai_service_get_state(void);

/**
 * 检查 AI 服务是否处于活动状态（非空闲）
 * @return true 活动状态，false 空闲状态
 */
bool cg_ai_service_is_active(void);

/**
 * 设置状态变化回调
 * @param callback 回调函数
 * @param user_data 用户数据，传递给回调函数
 */
void cg_ai_service_set_state_callback(cg_ai_state_callback_t callback,
                                      void *user_data);

/**
 * 获取最后一次活动时间（用于超时检测）
 * @return 最后活动时间戳（毫秒）
 */
uint32_t cg_ai_service_get_last_activity_time(void);

/**
 * 检查是否超时（没有语音输入）
 * @param timeout_sec 超时时间（秒）
 * @return true 已超时，false 未超时
 */
bool cg_ai_service_is_timeout(uint32_t timeout_sec);

#ifdef __cplusplus
}
#endif
