#include "NoteService.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "AudioRecorder.h"
#include "HttpClient.h"
#include "Utils.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "NoteService";

// 录音配置（从 app_config.h 读取）
#define RECORD_SAMPLE_RATE 16000
#define RECORD_CHANNELS 1
#define RECORD_BITS_PER_SAMPLE 16

// 录音数据缓冲区结构
typedef struct {
    uint8_t *buffer;        // 音频数据缓冲区
    size_t buffer_size;      // 缓冲区大小
    size_t data_size;        // 实际数据大小
    bool recording;          // 是否正在录音
} audio_buffer_t;

// 录音任务句柄和运行标志
static TaskHandle_t g_record_task_handle = NULL;
static bool g_record_task_running = false;

// 录音会话信息（在开始录音时初始化）
static char g_current_uuid[64] = {0};
static int g_file_counter = 0;

// 录音数据回调函数：将音频数据收集到内存缓冲区
static bool audio_data_callback(const void *data, size_t size, void *user_data) {
    audio_buffer_t *audio_buf = (audio_buffer_t *)user_data;
    
    if (audio_buf == NULL) {
        ESP_LOGE(TAG, "回调函数：audio_buf 为 NULL");
        return false;  // 停止录音
    }
    
    // 检查全局停止标志（用户点击停止按钮）
    if (!g_record_task_running) {
        ESP_LOGI(TAG, "回调函数：检测到停止标志，停止录音");
        audio_buf->recording = false;
        return false;  // 停止录音
    }
    
    if (!audio_buf->recording) {
        ESP_LOGD(TAG, "回调函数：录音已停止，忽略数据");
        return false;  // 停止录音
    }
    
    // 检查缓冲区是否有足够空间
    if (audio_buf->data_size + size > audio_buf->buffer_size) {
        ESP_LOGW(TAG, "音频缓冲区已满，停止录音 (已用: %d, 新增: %d, 总容量: %d)", 
                 audio_buf->data_size, size, audio_buf->buffer_size);
        return false;  // 缓冲区满，停止录音
    }
    
    // 复制数据到缓冲区
    memcpy(audio_buf->buffer + audio_buf->data_size, data, size);
    audio_buf->data_size += size;
    
    // 每收集 1MB 数据打印一次日志
    static size_t last_log_size = 0;
    if (audio_buf->data_size - last_log_size >= 1024 * 1024) {
        ESP_LOGI(TAG, "已收集音频数据: %d KB / %d KB", 
                 audio_buf->data_size / 1024, audio_buf->buffer_size / 1024);
        last_log_size = audio_buf->data_size;
    }
    
    return true;  // 继续录音
}

// 创建WAV文件头（在内存中）
static void create_wav_header_in_memory(uint8_t *buffer, uint32_t data_size,
                                        uint16_t sample_rate, uint16_t channels,
                                        uint16_t bits_per_sample) {
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

    wav_header_t *header = (wav_header_t *)buffer;
    memset(header, 0, sizeof(wav_header_t));
    
    memcpy(header->chunk_id, "RIFF", 4);
    header->chunk_size = data_size + sizeof(wav_header_t) - 8;
    memcpy(header->format, "WAVE", 4);
    memcpy(header->subchunk1_id, "fmt ", 4);
    header->subchunk1_size = 16;
    header->audio_format = 1;  // PCM
    header->num_channels = channels;
    header->sample_rate = sample_rate;
    header->byte_rate = sample_rate * channels * bits_per_sample / 8;
    header->block_align = channels * bits_per_sample / 8;
    header->bits_per_sample = bits_per_sample;
    memcpy(header->subchunk2_id, "data", 4);
    header->subchunk2_size = data_size;
}

// 录音任务（直接录音到内存，不保存文件，每30秒录音一次）
static void record_task(void *pvParameters) {
    ESP_LOGI(TAG, "开始录音任务（内存模式，UUID: %s，文件计数器: %d）", g_current_uuid, g_file_counter);

    while (g_record_task_running) {
        
        // 计算需要的缓冲区大小（30秒录音）
        int samples_to_record = RECORD_SAMPLE_RATE * RECORD_DURATION_SEC;
        int bytes_per_sample = RECORD_CHANNELS * RECORD_BITS_PER_SAMPLE / 8;
        size_t audio_data_size = samples_to_record * bytes_per_sample;
        size_t wav_header_size = 44;  // WAV文件头大小
        size_t total_buffer_size = wav_header_size + audio_data_size;
        
        // 检查可用内存
        size_t free_heap = esp_get_free_heap_size();
        size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        
        ESP_LOGI(TAG, "内存状态 - 总空闲: %d KB, PSRAM空闲: %d KB, 最大空闲块: %d KB, 需要: %d KB",
                 free_heap / 1024, free_spiram / 1024, largest_free_block / 1024, total_buffer_size / 1024);
        
        // 优先从PSRAM分配内存，如果失败则尝试普通内存
        uint8_t *wav_buffer = NULL;
        
        // 尝试从PSRAM分配（如果可用）
        if (free_spiram >= total_buffer_size) {
            wav_buffer = (uint8_t *)heap_caps_malloc(total_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (wav_buffer != NULL) {
                ESP_LOGI(TAG, "从PSRAM分配内存成功: %d KB", total_buffer_size / 1024);
            }
        }
        
        // 如果PSRAM分配失败，尝试从普通内存分配
        if (wav_buffer == NULL) {
            if (free_heap >= total_buffer_size) {
                wav_buffer = (uint8_t *)heap_caps_malloc(total_buffer_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
                if (wav_buffer != NULL) {
                    ESP_LOGW(TAG, "从普通内存分配成功: %d KB (PSRAM不可用或不足)", total_buffer_size / 1024);
                }
            }
        }
        
        // 如果仍然失败，尝试任何可用的内存
        if (wav_buffer == NULL) {
            wav_buffer = (uint8_t *)malloc(total_buffer_size);
            if (wav_buffer != NULL) {
                ESP_LOGW(TAG, "使用标准malloc分配: %d KB", total_buffer_size / 1024);
            }
        }
        
        // 如果内存分配失败，尝试分块录音模式
        bool use_chunk_mode = false;
        if (wav_buffer == NULL) {
            #if RECORD_ENABLE_CHUNK_MODE
            // 计算分块模式需要的缓冲区大小
            int samples_per_chunk = RECORD_SAMPLE_RATE * RECORD_CHUNK_DURATION_SEC;
            size_t chunk_audio_size = samples_per_chunk * bytes_per_sample;
            size_t chunk_buffer_size = wav_header_size + chunk_audio_size;
            
            // 检查分块模式是否有足够内存
            if (free_spiram >= chunk_buffer_size || free_heap >= chunk_buffer_size) {
                ESP_LOGW(TAG, "内存不足，自动切换到分块录音模式（每块 %d 秒）", RECORD_CHUNK_DURATION_SEC);
                use_chunk_mode = true;
            } else {
                ESP_LOGE(TAG, "无法分配内存缓冲区 (%d KB)，分块模式也需要 %d KB，可用内存不足", 
                         total_buffer_size / 1024, chunk_buffer_size / 1024);
                ESP_LOGE(TAG, "建议：1. 检查PSRAM是否启用 2. 减少录音时长或分块时长");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            #else
            ESP_LOGE(TAG, "无法分配内存缓冲区 (%d KB)，可用内存不足", total_buffer_size / 1024);
            ESP_LOGE(TAG, "建议：1. 检查PSRAM是否启用 2. 减少录音时长 3. 启用分块录音模式");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
            #endif
        }
        
        if (use_chunk_mode) {
            // ========== 分块录音模式 ==========
            int samples_per_chunk = RECORD_SAMPLE_RATE * RECORD_CHUNK_DURATION_SEC;
            size_t chunk_audio_size = samples_per_chunk * bytes_per_sample;
            size_t chunk_buffer_size = wav_header_size + chunk_audio_size;
            int total_chunks = (RECORD_DURATION_SEC + RECORD_CHUNK_DURATION_SEC - 1) / RECORD_CHUNK_DURATION_SEC;
            
            ESP_LOGI(TAG, "开始分块录音: note_box_%s_%s_%d.wav，共 %d 块", USER_ID, g_current_uuid, g_file_counter, total_chunks);
            
            // 构建上传URL
            char url[512];
            snprintf(url, sizeof(url), "%s%s", CG_API_URL, API_UPLOAD);
            
            for (int chunk_idx = 0; chunk_idx < total_chunks && g_record_task_running; chunk_idx++) {
                // 分配分块缓冲区
                uint8_t *chunk_buffer = NULL;
                if (free_spiram >= chunk_buffer_size) {
                    chunk_buffer = (uint8_t *)heap_caps_malloc(chunk_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
                if (chunk_buffer == NULL && free_heap >= chunk_buffer_size) {
                    chunk_buffer = (uint8_t *)heap_caps_malloc(chunk_buffer_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
                }
                if (chunk_buffer == NULL) {
                    chunk_buffer = (uint8_t *)malloc(chunk_buffer_size);
                }
                
                if (chunk_buffer == NULL) {
                    ESP_LOGE(TAG, "无法分配分块缓冲区（块 %d/%d）", chunk_idx + 1, total_chunks);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
                
                // 初始化音频缓冲区结构
                audio_buffer_t audio_buf = {
                    .buffer = chunk_buffer + wav_header_size,
                    .buffer_size = chunk_audio_size,
                    .data_size = 0,
                    .recording = true
                };
                
                ESP_LOGI(TAG, "开始录音块 %d/%d", chunk_idx + 1, total_chunks);
                
                // 启动录音
                esp_err_t ret = audio_recorder_start_with_callback(audio_data_callback, &audio_buf);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "启动录音失败（块 %d）", chunk_idx + 1);
                    free(chunk_buffer);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
                
                // 等待录音完成（定期检查停止标志）
                int wait_count = 0;
                int max_wait_count = (RECORD_CHUNK_DURATION_SEC * 1000 + 500) / 100;
                while (wait_count < max_wait_count && g_record_task_running && audio_recorder_is_recording()) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    wait_count++;
                }
                
                // 停止录音
                audio_buf.recording = false;
                audio_recorder_stop();
                
                // 等待录音任务结束
                int stop_wait_count = 0;
                while (audio_recorder_is_recording() && stop_wait_count < 50) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    stop_wait_count++;
                }
                
                if (audio_buf.data_size == 0) {
                    ESP_LOGW(TAG, "录音数据为空（块 %d）", chunk_idx + 1);
                    free(chunk_buffer);
                    continue;
                }
                
                ESP_LOGI(TAG, "录音块 %d 完成，数据大小: %d 字节", chunk_idx + 1, audio_buf.data_size);
                
                // 创建WAV文件头
                create_wav_header_in_memory(chunk_buffer, audio_buf.data_size,
                                           RECORD_SAMPLE_RATE, RECORD_CHANNELS,
                                           RECORD_BITS_PER_SAMPLE);
                
                size_t chunk_file_size = wav_header_size + audio_buf.data_size;
                
                // 构建文件名（每块使用不同的文件名，格式：note_box_{USER_ID}_{UUID}_{i}_chunk{chunk_idx}.wav）
                char filename[128];
                snprintf(filename, sizeof(filename), "note_box_%s_%s_%d_chunk%d.wav", USER_ID, g_current_uuid, g_file_counter, chunk_idx);
                
                // 上传
                http_request_config_t upload_config = {
                    .url = url,
                    .method = "POST",
                    .token = CG_TOKEN,
                    .timeout_ms = 30000,
                    .ssl_verify_mode = HTTP_SSL_VERIFY_NONE,  // 跳过证书验证（开发环境）
                };
                
                // 上传前检查内存状态（HTTPS握手需要约20-40KB内部RAM）
                size_t free_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                if (free_internal_before < 50 * 1024) {  // 少于50KB
                    ESP_LOGW(TAG, "分块上传前内存警告：内部RAM仅 %zu KB", free_internal_before / 1024);
                    utils_print_memory_breakdown();
                }
                
                int status_code = 0;
                char response_buffer[512] = {0};
                // 使用流式上传，避免文件数据重复占用内存
                ret = http_client_post_multipart_streaming(&upload_config, chunk_buffer, chunk_file_size,
                                                           filename, "file", "fileName",
                                                           &status_code, response_buffer, sizeof(response_buffer));
                
            if (ret == ESP_OK && status_code == 200) {
                ESP_LOGI(TAG, "块 %d 上传成功，响应: %s", chunk_idx + 1, response_buffer);
        } else {
                ESP_LOGW(TAG, "块 %d 上传失败，状态码: %d", chunk_idx + 1, status_code);
            }
            
            // 立即释放内存
            free(chunk_buffer);
            
            // 块之间稍作延迟
            if (chunk_idx < total_chunks - 1) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        
        // 所有块上传完成后，递增文件计数器
        g_file_counter++;
        ESP_LOGI(TAG, "完成分块录音（%d块），文件计数器递增至: %d", total_chunks, g_file_counter);
        
        // 等待到下一轮录音（如果还在运行）
        if (g_record_task_running) {
            ESP_LOGI(TAG, "等待下一轮录音...");
            vTaskDelay(pdMS_TO_TICKS(1000));  // 短暂延迟后继续循环
        }
        } else {
            // ========== 一次性录音模式 ==========
            // 初始化音频缓冲区结构
            audio_buffer_t audio_buf = {
                .buffer = wav_buffer + wav_header_size,  // 音频数据从WAV头之后开始
                .buffer_size = audio_data_size,
                .data_size = 0,
                .recording = true
            };
            
            ESP_LOGI(TAG, "开始录音（内存模式）: note_box_%s_%s_%d.wav", USER_ID, g_current_uuid, g_file_counter);
            
            // 使用回调模式录音到内存
            esp_err_t ret = audio_recorder_start_with_callback(audio_data_callback, &audio_buf);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "启动录音失败");
                free(wav_buffer);
                vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

            // 等待录音完成（定期检查数据收集情况和停止标志）
            int wait_count = 0;
            int max_wait_count = (RECORD_DURATION_SEC * 1000 + 2000) / 100;  // 总等待时间 + 2秒缓冲
            size_t last_data_size = 0;
            int no_data_increase_count = 0;
            
            while (wait_count < max_wait_count && g_record_task_running && audio_recorder_is_recording()) {
                vTaskDelay(pdMS_TO_TICKS(100));
                wait_count++;
                
                // 检查停止标志
                if (!g_record_task_running) {
                    ESP_LOGI(TAG, "检测到停止标志，立即停止录音");
                    break;
                }
                
                // 每5秒检查一次数据收集情况
                if (wait_count % 50 == 0) {
                    size_t current_data_size = audio_buf.data_size;
                    if (current_data_size > last_data_size) {
                        ESP_LOGI(TAG, "录音进行中，已收集: %d KB / %d KB (等待 %d 秒)", 
                                 current_data_size / 1024, audio_data_size / 1024, wait_count / 10);
                        last_data_size = current_data_size;
                        no_data_increase_count = 0;
                    } else {
                        no_data_increase_count++;
                        if (no_data_increase_count >= 10) {  // 连续10次（5秒）没有数据增长
                            ESP_LOGW(TAG, "录音任务运行中，但未收集到数据（可能I2S无输入）");
                            no_data_increase_count = 0;  // 重置计数，继续等待
                        }
                    }
                }
            }
            
            // 停止录音
            audio_buf.recording = false;
            audio_recorder_stop();
            
            // 等待录音任务完全结束
            int stop_wait_count = 0;
            while (audio_recorder_is_recording() && stop_wait_count < 100) {
                vTaskDelay(pdMS_TO_TICKS(100));
                stop_wait_count++;
            }
            
            if (audio_buf.data_size == 0) {
                ESP_LOGE(TAG, "录音数据为空！可能原因：1. I2S未读取到数据 2. 麦克风未连接 3. I2S配置错误");
                ESP_LOGE(TAG, "录音任务状态: %s, 等待次数: %d", 
                         audio_recorder_is_recording() ? "运行中" : "已停止", wait_count);
                free(wav_buffer);
            continue;
        }

            float data_size_kb = (float)audio_buf.data_size / 1024.0f;
            float duration_sec = (float)audio_buf.data_size / ((float)RECORD_SAMPLE_RATE * (float)RECORD_CHANNELS * (float)RECORD_BITS_PER_SAMPLE / 8.0f);
            ESP_LOGI(TAG, "录音完成，数据大小: %d 字节 (%.2f KB, 约 %.1f 秒)", 
                     audio_buf.data_size, data_size_kb, duration_sec);
            
            // 创建WAV文件头
            create_wav_header_in_memory(wav_buffer, audio_buf.data_size,
                                       RECORD_SAMPLE_RATE, RECORD_CHANNELS,
                                       RECORD_BITS_PER_SAMPLE);
            
            // 构建完整的WAV文件（头 + 数据）
            size_t wav_file_size = wav_header_size + audio_buf.data_size;
            
            // 构建文件名（格式：note_box_{USER_ID}_{UUID}_{i}.wav）
            char filename[128];
            snprintf(filename, sizeof(filename), "note_box_%s_%s_%d.wav", USER_ID, g_current_uuid, g_file_counter);
            
            // 构建上传URL
            char url[512];
            snprintf(url, sizeof(url), "%s%s", CG_API_URL, API_UPLOAD);
            
            // 上传前检查内存状态（HTTPS握手需要约20-40KB内部RAM）
            size_t free_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t min_free_before = esp_get_minimum_free_heap_size();
            ESP_LOGI(TAG, "上传前内存检查 - 内部RAM可用: %zu KB, 历史最小: %zu KB", 
                     free_internal_before / 1024, min_free_before / 1024);
            
            // 如果内部RAM不足，打印详细内存分解
            if (free_internal_before < 50 * 1024) {  // 少于50KB
                ESP_LOGW(TAG, "警告：内部RAM不足（%zu KB），HTTPS握手可能需要20-40KB", free_internal_before / 1024);
                ESP_LOGW(TAG, "打印详细内存占用分解...");
                // 打印详细内存分解，显示各个组件的占用情况
                utils_print_memory_breakdown();
            }
            
            // 使用HttpClient模块从内存上传
            http_request_config_t upload_config = {
                .url = url,
                .method = "POST",
                .token = CG_TOKEN,
                .timeout_ms = 30000,
                .ssl_verify_mode = HTTP_SSL_VERIFY_NONE,  // 跳过证书验证（开发环境）
            };

            int status_code = 0;
            char response_buffer[512] = {0};
            // 使用流式上传，避免文件数据重复占用内存
            // 内存占用：仅multipart头部和尾部（约200-300字节），而不是文件大小+头部+尾部
            ret = http_client_post_multipart_streaming(&upload_config, wav_buffer, wav_file_size,
                                                       filename, "file", "fileName",
                                                       &status_code, response_buffer, sizeof(response_buffer));
            
            // 上传后检查内存状态
            size_t free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            ESP_LOGI(TAG, "上传后内存检查 - 内部RAM可用: %zu KB (变化: %d KB)", 
                     free_internal_after / 1024, 
                     (int)(free_internal_after - free_internal_before) / 1024);
            
            if (ret == ESP_OK && status_code == 200) {
                ESP_LOGI(TAG, "上传成功，响应: %s", response_buffer);
                // 上传成功后，递增文件计数器
                g_file_counter++;
                ESP_LOGI(TAG, "文件计数器递增至: %d", g_file_counter);
            } else {
                ESP_LOGW(TAG, "上传失败，状态码: %d", status_code);
            }
            
            // 释放内存缓冲区
            free(wav_buffer);
            
            // 等待到下一轮录音（如果还在运行）
            if (g_record_task_running) {
                ESP_LOGI(TAG, "等待下一轮录音...");
                vTaskDelay(pdMS_TO_TICKS(1000));  // 短暂延迟后继续循环
            }
        }
    }

    g_record_task_running = false;
    g_record_task_handle = NULL;
    vTaskDelete(NULL);
}

int start_note_recording(void) {
    if (g_record_task_running) {
        ESP_LOGW(TAG, "录音业务逻辑已在进行中");
        return 0;
    }

    // 生成新的UUID并初始化文件计数器
    if (utils_generate_uuid(g_current_uuid, sizeof(g_current_uuid)) != 0) {
        ESP_LOGE(TAG, "生成UUID失败");
        return -1;
    }
    g_file_counter = 1;  // 初始化计数器为1
    
    ESP_LOGI(TAG, "开始新的录音会话，UUID: %s，文件计数器: %d", g_current_uuid, g_file_counter);

    // 初始化录音器（不需要文件系统）
    esp_err_t ret = audio_recorder_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化录音器失败");
        return -1;
    }

    // 启动录音任务（该任务会循环录音和上传，每30秒一次）
    g_record_task_running = true;
    xTaskCreate(record_task, "record_task", 8192, NULL, 5, &g_record_task_handle);
    
    ESP_LOGI(TAG, "开始录音业务逻辑（每30秒录音一次）");
    return 0;
}

int stop_note_recording(void) {
    if (!g_record_task_running) {
        ESP_LOGW(TAG, "录音业务逻辑未在进行");
        return 0;
    }

    ESP_LOGI(TAG, "开始停止录音业务逻辑...");
    
    // 停止录音任务循环（这会立即被回调函数和等待循环检测到）
    g_record_task_running = false;
    
    // 停止当前正在进行的录音
    if (audio_recorder_is_recording()) {
        ESP_LOGI(TAG, "停止当前录音...");
        audio_recorder_stop();
        
        // 等待录音完全停止
        int wait_count = 0;
        while (audio_recorder_is_recording() && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
    }
    
    // 等待录音任务结束
    if (g_record_task_handle != NULL) {
        ESP_LOGI(TAG, "等待录音任务结束...");
        int wait_count = 0;
        while (g_record_task_handle != NULL && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
    }

    // 清理录音器
    audio_recorder_deinit();
    
    // 重置UUID和计数器
    memset(g_current_uuid, 0, sizeof(g_current_uuid));
    g_file_counter = 0;

    ESP_LOGI(TAG, "录音业务逻辑已停止");
    return 0;
}

int generate_note(const char *note_id, const char *device, bool is_voice, int type, const char *version) {
    if (note_id == NULL || device == NULL || version == NULL) {
        ESP_LOGE(TAG, "参数不能为空");
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s%s", CG_API_URL, API_NOTE);

    // 构建JSON请求体
    char json_body[512];
    snprintf(json_body, sizeof(json_body),
        "{\"id\":\"%s\",\"device\":\"%s\",\"isVoice\":%s,\"type\":%d,\"v\":\"%s\"}",
        note_id, device, is_voice ? "true" : "false", type, version);

    ESP_LOGI(TAG, "生成笔记，URL: %s", url);
    ESP_LOGI(TAG, "请求体: %s", json_body);

    // 使用HttpClient模块发送JSON请求
    http_request_config_t config = {
        .url = url,
        .method = "POST",
        .content_type = "application/json",
        .token = CG_TOKEN,
        .body = json_body,
        .body_len = strlen(json_body),
        .timeout_ms = 30000,
        .ssl_verify_mode = HTTP_SSL_VERIFY_NONE,  // 跳过证书验证（开发环境）
    };

    int status_code = 0;
    char response_buffer[512] = {0};
    esp_err_t err = http_client_post_json(&config, &status_code, response_buffer, sizeof(response_buffer));
    
    if (err == ESP_OK && status_code == 200) {
        ESP_LOGI(TAG, "生成笔记成功，响应: %s", response_buffer);
            return 0;
        } else {
            ESP_LOGW(TAG, "生成笔记失败，状态码: %d", status_code);
        return -1;
    }
}

bool is_recording(void) {
    return g_record_task_running;
}

