#include "BAT_Driver.h"
#include "LVGL_Example.h"
#include "MIC_Speech.h"
#include "PCF85063.h"
#include "PCM5101.h"
#include "SD_MMC.h"
#include "ST77916.h"
#include "TCA9554PWR.h"
#include "Utils.h"
#include "Wireless.h"
#include "ui.h"

void Driver_Loop(void *parameter) {
  Wireless_Init();
  while (1) {
    PCF85063_Loop();
    BAT_Get_Volts();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelete(NULL);
}

void MemoryMonitor_Task(void *parameter) {
  // 等待系统初始化完成
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (1) {
    // 每5秒打印一次内存占用情况
    // 先打印简要信息
    utils_print_memory_info();
    // 如果内存紧张，打印详细分解
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_internal < 100 * 1024) { // 少于100KB时打印详细分解
      utils_print_memory_breakdown();
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
  vTaskDelete(NULL);
}
void Driver_Init(void) {
  Flash_Searching();
  BAT_Init();
  I2C_Init();
  EXIO_Init(); // Example Initialize EXIO
  PCF85063_Init();
  xTaskCreatePinnedToCore(Driver_Loop, "Other Driver task", 4096, NULL, 3, NULL,
                          0);
  // 创建内存监控任务，每5秒打印一次内存占用情况
  xTaskCreatePinnedToCore(MemoryMonitor_Task, "Memory Monitor", 4096, NULL, 1,
                          NULL, 1);
}
extern "C" void app_main(void) {
  Driver_Init();

  // SD_Init();
  LCD_Init();
  Audio_Init();
  // MIC_Speech_init();
  // Play_Music("/sdcard","AAA.mp3");
  LVGL_Init(); // returns the screen object

  // /********************* Demo *********************/
  //   Lvgl_Example1();
  ui_init();
  // lv_demo_widgets();
  // lv_demo_keypad_encoder();
  // lv_demo_benchmark();
  // lv_demo_stress();
  // lv_demo_music();

  while (1) {
    // raise the task priority of LVGL and/or reduce the handler period can
    // improve the performance
    ui_tick();
    // 增加延迟到5ms，减少刷新频率，降低SPI队列压力
    // 这样可以避免SPI队列堆积导致的传输失败和花屏问题
    vTaskDelay(pdMS_TO_TICKS(5));
    // The task running lv_timer_handler should have lower priority than that
    // running `lv_tick_inc`
    lv_timer_handler();
  }
}
