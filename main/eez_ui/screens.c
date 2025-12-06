#include <string.h>
#include "esp_log.h"

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"
#include "NoteService.h"
#include "Wireless.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"  // 确保包含LVGL主头文件，spinner会自动包含
#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;

// 呼吸灯任务句柄
static TaskHandle_t breathing_light_task_handle = NULL;
static bool breathing_light_running = false;
static int original_width = 300;   // 按钮原始宽度
static int original_height = 300;  // 按钮原始高度
static int original_x = 30;        // 按钮原始X位置
static int original_y = 30;        // 按钮原始Y位置

// 呼吸灯任务：让按钮的 opacity 从 100 到 10 循环变化，同时缩放大小
static void breathing_light_task(void *pvParameters) {
    const int min_opacity = 10;   // 最小透明度（百分比）
    const int max_opacity = 100;  // 最大透明度（百分比）
    const float min_scale = 0.85f;  // 最小缩放比例（85%）
    const float max_scale = 1.0f;   // 最大缩放比例（100%）
    const int cycle_ms = 2000;    // 一个完整循环的时间（毫秒）
    const int update_interval_ms = 50;  // 更新间隔（毫秒）
    
    int direction = -1;  // -1 表示递减，1 表示递增
    float current_opacity = max_opacity;
    
    while (breathing_light_running) {
        // 更新透明度
        if (direction < 0) {
            current_opacity -= (float)(max_opacity - min_opacity) * update_interval_ms / cycle_ms;
            if (current_opacity <= min_opacity) {
                current_opacity = min_opacity;
                direction = 1;  // 改为递增
            }
        } else {
            current_opacity += (float)(max_opacity - min_opacity) * update_interval_ms / cycle_ms;
            if (current_opacity >= max_opacity) {
                current_opacity = max_opacity;
                direction = -1;  // 改为递减
            }
        }
        
        // 根据透明度计算缩放比例（透明度高时，大小也大）
        float opacity_ratio = current_opacity / 100.0f;  // 0.1 到 1.0
        float scale = min_scale + (max_scale - min_scale) * opacity_ratio;
        
        // 设置按钮透明度和大小
        if (objects.btn_notes_end != NULL) {
            // 设置透明度（LVGL 使用 0-255 范围，需要转换）
            uint8_t opa_value = (uint8_t)(current_opacity * 255 / 100);
            lv_obj_set_style_opa(objects.btn_notes_end, opa_value, LV_PART_MAIN | LV_STATE_DEFAULT);
            
            // 设置大小（根据缩放比例）
            int new_width = (int)(original_width * scale);
            int new_height = (int)(original_height * scale);
            lv_obj_set_size(objects.btn_notes_end, new_width, new_height);
            
            // 调整位置以保持按钮中心点不变
            int size_diff_x = original_width - new_width;
            int size_diff_y = original_height - new_height;
            int new_x = original_x + size_diff_x / 2;
            int new_y = original_y + size_diff_y / 2;
            lv_obj_set_pos(objects.btn_notes_end, new_x, new_y);
        }
        
        vTaskDelay(pdMS_TO_TICKS(update_interval_ms));
    }
    
    // 恢复原始透明度、大小和位置
    if (objects.btn_notes_end != NULL) {
        lv_obj_set_style_opa(objects.btn_notes_end, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_size(objects.btn_notes_end, original_width, original_height);
        lv_obj_set_pos(objects.btn_notes_end, original_x, original_y);
    }
    
    breathing_light_task_handle = NULL;
    vTaskDelete(NULL);
}

// 启动呼吸灯效果
static void start_breathing_light(void) {
    if (breathing_light_running) {
        return;  // 已经在运行
    }
    
    // 保存按钮的原始大小和位置
    if (objects.btn_notes_end != NULL) {
        original_width = lv_obj_get_width(objects.btn_notes_end);
        original_height = lv_obj_get_height(objects.btn_notes_end);
        original_x = lv_obj_get_x(objects.btn_notes_end);
        original_y = lv_obj_get_y(objects.btn_notes_end);
    }
    
    breathing_light_running = true;
    xTaskCreate(breathing_light_task, "breathing_light", 2048, NULL, 5, &breathing_light_task_handle);
}

// 停止呼吸灯效果
static void stop_breathing_light(void) {
    if (!breathing_light_running) {
        return;  // 未在运行
    }
    
    breathing_light_running = false;
    
    // 等待任务结束
    if (breathing_light_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void event_handler_cb_main_btn_notes(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_CLICKED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 0, 0, e);
    }
}

static void event_handler_cb_main_obj0(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_CLICKED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 3, 0, e);
    }
}

static void event_handler_cb_page_notes_page_notes(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_page_notes_btn_notes_start(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_CLICKED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 0, 0, e);
        start_note_recording();
        // 启动呼吸灯效果
        start_breathing_light();
    }
}

static void event_handler_cb_page_notes_btn_notes_end(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_CLICKED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 3, 0, e);
        stop_note_recording();
        // 停止呼吸灯效果
        stop_breathing_light();
    }
}

static void event_handler_cb_page_conf_page_conf(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_page_conf_btn_notes_start_1(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_CLICKED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 0, 0, e);
    }
}

void create_screen_loading() {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.loading = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_set_style_arc_color(obj, lv_color_hex(0xffc02537), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_spinner_create(parent_obj, 1000, 60);
            objects.obj1 = obj;
            lv_obj_set_pos(obj, 130, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xffc02537), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        {
            // lab_loading
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.lab_loading = obj;
            lv_obj_set_pos(obj, 55, 65);
            lv_obj_set_size(obj, 250, 50);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "连接中");
        }
    }
    
    tick_screen_loading();
}

void delete_screen_loading() {
    lv_obj_del(objects.loading);
    objects.loading = 0;
    objects.obj1 = 0;
    objects.lab_loading = 0;
    deletePageFlowState(0);
}

void tick_screen_loading() {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
}

void create_screen_main() {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    {
        lv_obj_t *parent_obj = obj;
        {
            // btn_notes
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_notes = obj;
            lv_obj_set_pos(obj, 50, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_add_event_cb(obj, event_handler_cb_main_btn_notes, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffc02537), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "会议");
                }
            }
        }
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj0 = obj;
            lv_obj_set_pos(obj, 208, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_add_event_cb(obj, event_handler_cb_main_obj0, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff2196f3), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "AI");
                }
            }
        }
    }
    
    tick_screen_main();
}

void delete_screen_main() {
    lv_obj_del(objects.main);
    objects.main = 0;
    objects.btn_notes = 0;
    objects.obj0 = 0;
    deletePageFlowState(1);
}

void tick_screen_main() {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
}

void create_screen_page_notes() {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.page_notes = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_add_event_cb(obj, event_handler_cb_page_notes_page_notes, LV_EVENT_ALL, flowState);
    {
        lv_obj_t *parent_obj = obj;
        {
            // btn_notes_start
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_notes_start = obj;
            lv_obj_set_pos(obj, 130, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_add_event_cb(obj, event_handler_cb_page_notes_btn_notes_start, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffc02537), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "开始");
                }
            }
        }
        {
            // btn_notes_end
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_notes_end = obj;
            lv_obj_set_pos(obj, 30, 30);
            lv_obj_set_size(obj, 300, 300);
            lv_obj_add_event_cb(obj, event_handler_cb_page_notes_btn_notes_end, LV_EVENT_ALL, flowState);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_radius(obj, 150, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff2196f3), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "结束");
                }
            }
        }
    }
    
    tick_screen_page_notes();
}

void delete_screen_page_notes() {
    lv_obj_del(objects.page_notes);
    objects.page_notes = 0;
    objects.btn_notes_start = 0;
    objects.btn_notes_end = 0;
    deletePageFlowState(2);
}

void tick_screen_page_notes() {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
}

void create_screen_page_conf() {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.page_conf = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_add_event_cb(obj, event_handler_cb_page_conf_page_conf, LV_EVENT_ALL, flowState);
    {
        lv_obj_t *parent_obj = obj;
        {
            // btn_notes_start_1
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_notes_start_1 = obj;
            lv_obj_set_pos(obj, 130, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_add_event_cb(obj, event_handler_cb_page_conf_btn_notes_start_1, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffc02537), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj2 = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
    
    tick_screen_page_conf();
}

void delete_screen_page_conf() {
    lv_obj_del(objects.page_conf);
    objects.page_conf = 0;
    objects.btn_notes_start_1 = 0;
    objects.obj2 = 0;
    deletePageFlowState(3);
}

void tick_screen_page_conf() {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
    {
        const char *new_val = evalTextProperty(flowState, 2, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.obj2);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.obj2;
            lv_label_set_text(objects.obj2, new_val);
            tick_value_change_obj = NULL;
        }
    }
}


static const char *screen_names[] = { "loading", "Main", "page_notes", "page_conf" };
static const char *object_names[] = { "loading", "main", "page_notes", "page_conf", "btn_notes", "obj0", "btn_notes_start", "btn_notes_end", "btn_notes_start_1", "obj1", "lab_loading", "obj2" };


typedef void (*create_screen_func_t)();
create_screen_func_t create_screen_funcs[] = {
    create_screen_loading,
    create_screen_main,
    create_screen_page_notes,
    create_screen_page_conf,
};
void create_screen(int screen_index) {
    create_screen_funcs[screen_index]();
}
void create_screen_by_id(enum ScreensEnum screenId) {
    create_screen_funcs[screenId - 1]();
}

typedef void (*delete_screen_func_t)();
delete_screen_func_t delete_screen_funcs[] = {
    delete_screen_loading,
    delete_screen_main,
    delete_screen_page_notes,
    delete_screen_page_conf,
};
void delete_screen(int screen_index) {
    delete_screen_funcs[screen_index]();
}
void delete_screen_by_id(enum ScreensEnum screenId) {
    delete_screen_funcs[screenId - 1]();
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_loading,
    tick_screen_main,
    tick_screen_page_notes,
    tick_screen_page_conf,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

void create_screens() {
    eez_flow_init_screen_names(screen_names, sizeof(screen_names) / sizeof(const char *));
    eez_flow_init_object_names(object_names, sizeof(object_names) / sizeof(const char *));
    
    eez_flow_set_create_screen_func(create_screen);
    eez_flow_set_delete_screen_func(delete_screen);
    
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_loading();
    create_screen_main();
    create_screen_page_notes();
    create_screen_page_conf();
}
