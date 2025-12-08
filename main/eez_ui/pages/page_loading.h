/**
 * @file page_loading.h
 * @brief Loading 页面 - WiFi 连接等待界面
 *
 * 屏幕内容：
 * - 一个旋转的 spinner（加载动画）
 * - 一个标签显示"连接中"或 WiFi 状态信息
 */

#ifndef PAGES_PAGE_LOADING_H
#define PAGES_PAGE_LOADING_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 Loading 屏幕
 * 屏幕 ID=1，索引=0
 */
void create_screen_loading(void);

/**
 * @brief 删除 Loading 屏幕
 */
void delete_screen_loading(void);

/**
 * @brief Loading 屏幕 tick 函数
 */
void tick_screen_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGES_PAGE_LOADING_H */
