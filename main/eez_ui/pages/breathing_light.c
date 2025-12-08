/**
 * @file breathing_light.c
 * @brief 通用呼吸灯效果模块实现
 */

#include "breathing_light.h"
#include "esp_log.h"

static const char *TAG = "BREATHING_LIGHT";

/**
 * @brief 通用呼吸灯动画回调
 */
static void breathing_anim_exec_cb(void *var, int32_t v) {
    lv_obj_t *obj = (lv_obj_t *)var;
    
    // 通过 user_data 获取状态（需要在启动时设置）
    breathing_light_state_t *state = (breathing_light_state_t *)lv_obj_get_user_data(obj);
    if (state == NULL) {
        return;
    }
    
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

void breathing_light_start(breathing_light_state_t *state, lv_obj_t *btn) {
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
    
    // 设置 user_data 用于动画回调
    lv_obj_set_user_data(btn, state);
    
    // 初始化动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn);
    
    // 设置动画参数：从300px到100px
    int32_t min_size = 100;
    int32_t max_size = 300;

    lv_anim_set_values(&a, max_size, min_size);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_playback_time(&a, 1000);
    lv_anim_set_playback_delay(&a, 0);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, breathing_anim_exec_cb);

    lv_anim_start(&a);
    state->running = true;
    
    ESP_LOGI(TAG, "呼吸灯启动: btn=%p, size=%d", btn, state->original_width);
}

void breathing_light_stop(breathing_light_state_t *state) {
    if (!state->running || state->btn == NULL) {
        return;
    }
    
    // 删除动画
    lv_anim_del(state->btn, breathing_anim_exec_cb);
    
    // 恢复原始状态
    lv_obj_set_size(state->btn, state->original_width, state->original_height);
    lv_obj_set_pos(state->btn, state->original_x, state->original_y);
    lv_obj_set_style_radius(state->btn, state->original_radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 清除 user_data
    lv_obj_set_user_data(state->btn, NULL);
    
    ESP_LOGI(TAG, "呼吸灯停止: btn=%p", state->btn);
    
    state->running = false;
    state->btn = NULL;
}

bool breathing_light_is_running(breathing_light_state_t *state) {
    return state != NULL && state->running;
}

