#include "Wireless.h"
#include "app_config.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

uint16_t WIFI_NUM = 0;
bool Scan_finish = 0;

bool WiFi_Scan_Finish = 0;

static const char *TAG = "WIFI_INIT";
static bool wifi_connected = false;
static int current_wifi_index = 0;  // 当前尝试连接的 WiFi 索引
static char wifi_error_msg[128] = {0};  // WiFi错误信息
static bool wifi_init_complete = false;  // WiFi初始化是否完成
void Wireless_Init(void)
{
    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    // WiFi
    xTaskCreatePinnedToCore(
        WIFI_Init, 
        "WIFI task",
        4096, 
        NULL, 
        1, 
        NULL, 
        0);
}

// WiFi 事件处理函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi 开始连接...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        // 断开连接时不自动重连，由主循环控制尝试下一个网络
        ESP_LOGI(TAG, "WiFi 断开连接");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi 连接成功!");
        ESP_LOGI(TAG, "IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "子网掩码: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "网关: " IPSTR, IP2STR(&event->ip_info.gw));
        wifi_connected = true;
    }
}

void WIFI_Init(void *arg)
{
    // 初始化网络接口
    esp_netif_init();                                                     
    
    // 创建默认事件循环
    esp_event_loop_create_default();
    
    // 创建默认 WiFi STA 网络接口
    esp_netif_create_default_wifi_sta();
    
    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();                 
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 注册 WiFi 和 IP 事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    // 配置 WiFi 为 STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // 启动 WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi 初始化完成，开始尝试连接 WiFi 网络...");
    
    // 循环尝试连接所有配置的 WiFi 网络
    bool connected = false;
    for (int i = 0; i < WIFI_CONFIG_COUNT; i++) {
        current_wifi_index = i;
        const wifi_config_item_t *config = &wifi_configs[i];
        
        // 检查配置是否有效（SSID 不为空）
        if (config->ssid == NULL || strlen(config->ssid) == 0) {
            ESP_LOGW(TAG, "跳过无效的 WiFi 配置 [%d]", i);
            continue;
        }
        
        ESP_LOGI(TAG, "尝试连接 WiFi [%d/%d]: %s", i + 1, WIFI_CONFIG_COUNT, config->ssid);
        
        // 配置 WiFi SSID 和密码
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid) - 1);
        if (config->password != NULL) {
            strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password) - 1);
        }
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        
        esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置 WiFi 配置失败: %s", esp_err_to_name(ret));
            continue;
        }
        
        // 先断开之前的连接（如果有）
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));  // 等待断开完成
        
        // 开始连接（检查返回值，避免在已连接时重复连接导致错误）
        wifi_connected = false;
        ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "WiFi 正在连接中，等待连接结果...");
                // 如果正在连接，等待一段时间看是否成功
            } else {
                ESP_LOGE(TAG, "启动 WiFi 连接失败: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(1000));  // 等待1秒后尝试下一个
                continue;
            }
        }
        
        // 等待连接完成（带超时）
        int timeout_count = 0;
        while (!wifi_connected && timeout_count < WIFI_CONNECT_TIMEOUT_SEC * 10) {
            vTaskDelay(pdMS_TO_TICKS(100));
            timeout_count++;
        }
        
        if (wifi_connected) {
            ESP_LOGI(TAG, "WiFi 连接成功! 网络: %s", config->ssid);
            connected = true;
            break;  // 连接成功，退出循环
        } else {
            ESP_LOGW(TAG, "WiFi 连接超时: %s", config->ssid);
            // 断开当前连接，准备尝试下一个
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(1000));  // 等待1秒后尝试下一个
        }
    }
    
    if (!connected) {
        ESP_LOGE(TAG, "所有 WiFi 网络连接失败！已尝试 %d 个网络", WIFI_CONFIG_COUNT);
        ESP_LOGE(TAG, "请检查：1. WiFi 网络是否可用 2. SSID 和密码是否正确 3. 信号强度是否足够");
        snprintf(wifi_error_msg, sizeof(wifi_error_msg), "WiFi连接失败\n已尝试%d个网络\n请检查网络配置", WIFI_CONFIG_COUNT);
    } else {
        wifi_error_msg[0] = '\0';  // 清除错误信息
    }
    
    wifi_init_complete = true;  // 标记初始化完成
    vTaskDelete(NULL);
}
uint16_t WIFI_Scan(void)
{
    uint16_t ap_count = 0;
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    esp_wifi_scan_stop();
    WiFi_Scan_Finish =1;
    if(WiFi_Scan_Finish == 1)
        Scan_finish = 1;
    return ap_count;
}

// WiFi状态检查函数
bool WiFi_IsConnected(void) {
    if (!wifi_init_complete) {
        return false;  // 初始化未完成，返回未连接
    }
    
    // 直接使用事件处理函数中设置的 wifi_connected 变量
    // 这个变量在 IP_EVENT_STA_GOT_IP 事件中设置为 true
    // 在 WIFI_EVENT_STA_DISCONNECTED 事件中设置为 false
    return wifi_connected;
}

// 检查WiFi初始化是否完成
bool WiFi_IsInitComplete(void) {
    return wifi_init_complete;
}

// 获取WiFi错误信息
const char* WiFi_GetError(void) {
    if (wifi_error_msg[0] != '\0') {
        return wifi_error_msg;
    }
    if (!wifi_init_complete) {
        return "WiFi初始化中...";
    }
    if (!wifi_connected) {
        return "WiFi未连接";
    }
    return NULL;  // 无错误
}
