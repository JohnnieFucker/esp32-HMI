/**
 * @file page_notes.h
 * @brief 会议笔记页面 - 录音功能界面
 *
 * 屏幕内容：
 * - "开始"按钮 - 开始录音
 * - "结束"按钮 - 结束录音（带呼吸灯效果）
 */

#ifndef PAGES_PAGE_NOTES_H
#define PAGES_PAGE_NOTES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 page_notes 屏幕
 * 屏幕 ID=3，索引=2
 */
void create_screen_page_notes(void);

/**
 * @brief 删除 page_notes 屏幕
 */
void delete_screen_page_notes(void);

/**
 * @brief page_notes 屏幕 tick 函数
 */
void tick_screen_page_notes(void);

/**
 * @brief 检查录音是否正在进行
 * @return true 正在录音，false 未录音
 */
bool page_notes_is_recording(void);

/**
 * @brief 停止录音（供外部调用，如滑动离开时）
 */
void page_notes_stop_recording(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGES_PAGE_NOTES_H */
