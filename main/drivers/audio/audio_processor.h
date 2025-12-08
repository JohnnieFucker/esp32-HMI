/**
 * @file audio_processor.h
 * @brief 音频前端处理器
 *
 * 基于 ESP-SR 的音频处理模块，提供：
 * - VAD (Voice Activity Detection) - 语音活动检测
 * - NS (Noise Suppression) - 噪声抑制
 * - AGC (Automatic Gain Control) - 自动增益控制
 *
 * 使用方式：
 * 1. Initialize(channels, reference) - 初始化
 * 2. OnOutput(callback) - 设置输出回调
 * 3. OnVadStateChange(callback) - 设置 VAD 状态回调
 * 4. Start() / Stop() - 控制处理器运行
 * 5. Input(data) - 输入音频数据
 */

#pragma once

#include <functional>
#include <stdbool.h>
#include <stdint.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include "esp_afe_sr_models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/**
 * @brief 音频处理器类
 *
 * 封装 ESP-SR AFE（Audio Front-End）功能
 */
class AudioProcessor {
public:
  AudioProcessor();
  ~AudioProcessor();

  void Initialize(int channels, bool reference);
  void Start();
  void Stop();
  bool IsRunning();

  void Input(const std::vector<int16_t> &data);
  void OnOutput(std::function<void(std::vector<int16_t> &&data)> callback);
  void OnVadStateChange(std::function<void(bool speaking)> callback);

private:
  void AudioProcessorTask();

  const esp_afe_sr_iface_t *afe_iface_;
  afe_config_t *afe_config_;
  esp_afe_sr_data_t *afe_data_;
  EventGroupHandle_t event_group_;

  int channels_;
  bool reference_;
  bool is_speaking_;

  std::vector<int16_t> input_buffer_;
  std::function<void(std::vector<int16_t> &&data)> output_callback_;
  std::function<void(bool speaking)> vad_state_change_callback_;
};

#endif // __cplusplus
