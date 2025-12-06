#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
  lv_obj_t *main;
  lv_obj_t *page_notes;
  lv_obj_t *page_conf;
  lv_obj_t *screen_loading; // 加载界面
  lv_obj_t *screen_error;   // 错误界面
  lv_obj_t *btn_notes;
  lv_obj_t *obj0;
  lv_obj_t *btn_notes_start;
  lv_obj_t *btn_notes_end;
  lv_obj_t *btn_notes_start_1;
  lv_obj_t *obj1;
  lv_obj_t *loading_spinner; // 加载动画
  lv_obj_t *loading_label;   // 加载文字
  lv_obj_t *error_label;     // 错误文字
} objects_t;

extern objects_t objects;

enum ScreensEnum {
  SCREEN_ID_LOADING = 0, // 加载界面
  SCREEN_ID_ERROR = 4,   // 错误界面
  SCREEN_ID_MAIN = 1,
  SCREEN_ID_PAGE_NOTES = 2,
  SCREEN_ID_PAGE_CONF = 3,
};

void create_screen_main();
void delete_screen_main();
void tick_screen_main();

void create_screen_page_notes();
void delete_screen_page_notes();
void tick_screen_page_notes();

void create_screen_page_conf();
void delete_screen_page_conf();
void tick_screen_page_conf();

void create_screen_loading();
void delete_screen_loading();
void tick_screen_loading();

void create_screen_error();
void delete_screen_error();
void tick_screen_error();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/