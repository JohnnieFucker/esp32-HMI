#include "note_service.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "audio_recorder.h"
#include "http_client.h"
#include "utils.h"
#include "wifi_service.h"  // 添加 WiFi 状态检测
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "NoteService";

// 上传配置
#define UPLOAD_TIMEOUT_MS 30000       // 上传超时时间：30秒（原来是120秒）
#define UPLOAD_MAX_RETRIES 3          // 上传最大重试次数
#define UPLOAD_RETRY_DELAY_MS 2000    // 重试间隔：2秒
#define WIFI_WAIT_TIMEOUT_MS 10000    // WiFi 等待超时：10秒
#define QUEUE_WAIT_TIMEOUT_SEC 120    // 队列等待超时：2分钟

// 录音配置（从 app_config.h 读取）
#define RECORD_SAMPLE_RATE 16000
#define RECORD_CHANNELS 1
#define RECORD_BITS_PER_SAMPLE 16

// 上传队列项
typedef struct {
    uint8_t *wav_buffer;     // WAV数据缓冲区
    size_t wav_size;         // WAV文件大小
    char filename[128];      // 文件名
} upload_item_t;

// 录音数据缓冲区结构
typedef struct {
    uint8_t *buffer;        // 音频数据缓冲区
    size_t buffer_size;      // 缓冲区大小
    size_t data_size;        // 实际数据大小
    bool recording;          // 是否正在录音
} audio_buffer_t;

// 录音任务句柄和运行标志
static TaskHandle_t g_record_task_handle = NULL;
static TaskHandle_t g_upload_task_handle = NULL;
static bool g_record_task_running = false;

// 上传队列
static QueueHandle_t g_upload_queue = NULL;
#define UPLOAD_QUEUE_SIZE 2  // 最多缓存2个待上传文件

// 录音会话信息（在开始录音时初始化）
static char g_current_uuid[64] = {0};
static int g_file_counter = 0;

// 录音数据回调函数：将音频数据收集到内存缓冲区
static bool audio_data_callback(const void *data, size_t size, void *user_data) {
    audio_buffer_t *audio_buf = (audio_buffer_t *)user_data;
    
    if (audio_buf == NULL) {
        ESP_LOGE(TAG, "回调函数：audio_buf 为 NULL");
        return false;
    }
    
    // 检查全局停止标志
    if (!g_record_task_running) {
        ESP_LOGI(TAG, "回调函数：检测到停止标志，停止录音");
        audio_buf->recording = false;
        return false;
    }
    
    if (!audio_buf->recording) {
        return false;
    }
    
    // 检查缓冲区是否有足够空间
    if (audio_buf->data_size + size > audio_buf->buffer_size) {
        ESP_LOGW(TAG, "音频缓冲区已满，停止录音");
        return false;
    }
    
    // 复制数据到缓冲区
    memcpy(audio_buf->buffer + audio_buf->data_size, data, size);
    audio_buf->data_size += size;
    
    return true;
}

// 创建WAV文件头（在内存中）
static void create_wav_header_in_memory(uint8_t *buffer, uint32_t data_size,
                                        uint16_t sample_rate, uint16_t channels,
                                        uint16_t bits_per_sample) {
    typedef struct {
        char chunk_id[4];
        uint32_t chunk_size;
        char format[4];
        char subchunk1_id[4];
        uint32_t subchunk1_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char subchunk2_id[4];
        uint32_t subchunk2_size;
    } wav_header_t;

    wav_header_t *header = (wav_header_t *)buffer;
    memset(header, 0, sizeof(wav_header_t));
    
    memcpy(header->chunk_id, "RIFF", 4);
    header->chunk_size = data_size + sizeof(wav_header_t) - 8;
    memcpy(header->format, "WAVE", 4);
    memcpy(header->subchunk1_id, "fmt ", 4);
    header->subchunk1_size = 16;
    header->audio_format = 1;
    header->num_channels = channels;
    header->sample_rate = sample_rate;
    header->byte_rate = sample_rate * channels * bits_per_sample / 8;
    header->block_align = channels * bits_per_sample / 8;
    header->bits_per_sample = bits_per_sample;
    memcpy(header->subchunk2_id, "data", 4);
    header->subchunk2_size = data_size;
}

// 上传任务运行标志（独立于录音任务）
static bool g_upload_task_running = false;

// 等待 WiFi 连接的辅助函数
static bool wait_for_wifi(int timeout_ms) {
    int waited = 0;
    while (!WiFi_IsConnected() && waited < timeout_ms) {
        ESP_LOGW(TAG, "等待 WiFi 连接... (%d/%d ms)", waited, timeout_ms);
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited += 1000;
    }
    return WiFi_IsConnected();
}

// 上传任务（异步上传，不阻塞录音）
static void upload_task(void *pvParameters) {
    ESP_LOGI(TAG, "上传任务启动");
    g_upload_task_running = true;
    
    upload_item_t item;
    
    // 持续运行直到被明确停止，并且队列为空
    while (g_upload_task_running || uxQueueMessagesWaiting(g_upload_queue) > 0) {
        // 等待上传队列中的数据（最多等待1秒）
        if (xQueueReceive(g_upload_queue, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "========== 开始上传 ==========");
            ESP_LOGI(TAG, "文件名: %s", item.filename);
            ESP_LOGI(TAG, "文件大小: %zu KB", item.wav_size / 1024);
            ESP_LOGI(TAG, "队列剩余: %d 个文件", uxQueueMessagesWaiting(g_upload_queue));
            ESP_LOGI(TAG, "==============================");
            
            // 检查 WiFi 连接状态，如果断开则等待重连
            if (!WiFi_IsConnected()) {
                ESP_LOGW(TAG, "WiFi 未连接，等待重连...");
                if (!wait_for_wifi(WIFI_WAIT_TIMEOUT_MS)) {
                    ESP_LOGE(TAG, "WiFi 连接超时，丢弃文件: %s", item.filename);
                    free(item.wav_buffer);
                    ESP_LOGI(TAG, "已释放上传缓冲区，队列剩余: %d", uxQueueMessagesWaiting(g_upload_queue));
                    continue;
                }
                ESP_LOGI(TAG, "WiFi 已重新连接，继续上传");
            }
            
            // 构建上传URL
            char url[512];
            snprintf(url, sizeof(url), "%s%s", CG_API_URL, API_UPLOAD);
            
            http_request_config_t upload_config = {
                .url = url,
                .method = "POST",
                .token = CG_TOKEN,
                .timeout_ms = UPLOAD_TIMEOUT_MS,  // 使用新的超时时间
                .ssl_verify_mode = HTTP_SSL_VERIFY_NONE,
            };

            // 添加重试机制
            int retry_count = 0;
            bool upload_success = false;
            
            while (retry_count < UPLOAD_MAX_RETRIES && !upload_success) {
                if (retry_count > 0) {
                    ESP_LOGW(TAG, "第 %d 次重试上传: %s", retry_count, item.filename);
                    vTaskDelay(pdMS_TO_TICKS(UPLOAD_RETRY_DELAY_MS));
                    
                    // 重试前再次检查 WiFi 状态
                    if (!WiFi_IsConnected()) {
                        ESP_LOGW(TAG, "WiFi 断开，等待重连...");
                        if (!wait_for_wifi(WIFI_WAIT_TIMEOUT_MS)) {
                            ESP_LOGE(TAG, "WiFi 连接超时，停止重试");
                            break;
                        }
                    }
                }
                
                int status_code = 0;
                char response_buffer[512] = {0};
                esp_err_t ret = http_client_post_multipart_from_memory(
                    &upload_config, item.wav_buffer, item.wav_size,
                    item.filename, "file", "fileName",
                    &status_code, response_buffer, sizeof(response_buffer));
                
                if (ret == ESP_OK && status_code == 200) {
                    ESP_LOGI(TAG, "上传成功: %s", item.filename);
                    upload_success = true;
                } else {
                    ESP_LOGW(TAG, "上传失败: %s, 状态码: %d, 错误: %s", 
                             item.filename, status_code, esp_err_to_name(ret));
                    retry_count++;
                }
            }
            
            if (!upload_success) {
                ESP_LOGE(TAG, "上传最终失败（重试 %d 次）: %s", retry_count, item.filename);
            }
            
            // 释放缓冲区
            free(item.wav_buffer);
            ESP_LOGI(TAG, "已释放上传缓冲区，队列剩余: %d", uxQueueMessagesWaiting(g_upload_queue));
        }
    }
    
    ESP_LOGI(TAG, "上传任务结束（所有文件已处理）");
    g_upload_task_handle = NULL;
    vTaskDelete(NULL);
}

// 录音任务（连续录音，录音完成后放入上传队列）
static void record_task(void *pvParameters) {
    ESP_LOGI(TAG, "录音任务启动（UUID: %s）", g_current_uuid);
    
    const size_t wav_header_size = 44;
    const int bytes_per_sample = RECORD_CHANNELS * RECORD_BITS_PER_SAMPLE / 8;
    const int samples_to_record = RECORD_SAMPLE_RATE * RECORD_DURATION_SEC;
    const size_t audio_data_size = samples_to_record * bytes_per_sample;
    const size_t total_buffer_size = wav_header_size + audio_data_size;

    // 队列等待计时器
    uint32_t queue_wait_start = 0;
    
    while (g_record_task_running) {
        
        // 检查可用内存
        size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "内存状态 - PSRAM空闲: %d KB, 需要: %d KB, 上传队列: %d/%d",
                 free_spiram / 1024, total_buffer_size / 1024,
                 uxQueueMessagesWaiting(g_upload_queue), UPLOAD_QUEUE_SIZE);
        
        // 如果上传队列已满，等待队列有空位（添加超时机制）
        if (uxQueueMessagesWaiting(g_upload_queue) >= UPLOAD_QUEUE_SIZE) {
            // 初始化等待计时
            if (queue_wait_start == 0) {
                queue_wait_start = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
            }
            
            uint32_t now_sec = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
            uint32_t waited_sec = now_sec - queue_wait_start;
            
            // 检查是否超时
            if (waited_sec >= QUEUE_WAIT_TIMEOUT_SEC) {
                ESP_LOGE(TAG, "等待上传队列超时 (%lu 秒)，检查网络状态...", (unsigned long)waited_sec);
                
                // 检查 WiFi 状态
                if (!WiFi_IsConnected()) {
                    ESP_LOGE(TAG, "WiFi 已断开！停止录音任务。");
                    // 不再继续等待，让上传任务处理失败情况
                    g_record_task_running = false;
                    break;
                }
                
                // WiFi 正常但上传仍然卡住，重置等待计时器继续等待
                ESP_LOGW(TAG, "WiFi 正常，继续等待上传完成...");
                queue_wait_start = now_sec;
            }
            
            ESP_LOGW(TAG, "上传队列已满，等待上传完成... (已等待 %lu 秒)", (unsigned long)waited_sec);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // 队列有空位，重置等待计时器
        queue_wait_start = 0;
        
        // 分配内存（优先PSRAM）
        uint8_t *wav_buffer = NULL;
        if (free_spiram >= total_buffer_size) {
            wav_buffer = (uint8_t *)heap_caps_malloc(total_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (wav_buffer == NULL) {
            wav_buffer = (uint8_t *)malloc(total_buffer_size);
        }
        
        if (wav_buffer == NULL) {
            ESP_LOGE(TAG, "无法分配录音缓冲区，等待 5 秒后重试");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        ESP_LOGI(TAG, "分配录音缓冲区成功: %d KB", total_buffer_size / 1024);
        
        // 初始化音频缓冲区
        audio_buffer_t audio_buf = {
            .buffer = wav_buffer + wav_header_size,
            .buffer_size = audio_data_size,
            .data_size = 0,
            .recording = true
        };
        
        // 构建文件名
        char filename[128];
        snprintf(filename, sizeof(filename), "note_box_%s_%s_%d.wav", 
                 USER_ID, g_current_uuid, g_file_counter);
        
        ESP_LOGI(TAG, "===== 开始录音 #%d =====", g_file_counter);
        ESP_LOGI(TAG, "文件名: %s", filename);
        ESP_LOGI(TAG, "时长: %d 秒", RECORD_DURATION_SEC);
        
        // 启动录音
        esp_err_t ret = audio_recorder_start_with_callback(audio_data_callback, &audio_buf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "启动录音失败");
            free(wav_buffer);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 等待录音完成
        int wait_count = 0;
        int max_wait_count = (RECORD_DURATION_SEC * 1000 + 2000) / 100;
        
        while (wait_count < max_wait_count && g_record_task_running && audio_recorder_is_recording()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
            
            if (!g_record_task_running) {
                ESP_LOGI(TAG, "检测到停止标志");
                break;
            }
            
            // 每10秒打印进度
            if (wait_count % 100 == 0) {
                ESP_LOGI(TAG, "录音进度: %d KB / %d KB (%d秒)", 
                         audio_buf.data_size / 1024, audio_data_size / 1024, wait_count / 10);
            }
        }
        
        // 停止录音
        audio_buf.recording = false;
        audio_recorder_stop();
        
        // 等待录音完全停止
        int stop_wait = 0;
        while (audio_recorder_is_recording() && stop_wait < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            stop_wait++;
        }
        
        if (audio_buf.data_size == 0) {
            ESP_LOGE(TAG, "录音数据为空！");
            free(wav_buffer);
            continue;
        }

        float duration_sec = (float)audio_buf.data_size / (RECORD_SAMPLE_RATE * bytes_per_sample);
        ESP_LOGI(TAG, "录音完成: %d KB (约 %.1f 秒)", audio_buf.data_size / 1024, duration_sec);
        
        // 创建WAV文件头
        create_wav_header_in_memory(wav_buffer, audio_buf.data_size,
                                   RECORD_SAMPLE_RATE, RECORD_CHANNELS,
                                   RECORD_BITS_PER_SAMPLE);
        
        size_t wav_file_size = wav_header_size + audio_buf.data_size;
        
        // 将录音数据放入上传队列（不阻塞，立即开始下一轮录音）
        upload_item_t item = {
            .wav_buffer = wav_buffer,
            .wav_size = wav_file_size,
        };
        strncpy(item.filename, filename, sizeof(item.filename) - 1);
        
        if (xQueueSend(g_upload_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "录音 #%d 已加入上传队列", g_file_counter);
            g_file_counter++;
        } else {
            ESP_LOGW(TAG, "上传队列已满，丢弃录音 #%d", g_file_counter);
            free(wav_buffer);
        }
        
        // 立即开始下一轮录音（无需等待上传完成）
        ESP_LOGI(TAG, "===== 准备下一轮录音 =====");
    }

    g_record_task_running = false;
    g_record_task_handle = NULL;
    ESP_LOGI(TAG, "录音任务结束");
    vTaskDelete(NULL);
}

int start_note_recording(void) {
    if (g_record_task_running) {
        ESP_LOGW(TAG, "录音已在进行中");
        return 0;
    }

    // 生成UUID
    if (utils_generate_uuid(g_current_uuid, sizeof(g_current_uuid)) != 0) {
        ESP_LOGE(TAG, "生成UUID失败");
        return -1;
    }
    g_file_counter = 1;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始新的录音会话");
    ESP_LOGI(TAG, "UUID: %s", g_current_uuid);
    ESP_LOGI(TAG, "录音时长: %d 秒/次", RECORD_DURATION_SEC);
    ESP_LOGI(TAG, "上传队列大小: %d", UPLOAD_QUEUE_SIZE);
    ESP_LOGI(TAG, "========================================");

    // 创建上传队列
    if (g_upload_queue == NULL) {
        g_upload_queue = xQueueCreate(UPLOAD_QUEUE_SIZE, sizeof(upload_item_t));
        if (g_upload_queue == NULL) {
            ESP_LOGE(TAG, "创建上传队列失败");
            return -1;
        }
    }

    // 初始化录音器
    esp_err_t ret = audio_recorder_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化录音器失败");
        return -1;
    }

    g_record_task_running = true;
    
    // 启动上传任务
    xTaskCreate(upload_task, "upload_task", 8192, NULL, 4, &g_upload_task_handle);
    
    // 启动录音任务（优先级高于上传任务）
    xTaskCreate(record_task, "record_task", 8192, NULL, 5, &g_record_task_handle);
    
    return 0;
}

// 最小有效录音时长（秒）
#define MIN_RECORDING_DURATION_SEC 60

int stop_note_recording(uint32_t duration_sec) {
    if (!g_record_task_running && !g_upload_task_running) {
        ESP_LOGW(TAG, "录音未在进行");
        return 0;
    }

    ESP_LOGI(TAG, "停止录音... (持续时间: %lu 秒)", (unsigned long)duration_sec);
    
    // 判断是否需要提交数据
    bool should_submit = (duration_sec >= MIN_RECORDING_DURATION_SEC);
    if (!should_submit) {
        ESP_LOGI(TAG, "录音时长不足 %d 秒，不提交数据", MIN_RECORDING_DURATION_SEC);
    }
    
    // 1. 先停止录音任务
    g_record_task_running = false;
    
    if (audio_recorder_is_recording()) {
        audio_recorder_stop();
        int wait = 0;
        while (audio_recorder_is_recording() && wait < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait++;
        }
    }
    
    // 等待录音任务结束
    int wait = 0;
    while (g_record_task_handle != NULL && wait < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait++;
    }
    ESP_LOGI(TAG, "录音任务已停止");
    
    // 保存UUID副本（在清空前）
    char uuid_copy[64];
    strncpy(uuid_copy, g_current_uuid, sizeof(uuid_copy) - 1);
    uuid_copy[sizeof(uuid_copy) - 1] = '\0';
    
    if (should_submit) {
        // 2. 等待上传任务处理完队列中的所有数据
        int queue_count = uxQueueMessagesWaiting(g_upload_queue);
        if (queue_count > 0) {
            ESP_LOGI(TAG, "等待上传队列中的 %d 个文件上传完成...", queue_count);
        }
        
        wait = 0;
        while (uxQueueMessagesWaiting(g_upload_queue) > 0 && wait < 600) {  // 最多等待60秒
            vTaskDelay(pdMS_TO_TICKS(100));
            wait++;
            if (wait % 50 == 0) {
                ESP_LOGI(TAG, "等待上传完成，队列剩余: %d", uxQueueMessagesWaiting(g_upload_queue));
            }
        }
    } else {
        // 不提交数据时，清空上传队列中的数据
        upload_item_t item;
        while (xQueueReceive(g_upload_queue, &item, 0) == pdTRUE) {
            if (item.wav_buffer != NULL) {
                free(item.wav_buffer);
            }
            ESP_LOGI(TAG, "丢弃未上传的录音文件: %s", item.filename);
        }
    }
    
    // 3. 停止上传任务
    g_upload_task_running = false;
    
    wait = 0;
    while (g_upload_task_handle != NULL && wait < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait++;
    }
    ESP_LOGI(TAG, "上传任务已停止");
    
    // 4. 清理
    audio_recorder_deinit();
    
    if (g_upload_queue != NULL) {
        vQueueDelete(g_upload_queue);
        g_upload_queue = NULL;
    }
    
    memset(g_current_uuid, 0, sizeof(g_current_uuid));
    g_file_counter = 0;

    if (should_submit) {
        ESP_LOGI(TAG, "录音已完全停止，所有文件已上传");
        
        // 5. 使用保存的UUID副本调用生成笔记接口
        if (strlen(uuid_copy) > 0) {
            int ret = generate_note(uuid_copy, "box", true, 2, "box1.0");
            if (ret != 0) {
                ESP_LOGE(TAG, "生成笔记失败");
                return -1;
            }
        } else {
            ESP_LOGW(TAG, "UUID为空，跳过生成笔记");
        }
    } else {
        ESP_LOGI(TAG, "录音已停止（时长不足，未提交数据）");
    }
    
    return 0;
}

// 生成笔记任务参数
typedef struct {
    char note_id[64];
    char device[32];
    bool is_voice;
    int type;
    char version[16];
} generate_note_params_t;

// 生成笔记任务（在独立任务中执行，避免占用 main 任务栈）
static void generate_note_task(void *pvParameters) {
    generate_note_params_t *params = (generate_note_params_t *)pvParameters;
    
    ESP_LOGI(TAG, "生成笔记任务启动，UUID: %s", params->note_id);
    
    // 检查 WiFi 连接状态
    if (!WiFi_IsConnected()) {
        ESP_LOGW(TAG, "WiFi 未连接，等待重连...");
        if (!wait_for_wifi(WIFI_WAIT_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "WiFi 连接超时，生成笔记失败");
            free(params);
            ESP_LOGI(TAG, "生成笔记任务结束（网络错误）");
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGI(TAG, "WiFi 已重新连接，继续生成笔记");
    }
    
    // 使用动态分配减少栈压力
    char *url = (char *)malloc(256);
    char *json_body = (char *)malloc(256);
    char *response_buffer = (char *)malloc(256);
    
    if (url == NULL || json_body == NULL || response_buffer == NULL) {
        ESP_LOGE(TAG, "生成笔记：内存分配失败");
        goto cleanup;
    }
    
    snprintf(url, 256, "%s%s", CG_API_URL, API_NOTE);
    snprintf(json_body, 256,
        "{\"id\":\"%s\",\"device\":\"%s\",\"isVoice\":%s,\"type\":%d,\"v\":\"%s\"}",
        params->note_id, params->device, params->is_voice ? "true" : "false", 
        params->type, params->version);

    http_request_config_t config = {
        .url = url,
        .method = "POST",
        .content_type = "application/json",
        .token = CG_TOKEN,
        .body = json_body,
        .body_len = strlen(json_body),
        .timeout_ms = 30000,  // 减少超时时间到 30 秒
        .ssl_verify_mode = HTTP_SSL_VERIFY_NONE,
    };

    // 添加重试机制
    int retry_count = 0;
    bool success = false;
    
    while (retry_count < UPLOAD_MAX_RETRIES && !success) {
        if (retry_count > 0) {
            ESP_LOGW(TAG, "第 %d 次重试生成笔记", retry_count);
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_RETRY_DELAY_MS));
            
            // 重试前检查 WiFi
            if (!WiFi_IsConnected()) {
                ESP_LOGW(TAG, "WiFi 断开，等待重连...");
                if (!wait_for_wifi(WIFI_WAIT_TIMEOUT_MS)) {
                    ESP_LOGE(TAG, "WiFi 连接超时，停止重试");
                    break;
                }
            }
        }
        
        int status_code = 0;
        response_buffer[0] = '\0';
        esp_err_t err = http_client_post_json(&config, &status_code, response_buffer, 256);
        
        if (err == ESP_OK && status_code == 200) {
            ESP_LOGI(TAG, "生成笔记成功");
            success = true;
        } else {
            ESP_LOGW(TAG, "生成笔记失败，状态码: %d, 错误: %s", status_code, esp_err_to_name(err));
            retry_count++;
        }
    }
    
    if (!success) {
        ESP_LOGE(TAG, "生成笔记最终失败（重试 %d 次）", retry_count);
    }

cleanup:
    if (url) free(url);
    if (json_body) free(json_body);
    if (response_buffer) free(response_buffer);
    free(params);
    
    ESP_LOGI(TAG, "生成笔记任务结束");
    vTaskDelete(NULL);
}

int generate_note(const char *note_id, const char *device, bool is_voice, int type, const char *version) {
    if (note_id == NULL || device == NULL || version == NULL) {
        ESP_LOGE(TAG, "参数不能为空");
        return -1;
    }
    
    if (strlen(note_id) == 0) {
        ESP_LOGE(TAG, "note_id 不能为空字符串");
        return -1;
    }

    // 分配参数结构体
    generate_note_params_t *params = (generate_note_params_t *)malloc(sizeof(generate_note_params_t));
    if (params == NULL) {
        ESP_LOGE(TAG, "生成笔记：参数内存分配失败");
        return -1;
    }
    
    // 复制参数
    strncpy(params->note_id, note_id, sizeof(params->note_id) - 1);
    params->note_id[sizeof(params->note_id) - 1] = '\0';
    strncpy(params->device, device, sizeof(params->device) - 1);
    params->device[sizeof(params->device) - 1] = '\0';
    params->is_voice = is_voice;
    params->type = type;
    strncpy(params->version, version, sizeof(params->version) - 1);
    params->version[sizeof(params->version) - 1] = '\0';
    
    // 创建独立任务执行（8KB 栈空间）
    BaseType_t ret = xTaskCreate(generate_note_task, "generate_note", 8192, params, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建生成笔记任务失败");
        free(params);
        return -1;
    }
    
    ESP_LOGI(TAG, "已启动生成笔记任务");
    return 0;
}

bool is_recording(void) {
    return g_record_task_running;
}
