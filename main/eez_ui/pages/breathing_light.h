/**
 * @file breathing_light.h
 * @brief 通用呼吸灯效果模块
 *
 * 提供按钮的呼吸灯动画效果，支持多个独立的呼吸灯实例
 */

#ifndef PAGES_BREATHING_LIGHT_H
#define PAGES_BREATHING_LIGHT_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 呼吸灯状态结构体
 */
typedef struct {
  lv_obj_t *btn;       // 按钮对象
  bool running;        // 是否正在运行
  int original_width;  // 原始宽度
  int original_height; // 原始高度
  int original_x;      // 原始 X 位置
  int original_y;      // 原始 Y 位置
  int original_radius; // 原始圆角
} breathing_light_state_t;

/**
 * @brief 启动按钮呼吸灯效果
 * @param state 呼吸灯状态结构体
 * @param btn 目标按钮
 */
void breathing_light_start(breathing_light_state_t *state, lv_obj_t *btn);

/**
 * @brief 停止按钮呼吸灯效果
 * @param state 呼吸灯状态结构体
 */
void breathing_light_stop(breathing_light_state_t *state);

/**
 * @brief 检查呼吸灯是否正在运行
 * @param state 呼吸灯状态结构体
 * @return true 正在运行，false 未运行
 */
bool breathing_light_is_running(breathing_light_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PAGES_BREATHING_LIGHT_H */
