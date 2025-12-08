#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "audio_player.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/semphr.h"

#include "sd_card.h"

#define CONFIG_BSP_I2S_NUM 0

#define BSP_I2S_SCLK (GPIO_NUM_48)
#define BSP_I2S_MCLK (GPIO_NUM_NC)
#define BSP_I2S_LCLK (GPIO_NUM_38)
#define BSP_I2S_DOUT (GPIO_NUM_47)
#define BSP_I2S_DSIN (GPIO_NUM_NC)

#define BSP_I2S_GPIO_CFG                                                       \
  {                                                                            \
      .mclk = BSP_I2S_MCLK,                                                    \
      .bclk = BSP_I2S_SCLK,                                                    \
      .ws = BSP_I2S_LCLK,                                                      \
      .dout = BSP_I2S_DOUT,                                                    \
      .din = BSP_I2S_DSIN,                                                     \
      .invert_flags =                                                          \
          {                                                                    \
              .mclk_inv = false,                                               \
              .bclk_inv = false,                                               \
              .ws_inv = false,                                                 \
          },                                                                   \
  }

#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                                  \
  {                                                                            \
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                     \
      .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, \
                                                     I2S_SLOT_MODE_MONO),      \
      .gpio_cfg = BSP_I2S_GPIO_CFG,                                            \
  }

#define Volume_MAX 100
extern bool Music_Next_Flag;
extern uint8_t Volume;
void Audio_Init(void);
void Play_Music(const char *directory, const char *fileName);
void Music_resume(void);
void Music_pause(void);

uint32_t Music_Duration(void);
uint32_t Music_Elapsed(void);
uint16_t Music_Energy(void);
void Volume_adjustment(uint8_t Volume);

/**
 * 直接写入 I2S 音频数据（供外部模块复用 I2S 通道）
 * @param data 16位 PCM 数据
 * @param samples 采样数
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功
 */
esp_err_t Audio_I2S_Write(const int16_t *data, size_t samples,
                          uint32_t timeout_ms);

/**
 * 重新配置 I2S 时钟（用于切换采样率）
 * @param sample_rate 采样率
 * @return ESP_OK 成功
 */
esp_err_t Audio_I2S_Reconfig(uint32_t sample_rate);

#ifdef __cplusplus
}
#endif