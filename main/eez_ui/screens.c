#include <string.h>
#include "esp_log.h"

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"
#include "note_service.h"
#include "ai_service.h"
#include "wifi_service.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lvgl.h"  // 确保包含LVGL主头文件，spinner会自动包含
#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;

// ============== 通用呼吸灯效果 ==============
// 呼吸灯状态结构体
typedef struct {
    lv_obj_t *btn;           // 按钮对象
    bool running;            // 是否正在运行
    int original_width;      // 原始宽度
    int original_height;     // 原始高度
    int original_x;          // 原始 X 位置
    int original_y;          // 原始 Y 位置
    int original_radius;     // 原始圆角
} breathing_light_state_t;

// page_notes 呼吸灯状态
static breathing_light_state_t notes_breathing = {0};

// page_conf AI 呼吸灯状态  
static breathing_light_state_t ai_breathing = {0};

// AI 超时定时器
static TimerHandle_t ai_timeout_timer = NULL;

// 前向声明
static void stop_ai_service_cleanup(void);

// 通用呼吸灯动画回调
static void breathing_anim_exec_cb(void *var, int32_t v) {
    lv_obj_t *obj = (lv_obj_t *)var;
    
    // 找到对应的状态
    breathing_light_state_t *state = NULL;
    if (obj == notes_breathing.btn) {
        state = &notes_breathing;
    } else if (obj == ai_breathing.btn) {
        state = &ai_breathing;
    }
    
    if (state == NULL) return;
    
    int new_size = v;
    
    // 计算居中位置
    int center_x = state->original_x + state->original_width / 2;
    int center_y = state->original_y + state->original_height / 2;
    
    int new_x = center_x - new_size / 2;
    int new_y = center_y - new_size / 2;
    
    lv_obj_set_size(obj, new_size, new_size);
    lv_obj_set_pos(obj, new_x, new_y);
    lv_obj_set_style_radius(obj, new_size / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * 启动按钮呼吸灯效果
 * @param state 呼吸灯状态结构体
 * @param btn 目标按钮
 */
static void breathing_light_start(breathing_light_state_t *state, lv_obj_t *btn) {
    if (state->running || btn == NULL) {
        return;
    }
    
    // 保存原始信息
    state->btn = btn;
    state->original_width = lv_obj_get_width(btn);
    state->original_height = lv_obj_get_height(btn);
    state->original_x = lv_obj_get_x(btn);
    state->original_y = lv_obj_get_y(btn);
    state->original_radius = lv_obj_get_style_radius(btn, LV_PART_MAIN);
    
    // 初始化动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn);
    
    // 设置动画参数：从原始大小的 85% 到 100%
    int32_t min_size = (int32_t)(state->original_width * 0.85f);
    int32_t max_size = state->original_width;
    
    lv_anim_set_values(&a, max_size, min_size);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_playback_time(&a, 1000);
    lv_anim_set_playback_delay(&a, 0);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, breathing_anim_exec_cb);
    
    lv_anim_start(&a);
    state->running = true;
    
    ESP_LOGI("SCREENS", "呼吸灯启动: btn=%p, size=%d", btn, state->original_width);
}

/**
 * 停止按钮呼吸灯效果
 * @param state 呼吸灯状态结构体
 */
static void breathing_light_stop(breathing_light_state_t *state) {
    if (!state->running || state->btn == NULL) {
        return;
    }
    
    // 删除动画
    lv_anim_del(state->btn, breathing_anim_exec_cb);
    
    // 恢复原始状态
    lv_obj_set_size(state->btn, state->original_width, state->original_height);
    lv_obj_set_pos(state->btn, state->original_x, state->original_y);
    lv_obj_set_style_radius(state->btn, state->original_radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    ESP_LOGI("SCREENS", "呼吸灯停止: btn=%p", state->btn);
    
    state->running = false;
    state->btn = NULL;
}

// ============== page_notes 录音控制 ==============

// 启动录音（显示结束按钮、隐藏开始按钮、启动呼吸灯）
static void notes_start_recording(void) {
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

// 停止录音（停止呼吸灯、隐藏结束按钮、显示开始按钮）
static void notes_stop_recording(void) {
    stop_note_recording();
    
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

// ============== page_conf AI 服务控制 ==============

// AI 状态变化回调（从 CgAiService 调用）
static void ai_state_callback(cg_ai_state_t new_state, void *user_data) {
    ESP_LOGI("SCREENS", "AI 状态变化: %d", new_state);
    // 可以在这里更新 UI 显示 AI 状态
}

// 停止 AI 服务并清理资源（不切换界面）
static void stop_ai_service_cleanup(void) {
    ESP_LOGI("SCREENS", "停止 AI 服务");
    
    // 停止 AI 服务
    cg_ai_service_stop();
    
    // 停止呼吸灯
    breathing_light_stop(&ai_breathing);
    
    // 停止超时定时器
    if (ai_timeout_timer != NULL) {
        xTimerStop(ai_timeout_timer, 0);
        xTimerDelete(ai_timeout_timer, 0);
        ai_timeout_timer = NULL;
    }
}

// 停止 AI 服务并返回主界面
static void stop_ai_and_return_main(void) {
    stop_ai_service_cleanup();
    // 切换到主界面
    lv_scr_load(objects.main);
}

// AI 超时定时器回调
static void ai_timeout_timer_callback(TimerHandle_t xTimer) {
    // 检查是否超时
    if (cg_ai_service_is_timeout(CLOSE_CONNECTION_NO_VOICE_TIME)) {
        ESP_LOGW("SCREENS", "AI 服务超时，自动退出");
        stop_ai_and_return_main();
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
        // 滑动时停止录音（效果等同于点击结束按钮）
        if (notes_breathing.running) {
            ESP_LOGI("SCREENS", "滑动离开 page_notes，停止录音");
            notes_stop_recording();
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
        notes_stop_recording();
    }
}

static void event_handler_cb_page_conf_page_conf(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        // 滑动时停止 AI 服务（先结束再切换）
        if (cg_ai_service_is_active()) {
            ESP_LOGI("SCREENS", "滑动离开 page_conf，停止 AI 服务");
            stop_ai_service_cleanup();
        }
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_page_conf_btn_notes_start_1(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    (void)e;
    
    if (event == LV_EVENT_CLICKED) {
        // 检查 AI 服务是否正在运行
        if (!cg_ai_service_is_active()) {
            // === 启动 AI 服务 ===
            ESP_LOGI("SCREENS", "AI 按钮点击 - 启动服务");
            
            // 初始化 AI 服务
            esp_err_t ret = cg_ai_service_init();
            if (ret != ESP_OK) {
                ESP_LOGE("SCREENS", "AI 服务初始化失败");
                return;
            }
            
            // 设置状态回调
            cg_ai_service_set_state_callback(ai_state_callback, NULL);
            
            // 启动 AI 服务
            ret = cg_ai_service_start();
            if (ret != ESP_OK) {
                ESP_LOGE("SCREENS", "AI 服务启动失败");
                cg_ai_service_deinit();
                return;
            }
            
            // 启动呼吸灯效果
            breathing_light_start(&ai_breathing, objects.btn_notes_start_1);
            
            // 启动超时检测定时器
            if (ai_timeout_timer == NULL) {
                ai_timeout_timer = xTimerCreate("ai_timeout", pdMS_TO_TICKS(1000), pdTRUE, 
                                                NULL, ai_timeout_timer_callback);
            }
            if (ai_timeout_timer != NULL) {
                xTimerStart(ai_timeout_timer, 0);
            }
            
            ESP_LOGI("SCREENS", "AI 服务已启动");
        } else {
            // === 停止 AI 服务 ===
            ESP_LOGI("SCREENS", "AI 按钮点击 - 停止服务");
            stop_ai_and_return_main();
        }
    }
}

// ============================================================================
// 创建Loading屏幕（屏幕ID=1，索引=0）
// ============================================================================
// 功能：创建WiFi连接等待界面
// 调用时机：
//   1. 在create_screens()中自动创建（ui_init时）
//   2. 如果屏幕被删除，在切换到此屏幕时会自动重新创建
// 屏幕内容：
//   - 一个旋转的spinner（加载动画）
//   - 一个标签显示"连接中"或WiFi状态信息
// 重要说明：
//   - objects.loading 是屏幕根对象，会被注册到objects数组的索引0
//   - objects.lab_loading 是状态标签，用于显示WiFi连接状态
void create_screen_loading() {
    ESP_LOGI("SCREENS", "[create_screen_loading] 开始创建Loading屏幕...");
    
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    
    // 创建屏幕根对象（父对象为NULL，表示这是屏幕对象）
    lv_obj_t *obj = lv_obj_create(0);
    objects.loading = obj;  // 保存到objects数组索引0
    
    // 设置屏幕位置和大小
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    lv_obj_set_style_arc_color(obj, lv_color_hex(0xffc02537), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    
    {
        lv_obj_t *parent_obj = obj;
        
        // 创建旋转的spinner（加载动画）
        {
            lv_obj_t *obj = lv_spinner_create(parent_obj, 1000, 60);
            objects.obj1 = obj;  // 保存到objects数组索引9
            lv_obj_set_pos(obj, 130, 130);
            lv_obj_set_size(obj, 100, 100);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xffc02537), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        
        // 创建状态标签（显示"连接中"或WiFi状态信息）
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.lab_loading = obj;  // 保存到objects数组索引10
            lv_obj_set_pos(obj, 55, 65);
            lv_obj_set_size(obj, 250, 50);
            lv_obj_set_style_text_font(obj, &ui_font_chinese_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "连接中");
        }
    }
    
    // 调用屏幕的tick函数（初始化屏幕状态）
    tick_screen_loading();
    
    ESP_LOGI("SCREENS", "[create_screen_loading] ✓ Loading屏幕创建完成");
    ESP_LOGI("SCREENS", "[create_screen_loading] 屏幕对象指针: %p", (void*)objects.loading);
    ESP_LOGI("SCREENS", "[create_screen_loading] 标签对象指针: %p", (void*)objects.lab_loading);
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

// ============================================================================
// 创建Main屏幕（屏幕ID=2，索引=1）
// ============================================================================
// 功能：创建主界面（WiFi连接成功后应该切换到此屏幕）
// 调用时机：
//   1. 在create_screens()中自动创建（ui_init时）
//   2. 如果屏幕被删除，在切换到此屏幕时会自动重新创建
// 屏幕内容：
//   - "会议"按钮（红色，圆形）
//   - "AI"按钮（蓝色，圆形）
// 重要说明：
//   - objects.main 是屏幕根对象，会被注册到objects数组的索引1
//   - 这是WiFi连接成功后应该切换到的目标屏幕
//   - 如果此屏幕未正确创建，屏幕切换会失败
void create_screen_main() {
    ESP_LOGI("SCREENS", "[create_screen_main] 开始创建Main屏幕...");
    
    void *flowState = getFlowState(0, 1);
    (void)flowState;
    
    // 创建屏幕根对象（父对象为NULL，表示这是屏幕对象）
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;  // 保存到objects数组索引1（重要！这是屏幕切换时查找的对象）
    
    // 设置屏幕位置和大小
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    
    {
        lv_obj_t *parent_obj = obj;
        
        // 创建"会议"按钮（红色，圆形）
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.btn_notes = obj;  // 保存到objects数组索引4
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
            objects.obj0 = obj;  // 保存到objects数组索引5
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
    
    // 调用屏幕的tick函数（初始化屏幕状态）
    tick_screen_main();
    
    ESP_LOGI("SCREENS", "[create_screen_main] ✓ Main屏幕创建完成");
    ESP_LOGI("SCREENS", "[create_screen_main] 屏幕对象指针: %p", (void*)objects.main);
    ESP_LOGI("SCREENS", "[create_screen_main] 这是WiFi连接成功后应该切换到的目标屏幕");
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
            lv_obj_add_event_cb(obj, event_handler_cb_page_conf_btn_notes_start_1, LV_EVENT_ALL, flowState);  // 添加事件处理器
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
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
    
    tick_screen_page_conf();
}

void delete_screen_page_conf() {
    // 清理 AI 相关资源
    if (cg_ai_service_is_active()) {
        stop_ai_service_cleanup();
    }
    
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

// ============================================================================
// 创建所有屏幕（在ui_init中由eez_flow_init调用）
// ============================================================================
// 功能：
//   1. 初始化屏幕名称和对象名称映射
//   2. 注册屏幕创建和删除函数
//   3. 设置LVGL主题
//   4. 创建所有屏幕对象（但不加载显示）
//
// 重要说明：
//   - 此函数在eez_flow_init()中自动调用
//   - 所有屏幕都会在此函数中创建，但只有loading屏幕会被默认加载显示
//   - 屏幕对象会被保存到objects结构体中，并注册到objects数组
//   - 屏幕索引映射：
//     * screen_names[0] = "loading" -> SCREEN_ID_LOADING = 1 -> objects.loading (索引0)
//     * screen_names[1] = "Main"    -> SCREEN_ID_MAIN = 2     -> objects.main (索引1)
//     * screen_names[2] = "page_notes" -> SCREEN_ID_PAGE_NOTES = 3 -> objects.page_notes (索引2)
//     * screen_names[3] = "page_conf"   -> SCREEN_ID_PAGE_CONF = 4  -> objects.page_conf (索引3)
//   - 对象索引映射（object_names数组）：
//     * [0] = "loading" -> objects.loading
//     * [1] = "main"     -> objects.main (重要！屏幕切换时通过此索引查找)
//     * [2] = "page_notes" -> objects.page_notes
//     * [3] = "page_conf"  -> objects.page_conf
//     * [4] = "btn_notes" -> objects.btn_notes
//     * ... 等等
void create_screens() {
    ESP_LOGI("SCREENS", "[create_screens] ========================================");
    ESP_LOGI("SCREENS", "[create_screens] 开始创建所有屏幕...");
    
    // 初始化屏幕名称映射（用于通过名称查找屏幕ID）
    // screen_names: ["loading", "Main", "page_notes", "page_conf"]
    eez_flow_init_screen_names(screen_names, sizeof(screen_names) / sizeof(const char *));
    ESP_LOGI("SCREENS", "[create_screens] 屏幕名称已注册: %zu 个", sizeof(screen_names) / sizeof(const char *));
    
    // 初始化对象名称映射（用于通过名称查找对象索引）
    // object_names: ["loading", "main", "page_notes", "page_conf", "btn_notes", ...]
    eez_flow_init_object_names(object_names, sizeof(object_names) / sizeof(const char *));
    ESP_LOGI("SCREENS", "[create_screens] 对象名称已注册: %zu 个", sizeof(object_names) / sizeof(const char *));
    
    // 注册屏幕创建和删除函数
    // 当需要切换到一个未创建的屏幕时，eez_flow会自动调用create_screen函数
    eez_flow_set_create_screen_func(create_screen);
    eez_flow_set_delete_screen_func(delete_screen);
    ESP_LOGI("SCREENS", "[create_screens] 屏幕创建/删除函数已注册");
    
    // 设置LVGL主题
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    ESP_LOGI("SCREENS", "[create_screens] LVGL主题已设置");
    
    // 创建所有屏幕对象
    // 注意：这里只是创建屏幕对象，但不会加载显示（只有loading屏幕会被默认加载）
    ESP_LOGI("SCREENS", "[create_screens] 开始创建各个屏幕对象...");
    
    create_screen_loading();  // 屏幕ID=1, 索引=0, objects.loading
    create_screen_main();     // 屏幕ID=2, 索引=1, objects.main (WiFi连接后切换的目标)
    create_screen_page_notes(); // 屏幕ID=3, 索引=2, objects.page_notes
    create_screen_page_conf();  // 屏幕ID=4, 索引=3, objects.page_conf
    
    ESP_LOGI("SCREENS", "[create_screens] ✓ 所有屏幕创建完成");
    ESP_LOGI("SCREENS", "[create_screens] 屏幕对象验证:");
    ESP_LOGI("SCREENS", "[create_screens]   - loading: %p", (void*)objects.loading);
    ESP_LOGI("SCREENS", "[create_screens]   - main:    %p (WiFi连接后切换目标)", (void*)objects.main);
    ESP_LOGI("SCREENS", "[create_screens]   - page_notes: %p", (void*)objects.page_notes);
    ESP_LOGI("SCREENS", "[create_screens]   - page_conf:  %p", (void*)objects.page_conf);
    ESP_LOGI("SCREENS", "[create_screens] ========================================");
}
