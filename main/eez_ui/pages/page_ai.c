/**
 * @file page_ai.c
 * @brief AI 对话页面实现 - 语音 AI 交互
 */

#include "page_ai.h"
#include "breathing_light.h"
#include "../screens.h"
#include "../fonts.h"
#include "../eez-flow.h"
#include "ai_service.h"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "PAGE_AI";

// 呼吸灯状态
static breathing_light_state_t ai_breathing = {0};

// AI 超时定时器
static TimerHandle_t ai_timeout_timer = NULL;

// AI 按钮原始文本（保存进入 AI 模式前的文本，用于退出时恢复）
char ai_btn_original_text[32] = "开始";

// ============== AI 服务控制 ==============

/**
 * @brief 停止 AI 服务并清理资源（不切换界面）
 */
static void stop_ai_service_cleanup(void) {
    ESP_LOGI(TAG, "停止 AI 服务");
    
    // 停止 AI 服务
    cg_ai_service_stop();
    
    // 停止呼吸灯
    breathing_light_stop(&ai_breathing);
    
    // 恢复按钮原始文本
    if (objects.obj2 && ai_btn_original_text[0] != '\0') {
        lv_label_set_text(objects.obj2, ai_btn_original_text);
        ESP_LOGI(TAG, "恢复按钮文本: %s", ai_btn_original_text);
    }
    
    // 停止超时定时器
    if (ai_timeout_timer != NULL) {
        xTimerStop(ai_timeout_timer, 0);
        xTimerDelete(ai_timeout_timer, 0);
        ai_timeout_timer = NULL;
    }
}

/**
 * @brief 停止 AI 服务并返回主界面
 */
static void stop_ai_and_return_main(void) {
    stop_ai_service_cleanup();
    // 切换到主界面
    lv_scr_load(objects.main);
}

/**
 * @brief AI 状态变化回调
 */
static void ai_state_callback(cg_ai_state_t new_state, void *user_data) {
    ESP_LOGI(TAG, "AI 状态变化: %d", new_state);
    
    // 根据状态控制呼吸灯
    // 只有 SENDING 和 SPEAKING 状态才有呼吸灯效果
    if (new_state == CG_AI_STATE_SENDING || new_state == CG_AI_STATE_SPEAKING) {
        // 启动呼吸灯（如果尚未启动）
        if (!breathing_light_is_running(&ai_breathing) && objects.btn_ai_start) {
            breathing_light_start(&ai_breathing, objects.btn_ai_start);
            ESP_LOGI(TAG, "进入 %s 状态，启动呼吸灯", 
                     new_state == CG_AI_STATE_SENDING ? "SENDING" : "SPEAKING");
        }
    } else {
        // 其他状态停止呼吸灯
        if (breathing_light_is_running(&ai_breathing)) {
            breathing_light_stop(&ai_breathing);
            ESP_LOGI(TAG, "离开 SENDING/SPEAKING 状态，停止呼吸灯");
        }
    }
    
    // 更新按钮文本
    if (objects.obj2) {
        switch (new_state) {
            case CG_AI_STATE_IDLE:
            case CG_AI_STATE_ERROR:
                lv_label_set_text(objects.obj2, "开始");
                break;
            case CG_AI_STATE_CONNECTING:
            case CG_AI_STATE_CONNECTED:
                lv_label_set_text(objects.obj2, "连接中");
                break;
            case CG_AI_STATE_LISTENING:
                lv_label_set_text(objects.obj2, "聆听中");
                break;
            case CG_AI_STATE_SENDING:
                lv_label_set_text(objects.obj2, "发送中");
                break;
            case CG_AI_STATE_SPEAKING:
                lv_label_set_text(objects.obj2, "播放中");
                break;
            default:
                break;
        }
    }
}

/**
 * @brief AI 超时定时器回调
 */
static void ai_timeout_timer_callback(TimerHandle_t xTimer) {
    if (cg_ai_service_is_timeout(CLOSE_CONNECTION_NO_VOICE_TIME)) {
        ESP_LOGW(TAG, "AI 服务超时，自动退出");
        stop_ai_and_return_main();
    }
}

// ============== 公开接口 ==============

bool page_ai_is_active(void) {
    return cg_ai_service_is_active();
}

void page_ai_stop_service(void) {
    if (page_ai_is_active()) {
        stop_ai_service_cleanup();
    }
}

// ============== 事件处理器 ==============

static void event_handler_cb_page_ai(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        // 滑动时停止 AI 服务
        if (cg_ai_service_is_active()) {
            ESP_LOGI(TAG, "滑动离开，停止 AI 服务");
            stop_ai_service_cleanup();
        }
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_page_ai_btn_ai_start(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    (void)e;
    
    if (event == LV_EVENT_CLICKED) {
        // 检查 AI 服务是否正在运行
        if (!cg_ai_service_is_active()) {
            // === 启动 AI 服务 ===
            ESP_LOGI(TAG, "AI 按钮点击 - 启动服务");
            
            // 保存当前按钮文本
            if (objects.obj2) {
                const char *current_text = lv_label_get_text(objects.obj2);
                if (current_text && strcmp(current_text, "开始") != 0) {
                    strncpy(ai_btn_original_text, current_text, sizeof(ai_btn_original_text) - 1);
                    ai_btn_original_text[sizeof(ai_btn_original_text) - 1] = '\0';
                }
            }
            
            // 初始化 AI 服务
            esp_err_t ret = cg_ai_service_init();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "AI 服务初始化失败");
                return;
            }
            
            // 设置状态回调
            cg_ai_service_set_state_callback(ai_state_callback, NULL);
            
            // 更新按钮文本为"连接中"
            if (objects.obj2) {
                lv_label_set_text(objects.obj2, "连接中");
            }
            
            // 启动 AI 服务
            ret = cg_ai_service_start();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "AI 服务启动失败");
                cg_ai_service_deinit();
                // 恢复按钮文本
                if (objects.obj2 && ai_btn_original_text[0] != '\0') {
                    lv_label_set_text(objects.obj2, ai_btn_original_text);
                }
                return;
            }
            
            // 启动超时检测定时器
            if (ai_timeout_timer == NULL) {
                ai_timeout_timer = xTimerCreate("ai_timeout", pdMS_TO_TICKS(1000), pdTRUE, 
                                                NULL, ai_timeout_timer_callback);
            }
            if (ai_timeout_timer != NULL) {
                xTimerStart(ai_timeout_timer, 0);
            }
            
            ESP_LOGI(TAG, "AI 服务已启动");
        } else {
            // === 停止 AI 服务 ===
            ESP_LOGI(TAG, "AI 按钮点击 - 停止服务");
            stop_ai_and_return_main();
        }
    }
}

// ============== 屏幕管理 ==============

void create_screen_page_ai(void) {
    ESP_LOGI(TAG, "开始创建 page_ai 屏幕...");
    
    void *flowState = getFlowState(0, 3);
    (void)flowState;
    
    lv_obj_t *obj = lv_obj_create(0);
    objects.page_ai = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_add_event_cb(obj, event_handler_cb_page_ai, LV_EVENT_ALL, flowState);
    
    {
        lv_obj_t *parent_obj = obj;
        
        // btn_ai_start - AI 对话按钮
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_ai_start = obj;
            lv_obj_set_pos(obj, 130, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_add_event_cb(obj, event_handler_cb_page_ai_btn_ai_start, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff2196f3), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj2 = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "开始");
                }
            }
        }
    }
    
    tick_screen_page_ai();
    
    ESP_LOGI(TAG, "✓ page_ai 屏幕创建完成, obj=%p", (void*)objects.page_ai);
}

void delete_screen_page_ai(void) {
    // 清理 AI 相关资源
    if (cg_ai_service_is_active()) {
        stop_ai_service_cleanup();
    }
    
    lv_obj_del(objects.page_ai);
    objects.page_ai = 0;
    objects.btn_ai_start = 0;
    objects.obj2 = 0;
    deletePageFlowState(3);
}

void tick_screen_page_ai(void) {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
    // 标签文本由 ai_state_callback() 动态管理，不使用 eez-flow 数据绑定
}

