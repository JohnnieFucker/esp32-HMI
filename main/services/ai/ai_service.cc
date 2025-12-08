/**
 * CG AI 语音服务
 *
 * 功能：
 * 1. WebSocket 连接到 CG_AI_URL
 * 2. Opus 编码麦克风音频并发送
 * 3. 接收 Opus 音频并解码播放
 * 4. 超时无语音输入时自动断开连接
 */

#include "ai_service.h"
#include "app_config.h"

extern "C" {
#include "driver/i2s_std.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mic_driver.h"
#include "pcm5101.h"
#include <sys/time.h>
#include <time.h>
}

#include "audio_processor.h"
#include "opus_decoder.h"
#include "opus_encoder.h"

#include <cmath>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "esp_heap_caps.h" // 用于 PSRAM 分配

static const char *TAG = "CgAiService";

// ============== BackgroundTask 类（用于后台编码任务）==============
/**
 * 简单的后台任务调度器
 * 用于在独立任务中执行编码等耗时操作，避免阻塞主循环
 */
class BackgroundTask {
public:
  BackgroundTask(size_t stack_size) : stack_size_(stack_size), running_(false) {
    // 使用指针队列，每个元素是指向 std::function 的指针
    queue_ = xQueueCreate(10, sizeof(void *));
    if (queue_ == nullptr) {
      ESP_LOGE(TAG, "创建后台任务队列失败");
      return;
    }

    running_ = true;
    xTaskCreate(task_wrapper, "bg_task", stack_size_ / sizeof(StackType_t),
                this, 5, &task_handle_);
  }

  ~BackgroundTask() {
    running_ = false;
    if (queue_) {
      // 发送空指针作为停止信号
      void *stop_signal = nullptr;
      xQueueSend(queue_, &stop_signal, portMAX_DELAY);
    }
    if (task_handle_) {
      vTaskDelete(task_handle_);
    }
    // 清空队列中剩余的任务
    if (queue_) {
      void *task_ptr = nullptr;
      while (xQueueReceive(queue_, &task_ptr, 0) == pdTRUE) {
        if (task_ptr != nullptr) {
          delete static_cast<std::function<void()> *>(task_ptr);
        }
      }
      vQueueDelete(queue_);
    }
  }

  void Schedule(std::function<void()> &&task) {
    if (!running_ || queue_ == nullptr) {
      return;
    }

    // 在堆上分配函数对象
    auto *task_ptr = new std::function<void()>(std::move(task));
    void *ptr = static_cast<void *>(task_ptr);
    if (xQueueSend(queue_, &ptr, portMAX_DELAY) != pdTRUE) {
      delete task_ptr;
      ESP_LOGE(TAG, "调度后台任务失败");
    }
  }

  void WaitForCompletion() {
    // 简单实现：等待队列为空
    int wait_count = 0;
    while (uxQueueMessagesWaiting(queue_) > 0 && wait_count < 100) {
      vTaskDelay(pdMS_TO_TICKS(10));
      wait_count++;
    }
  }

private:
  static void task_wrapper(void *arg) {
    auto *self = static_cast<BackgroundTask *>(arg);
    self->task_loop();
    vTaskDelete(nullptr);
  }

  void task_loop() {
    void *ptr = nullptr;

    while (running_) {
      if (xQueueReceive(queue_, &ptr, portMAX_DELAY) == pdTRUE) {
        if (ptr == nullptr) {
          // 停止信号
          break;
        }

        // 执行任务
        auto *task_ptr = static_cast<std::function<void()> *>(ptr);
        (*task_ptr)();
        delete task_ptr;
        ptr = nullptr;
      }
    }
  }

  size_t stack_size_;
  QueueHandle_t queue_;
  TaskHandle_t task_handle_;
  volatile bool running_;
};

// ============== 常量定义 ==============
#define OPUS_FRAME_DURATION_MS 60 // Opus 帧时长（毫秒）- 与服务器配置一致
#define OPUS_SAMPLE_RATE 16000    // 采样率
#define OPUS_CHANNELS 1           // 声道数
#define OPUS_FRAME_SIZE                                                        \
  (OPUS_SAMPLE_RATE * OPUS_FRAME_DURATION_MS / 1000) // 每帧采样数 = 320

#define MIC_READ_SAMPLES OPUS_FRAME_SIZE
#define AUDIO_QUEUE_SIZE 200 // 音频队列大小（约 12 秒，避免丢弃 TTS 音频）

// I2S 输出采样率
#define I2S_OUT_SAMPLE_RATE 16000

// ============== 全局变量 ==============
static cg_ai_state_t g_state = CG_AI_STATE_IDLE;
static cg_ai_state_callback_t g_state_callback = nullptr;
static void *g_callback_user_data = nullptr;
static SemaphoreHandle_t g_state_mutex = nullptr;

static esp_websocket_client_handle_t g_ws_client = nullptr;
static TaskHandle_t g_mic_task_handle = nullptr;
static TaskHandle_t g_audio_out_task_handle = nullptr;
static volatile bool g_running = false;

// Opus 编解码器
static std::unique_ptr<OpusEncoderWrapper> g_opus_encoder;
static std::unique_ptr<OpusDecoderWrapper> g_opus_decoder;

// 后台编码任务
static std::unique_ptr<BackgroundTask> g_background_task;

// 音频处理器（用于 VAD 和音频处理）
static std::unique_ptr<AudioProcessor> g_audio_processor;

// 音频输出队列（存储原始 Opus 数据，在播放任务中解码）
static std::mutex g_audio_mutex;
static std::list<std::vector<uint8_t>> g_audio_out_queue;

// 音频缓冲区（用于累积说话时的音频，断句时发送）
static std::mutex g_speech_buffer_mutex;
static std::vector<int16_t> g_speech_buffer; // 累积的音频数据
static bool g_is_speaking = false;           // VAD 检测到的说话状态

// VAD 预缓冲（保留语音开始前的音频，避免丢失开头）
static std::deque<std::vector<int16_t>> g_pre_speech_buffer; // 预缓冲队列
static const size_t PRE_SPEECH_BUFFER_COUNT = 5;             // 保留最近 5 帧

// VAD 防抖参数
static uint32_t g_listening_start_time = 0; // 进入 LISTENING 状态的时间
static uint32_t g_speech_start_time = 0;    // 开始说话的时间
static const uint32_t VAD_STARTUP_IGNORE_MS =
    300; // 录音开始后忽略 VAD 的时间（ms）
static const uint32_t MIN_SPEECH_DURATION_MS = 300; // 最小有效说话时长
static const int32_t MIN_AUDIO_ENERGY_THRESHOLD =
    500; // 最小音频能量阈值（RMS）

/**
 * 计算音频的 RMS 能量（用于过滤噪音）
 */
static int32_t calculate_audio_rms(const std::vector<int16_t> &audio) {
  if (audio.empty())
    return 0;

  int64_t sum_squares = 0;
  for (const auto &sample : audio) {
    sum_squares += (int64_t)sample * sample;
  }
  return (int32_t)sqrt((double)sum_squares / audio.size());
}

// WebSocket 二进制消息分片缓冲区
static std::vector<uint8_t> g_ws_binary_buffer;

// I2S 输出状态
static bool g_i2s_output_configured = false;

// 时间戳
static uint32_t g_last_activity_time = 0;
static bool g_server_hello_received = false;
static int g_server_sample_rate = 16000;

// 会话 ID（从服务器 hello 响应中获取，所有消息都需要包含）
static std::string g_session_id;

// WebSocket headers（需要在整个连接期间保持有效）
static std::string g_ws_headers;

// ============== 辅助函数 ==============

static uint32_t get_time_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void update_activity_time(void) { g_last_activity_time = get_time_ms(); }

// 前向声明
static void send_accumulated_audio(void);
static void set_state(cg_ai_state_t new_state);

/**
 * 延迟初始化 AudioProcessor（esp-sr）
 * 在 WebSocket 连接成功后调用，避免内存不足导致 SSL 握手失败
 */
static void init_audio_processor(void) {
  if (g_audio_processor) {
    ESP_LOGW(TAG, "AudioProcessor 已初始化，跳过");
    return;
  }

  ESP_LOGI(TAG, "开始延迟初始化 AudioProcessor...");

  // 初始化音频处理器（用于 VAD 和音频处理）
  // 单声道，无参考通道
  g_audio_processor = std::make_unique<AudioProcessor>();
  if (!g_audio_processor) {
    ESP_LOGE(TAG, "创建音频处理器失败（内存不足）");
    return;
  }
  g_audio_processor->Initialize(1, false); // 1 通道，无参考通道

  // 设置 VAD 状态变化回调
  g_audio_processor->OnVadStateChange([](bool speaking) {
    uint32_t now = get_time_ms();

    // 防抖：忽略录音开始后的前 N 毫秒的 VAD 事件
    if (g_listening_start_time > 0) {
      uint32_t elapsed = now - g_listening_start_time;
      if (elapsed < VAD_STARTUP_IGNORE_MS) {
        ESP_LOGD(TAG, "忽略 VAD 事件（录音启动防抖期，已过 %lu ms）",
                 (unsigned long)elapsed);
        return;
      }
    }

    ESP_LOGI(TAG, "VAD 状态变化: %s", speaking ? "说话中" : "静音");

    bool should_send = false;

    {
      // 使用局部作用域控制锁的生命周期
      std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);

      if (speaking) {
        // 开始说话：记录开始时间，把预缓冲区的数据加入语音缓冲区
        g_speech_start_time = now;
        g_is_speaking = true;
        g_speech_buffer.clear();

        // 将预缓冲区的音频数据添加到语音缓冲区
        size_t pre_samples = 0;
        for (auto &frame : g_pre_speech_buffer) {
          g_speech_buffer.insert(g_speech_buffer.end(), frame.begin(),
                                 frame.end());
          pre_samples += frame.size();
        }
        g_pre_speech_buffer.clear();

        ESP_LOGI(TAG, "开始检测到语音，已添加 %zu 采样的预缓冲", pre_samples);
      } else {
        // 停止说话（断句）：检查说话时长和音频能量
        if (g_is_speaking && !g_speech_buffer.empty()) {
          uint32_t speech_duration = now - g_speech_start_time;
          int32_t audio_rms = calculate_audio_rms(g_speech_buffer);

          if (speech_duration < MIN_SPEECH_DURATION_MS) {
            ESP_LOGI(TAG, "说话时长不足 (%lu ms < %lu ms)，忽略",
                     (unsigned long)speech_duration,
                     (unsigned long)MIN_SPEECH_DURATION_MS);
            g_speech_buffer.clear();
          } else if (audio_rms < MIN_AUDIO_ENERGY_THRESHOLD) {
            ESP_LOGI(TAG, "音频能量不足 (RMS=%ld < %ld)，判定为噪音，忽略",
                     (long)audio_rms, (long)MIN_AUDIO_ENERGY_THRESHOLD);
            g_speech_buffer.clear();
          } else {
            ESP_LOGI(TAG, "检测到断句，时长: %lu ms，能量 RMS=%ld，准备发送",
                     (unsigned long)speech_duration, (long)audio_rms);
            should_send = true;
          }
        }
        g_is_speaking = false;
        g_speech_start_time = 0;
      }
    }

    // 在锁外发送，避免死锁
    // 注意：不要在这里改变状态，直接发送即可
    if (should_send) {
      send_accumulated_audio();
    }
  });

  // 设置音频输出回调（处理后的音频数据）
  g_audio_processor->OnOutput([](std::vector<int16_t> &&data) {
    std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);

    if (g_is_speaking) {
      // 正在说话：累积音频数据
      g_speech_buffer.insert(g_speech_buffer.end(), data.begin(), data.end());
    } else {
      // 未说话：更新预缓冲区（滑动窗口，保留最近的音频帧）
      g_pre_speech_buffer.push_back(data);
      while (g_pre_speech_buffer.size() > PRE_SPEECH_BUFFER_COUNT) {
        g_pre_speech_buffer.pop_front();
      }
    }
  });

  // 注意：不在这里启动，由 mic_task 控制启动/停止
  ESP_LOGI(TAG, "AudioProcessor 延迟初始化完成");
}

/**
 * 状态名称（用于日志）
 */
static const char *get_state_name(cg_ai_state_t state) {
  switch (state) {
  case CG_AI_STATE_IDLE:
    return "IDLE";
  case CG_AI_STATE_CONNECTING:
    return "CONNECTING";
  case CG_AI_STATE_CONNECTED:
    return "CONNECTED";
  case CG_AI_STATE_LISTENING:
    return "LISTENING";
  case CG_AI_STATE_SENDING:
    return "SENDING";
  case CG_AI_STATE_SPEAKING:
    return "SPEAKING";
  case CG_AI_STATE_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

/**
 * 统一状态管理
 *
 * VAD 控制规则：
 * - LISTENING: VAD 开启
 * - SENDING/SPEAKING/其他: VAD 关闭
 */
static void set_state(cg_ai_state_t new_state) {
  if (g_state_mutex) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
  }

  cg_ai_state_t old_state = g_state;

  if (old_state != new_state) {
    ESP_LOGI(TAG, "状态变化: %s -> %s", get_state_name(old_state),
             get_state_name(new_state));
    g_state = new_state;

    // ========== VAD 控制逻辑 ==========
    // 进入 LISTENING 状态：启动 VAD
    if (new_state == CG_AI_STATE_LISTENING) {
      // 记录进入 LISTENING 的时间（用于 VAD 防抖）
      g_listening_start_time = get_time_ms();
      g_speech_start_time = 0;

      // 清空缓冲区
      {
        std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
        g_speech_buffer.clear();
        g_pre_speech_buffer.clear();
        g_is_speaking = false;
      }
      // 启动音频处理器（VAD）
      if (g_audio_processor && !g_audio_processor->IsRunning()) {
        g_audio_processor->Start();
        ESP_LOGI(TAG, "VAD 已启动（%lu ms 后开始检测）",
                 (unsigned long)VAD_STARTUP_IGNORE_MS);
      }
      // 重置超时计时
      update_activity_time();
    }

    // 离开 LISTENING 状态：停止 VAD
    if (old_state == CG_AI_STATE_LISTENING &&
        new_state != CG_AI_STATE_LISTENING) {
      if (g_audio_processor && g_audio_processor->IsRunning()) {
        g_audio_processor->Stop();
        ESP_LOGI(TAG, "VAD 已停止");
      }
    }

    // 进入 SENDING 状态：确保 VAD 关闭
    if (new_state == CG_AI_STATE_SENDING) {
      if (g_audio_processor && g_audio_processor->IsRunning()) {
        g_audio_processor->Stop();
        ESP_LOGI(TAG, "进入发送状态，VAD 已停止");
      }
    }

    // 进入 SPEAKING 状态：确保 VAD 关闭，清空缓冲区
    if (new_state == CG_AI_STATE_SPEAKING) {
      if (g_audio_processor && g_audio_processor->IsRunning()) {
        g_audio_processor->Stop();
        ESP_LOGI(TAG, "进入播放状态，VAD 已停止");
      }
      // 清空语音缓冲区
      {
        std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
        g_speech_buffer.clear();
        g_pre_speech_buffer.clear();
        g_is_speaking = false;
      }
    }

    // 回调通知
    if (g_state_callback) {
      g_state_callback(new_state, g_callback_user_data);
    }
  }

  if (g_state_mutex) {
    xSemaphoreGive(g_state_mutex);
  }
}

/**
 * 获取设备 MAC 地址（格式：XX:XX:XX:XX:XX:XX）
 */
static std::string get_device_mac_address(void) {
  uint8_t mac[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "获取 MAC 地址失败，使用默认值");
    return "00:00:00:00:00:00";
  }

  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);

  return std::string(mac_str);
}

/**
 * 生成 UUID v4（格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx）
 */
static std::string generate_uuid(void) {
  // 使用时间戳和随机数生成 UUID
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  // 使用时间戳和任务句柄作为随机种子
  uint32_t seed = (uint32_t)(tv.tv_sec ^ tv.tv_usec ^
                             (uintptr_t)xTaskGetCurrentTaskHandle());

  // 简单的伪随机数生成器（线性同余）
  uint32_t r1 = seed * 1103515245 + 12345;
  uint32_t r2 = r1 * 1103515245 + 12345;
  uint32_t r3 = r2 * 1103515245 + 12345;
  uint32_t r4 = r3 * 1103515245 + 12345;

  char uuid[37];
  snprintf(uuid, sizeof(uuid), "%08lx-%04x-4%03x-%04x-%08lx%04x",
           (unsigned long)r1, (unsigned int)(r2 >> 16),
           (unsigned int)(r2 & 0x0fff),
           (unsigned int)((r3 >> 16) | 0x8000), // 版本 4 标识
           (unsigned long)r3, (unsigned int)(r4 >> 16));

  return std::string(uuid);
}

// ============== I2S 输出（复用 PCM5101 的 I2S 通道）==============

static esp_err_t i2s_output_init(void) {
  if (g_i2s_output_configured) {
    return ESP_OK;
  }

  // 复用 PCM5101 的 I2S 通道，重新配置采样率
  ESP_LOGI(TAG, "配置 I2S 输出（采样率: %d）", I2S_OUT_SAMPLE_RATE);
  esp_err_t ret = Audio_I2S_Reconfig(I2S_OUT_SAMPLE_RATE);
  if (ret == ESP_OK) {
    g_i2s_output_configured = true;
    ESP_LOGI(TAG, "I2S 输出配置成功（复用 PCM5101 通道）");
  } else {
    ESP_LOGW(TAG, "I2S 重新配置失败: %s", esp_err_to_name(ret));
  }

  return ESP_OK; // 即使配置失败也继续，因为 PCM5101 可能已经处于正确状态
}

static void i2s_output_write(const int16_t *data, size_t samples) {
  Audio_I2S_Write(data, samples, 100);
}

static void i2s_output_deinit(void) {
  // 不需要做任何事，I2S 通道由 PCM5101 管理
  g_i2s_output_configured = false;
}

// ============== WebSocket 事件处理 ==============

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WebSocket 已连接");
    update_activity_time();

    // 发送 hello 消息
    {
      char hello_msg[256];
      snprintf(hello_msg, sizeof(hello_msg),
               "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
               "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":%d,"
               "\"channels\":%d,\"frame_duration\":%d}}",
               OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_FRAME_DURATION_MS);

      esp_websocket_client_send_text(g_ws_client, hello_msg, strlen(hello_msg),
                                     portMAX_DELAY);
      ESP_LOGI(TAG, "已发送 hello: %s", hello_msg);
    }
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "WebSocket 断开连接，重置状态");
    g_server_hello_received = false;
    g_session_id.clear();
    g_running = false; // 允许重新启动
    // 清空音频队列
    {
      std::lock_guard<std::mutex> lock(g_audio_mutex);
      g_audio_out_queue.clear();
    }
    set_state(CG_AI_STATE_IDLE);
    break;

  case WEBSOCKET_EVENT_DATA:
    // 打印收到的消息（调试用）
    if (data->op_code == 0x02) { // 二进制数据（Opus 音频）
      // 处理 WebSocket 消息分片
      // data_offset: 当前片段在完整消息中的偏移
      // payload_len: 完整消息的长度
      // data_len: 当前片段的长度
      if (data->payload_offset == 0) {
        // 新消息的第一个片段，清空缓冲区
        g_ws_binary_buffer.clear();
      }

      // 追加数据到缓冲区
      if (data->data_len > 0) {
        g_ws_binary_buffer.insert(g_ws_binary_buffer.end(),
                                  (uint8_t *)data->data_ptr,
                                  (uint8_t *)data->data_ptr + data->data_len);
      }

      // 检查是否是完整消息（当前偏移 + 当前长度 >= 完整长度）
      bool message_complete =
          (data->payload_offset + data->data_len >= data->payload_len);

      if (message_complete && !g_ws_binary_buffer.empty()) {
        ESP_LOGD(TAG, "收到完整 Opus 数据: %zu 字节",
                 g_ws_binary_buffer.size());
        // 在 SENDING 或 SPEAKING 状态下接收音频
        if (g_state == CG_AI_STATE_SPEAKING || g_state == CG_AI_STATE_SENDING) {
          // 收到 TTS 音频时自动进入 SPEAKING 状态（不依赖 tts start 消息）
          if (g_state == CG_AI_STATE_SENDING) {
            set_state(CG_AI_STATE_SPEAKING);
            ESP_LOGI(TAG, "收到 TTS 音频，自动进入播放状态");
          }
          std::lock_guard<std::mutex> lock(g_audio_mutex);
          g_audio_out_queue.push_back(std::move(g_ws_binary_buffer));
        }
        g_ws_binary_buffer.clear();
      }
      update_activity_time();
    } else if (data->op_code == 0x01) { // 文本数据（JSON）
      // 打印收到的文本消息
      ESP_LOGI(TAG, "收到文本消息: %.*s", data->data_len, data->data_ptr);
      // 解析 JSON 消息
      cJSON *root = cJSON_ParseWithLength(data->data_ptr, data->data_len);
      if (root) {
        cJSON *type = cJSON_GetObjectItem(root, "type");
        if (type && cJSON_IsString(type)) {
          const char *type_str = type->valuestring;
          ESP_LOGI(TAG, "收到消息类型: %s", type_str);

          if (strcmp(type_str, "hello") == 0) {
            // 服务器 hello 响应 - 只在 CONNECTING 状态时处理
            if (g_state != CG_AI_STATE_CONNECTING) {
              ESP_LOGW(TAG,
                       "收到重复的 hello 消息，忽略（当前状态非 CONNECTING）");
              cJSON_Delete(root);
              break;
            }

            cJSON *audio_params = cJSON_GetObjectItem(root, "audio_params");
            if (audio_params) {
              cJSON *sample_rate =
                  cJSON_GetObjectItem(audio_params, "sample_rate");
              if (sample_rate && cJSON_IsNumber(sample_rate)) {
                g_server_sample_rate = sample_rate->valueint;
                ESP_LOGI(TAG, "服务器采样率: %d", g_server_sample_rate);
              }
            }

            // 保存 session_id（后续所有消息都需要）
            cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
            if (session_id && cJSON_IsString(session_id)) {
              g_session_id = session_id->valuestring;
              ESP_LOGI(TAG, "会话 ID: %s", g_session_id.c_str());
            }

            g_server_hello_received = true;
            set_state(CG_AI_STATE_CONNECTED);

            // WebSocket 连接成功后，延迟初始化 AudioProcessor（esp-sr）
            // 避免在 SSL 握手时内存不足
            init_audio_processor();

          } else if (strcmp(type_str, "tts") == 0) {
            cJSON *state = cJSON_GetObjectItem(root, "state");
            if (state && cJSON_IsString(state)) {
              if (strcmp(state->valuestring, "start") == 0) {
                // AI 开始说话：进入 SPEAKING 状态
                set_state(CG_AI_STATE_SPEAKING);
                ESP_LOGI(TAG, "AI 开始播放语音回复");
              } else if (strcmp(state->valuestring, "stop") == 0) {
                // AI 说完：从 SPEAKING 回到 LISTENING（唯一回到 LISTENING
                // 的路径）
                set_state(CG_AI_STATE_LISTENING);
                ESP_LOGI(TAG, "AI 语音播放完成，恢复监听");
              }
            }
          } else if (strcmp(type_str, "stt_end") == 0 ||
                     strcmp(type_str, "asr_end") == 0) {
            // 语音识别结束，如果当前在 SENDING 状态，保持等待 TTS
            ESP_LOGI(TAG, "语音识别处理完成");
          } else if (strcmp(type_str, "stt") == 0) {
            cJSON *text = cJSON_GetObjectItem(root, "text");
            if (text && cJSON_IsString(text)) {
              ESP_LOGI(TAG, "识别结果: %s", text->valuestring);
            }
            update_activity_time();
          }
        }
        cJSON_Delete(root);
      }
    }
    break;

  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG, "WebSocket 错误，重置状态");
    g_server_hello_received = false;
    g_session_id.clear();
    g_running = false; // 允许重新启动
    // 清空音频队列
    {
      std::lock_guard<std::mutex> lock(g_audio_mutex);
      g_audio_out_queue.clear();
    }
    set_state(CG_AI_STATE_IDLE);
    break;

  case WEBSOCKET_EVENT_CLOSED:
    ESP_LOGW(TAG, "WebSocket 已关闭，重置状态");
    g_server_hello_received = false;
    g_session_id.clear();
    g_running = false; // 允许重新启动
    // 清空音频队列
    {
      std::lock_guard<std::mutex> lock(g_audio_mutex);
      g_audio_out_queue.clear();
    }
    set_state(CG_AI_STATE_IDLE);
    break;

  default:
    break;
  }
}

static int websocket_connect(void) {
  ESP_LOGI(TAG, "连接到: %s", CG_AI_URL);

  // 获取设备 MAC 地址
  std::string device_id = get_device_mac_address();

  // 生成新的 UUID（每次连接时重新生成）
  std::string client_id = generate_uuid();
  std::string device_type = "boxbot";

  ESP_LOGI(TAG, "Device-Id: %s", device_id.c_str());
  ESP_LOGI(TAG, "Client-Id: %s", client_id.c_str());
  ESP_LOGI(TAG, "Device-Type: %s", device_type.c_str());

  // 构建 headers 字符串（每个 header 以 \r\n 结尾）
  // 使用全局变量确保字符串在连接期间保持有效
  g_ws_headers = "Device-Id: " + device_id + "\r\n" +
                 "Client-Id: " + client_id + "\r\n" +
                 "Device-Type: " + device_type + "\r\n";

  esp_websocket_client_config_t config = {};
  config.uri = CG_AI_URL;
  config.buffer_size = 8192; // 增大缓冲区以处理较大的 Opus 帧
  // 增加任务栈大小，SSL 握手需要更多栈空间
  config.task_stack = 8192;
  config.skip_cert_common_name_check = true;
  config.cert_pem = nullptr;             // 跳过证书验证
  config.headers = g_ws_headers.c_str(); // 设置自定义 headers
  // 使用 PSRAM 分配任务栈（如果可用）
  config.task_prio = 5;

  g_ws_client = esp_websocket_client_init(&config);
  if (g_ws_client == nullptr) {
    ESP_LOGE(TAG, "创建 WebSocket 客户端失败");
    return -1;
  }

  esp_websocket_register_events(g_ws_client, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, nullptr);

  esp_err_t err = esp_websocket_client_start(g_ws_client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "启动 WebSocket 客户端失败: %s", esp_err_to_name(err));
    esp_websocket_client_destroy(g_ws_client);
    g_ws_client = nullptr;
    return -1;
  }

  return 0;
}

static void websocket_disconnect(void) {
  if (g_ws_client) {
    esp_websocket_client_stop(g_ws_client);
    esp_websocket_client_destroy(g_ws_client);
    g_ws_client = nullptr;
  }
  g_server_hello_received = false;
  g_session_id.clear();
}

static void websocket_send_audio(const std::vector<uint8_t> &opus_data) {
  if (!g_ws_client) {
    ESP_LOGE(TAG, "WebSocket 客户端为空，无法发送音频");
    return;
  }

  if (!esp_websocket_client_is_connected(g_ws_client)) {
    ESP_LOGE(TAG, "WebSocket 未连接，无法发送音频");
    // 连接已断开，重置状态
    if (g_state != CG_AI_STATE_IDLE) {
      ESP_LOGW(TAG, "检测到连接断开，重置状态到 IDLE");
      g_server_hello_received = false;
      g_session_id.clear();
      g_running = false; // 允许重新启动
      {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        g_audio_out_queue.clear();
      }
      set_state(CG_AI_STATE_IDLE);
    }
    return;
  }

  static int send_count = 0;
  send_count++;

  ESP_LOGI(TAG, ">>> 发送 Opus 帧 #%d: %zu 字节", send_count, opus_data.size());

  int ret =
      esp_websocket_client_send_bin(g_ws_client, (const char *)opus_data.data(),
                                    opus_data.size(), portMAX_DELAY);
  if (ret < 0) {
    ESP_LOGE(TAG, "!!! 发送失败，返回值: %d", ret);
  } else {
    ESP_LOGI(TAG, "<<< 发送成功，返回值: %d 字节", ret);
  }
}

static void websocket_send_start_listening(void) {
  if (g_ws_client && esp_websocket_client_is_connected(g_ws_client)) {
    // 包含 session_id（服务器需要识别会话）
    std::string msg =
        "{\"session_id\":\"" + g_session_id +
        "\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"auto\"}";
    esp_websocket_client_send_text(g_ws_client, msg.c_str(), msg.length(),
                                   portMAX_DELAY);
    ESP_LOGI(TAG, "发送开始监听消息: %s", msg.c_str());
  }
}

static void websocket_send_stop_listening(void) {
  if (g_ws_client && esp_websocket_client_is_connected(g_ws_client)) {
    std::string msg = "{\"session_id\":\"" + g_session_id +
                      "\",\"type\":\"listen\",\"state\":\"stop\"}";
    esp_websocket_client_send_text(g_ws_client, msg.c_str(), msg.length(),
                                   portMAX_DELAY);
    ESP_LOGI(TAG, "发送停止监听消息: %s", msg.c_str());

    // 发送一帧静音音频，触发服务器处理
    // 服务器在 handleAudioMessage 中检查
    // client_voice_stop，需要收到音频才能触发
    if (g_opus_encoder) {
      std::vector<int16_t> silence(OPUS_FRAME_SIZE, 0);
      g_opus_encoder->Encode(std::move(silence),
                             [](std::vector<uint8_t> &&opus) {
                               websocket_send_audio(opus);
                               ESP_LOGI(TAG, "发送静音帧触发服务器处理");
                             });
    }
  }
}

/**
 * 发送累积的音频数据（在断句时调用）
 * 发送完成后等待服务器 TTS 响应，只有 TTS 播放完成后才回到 LISTENING
 */
static void send_accumulated_audio(void) {
  // 如果已经断开连接，不再尝试发送
  if (!g_running || g_state == CG_AI_STATE_IDLE) {
    ESP_LOGW(TAG, "连接已断开，跳过发送累积音频");
    // 清空缓冲区
    std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
    g_speech_buffer.clear();
    return;
  }

  std::vector<int16_t> audio_to_send;

  {
    std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);

    if (g_speech_buffer.empty()) {
      return;
    }

    ESP_LOGI(TAG, "发送累积的音频数据，大小: %zu 采样", g_speech_buffer.size());

    // 复制数据，因为会被移动到后台任务
    audio_to_send = std::move(g_speech_buffer);
    g_speech_buffer.clear();
  }

  // 将累积的音频数据编码并发送
  if (g_background_task && g_opus_encoder) {
    size_t total_samples = audio_to_send.size();
    g_background_task->Schedule(
        [audio_to_send = std::move(audio_to_send), total_samples]() mutable {
          // 再次检查：任务执行时可能连接已断开
          if (!g_running || g_state == CG_AI_STATE_IDLE) {
            ESP_LOGW(TAG, "编码任务执行时连接已断开，跳过");
            return;
          }
          if (g_opus_encoder) {
            // 将音频数据分帧编码（Opus 每帧 960 采样，60ms）
            const size_t frame_size = OPUS_FRAME_SIZE;
            size_t offset = 0;
            int frame_count = 0;

            ESP_LOGI(TAG, "开始编码 %zu 采样，帧大小: %zu", total_samples,
                     frame_size);

            while (offset < audio_to_send.size()) {
              size_t remaining = audio_to_send.size() - offset;
              size_t current_frame_size =
                  (remaining > frame_size) ? frame_size : remaining;

              std::vector<int16_t> frame(audio_to_send.begin() + offset,
                                         audio_to_send.begin() + offset +
                                             current_frame_size);

              g_opus_encoder->Encode(std::move(frame),
                                     [](std::vector<uint8_t> &&opus) {
                                       websocket_send_audio(opus);
                                       update_activity_time();
                                     });

              offset += current_frame_size;
              frame_count++;
            }

            ESP_LOGI(TAG, "音频编码完成，共 %d 帧", frame_count);

            // 再次检查连接状态
            if (!g_running || g_state == CG_AI_STATE_IDLE) {
              ESP_LOGW(TAG, "编码完成但连接已断开，不改变状态");
              return;
            }

            // 发送停止监听消息，告诉服务器这段话说完了
            websocket_send_stop_listening();

            // 只有在 LISTENING 状态且仍在运行才切换到 SENDING
            if (g_running && g_state == CG_AI_STATE_LISTENING) {
              set_state(CG_AI_STATE_SENDING);
            }
          } else {
            ESP_LOGE(TAG, "Opus 编码器为空，无法编码");
          }

          ESP_LOGI(TAG, "音频编码发送完成");
        });
  } else {
    ESP_LOGE(TAG, "后台任务或编码器为空: bg_task=%p, encoder=%p",
             g_background_task.get(), g_opus_encoder.get());
  }

  ESP_LOGI(TAG, "累积音频已提交发送队列，等待 TTS 响应");
}

// ============== 麦克风任务 ==============

static void mic_task(void *pvParameters) {
  ESP_LOGI(TAG, "麦克风任务启动");

  std::vector<int16_t> pcm_buffer(MIC_READ_SAMPLES);

  // 等待 WebSocket 连接成功（先不启用麦克风，避免 DMA 缓冲区溢出）
  int wait_count = 0;
  while (!g_server_hello_received && g_running && wait_count < 100) {
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_count++;
  }

  if (!g_server_hello_received) {
    ESP_LOGE(TAG, "等待服务器 hello 超时");
    g_mic_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // 等待 AudioProcessor 初始化完成（最多等待 5 秒）
  wait_count = 0;
  while (!g_audio_processor && g_running && wait_count < 50) {
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_count++;
  }

  // 发送开始监听消息
  websocket_send_start_listening();
  set_state(CG_AI_STATE_LISTENING);
  update_activity_time();

  // 启动音频处理器
  if (g_audio_processor) {
    g_audio_processor->Start();
    ESP_LOGI(TAG, "音频处理器已启动，IsRunning=%d",
             g_audio_processor->IsRunning());
  } else {
    ESP_LOGW(TAG, "AudioProcessor 未初始化，使用直接编码模式");
  }

  // 清空音频缓冲区
  {
    std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
    g_speech_buffer.clear();
    g_is_speaking = false;
  }

  // 启用麦克风
  MIC_Enable(true);
  // 等待 I2S DMA 缓冲区稳定
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "开始录音...");

  while (g_running) {
    // 只在监听状态下录音（SENDING 和 SPEAKING 状态不录音）
    if (g_state != CG_AI_STATE_LISTENING) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // 读取麦克风数据（使用较长超时，与 koi_esp32 一致）
    size_t bytes_read = 0;
    esp_err_t ret =
        MIC_Read(pcm_buffer.data(), MIC_READ_SAMPLES, &bytes_read, 1000);

    // 每秒打印一次读取状态（约每50次）
    static int read_count = 0;
    if (++read_count % 50 == 1) {
      ESP_LOGI(
          TAG,
          "MIC_Read: ret=%d, bytes_read=%d, state=%d, processor=%p, running=%d",
          ret, bytes_read, g_state, g_audio_processor.get(),
          g_audio_processor ? g_audio_processor->IsRunning() : 0);
    }

    if (ret == ESP_OK && bytes_read > 0) {
      // 再次检查状态，避免在 MIC_Read 阻塞期间状态变化
      if (g_state != CG_AI_STATE_LISTENING) {
        continue;
      }

      size_t samples_read = bytes_read / sizeof(int16_t);

      // 创建临时 vector
      std::vector<int16_t> pcm_data(pcm_buffer.begin(),
                                    pcm_buffer.begin() + samples_read);

      // 使用音频处理器进行 VAD 检测和音频处理
      static int input_count = 0;
      if (g_audio_processor && g_audio_processor->IsRunning()) {
        g_audio_processor->Input(pcm_data);
        if (++input_count % 50 == 1) {
          ESP_LOGI(TAG, "已输入 %d 次音频到 AudioProcessor, samples=%d",
                   input_count, samples_read);
        }
      } else {
        // 回退：如果没有音频处理器，直接编码发送
        static int fallback_count = 0;
        if (++fallback_count % 50 == 1) {
          ESP_LOGI(TAG, "直接发送音频 #%d, samples=%d", fallback_count,
                   samples_read);
        }
        if (g_background_task) {
          g_background_task->Schedule(
              [pcm_data = std::move(pcm_data)]() mutable {
                if (g_opus_encoder) {
                  g_opus_encoder->Encode(std::move(pcm_data),
                                         [](std::vector<uint8_t> &&opus) {
                                           websocket_send_audio(opus);
                                           update_activity_time();
                                         });
                }
              });
        }
      }
    }

    // 检查超时
    if (cg_ai_service_is_timeout(CLOSE_CONNECTION_NO_VOICE_TIME)) {
      ESP_LOGW(TAG, "超时无语音输入，停止服务");
      g_running = false;
      break;
    }
  }

  // 停止音频处理器
  if (g_audio_processor) {
    g_audio_processor->Stop();
    ESP_LOGI(TAG, "音频处理器已停止");
  }

  // 发送剩余的累积音频（如果有）
  {
    bool has_remaining = false;
    {
      std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
      has_remaining = !g_speech_buffer.empty();
    }
    if (has_remaining) {
      ESP_LOGI(TAG, "发送剩余的累积音频");
      send_accumulated_audio();
    }
  }

  MIC_Enable(false);
  ESP_LOGI(TAG, "麦克风任务结束");
  g_mic_task_handle = nullptr;
  vTaskDelete(nullptr);
}

// ============== 音频输出任务 ==============

static void audio_out_task(void *pvParameters) {
  ESP_LOGI(TAG, "音频输出任务启动");

  int empty_count = 0;           // 队列为空的计数
  bool has_played_audio = false; // 是否播放过音频

  while (g_running) {
    std::vector<uint8_t> opus_data;
    bool queue_empty = false;

    {
      std::lock_guard<std::mutex> lock(g_audio_mutex);
      if (!g_audio_out_queue.empty()) {
        opus_data = std::move(g_audio_out_queue.front());
        g_audio_out_queue.pop_front();
        empty_count = 0;
        has_played_audio = true; // 标记已播放过音频
      } else {
        queue_empty = true;
      }
    }

    if (!opus_data.empty() && g_opus_decoder) {
      // 解码 Opus 数据
      std::vector<int16_t> pcm;
      if (g_opus_decoder->Decode(std::move(opus_data), pcm)) {
        // 打印解码结果（调试用，每 10 帧打印一次）
        static int decode_count = 0;
        if (++decode_count % 10 == 1) {
          ESP_LOGI(TAG, "解码成功 #%d, PCM samples=%zu", decode_count,
                   pcm.size());
        }
        // 播放音频（使用足够长的超时确保写入完成）
        Audio_I2S_Write(pcm.data(), pcm.size(), 1000);
      } else {
        ESP_LOGW(TAG, "Opus 解码失败，数据大小=%zu", opus_data.size());
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));

      // 只有播放过音频后，队列持续为空才恢复监听
      if (queue_empty && g_state == CG_AI_STATE_SPEAKING && has_played_audio) {
        empty_count++;
        // 队列为空超过 500ms（50 次 * 10ms），认为播放完成
        if (empty_count > 50) {
          ESP_LOGI(TAG, "TTS 播放完成，自动恢复监听");
          set_state(CG_AI_STATE_LISTENING);
          empty_count = 0;
          has_played_audio = false; // 重置标志
        }
      }

      // 状态不是 SPEAKING 时，重置标志
      if (g_state != CG_AI_STATE_SPEAKING) {
        has_played_audio = false;
        empty_count = 0;
      }
    }
  }

  // 清空队列
  {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    g_audio_out_queue.clear();
  }

  ESP_LOGI(TAG, "音频输出任务结束");
  g_audio_out_task_handle = nullptr;
  vTaskDelete(nullptr);
}

// ============== 公共接口（C 兼容） ==============

extern "C" {

esp_err_t cg_ai_service_init(void) {
  ESP_LOGI(TAG, "初始化 AI 服务...");

  // 检查是否已经初始化（通过检查关键对象是否存在）
  if (g_opus_encoder != nullptr && g_audio_processor != nullptr) {
    ESP_LOGW(TAG, "AI 服务已经初始化，跳过重复初始化");
    return ESP_OK;
  }

  if (g_state_mutex == nullptr) {
    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == nullptr) {
      ESP_LOGE(TAG, "创建互斥锁失败");
      return ESP_ERR_NO_MEM;
    }
  }

  // 初始化麦克风
  esp_err_t ret = MIC_Init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "初始化麦克风失败");
    return ret;
  }

  // 初始化 Opus 编码器
  g_opus_encoder = std::make_unique<OpusEncoderWrapper>(
      OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_FRAME_DURATION_MS);
  if (!g_opus_encoder) {
    ESP_LOGE(TAG, "创建 Opus 编码器失败（内存不足）");
    return ESP_ERR_NO_MEM;
  }
  g_opus_encoder->SetComplexity(3); // 降低复杂度以节省 CPU

  // 初始化 Opus 解码器（只传采样率和通道数，使用默认 60ms 帧时长）
  g_opus_decoder =
      std::make_unique<OpusDecoderWrapper>(OPUS_SAMPLE_RATE, OPUS_CHANNELS);
  if (!g_opus_decoder) {
    ESP_LOGE(TAG, "创建 Opus 解码器失败（内存不足）");
    return ESP_ERR_NO_MEM;
  }

  // 初始化后台编码任务（栈大小 32KB，使用 PSRAM）
  static const size_t BG_TASK_STACK_SIZE = 32768;
  g_background_task = std::make_unique<BackgroundTask>(BG_TASK_STACK_SIZE);
  if (!g_background_task) {
    ESP_LOGE(TAG, "创建后台任务失败（内存不足）");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "后台编码任务已创建");

  // 注意：AudioProcessor（esp-sr）延迟初始化
  // 在 WebSocket 连接成功后再初始化，避免内存不足导致 SSL 握手失败
  ESP_LOGI(TAG, "AudioProcessor 将在 WebSocket 连接成功后初始化");

  // 初始化 I2S 输出
  ret = i2s_output_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "I2S 输出初始化警告: %s（可能已被音频播放器使用）",
             esp_err_to_name(ret));
    // 不返回错误，因为 I2S 可能已经被 Audio_Init 初始化
  }

  ESP_LOGI(TAG, "AI 服务初始化成功");
  return ESP_OK;
}

void cg_ai_service_deinit(void) {
  cg_ai_service_stop();

  // 停止并清理音频处理器（必须在后台任务之前停止）
  if (g_audio_processor) {
    g_audio_processor->Stop();
    // 等待任务结束
    vTaskDelay(pdMS_TO_TICKS(100));
    g_audio_processor.reset();
  }

  // 等待后台任务完成
  if (g_background_task) {
    g_background_task->WaitForCompletion();
    g_background_task.reset();
  }

  // 清空音频缓冲区
  {
    std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
    g_speech_buffer.clear();
    g_pre_speech_buffer.clear();
    g_is_speaking = false;
  }

  // 清空 WebSocket 二进制缓冲区
  g_ws_binary_buffer.clear();

  g_opus_encoder.reset();
  g_opus_decoder.reset();

  MIC_Deinit();
  i2s_output_deinit();

  if (g_state_mutex) {
    vSemaphoreDelete(g_state_mutex);
    g_state_mutex = nullptr;
  }

  ESP_LOGI(TAG, "AI 服务已反初始化");
}

esp_err_t cg_ai_service_start(void) {
  if (g_running) {
    ESP_LOGW(TAG, "AI 服务已在运行");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "启动 AI 服务...");
  ESP_LOGI(TAG, "目标 URL: %s", CG_AI_URL);

  set_state(CG_AI_STATE_CONNECTING);
  g_running = true;
  g_server_hello_received = false;
  update_activity_time();

  // 重置编码器状态
  if (g_opus_encoder) {
    g_opus_encoder->ResetState();
  }
  if (g_opus_decoder) {
    g_opus_decoder->ResetState();
  }

  // 清空音频队列
  {
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    g_audio_out_queue.clear();
  }

  // 连接 WebSocket
  if (websocket_connect() != 0) {
    ESP_LOGE(TAG, "WebSocket 连接失败");
    g_running = false;
    set_state(CG_AI_STATE_ERROR);
    return ESP_FAIL;
  }

  // 启动音频输出任务（Opus 解码需要较大栈空间）
  xTaskCreatePinnedToCore(audio_out_task, "ai_audio_out", 16384, nullptr, 4,
                          &g_audio_out_task_handle, 1);

  // 启动麦克风任务（使用 PSRAM，Opus 编码需要约 20KB+ 栈空间）
  // 栈大小设为 32KB 以确保足够
  static const size_t MIC_TASK_STACK_SIZE = 32768;
  static StaticTask_t mic_task_buffer;
  static StackType_t *mic_task_stack = nullptr;

  if (mic_task_stack == nullptr) {
    // 在 PSRAM 中分配栈空间
    mic_task_stack = (StackType_t *)heap_caps_malloc(
        MIC_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mic_task_stack == nullptr) {
      ESP_LOGE(TAG, "无法在 PSRAM 中分配麦克风任务栈");
      // 回退到普通内存（可能会失败，因为内部 RAM 有限）
      xTaskCreatePinnedToCore(mic_task, "ai_mic",
                              MIC_TASK_STACK_SIZE / sizeof(StackType_t),
                              nullptr, 5, &g_mic_task_handle, 0);
    } else {
      ESP_LOGI(TAG, "麦克风任务栈已在 PSRAM 中分配 (%d 字节)",
               MIC_TASK_STACK_SIZE);
      g_mic_task_handle = xTaskCreateStaticPinnedToCore(
          mic_task, "ai_mic", MIC_TASK_STACK_SIZE / sizeof(StackType_t),
          nullptr, 5, mic_task_stack, &mic_task_buffer, 0);
    }
  } else {
    g_mic_task_handle = xTaskCreateStaticPinnedToCore(
        mic_task, "ai_mic", MIC_TASK_STACK_SIZE / sizeof(StackType_t), nullptr,
        5, mic_task_stack, &mic_task_buffer, 0);
  }

  return ESP_OK;
}

void cg_ai_service_stop(void) {
  if (!g_running && g_state == CG_AI_STATE_IDLE) {
    return;
  }

  ESP_LOGI(TAG, "停止 AI 服务...");

  g_running = false;

  // 停止音频处理器
  if (g_audio_processor) {
    g_audio_processor->Stop();
  }

  // 发送剩余的累积音频（如果有）
  {
    bool has_remaining = false;
    {
      std::lock_guard<std::mutex> lock(g_speech_buffer_mutex);
      has_remaining = !g_speech_buffer.empty();
    }
    if (has_remaining) {
      ESP_LOGI(TAG, "停止时发送剩余的累积音频");
      send_accumulated_audio();
    }
  }

  // 等待后台任务完成
  if (g_background_task) {
    g_background_task->WaitForCompletion();
  }

  // 等待任务结束
  int wait = 0;
  while ((g_mic_task_handle != nullptr || g_audio_out_task_handle != nullptr) &&
         wait < 50) {
    vTaskDelay(pdMS_TO_TICKS(100));
    wait++;
  }

  // 断开 WebSocket
  websocket_disconnect();

  set_state(CG_AI_STATE_IDLE);

  ESP_LOGI(TAG, "AI 服务已停止");
}

cg_ai_state_t cg_ai_service_get_state(void) { return g_state; }

bool cg_ai_service_is_active(void) {
  return g_state != CG_AI_STATE_IDLE && g_state != CG_AI_STATE_ERROR;
}

void cg_ai_service_set_state_callback(cg_ai_state_callback_t callback,
                                      void *user_data) {
  g_state_callback = callback;
  g_callback_user_data = user_data;
}

uint32_t cg_ai_service_get_last_activity_time(void) {
  return g_last_activity_time;
}

bool cg_ai_service_is_timeout(uint32_t timeout_sec) {
  if (g_last_activity_time == 0) {
    return false;
  }

  uint32_t now = get_time_ms();
  uint32_t elapsed_sec = (now - g_last_activity_time) / 1000;

  return elapsed_sec >= timeout_sec;
}

} // extern "C"
