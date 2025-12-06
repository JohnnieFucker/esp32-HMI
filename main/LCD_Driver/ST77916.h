#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"

// Include ESP-LCD headers
// Our wrapper esp_lcd_panel_io.h automatically includes our compatibility
// version of esp_lcd_io_i2c.h instead of ESP-IDF's conflicting version
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CST816.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"
#include "esp_lcd_st77916.h"

#define EXAMPLE_LCD_WIDTH (360)
#define EXAMPLE_LCD_HEIGHT (360)
#define EXAMPLE_LCD_COLOR_BITS (16)

#define ESP_PANEL_HOST_SPI_ID_DEFAULT (SPI2_HOST)
#define ESP_PANEL_LCD_SPI_MODE (0) // 0/1/2/3, typically set to 0
// SPI时钟频率配置
// 注意：QSPI模式下，频率过高可能导致传输错误和花屏
// 建议值：20-40MHz（QSPI模式），如果仍有问题，可以降到10MHz
// 注意：频率过低也可能导致初始化失败或显示异常
#define ESP_PANEL_LCD_SPI_CLK_HZ                                               \
  (40 * 1000 * 1000) // 先使用40MHz，如果仍有问题再降低到20MHz或10MHz
#define ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ                                       \
  (20) // 增大队列深度，避免队列溢出（从10增加到20）
#define ESP_PANEL_LCD_SPI_CMD_BITS (32)  // Typically set to 32
#define ESP_PANEL_LCD_SPI_PARAM_BITS (8) // Typically set to 8

#define ESP_PANEL_LCD_SPI_IO_TE (18)
#define ESP_PANEL_LCD_SPI_IO_SCK (40)
#define ESP_PANEL_LCD_SPI_IO_DATA0 (46)
#define ESP_PANEL_LCD_SPI_IO_DATA1 (45)
#define ESP_PANEL_LCD_SPI_IO_DATA2 (42)
#define ESP_PANEL_LCD_SPI_IO_DATA3 (41)
#define ESP_PANEL_LCD_SPI_IO_CS (21)
#define EXAMPLE_LCD_PIN_NUM_RST (-1) // EXIO2
#define EXAMPLE_LCD_PIN_NUM_BK_LIGHT (5)

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL (1)
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

// SPI最大传输大小配置
// 重要：ESP32-S3的DMA限制通常是32KB或64KB，取决于具体配置
// 计算公式：屏幕宽度 * 屏幕高度 * 每像素字节数
// 对于360x360 16位色：360 * 360 * 2 = 259,200 字节
// LVGL缓冲区是屏幕的1/4，即 360*360/4 = 32,400 像素
// 每次刷新可能传输 360x90 = 32,400 像素 = 64,800 字节（16位色）
// 由于64,800字节超过了32KB限制，我们使用32KB并依赖手动分块传输
// 如果您的ESP32-S3支持64KB DMA，可以尝试设置为64KB
#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE                                   \
  (32 * 1024) // 32KB，ESP32-S3
              // DMA的安全限制，超过此大小的传输会在刷新函数中自动分块

#define LEDC_HS_TIMER LEDC_TIMER_0
#define LEDC_LS_MODE LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_GPIO EXAMPLE_LCD_PIN_NUM_BK_LIGHT
#define LEDC_HS_CH0_CHANNEL LEDC_CHANNEL_0
#define LEDC_TEST_DUTY (4000)
#define LEDC_ResolutionRatio LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty ((1 << LEDC_ResolutionRatio) - 1)
#define Backlight_MAX 100

extern esp_lcd_panel_handle_t panel_handle;
extern uint8_t LCD_Backlight;

void ST77916_Init();

void LCD_Init(void); // Call this function to initialize the screen (must be
                     // called in the main function) !!!!!
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend,
                   uint16_t Yend, uint16_t *color);

void Backlight_Init(void); // Initialize the LCD backlight, which has been
                           // called in the LCD_Init function, ignore it
void Set_Backlight(uint8_t Light); // Call this function to adjust the
                                   // brightness of the backlight. The value of
                                   // the parameter Light ranges from 0 to 100

#ifdef __cplusplus
}
#endif
