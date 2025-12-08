/**
 * @file page_loading.c
 * @brief Loading 页面实现 - WiFi 连接等待界面
 */

#include "page_loading.h"
#include "../screens.h"
#include "../fonts.h"
#include "../eez-flow.h"
#include "esp_log.h"

static const char *TAG = "PAGE_LOADING";

void create_screen_loading(void) {
    ESP_LOGI(TAG, "开始创建 Loading 屏幕...");
    
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    
    // 创建屏幕根对象
    lv_obj_t *obj = lv_obj_create(0);
    objects.loading = obj;
    
    // 设置屏幕位置和大小
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_set_style_arc_color(obj, lv_color_hex(0xffc02537), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    
    {
        lv_obj_t *parent_obj = obj;
        
        // 创建旋转的 spinner（加载动画）
        {
            lv_obj_t *obj = lv_spinner_create(parent_obj, 1000, 60);
            objects.obj1 = obj;
            lv_obj_set_pos(obj, 130, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xffc02537), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        
        // 创建状态标签
        {
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
    
    ESP_LOGI(TAG, "✓ Loading 屏幕创建完成, obj=%p", (void*)objects.loading);
}

void delete_screen_loading(void) {
    lv_obj_del(objects.loading);
    objects.loading = 0;
    objects.obj1 = 0;
    objects.lab_loading = 0;
    deletePageFlowState(0);
}

void tick_screen_loading(void) {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
}

