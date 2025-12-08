#include "utils.h"
#include <stdio.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "Utils";

int utils_generate_uuid(char *uuid, size_t uuid_size) {
    if (uuid == NULL || uuid_size < 37) {
        return -1;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t random_part = (uint32_t)(tv.tv_usec ^ tv.tv_sec);
    
    snprintf(uuid, uuid_size, "%08x-%04x-%04x-%04x-%08x%04x",
             (unsigned int)(tv.tv_sec & 0xFFFFFFFF),
             (unsigned int)((tv.tv_usec >> 16) & 0xFFFF),
             (unsigned int)(random_part & 0xFFFF),
             (unsigned int)((random_part >> 16) & 0xFFFF),
             (unsigned int)(tv.tv_usec & 0xFFFFFFFF),
             (unsigned int)(random_part & 0xFFFF));

    return 0;
}

void utils_print_memory_info(void) {
    // 获取系统信息
    // esp_chip_info_t chip_info;
    // esp_chip_info(&chip_info);
    
    // // 内部RAM信息
    // size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    // size_t largest_free_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    // size_t min_free_internal = esp_get_minimum_free_heap_size();
    // size_t total_free_heap = esp_get_free_heap_size();
    
    // // SPIRAM信息
    // size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    // size_t largest_free_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    // // DMA内存信息
    // size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    
    // // FreeRTOS任务栈信息
    // UBaseType_t high_water_mark = uxTaskGetStackHighWaterMark(NULL);
    
    // ESP_LOGI(TAG, "========== ESP32 内存使用情况 ==========");
    // ESP_LOGI(TAG, "芯片信息:");
    // ESP_LOGI(TAG, "  - 型号: ESP32-S3");
    // ESP_LOGI(TAG, "  - 核心数: %d", chip_info.cores);
    // ESP_LOGI(TAG, "  - 特性: %s%s%s%s",
    //          (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "嵌入式Flash " : "",
    //          (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
    //          (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
    //          (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "");
    
    // ESP_LOGI(TAG, "内部RAM (Internal RAM):");
    // ESP_LOGI(TAG, "  - 当前可用: %zu KB (%.2f MB)", free_internal / 1024, free_internal / 1024.0 / 1024.0);
    // ESP_LOGI(TAG, "  - 最大连续块: %zu KB (%.2f MB)", 
    //          largest_free_internal / 1024, largest_free_internal / 1024.0 / 1024.0);
    // ESP_LOGI(TAG, "  - 历史最小剩余: %zu KB (%.2f MB)", 
    //          min_free_internal / 1024, min_free_internal / 1024.0 / 1024.0);
    // ESP_LOGI(TAG, "  - 总可用堆: %zu KB (%.2f MB)", 
    //          total_free_heap / 1024, total_free_heap / 1024.0 / 1024.0);
    
    // if (free_spiram > 0) {
    //     ESP_LOGI(TAG, "SPIRAM (外部PSRAM):");
    //     ESP_LOGI(TAG, "  - 当前可用: %zu KB (%.2f MB)", 
    //              free_spiram / 1024, free_spiram / 1024.0 / 1024.0);
    //     ESP_LOGI(TAG, "  - 最大连续块: %zu KB (%.2f MB)", 
    //              largest_free_spiram / 1024, largest_free_spiram / 1024.0 / 1024.0);
    // } else {
    //     ESP_LOGI(TAG, "SPIRAM: 未启用或不可用");
    // }
    
    // ESP_LOGI(TAG, "DMA内存:");
    // ESP_LOGI(TAG, "  - 当前可用: %zu KB (%.2f MB)", 
    //          free_dma / 1024, free_dma / 1024.0 / 1024.0);
    
    // ESP_LOGI(TAG, "当前任务栈:");
    // ESP_LOGI(TAG, "  - 剩余栈空间: %u 字节", high_water_mark);
    
    // // 计算使用率（估算）
    // if (free_spiram > 0) {
    //     // ESP32-S3 通常有 512KB 内部RAM 和 8MB SPIRAM
    //     float internal_usage = (512.0 * 1024 - free_internal) / (512.0 * 1024) * 100.0;
    //     float spiram_usage = (8.0 * 1024 * 1024 - free_spiram) / (8.0 * 1024 * 1024) * 100.0;
    //     ESP_LOGI(TAG, "内存使用率估算:");
    //     ESP_LOGI(TAG, "  - 内部RAM使用率: %.1f%%", internal_usage);
    //     ESP_LOGI(TAG, "  - SPIRAM使用率: %.1f%%", spiram_usage);
    // }
    
    // ESP_LOGI(TAG, "========================================");
}

void utils_print_memory_breakdown(void) {
    // ESP_LOGI(TAG, "========== 详细内存占用分解 ==========");
    
    // // 1. 获取总内存和已用内存
    // size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    // size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    // size_t used_internal = total_internal - free_internal;
    
    // size_t total_spiram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    // size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    // size_t used_spiram = total_spiram > 0 ? (total_spiram - free_spiram) : 0;
    
    // ESP_LOGI(TAG, "【内部RAM (512KB总容量)】");
    // ESP_LOGI(TAG, "  - 总容量: %zu KB", total_internal / 1024);
    // ESP_LOGI(TAG, "  - 已使用: %zu KB (%.1f%%)", used_internal / 1024, 
    //          (float)used_internal / total_internal * 100.0f);
    // ESP_LOGI(TAG, "  - 可用: %zu KB (%.1f%%)", free_internal / 1024,
    //          (float)free_internal / total_internal * 100.0f);
    // ESP_LOGI(TAG, "  - 最大连续块: %zu KB", 
    //          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    
    // if (total_spiram > 0) {
    //     ESP_LOGI(TAG, "【SPIRAM (外部PSRAM)】");
    //     ESP_LOGI(TAG, "  - 总容量: %zu KB (%.2f MB)", total_spiram / 1024, total_spiram / 1024.0 / 1024.0);
    //     ESP_LOGI(TAG, "  - 已使用: %zu KB (%.1f%%)", used_spiram / 1024,
    //              (float)used_spiram / total_spiram * 100.0f);
    //     ESP_LOGI(TAG, "  - 可用: %zu KB (%.1f%%)", free_spiram / 1024,
    //              (float)free_spiram / total_spiram * 100.0f);
    //     ESP_LOGI(TAG, "  - 最大连续块: %zu KB", 
    //              heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024);
    // }
    
    // // 2. 打印堆信息（ESP-IDF内部信息）
    // ESP_LOGI(TAG, "【堆内存详细信息】");
    // ESP_LOGI(TAG, "内部RAM堆:");
    // heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    
    // if (total_spiram > 0) {
    //     ESP_LOGI(TAG, "SPIRAM堆:");
    //     heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    // }
    
    // // 3. 任务栈占用统计
    // ESP_LOGI(TAG, "【FreeRTOS任务栈占用】");
    // UBaseType_t task_count = uxTaskGetNumberOfTasks();
    // TaskStatus_t *task_array = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * task_count);
    
    // if (task_array != NULL) {
    //     UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, NULL);
        
    //     for (UBaseType_t i = 0; i < actual_count; i++) {
    //         UBaseType_t stack_remaining = task_array[i].usStackHighWaterMark;
            
    //         // 只显示栈剩余空间较小的任务（说明使用了较多栈）
    //         if (stack_remaining < 2048) {  // 剩余少于2KB的任务
    //             ESP_LOGI(TAG, "  - %s: 剩余 %u 字节 (栈使用率较高)",
    //                      task_array[i].pcTaskName, stack_remaining);
    //         }
    //     }
        
    //     ESP_LOGI(TAG, "  - 总任务数: %u", actual_count);
    //     ESP_LOGI(TAG, "  - 当前任务栈剩余: %zu KB", 
    //              uxTaskGetStackHighWaterMark(NULL) / 1024);
    //     free(task_array);
    // }
    
    // // 4. 组件内存占用估算
    // ESP_LOGI(TAG, "【组件内存占用估算】");
    
    // // WiFi内存占用估算
    // size_t wifi_estimated = 0;
    // #ifdef CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM
    // // WiFi静态缓冲区：每个约1.6KB
    // wifi_estimated += CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM * 1600;
    // #endif
    // #ifdef CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM
    // // WiFi动态缓冲区：每个约1.6KB
    // wifi_estimated += CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM * 1600;
    // #endif
    // if (wifi_estimated > 0) {
    //     ESP_LOGI(TAG, "  - WiFi缓冲区: 约 %zu KB (静态+动态)", wifi_estimated / 1024);
    // }
    
    // // LWIP TCP/IP栈
    // #ifdef CONFIG_LWIP_TCP_SND_BUF_DEFAULT
    // size_t lwip_estimated = CONFIG_LWIP_TCP_SND_BUF_DEFAULT + 
    //                        CONFIG_LWIP_TCP_WND_DEFAULT;
    // ESP_LOGI(TAG, "  - LWIP TCP/IP栈: 约 %zu KB", lwip_estimated / 1024);
    // #endif
    
    // // mbedTLS内存（HTTPS需要）
    // size_t mbedtls_estimated = 40 * 1024;  // 估算40KB用于HTTPS握手
    // ESP_LOGI(TAG, "  - mbedTLS (HTTPS): 约 %zu KB (握手时临时占用)", mbedtls_estimated / 1024);
    
    // // 5. 内存碎片化分析
    // ESP_LOGI(TAG, "【内存碎片化分析】");
    // size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    // float fragmentation = free_internal > 0 ? 
    //                      (1.0f - (float)largest_block / free_internal) * 100.0f : 0.0f;
    // ESP_LOGI(TAG, "  - 碎片化程度: %.1f%% (0%%=无碎片, 100%%=完全碎片化)", fragmentation);
    // if (fragmentation > 50.0f) {
    //     ESP_LOGW(TAG, "  ⚠️ 警告：内存碎片化严重，可能影响大块内存分配");
    // }
    
    // // 6. 内存使用建议
    // ESP_LOGI(TAG, "【内存优化建议】");
    // if (free_internal < 50 * 1024) {
    //     ESP_LOGW(TAG, "  ⚠️ 内部RAM严重不足（<50KB），建议：");
    //     ESP_LOGW(TAG, "     1. 检查是否有内存泄漏");
    //     ESP_LOGW(TAG, "     2. 减少WiFi缓冲区数量（当前: %d)", 
    //              #ifdef CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM
    //              CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM
    //              #else
    //              10
    //              #endif
    //              );
    //     ESP_LOGW(TAG, "     3. 确保大缓冲区（>64KB）使用SPIRAM");
    //     ESP_LOGW(TAG, "     4. 减少任务栈大小");
    // }
    
    // if (fragmentation > 30.0f) {
    //     ESP_LOGW(TAG, "  ⚠️ 内存碎片化，建议：");
    //     ESP_LOGW(TAG, "     1. 避免频繁分配/释放不同大小的内存");
    //     ESP_LOGW(TAG, "     2. 使用内存池管理固定大小的内存块");
    // }
    
    // ESP_LOGI(TAG, "========================================");
}

int utils_get_memory_info_string(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t min_free_internal = esp_get_minimum_free_heap_size();
    size_t total_free_heap = esp_get_free_heap_size();
    
    int written = snprintf(buffer, buffer_size,
        "内存: 内部RAM=%zuKB(最小%zuKB) SPIRAM=%zuKB 总堆=%zuKB",
        free_internal / 1024,
        min_free_internal / 1024,
        free_spiram / 1024,
        total_free_heap / 1024);
    
    return (written < 0 || (size_t)written >= buffer_size) ? -1 : written;
}

