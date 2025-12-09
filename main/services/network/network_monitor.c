/**
 * @file network_monitor.c
 * @brief 网络连接监控服务实现
 */

#include "network_monitor.h"
#include "wifi_service.h"
#include "pcm5101.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "NetMonitor";

// ============================================================================
// 配置参数
// ============================================================================

// 网络断开提示音文件路径（SD卡）
#define NETWORK_ALERT_DIR "/sdcard"
#define NETWORK_ALERT_FILE "no_network.mp3"

// 网络检测间隔（毫秒）
#define NETWORK_CHECK_INTERVAL_MS 5000

// 网络断开后播放提示音的间隔（毫秒）- 避免频繁播放
#define NETWORK_ALERT_INTERVAL_MS 60000

// 启动后首次检测延迟（等待音频系统就绪）
#define STARTUP_DELAY_MS 3000

// ============================================================================
// 内部变量
// ============================================================================

static TaskHandle_t g_monitor_task_handle = NULL;
static volatile bool g_monitor_running = false;

// ============================================================================
// 任务实现
// ============================================================================

/**
 * @brief 网络监控任务
 *
 * 检测网络连接状态，断开时播放语音提示
 */
static void network_monitor_task(void *parameter) {
    ESP_LOGI(TAG, "网络监控任务启动，等待系统初始化...");
    
    // 等待系统完全初始化（WiFi + 音频）
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));
    
    // 等待 WiFi 初始化完成
    int wait_count = 0;
    while (!WiFi_IsInitComplete() && wait_count < 60) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_count++;
    }
    
    if (!WiFi_IsInitComplete()) {
        ESP_LOGE(TAG, "WiFi 初始化超时，网络监控任务退出");
        g_monitor_task_handle = NULL;
        g_monitor_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "开始监控网络连接状态");
    
    bool last_connected = WiFi_IsConnected();
    uint32_t last_alert_time = 0;
    
    // 如果启动时就没有网络，立即播放提示音
    if (!last_connected) {
        ESP_LOGW(TAG, "启动时网络未连接，播放提示音");
        Play_Music(NETWORK_ALERT_DIR, NETWORK_ALERT_FILE);
        last_alert_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    
    while (g_monitor_running) {
        bool connected = WiFi_IsConnected();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // 检测网络状态变化
        if (connected != last_connected) {
            if (!connected) {
                // 网络刚断开
                ESP_LOGW(TAG, "检测到网络断开！");
                
                // 播放提示音
                ESP_LOGI(TAG, "播放网络断开提示音: %s/%s", 
                         NETWORK_ALERT_DIR, NETWORK_ALERT_FILE);
                Play_Music(NETWORK_ALERT_DIR, NETWORK_ALERT_FILE);
                last_alert_time = now;
            } else {
                // 网络恢复连接
                ESP_LOGI(TAG, "网络已恢复连接");
            }
            last_connected = connected;
        }
        
        // 如果网络持续断开，定期播放提示音
        if (!connected && (now - last_alert_time >= NETWORK_ALERT_INTERVAL_MS)) {
            ESP_LOGW(TAG, "网络仍未连接，再次播放提示音");
            Play_Music(NETWORK_ALERT_DIR, NETWORK_ALERT_FILE);
            last_alert_time = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(NETWORK_CHECK_INTERVAL_MS));
    }
    
    ESP_LOGI(TAG, "网络监控任务结束");
    g_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// 公共接口
// ============================================================================

void network_monitor_start(void) {
    if (g_monitor_running) {
        ESP_LOGW(TAG, "网络监控已在运行");
        return;
    }
    
    g_monitor_running = true;
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        network_monitor_task,
        "net_monitor",
        4096,
        NULL,
        2,  // 优先级
        &g_monitor_task_handle,
        1   // 运行在 Core 1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建网络监控任务失败");
        g_monitor_running = false;
        return;
    }
    
    ESP_LOGI(TAG, "网络监控服务已启动");
}

void network_monitor_stop(void) {
    if (!g_monitor_running) {
        return;
    }
    
    g_monitor_running = false;
    
    // 等待任务结束
    int wait = 0;
    while (g_monitor_task_handle != NULL && wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait++;
    }
    
    ESP_LOGI(TAG, "网络监控服务已停止");
}

bool network_monitor_is_running(void) {
    return g_monitor_running;
}

