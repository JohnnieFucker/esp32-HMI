/*
 * Compatibility header for ESP-IDF esp_lcd_io_i2c.h C++ compilation issue
 *
 * This file replaces esp_lcd_io_i2c.h to avoid C++ compilation conflicts.
 * The original ESP-IDF header defines two static inline functions with the
 * same name but different parameter types in C++ mode, which causes a conflict.
 *
 * FINAL SOLUTION: Use #pragma once with a check to prevent ESP-IDF's version
 * from being included if our version is already included. Since #pragma once
 * is based on file path, we need to ensure our version is found first.
 */
#pragma once

// Define a marker to indicate our version is included
#define _ESP_LCD_IO_I2C_COMPAT_VERSION_INCLUDED

#include "driver/i2c_types.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Type definitions (same as ESP-IDF)
typedef uint32_t esp_lcd_i2c_bus_handle_t;

typedef struct {
  uint32_t dev_addr;
  esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
  void *user_ctx;
  size_t control_phase_bytes;
  unsigned int dc_bit_offset;
  int lcd_cmd_bits;
  int lcd_param_bits;
  struct {
    unsigned int dc_low_on_data : 1;
    unsigned int disable_control_phase : 1;
  } flags;
  uint32_t scl_speed_hz;
} esp_lcd_panel_io_i2c_config_t;

// Function declarations (v1 and v2 versions)
esp_err_t
esp_lcd_new_panel_io_i2c_v1(uint32_t bus,
                            const esp_lcd_panel_io_i2c_config_t *io_config,
                            esp_lcd_panel_io_handle_t *ret_io);
esp_err_t
esp_lcd_new_panel_io_i2c_v2(i2c_master_bus_handle_t bus,
                            const esp_lcd_panel_io_i2c_config_t *io_config,
                            esp_lcd_panel_io_handle_t *ret_io);

#ifdef __cplusplus
}
#endif
