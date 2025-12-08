/**
 * @file page_main.h
 * @brief 主界面页面 - WiFi 连接成功后的主屏幕
 *
 * 屏幕内容：
 * - "会议"按钮（红色，圆形）- 跳转到会议笔记页面
 * - "AI"按钮（蓝色，圆形）- 跳转到 AI 对话页面
 */

#ifndef PAGES_PAGE_MAIN_H
#define PAGES_PAGE_MAIN_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 Main 屏幕
 * 屏幕 ID=2，索引=1
 */
void create_screen_main(void);

/**
 * @brief 删除 Main 屏幕
 */
void delete_screen_main(void);

/**
 * @brief Main 屏幕 tick 函数
 */
void tick_screen_main(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGES_PAGE_MAIN_H */
