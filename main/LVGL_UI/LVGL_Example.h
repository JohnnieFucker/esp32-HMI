#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "demos/lv_demos.h"
#include "lvgl.h"

#include "BAT_Driver.h"
#include "LVGL_Driver.h"
#include "PCF85063.h"
#include "SD_MMC.h"
#include "Wireless.h"

#define EXAMPLE1_LVGL_TICK_PERIOD_MS 1000

void Backlight_adjustment_event_cb(lv_event_t *e);

void Lvgl_Example1(void);
void LVGL_Backlight_adjustment(uint8_t Backlight);

#ifdef __cplusplus
}
#endif