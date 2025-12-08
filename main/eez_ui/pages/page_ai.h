/**
 * @file page_ai.h
 * @brief AI 对话页面 - 语音 AI 交互界面
 *
 * 屏幕内容：
 * - AI 对话按钮（带呼吸灯效果）
 * - 状态文本显示（开始/连接中/聆听中/发送中/播放中）
 */

#ifndef PAGES_PAGE_AI_H
#define PAGES_PAGE_AI_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 page_ai 屏幕
 * 屏幕 ID=4，索引=3
 */
void create_screen_page_ai(void);

/**
 * @brief 删除 page_ai 屏幕
 */
void delete_screen_page_ai(void);

/**
 * @brief page_ai 屏幕 tick 函数
 */
void tick_screen_page_ai(void);

/**
 * @brief 检查 AI 服务是否正在运行
 * @return true 正在运行，false 未运行
 */
bool page_ai_is_active(void);

/**
 * @brief 停止 AI 服务（供外部调用，如滑动离开时）
 */
void page_ai_stop_service(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGES_PAGE_AI_H */
