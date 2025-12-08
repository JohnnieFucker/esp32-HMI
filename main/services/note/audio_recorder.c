#include "audio_recorder.h"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

// 使用 MIC_Speech 驱动统一管理 I2S 通道
#include "mic_driver.h"

// 标记是否使用共享的 MIC 驱动
static bool g_use_shared_mic = false;

static const char *TAG = "AudioRecorder";

// 麦克风增益（如果未定义，使用默认值）
#ifndef MIC_GAIN
#define MIC_GAIN 1
#endif

// 默认配置
#define DEFAULT_SAMPLE_RATE 16000
#define DEFAULT_CHANNELS 1
#define DEFAULT_BITS_PER_SAMPLE 16
#define DEFAULT_BUFFER_SIZE 4096
#define DEFAULT_GPIO_BCLK GPIO_NUM_15
#define DEFAULT_GPIO_WS GPIO_NUM_2
#define DEFAULT_GPIO_DIN GPIO_NUM_39

// 录音状态
static bool g_recording = false;
static TaskHandle_t g_record_task_handle = NULL;
static audio_recorder_config_t g_config;
static char g_current_filepath[256] = {0};
static uint32_t g_duration_sec = 0;
static audio_recorder_data_cb_t g_data_cb = NULL;
static void *g_user_data = NULL;

// WAV文件头结构
typedef struct {
    char chunk_id[4];        // "RIFF"
    uint32_t chunk_size;     // 文件大小 - 8
    char format[4];          // "WAVE"
    char subchunk1_id[4];    // "fmt "
    uint32_t subchunk1_size;  // 16 for PCM
    uint16_t audio_format;   // 1 for PCM
    uint16_t num_channels;   // 1 or 2
    uint32_t sample_rate;    // 16000, 44100, etc.
    uint32_t byte_rate;      // sample_rate * num_channels * bits_per_sample / 8
    uint16_t block_align;    // num_channels * bits_per_sample / 8
    uint16_t bits_per_sample;// 16, 24, 32, etc.
    char subchunk2_id[4];    // "data"
    uint32_t subchunk2_size; // data_size
} wav_header_t;

/**
 * 内部辅助函数：读取音频数据
 * 使用 MIC_Speech 接口统一读取
 * @param buffer 输出缓冲区（16 位 PCM）
 * @param samples 要读取的采样数
 * @param bytes_read 实际读取的字节数
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功，其他值表示失败
 */
static esp_err_t audio_recorder_read(int16_t *buffer, size_t samples, size_t *bytes_read, uint32_t timeout_ms) {
    if (!g_use_shared_mic) {
        return ESP_ERR_INVALID_STATE;
    }
    return MIC_Read(buffer, samples, bytes_read, timeout_ms);
}

// 录音任务（文件模式）
static void record_to_file_task(void *pvParameters) {
    uint8_t *buffer = (uint8_t *)malloc(g_config.buffer_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate record buffer");
        g_recording = false;
        vTaskDelete(NULL);
        return;
    }

    // 打开WAV文件
    FILE *wav_file = fopen(g_current_filepath, "wb");
    if (wav_file == NULL) {
        ESP_LOGE(TAG, "无法创建WAV文件: %s", g_current_filepath);
        free(buffer);
        g_recording = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "开始录音: %s", g_current_filepath);

    // 先写入占位符头部（稍后更新）
    audio_recorder_create_wav_header(g_current_filepath, 0, g_config.sample_rate,
                                     g_config.channels, g_config.bits_per_sample);

    // 计算需要录制的数据量
    int samples_to_record = 0;
    int total_bytes = 0;
    if (g_duration_sec > 0) {
        samples_to_record = g_config.sample_rate * g_duration_sec;
        int bytes_per_sample = g_config.channels * g_config.bits_per_sample / 8;
        total_bytes = samples_to_record * bytes_per_sample;
    }

    int bytes_recorded = 0;

    while (g_recording) {
        size_t bytes_read = 0;
        int samples_to_read = g_config.buffer_size / sizeof(int16_t);
        
        if (g_duration_sec > 0 && bytes_recorded >= total_bytes) {
            break;  // 达到指定时长
        }
        
        if (g_duration_sec > 0 && (total_bytes - bytes_recorded) < g_config.buffer_size) {
            samples_to_read = (total_bytes - bytes_recorded) / sizeof(int16_t);
        }

        // 使用统一的读取接口（支持共享 MIC 和独立通道）
        esp_err_t ret = audio_recorder_read((int16_t *)buffer, samples_to_read, &bytes_read, 100);
        if (ret == ESP_OK && bytes_read > 0) {
            int samples = bytes_read / sizeof(int16_t);
            fwrite(buffer, sizeof(int16_t), samples, wav_file);
            bytes_recorded += bytes_read;
        }
    }

    // 更新WAV文件头
    fseek(wav_file, 0, SEEK_SET);
    audio_recorder_create_wav_header(g_current_filepath, bytes_recorded,
                                     g_config.sample_rate, g_config.channels,
                                     g_config.bits_per_sample);
    fclose(wav_file);

    if (!g_recording) {
        // 如果停止录音，删除未完成的文件
        remove(g_current_filepath);
        ESP_LOGI(TAG, "录音已停止，删除未完成文件");
    } else {
        ESP_LOGI(TAG, "录音完成，文件大小: %d 字节", bytes_recorded);
    }

    free(buffer);
    g_recording = false;
    g_record_task_handle = NULL;
    memset(g_current_filepath, 0, sizeof(g_current_filepath));
    vTaskDelete(NULL);
}

// 录音任务（回调模式）
static void record_with_callback_task(void *pvParameters) {
    uint8_t *buffer = (uint8_t *)malloc(g_config.buffer_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate record buffer");
        g_recording = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "开始录音（回调模式）");
    
    size_t total_bytes_read = 0;
    size_t read_count = 0;
    size_t empty_read_count = 0;

    int samples_to_read = g_config.buffer_size / sizeof(int16_t);
    
    while (g_recording && g_data_cb != NULL) {
        size_t bytes_read = 0;
        // 使用统一的读取接口（支持共享 MIC 和独立通道）
        // 使用较长的超时时间确保能读取到数据
        esp_err_t ret = audio_recorder_read((int16_t *)buffer, samples_to_read, &bytes_read, 1000);
        read_count++;
        
        if (ret == ESP_OK && bytes_read > 0) {
            total_bytes_read += bytes_read;
            empty_read_count = 0;  // 重置空读取计数
            
            int16_t *out_buffer = (int16_t *)buffer;
            int samples = bytes_read / sizeof(int16_t);
            
            // 每50次打印一次数据统计（约5秒）
            static int debug_counter = 0;
            if (debug_counter++ % 50 == 0) {
                int16_t out_max = 0, out_min = 0;
                for (int i = 0; i < samples; i++) {
                    if (out_buffer[i] > out_max) out_max = out_buffer[i];
                    if (out_buffer[i] < out_min) out_min = out_buffer[i];
                }
                ESP_LOGI(TAG, "音频: 样本数=%d, 输出范围[%d~%d], 增益=%dx", 
                         samples, out_min, out_max, MIC_GAIN);
            }
            
            // 调用回调函数
            if (!g_data_cb(out_buffer, bytes_read, g_user_data)) {
                ESP_LOGI(TAG, "回调函数返回 false，停止录音");
                break;  // 回调返回false，停止录音
            }
        } else {
            empty_read_count++;
            // 如果连续多次读取为空，可能是I2S配置问题或没有数据输入
            if (empty_read_count == 50) {  // 5秒没有数据
                ESP_LOGW(TAG, "I2S 连续 %d 次读取为空 (ret=%s, bytes_read=%d)，可能没有音频输入", 
                         empty_read_count, esp_err_to_name(ret), bytes_read);
            }
        }
    }
    
    ESP_LOGI(TAG, "录音任务结束：总读取次数=%d, 总读取字节=%d, 空读取次数=%d", 
             read_count, total_bytes_read, empty_read_count);

    free(buffer);
    g_recording = false;
    g_record_task_handle = NULL;
    g_data_cb = NULL;
    g_user_data = NULL;
    ESP_LOGI(TAG, "录音任务结束（回调模式）");
    vTaskDelete(NULL);
}

esp_err_t audio_recorder_init(const audio_recorder_config_t *config) {
    if (g_use_shared_mic) {
        return ESP_OK;  // 已经初始化
    }

    // 设置配置
    if (config != NULL) {
        memcpy(&g_config, config, sizeof(audio_recorder_config_t));
    } else {
        // 使用默认配置
        g_config.sample_rate = DEFAULT_SAMPLE_RATE;
        g_config.channels = DEFAULT_CHANNELS;
        g_config.bits_per_sample = DEFAULT_BITS_PER_SAMPLE;
        g_config.buffer_size = DEFAULT_BUFFER_SIZE;
        g_config.gpio_bclk = DEFAULT_GPIO_BCLK;
        g_config.gpio_ws = DEFAULT_GPIO_WS;
        g_config.gpio_din = DEFAULT_GPIO_DIN;
    }

    // 使用 MIC_Speech 驱动（统一管理 I2S 通道，避免冲突）
    esp_err_t ret = MIC_Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MIC_Init 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_use_shared_mic = true;
    ESP_LOGI(TAG, "AudioRecorder 使用 MIC_Speech 驱动初始化成功");
    return ESP_OK;
}

esp_err_t audio_recorder_deinit(void) {
    if (g_recording) {
        audio_recorder_stop();
    }

    if (g_use_shared_mic) {
        // 完全释放 MIC 资源（包括 I2S 通道）
        MIC_Deinit();
        g_use_shared_mic = false;
        ESP_LOGI(TAG, "MIC 驱动已释放");
    }

    return ESP_OK;
}

esp_err_t audio_recorder_start(const char *filepath, uint32_t duration_sec) {
    if (g_recording) {
        ESP_LOGW(TAG, "录音已在进行中");
        return ESP_ERR_INVALID_STATE;
    }

    if (filepath == NULL) {
        ESP_LOGE(TAG, "文件路径不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 检查是否已初始化
    if (!g_use_shared_mic) {
        esp_err_t ret = audio_recorder_init(NULL);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 启用 MIC
    MIC_Enable(true);

    strncpy(g_current_filepath, filepath, sizeof(g_current_filepath) - 1);
    g_duration_sec = duration_sec;
    g_data_cb = NULL;
    g_user_data = NULL;

    g_recording = true;
    xTaskCreate(record_to_file_task, "record_to_file_task", 8192, NULL, 5, &g_record_task_handle);
    
    ESP_LOGI(TAG, "开始录音到文件: %s", filepath);
    return ESP_OK;
}

esp_err_t audio_recorder_start_with_callback(audio_recorder_data_cb_t data_cb, void *user_data) {
    if (g_recording) {
        ESP_LOGW(TAG, "录音已在进行中");
        return ESP_ERR_INVALID_STATE;
    }

    if (data_cb == NULL) {
        ESP_LOGE(TAG, "回调函数不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 检查是否已初始化
    if (!g_use_shared_mic) {
        esp_err_t ret = audio_recorder_init(NULL);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // 启用 MIC
    MIC_Enable(true);

    g_data_cb = data_cb;
    g_user_data = user_data;
    memset(g_current_filepath, 0, sizeof(g_current_filepath));
    g_duration_sec = 0;

    g_recording = true;
    xTaskCreate(record_with_callback_task, "record_with_callback_task", 8192, NULL, 5, &g_record_task_handle);
    
    ESP_LOGI(TAG, "开始录音（回调模式）");
    return ESP_OK;
}

esp_err_t audio_recorder_stop(void) {
    if (!g_recording) {
        ESP_LOGW(TAG, "录音未在进行");
        return ESP_OK;
    }

    g_recording = false;
    
    // 等待任务结束
    if (g_record_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 等待最多1秒
    }

    // 如果使用共享 MIC，禁用它
    if (g_use_shared_mic) {
        MIC_Enable(false);
    }

    ESP_LOGI(TAG, "停止录音");
    return ESP_OK;
}

bool audio_recorder_is_recording(void) {
    return g_recording;
}

esp_err_t audio_recorder_create_wav_header(const char *filepath, uint32_t data_size,
                                          uint16_t sample_rate, uint16_t channels,
                                          uint16_t bits_per_sample) {
    FILE *file = fopen(filepath, "r+b");
    if (file == NULL) {
        // 如果文件不存在，创建新文件
        file = fopen(filepath, "wb");
        if (file == NULL) {
            ESP_LOGE(TAG, "无法打开文件: %s", filepath);
            return ESP_ERR_NOT_FOUND;
        }
    }

    wav_header_t header;
    memset(&header, 0, sizeof(header));
    
    memcpy(header.chunk_id, "RIFF", 4);
    header.chunk_size = data_size + sizeof(header) - 8;
    memcpy(header.format, "WAVE", 4);
    memcpy(header.subchunk1_id, "fmt ", 4);
    header.subchunk1_size = 16;
    header.audio_format = 1;  // PCM
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.block_align = channels * bits_per_sample / 8;
    header.bits_per_sample = bits_per_sample;
    memcpy(header.subchunk2_id, "data", 4);
    header.subchunk2_size = data_size;
    
    fwrite(&header, sizeof(header), 1, file);
    fclose(file);

    return ESP_OK;
}

