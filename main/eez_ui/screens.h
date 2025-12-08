/**
 * @file screens.h
 * @brief 屏幕全局管理头文件
 *
 * 定义：
 * - 全局 UI 对象结构体 (objects_t)
 * - 屏幕枚举 (ScreensEnum)
 * - 屏幕管理接口
 */

#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============== 全局 UI 对象 ==============

/**
 * @brief 全局 UI 对象结构体
 *
 * 包含所有屏幕和重要 UI 元素的指针
 */
typedef struct _objects_t {
  // 屏幕对象
  lv_obj_t *loading;    // Loading 屏幕（索引 0）
  lv_obj_t *main;       // 主屏幕（索引 1）
  lv_obj_t *page_notes; // 会议笔记屏幕（索引 2）
  lv_obj_t *page_ai;    // AI 对话屏幕（索引 3）

  // 主屏幕元素
  lv_obj_t *btn_notes; // "会议"按钮
  lv_obj_t *obj0;      // "AI"按钮

  // 会议笔记屏幕元素
  lv_obj_t *btn_notes_start; // "开始"按钮
  lv_obj_t *btn_notes_end;   // "结束"按钮

  // Loading 屏幕元素
  lv_obj_t *obj1;        // Spinner
  lv_obj_t *lab_loading; // 状态标签

  // AI 对话屏幕元素
  lv_obj_t *btn_ai_start; // AI 对话按钮
  lv_obj_t *obj2;              // AI 状态标签
} objects_t;

extern objects_t objects;

// tick 时值变化的对象（用于 eez-flow）
extern lv_obj_t *tick_value_change_obj;

// ============== 屏幕枚举 ==============

/**
 * @brief 屏幕 ID 枚举
 *
 * 注意：ID 从 1 开始，索引 = ID - 1
 */
enum ScreensEnum {
  SCREEN_ID_LOADING = 1,    // Loading 屏幕
  SCREEN_ID_MAIN = 2,       // 主屏幕
  SCREEN_ID_PAGE_NOTES = 3, // 会议笔记屏幕
  SCREEN_ID_PAGE_CONF = 4,  // AI 对话屏幕（保持原命名兼容）
};

// ============== 屏幕管理接口 ==============

// 各页面的创建/删除/tick 函数声明
// 实现在 pages/ 目录下的各个页面文件中
void create_screen_loading(void);
void delete_screen_loading(void);
void tick_screen_loading(void);

void create_screen_main(void);
void delete_screen_main(void);
void tick_screen_main(void);

void create_screen_page_notes(void);
void delete_screen_page_notes(void);
void tick_screen_page_notes(void);

void create_screen_page_ai(void);
void delete_screen_page_ai(void);
void tick_screen_page_ai(void);

// 为保持兼容性，添加 page_conf 的别名
#define create_screen_page_conf create_screen_page_ai
#define delete_screen_page_conf delete_screen_page_ai
#define tick_screen_page_conf tick_screen_page_ai

// 通用屏幕管理函数
void create_screen(int screen_index);
void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen(int screen_index);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);
void tick_screen_by_id(enum ScreensEnum screenId);

// 初始化所有屏幕
void create_screens(void);

#ifdef __cplusplus
}
#endif

#endif /* EEZ_LVGL_UI_SCREENS_H */
