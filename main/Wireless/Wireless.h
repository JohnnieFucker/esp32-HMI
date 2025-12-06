#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include <stdio.h>
#include <string.h> // For memcpy

extern uint16_t WIFI_NUM;
extern bool Scan_finish;

void Wireless_Init(void);
void WIFI_Init(void *arg);
uint16_t WIFI_Scan(void);

// WiFi状态检查函数
bool WiFi_IsConnected(void);
bool WiFi_IsInitComplete(void); // 检查WiFi初始化是否完成
const char *WiFi_GetError(void);

#ifdef __cplusplus
}
#endif
