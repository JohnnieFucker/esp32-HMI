/**
 * @file page_notes.c
 * @brief 会议笔记页面实现 - 录音功能
 */

#include "page_notes.h"
#include "breathing_light.h"
#include "../screens.h"
#include "../fonts.h"
#include "../eez-flow.h"
#include "note_service.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "PAGE_NOTES";

// 呼吸灯状态
static breathing_light_state_t notes_breathing = {0};

// 录音开始时间（微秒）
static int64_t recording_start_time_us = 0;

// ============== 录音控制 ==============

/**
 * @brief 启动录音
 */
static void notes_start_recording(void) {
    // 记录开始时间
    recording_start_time_us = esp_timer_get_time();
    ESP_LOGI(TAG, "开始录音，记录开始时间");
    
    start_note_recording();
    
    // 隐藏开始按钮，显示结束按钮
    if (objects.btn_notes_start) {
        lv_obj_add_flag(objects.btn_notes_start, LV_OBJ_FLAG_HIDDEN);
    }
    if (objects.btn_notes_end) {
        lv_obj_clear_flag(objects.btn_notes_end, LV_OBJ_FLAG_HIDDEN);
        breathing_light_start(&notes_breathing, objects.btn_notes_end);
    }
}

/**
 * @brief 停止录音
 */
static void notes_stop_recording_internal(void) {
    // 计算录音持续时间
    int64_t now_us = esp_timer_get_time();
    uint32_t duration_sec = 0;
    
    if (recording_start_time_us > 0) {
        duration_sec = (uint32_t)((now_us - recording_start_time_us) / 1000000);
        ESP_LOGI(TAG, "停止录音，持续时间: %lu 秒", (unsigned long)duration_sec);
    }
    
    // 重置开始时间
    recording_start_time_us = 0;
    
    // 停止录音（传入持续时间，由 note_service 判断是否提交数据）
    stop_note_recording(duration_sec);
    
    // 停止呼吸灯
    breathing_light_stop(&notes_breathing);
    
    // 隐藏结束按钮，显示开始按钮
    if (objects.btn_notes_end) {
        lv_obj_add_flag(objects.btn_notes_end, LV_OBJ_FLAG_HIDDEN);
    }
    if (objects.btn_notes_start) {
        lv_obj_clear_flag(objects.btn_notes_start, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============== 公开接口 ==============

bool page_notes_is_recording(void) {
    return breathing_light_is_running(&notes_breathing);
}

void page_notes_stop_recording(void) {
    if (page_notes_is_recording()) {
        notes_stop_recording_internal();
    }
}

// ============== 事件处理器 ==============

static void event_handler_cb_page_notes_page_notes(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        // 滑动时停止录音
        if (page_notes_is_recording()) {
            ESP_LOGI(TAG, "滑动离开，停止录音");
            notes_stop_recording_internal();
        }
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
        notes_start_recording();
    }
}

static void event_handler_cb_page_notes_btn_notes_end(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_CLICKED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 3, 0, e);
        notes_stop_recording_internal();
    }
}

// ============== 屏幕管理 ==============

void create_screen_page_notes(void) {
    ESP_LOGI(TAG, "开始创建 page_notes 屏幕...");
    
    void *flowState = getFlowState(0, 2);
    (void)flowState;
    
    lv_obj_t *obj = lv_obj_create(0);
    objects.page_notes = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_add_event_cb(obj, event_handler_cb_page_notes_page_notes, LV_EVENT_ALL, flowState);
    
    {
        lv_obj_t *parent_obj = obj;
        
        // btn_notes_start - 开始按钮
        {
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
        
        // btn_notes_end - 结束按钮（默认隐藏）
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_notes_end = obj;
            lv_obj_set_pos(obj, 30, 30);
            lv_obj_set_size(obj, 300, 300);
            lv_obj_add_event_cb(obj, event_handler_cb_page_notes_btn_notes_end, LV_EVENT_ALL, flowState);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_radius(obj, 150, LV_PART_MAIN | LV_STATE_DEFAULT);
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
                    lv_label_set_text(obj, "结束");
                }
            }
        }
    }
    
    tick_screen_page_notes();
    
    ESP_LOGI(TAG, "✓ page_notes 屏幕创建完成, obj=%p", (void*)objects.page_notes);
}

void delete_screen_page_notes(void) {
    // 确保停止录音
    if (page_notes_is_recording()) {
        notes_stop_recording_internal();
    }
    
    lv_obj_del(objects.page_notes);
    objects.page_notes = 0;
    objects.btn_notes_start = 0;
    objects.btn_notes_end = 0;
    deletePageFlowState(2);
}

void tick_screen_page_notes(void) {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
}

