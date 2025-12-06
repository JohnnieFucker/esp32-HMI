#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * 录音配置参数
 */
typedef struct {
  uint32_t sample_rate;     // 采样率，默认16000
  uint16_t channels;        // 声道数，默认1（单声道）
  uint16_t bits_per_sample; // 位深度，默认16
  uint32_t buffer_size;     // 缓冲区大小，默认4096
  int gpio_bclk;            // I2S BCLK GPIO
  int gpio_ws;              // I2S WS GPIO
  int gpio_din;             // I2S DIN GPIO
} audio_recorder_config_t;

/**
 * 录音数据回调函数类型
 * @param data 音频数据
 * @param size 数据大小（字节）
 * @param user_data 用户数据
 * @return 返回true继续录音，false停止录音
 */
typedef bool (*audio_recorder_data_cb_t)(const void *data, size_t size,
                                         void *user_data);

/**
 * 初始化录音器
 * @param config 录音配置，如果为NULL则使用默认配置
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t audio_recorder_init(const audio_recorder_config_t *config);

/**
 * 反初始化录音器
 * @return ESP_OK 成功
 */
esp_err_t audio_recorder_deinit(void);

/**
 * 开始录音
 * @param filepath 录音文件保存路径（WAV格式）
 * @param duration_sec 录音时长（秒），0表示持续录音直到调用stop
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t audio_recorder_start(const char *filepath, uint32_t duration_sec);

/**
 * 开始录音（使用回调）
 * @param data_cb 数据回调函数
 * @param user_data 用户数据
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t audio_recorder_start_with_callback(audio_recorder_data_cb_t data_cb,
                                             void *user_data);

/**
 * 停止录音
 * @return ESP_OK 成功
 */
esp_err_t audio_recorder_stop(void);

/**
 * 检查是否正在录音
 * @return true 正在录音，false 未在录音
 */
bool audio_recorder_is_recording(void);

/**
 * 创建WAV文件头
 * @param filepath 文件路径
 * @param data_size 音频数据大小（字节）
 * @param sample_rate 采样率
 * @param channels 声道数
 * @param bits_per_sample 位深度
 * @return ESP_OK 成功
 */
esp_err_t audio_recorder_create_wav_header(const char *filepath,
                                           uint32_t data_size,
                                           uint16_t sample_rate,
                                           uint16_t channels,
                                           uint16_t bits_per_sample);

#ifdef __cplusplus
}
#endif
