#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/i2s_std.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// I2S 配置宏
#define MIC_I2S_NUM I2S_NUM_1
#define MIC_SAMPLE_RATE 16000
#define MIC_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT
#define MIC_CHANNEL_NUM 1

#define I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan)            \
  {                                                                            \
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),                      \
      .slot_cfg =                                                              \
          I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits_per_chan, channel_fmt),     \
      .gpio_cfg =                                                              \
          {                                                                    \
              .mclk = GPIO_NUM_NC,                                             \
              .bclk = GPIO_NUM_15,                                             \
              .ws = GPIO_NUM_2,                                                \
              .dout = GPIO_NUM_NC,                                             \
              .din = GPIO_NUM_39,                                              \
              .invert_flags =                                                  \
                  {                                                            \
                      .mclk_inv = false,                                       \
                      .bclk_inv = false,                                       \
                      .ws_inv = false,                                         \
                  },                                                           \
          },                                                                   \
  }

/**
 * 初始化麦克风 I2S 接口
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t MIC_Init(void);

/**
 * 反初始化麦克风 I2S 接口
 */
void MIC_Deinit(void);

/**
 * 启用/禁用麦克风
 * @param enable true 启用，false 禁用
 */
void MIC_Enable(bool enable);

/**
 * 检查麦克风是否已启用
 * @return true 已启用，false 未启用
 */
bool MIC_IsEnabled(void);

/**
 * 从麦克风读取 PCM 数据
 * @param buffer 输出缓冲区（16位 PCM 数据）
 * @param samples 要读取的采样数
 * @param bytes_read 实际读取的字节数（可为 NULL）
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t MIC_Read(int16_t *buffer, size_t samples, size_t *bytes_read,
                   uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
