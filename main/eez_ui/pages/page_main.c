/**
 * @file page_main.c
 * @brief 主界面页面实现
 */

#include "page_main.h"
#include "../screens.h"
#include "../fonts.h"
#include "../eez-flow.h"
#include "esp_log.h"

static const char *TAG = "PAGE_MAIN";

// ============== 事件处理器 ==============

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

// ============== 屏幕管理 ==============

void create_screen_main(void) {
    ESP_LOGI(TAG, "开始创建 Main 屏幕...");
    
    void *flowState = getFlowState(0, 1);
    (void)flowState;
    
    // 创建屏幕根对象
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    
    // 设置屏幕位置和大小
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    
    {
        lv_obj_t *parent_obj = obj;
        
        // 创建"会议"按钮（红色，圆形）
        {
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
        
        // 创建"AI"按钮（蓝色，圆形）
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
    
    ESP_LOGI(TAG, "✓ Main 屏幕创建完成, obj=%p", (void*)objects.main);
}

void delete_screen_main(void) {
    lv_obj_del(objects.main);
    objects.main = 0;
    objects.btn_notes = 0;
    objects.obj0 = 0;
    deletePageFlowState(1);
}

void tick_screen_main(void) {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
}

