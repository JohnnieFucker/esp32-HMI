#include "BAT_Driver.h"
#include "LVGL_Example.h"
#include "MIC_Speech.h"
#include "PCF85063.h"
#include "PCM5101.h"
#include "SD_MMC.h"
#include "ST77916.h"
#include "TCA9554PWR.h"
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
void Driver_Init(void) {
  Flash_Searching();
  BAT_Init();
  I2C_Init();
  EXIO_Init(); // Example Initialize EXIO
  PCF85063_Init();
  xTaskCreatePinnedToCore(Driver_Loop, "Other Driver task", 4096, NULL, 3, NULL,
                          0);
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
    vTaskDelay(pdMS_TO_TICKS(1));
    // The task running lv_timer_handler should have lower priority than that
    // running `lv_tick_inc`
    lv_timer_handler();
  }
}
