/**
 * @file main.cpp
 * @brief ESP32-HMI 主程序入口
 *
 * 系统启动流程：
 * 1. 初始化基础驱动（I2C、GPIO扩展、RTC、电池）
 * 2. 初始化显示系统（LCD、背光、触摸）
 * 3. 初始化音频系统（I2S、音频播放器）
 * 4. 初始化 LVGL 图形库和 UI
 * 5. 进入主循环处理 UI 事件
 */

#include "bat_driver.h"
#include "lvgl.h"
#include "lvgl_driver.h"
#include "pcf85063.h"
#include "pcm5101.h"
#include "st77916.h"
#include "tca9554.h"
#include "ui.h"
#include "utils.h"
#include "wifi_service.h"

#include "esp_heap_caps.h"

// ============================================================================
// 任务函数
// ============================================================================

/**
 * @brief 后台驱动任务
 *
 * 负责处理：
 * - WiFi 连接管理
 * - RTC 时间更新
 * - 电池电压监测
 */
static void driver_task(void *parameter) {
  Wireless_Init();

  while (1) {
    PCF85063_Loop();
    BAT_Get_Volts();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  vTaskDelete(NULL);
}

/**
 * @brief 内存监控任务
 *
 * 定期打印内存使用情况，用于调试和内存优化
 */
static void memory_monitor_task(void *parameter) {
  // 等待系统初始化完成
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (1) {
    utils_print_memory_info();

    // 内存紧张时打印详细分解
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_internal < 100 * 1024) {
      utils_print_memory_breakdown();
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  vTaskDelete(NULL);
}

// ============================================================================
// 初始化函数
// ============================================================================

/**
 * @brief 初始化基础驱动
 */
static void driver_init(void) {
  Flash_Searching(); // 启动动画
  BAT_Init();        // 电池 ADC
  I2C_Init();        // I2C 总线
  EXIO_Init();       // IO 扩展器
  PCF85063_Init();   // RTC 时钟

  // 创建后台任务
  xTaskCreatePinnedToCore(driver_task, "driver_task", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(memory_monitor_task, "mem_monitor", 4096, NULL, 1,
                          NULL, 1);
}

// ============================================================================
// 主入口
// ============================================================================

extern "C" void app_main(void) {
  // 阶段1：基础驱动初始化
  driver_init();

  // 阶段2：显示系统初始化
  LCD_Init();

  // 阶段3：音频系统初始化
  Audio_Init();

  // 阶段4：UI 初始化
  LVGL_Init();
  ui_init();

  // 阶段5：主循环
  while (1) {
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(5)); // 减少 SPI 队列压力
    lv_timer_handler();
  }
}
