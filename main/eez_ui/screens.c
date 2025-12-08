/**
 * @file screens.c
 * @brief 屏幕全局管理模块
 * 
 * 职责：
 * - 管理所有屏幕对象（objects_t）
 * - 屏幕创建/删除/tick 函数的注册和调度
 * - LVGL 主题设置
 * - 屏幕名称和对象名称映射
 * 
 * 各页面的具体逻辑已拆分到 pages/ 目录下：
 * - pages/page_loading.c  - Loading 页面
 * - pages/page_main.c     - 主页面
 * - pages/page_notes.c    - 会议笔记页面
 * - pages/page_ai.c       - AI 对话页面
 * - pages/breathing_light.c - 呼吸灯通用模块
 */

#include "esp_log.h"
#include "lvgl.h"

#include "screens.h"
#include "eez-flow.h"

// 包含各页面头文件
#include "pages/page_loading.h"
#include "pages/page_main.h"
#include "pages/page_notes.h"
#include "pages/page_ai.h"

static const char *TAG = "SCREENS";

// ============== 全局对象 ==============

objects_t objects;
lv_obj_t *tick_value_change_obj;


// ============== 屏幕名称映射 ==============

static const char *screen_names[] = { 
    "loading", 
    "Main", 
    "page_notes", 
    "page_ai" 
};

static const char *object_names[] = { 
    "loading", 
    "main", 
    "page_notes", 
    "page_ai", 
    "btn_notes", 
    "obj0", 
    "btn_notes_start", 
    "btn_notes_end", 
    "btn_ai_start", 
    "obj1", 
    "lab_loading", 
    "obj2" 
};

// ============== 屏幕函数数组 ==============

typedef void (*create_screen_func_t)(void);
static create_screen_func_t create_screen_funcs[] = {
    create_screen_loading,
    create_screen_main,
    create_screen_page_notes,
    create_screen_page_ai,
};

typedef void (*delete_screen_func_t)(void);
static delete_screen_func_t delete_screen_funcs[] = {
    delete_screen_loading,
    delete_screen_main,
    delete_screen_page_notes,
    delete_screen_page_ai,
};

typedef void (*tick_screen_func_t)(void);
static tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_loading,
    tick_screen_main,
    tick_screen_page_notes,
    tick_screen_page_ai,
};

// ============== 屏幕管理接口 ==============

void create_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < (int)(sizeof(create_screen_funcs) / sizeof(create_screen_funcs[0]))) {
        create_screen_funcs[screen_index]();
    }
}

void create_screen_by_id(enum ScreensEnum screenId) {
    create_screen(screenId - 1);
}

void delete_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < (int)(sizeof(delete_screen_funcs) / sizeof(delete_screen_funcs[0]))) {
        delete_screen_funcs[screen_index]();
    }
}

void delete_screen_by_id(enum ScreensEnum screenId) {
    delete_screen(screenId - 1);
}

void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < (int)(sizeof(tick_screen_funcs) / sizeof(tick_screen_funcs[0]))) {
    tick_screen_funcs[screen_index]();
}
}

void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

// ============== 初始化所有屏幕 ==============

/**
 * @brief 创建所有屏幕
 * 
 * 在 ui_init 中由 eez_flow_init 调用
 * 
 * 功能：
 * 1. 初始化屏幕名称和对象名称映射
 * 2. 注册屏幕创建和删除函数
 * 3. 设置 LVGL 主题
 * 4. 创建所有屏幕对象（但不加载显示）
 * 
 * 屏幕索引映射：
 * - screen_names[0] = "loading"    -> SCREEN_ID_LOADING = 1
 * - screen_names[1] = "Main"       -> SCREEN_ID_MAIN = 2
 * - screen_names[2] = "page_notes" -> SCREEN_ID_PAGE_NOTES = 3
 * - screen_names[3] = "page_ai"    -> SCREEN_ID_PAGE_CONF = 4
 */
void create_screens(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始创建所有屏幕...");
    
    // 初始化屏幕名称映射
    eez_flow_init_screen_names(screen_names, sizeof(screen_names) / sizeof(const char *));
    ESP_LOGI(TAG, "屏幕名称已注册: %zu 个", sizeof(screen_names) / sizeof(const char *));
    
    // 初始化对象名称映射
    eez_flow_init_object_names(object_names, sizeof(object_names) / sizeof(const char *));
    ESP_LOGI(TAG, "对象名称已注册: %zu 个", sizeof(object_names) / sizeof(const char *));
    
    // 注册屏幕创建和删除函数
    eez_flow_set_create_screen_func(create_screen);
    eez_flow_set_delete_screen_func(delete_screen);
    ESP_LOGI(TAG, "屏幕创建/删除函数已注册");
    
    // 设置 LVGL 主题
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(
        dispp, 
        lv_palette_main(LV_PALETTE_BLUE), 
        lv_palette_main(LV_PALETTE_RED), 
        true, 
        LV_FONT_DEFAULT
    );
    lv_disp_set_theme(dispp, theme);
    ESP_LOGI(TAG, "LVGL 主题已设置");
    
    // 创建所有屏幕对象
    ESP_LOGI(TAG, "开始创建各个屏幕对象...");
    
    create_screen_loading();    // 屏幕ID=1, 索引=0
    create_screen_main();       // 屏幕ID=2, 索引=1
    create_screen_page_notes(); // 屏幕ID=3, 索引=2
    create_screen_page_ai();    // 屏幕ID=4, 索引=3
    
    ESP_LOGI(TAG, "✓ 所有屏幕创建完成");
    ESP_LOGI(TAG, "屏幕对象验证:");
    ESP_LOGI(TAG, "  - loading:    %p", (void*)objects.loading);
    ESP_LOGI(TAG, "  - main:       %p (WiFi 连接后切换目标)", (void*)objects.main);
    ESP_LOGI(TAG, "  - page_notes: %p", (void*)objects.page_notes);
    ESP_LOGI(TAG, "  - page_ai:    %p", (void*)objects.page_ai);
    ESP_LOGI(TAG, "========================================");
}
