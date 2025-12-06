#include "Wireless.h"
#include "app_config.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

uint16_t BLE_NUM = 0;
uint16_t WIFI_NUM = 0;
bool Scan_finish = 0;

bool WiFi_Scan_Finish = 0;
bool BLE_Scan_Finish = 0;

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
    // // BLE
    // xTaskCreatePinnedToCore(
    //     BLE_Init, 
    //     "BLE task",
    //     4096, 
    //     NULL, 
    //     2, 
    //     NULL, 
    //     0);
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
    if(BLE_Scan_Finish == 1)
        Scan_finish = 1;
    if(WiFi_Scan_Finish == 1)
        Scan_finish = 1;
    return ap_count;
}


#define GATTC_TAG "GATTC_TAG"
#define SCAN_DURATION 5  
#define MAX_DISCOVERED_DEVICES 100 

typedef struct {
    uint8_t address[6];
    bool is_valid;
} discovered_device_t;

static discovered_device_t discovered_devices[MAX_DISCOVERED_DEVICES];
static size_t num_discovered_devices = 0;
static size_t num_devices_with_name = 0; 

static bool is_device_discovered(const uint8_t *addr) {
    for (size_t i = 0; i < num_discovered_devices; i++) {
        if (memcmp(discovered_devices[i].address, addr, 6) == 0) {
            return true;
        }
    }
    return false;
}

static void add_device_to_list(const uint8_t *addr) {
    if (num_discovered_devices < MAX_DISCOVERED_DEVICES) {
        memcpy(discovered_devices[num_discovered_devices].address, addr, 6);
        discovered_devices[num_discovered_devices].is_valid = true;
        num_discovered_devices++;
    }
}

static bool extract_device_name(const uint8_t *adv_data, uint8_t adv_data_len, char *device_name, size_t max_name_len) {
    size_t offset = 0;
    while (offset < adv_data_len) {
        if (adv_data[offset] == 0) break; 

        uint8_t length = adv_data[offset];
        if (length == 0 || offset + length > adv_data_len) break; 

        uint8_t type = adv_data[offset + 1];
        if (type == ESP_BLE_AD_TYPE_NAME_CMPL || type == ESP_BLE_AD_TYPE_NAME_SHORT) {
            if (length > 1 && length - 1 < max_name_len) {
                memcpy(device_name, &adv_data[offset + 2], length - 1);
                device_name[length - 1] = '\0'; 
                return true;
            } else {
                return false;
            }
        }
        offset += length + 1;
    }
    return false;
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    static char device_name[100]; 

    switch (event) {
        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
                if (!is_device_discovered(param->scan_rst.bda)) {
                    add_device_to_list(param->scan_rst.bda);
                    BLE_NUM++; 

                    if (extract_device_name(param->scan_rst.ble_adv, param->scan_rst.adv_data_len, device_name, sizeof(device_name))) {
                        num_devices_with_name++;
                        // printf("Found device: %02X:%02X:%02X:%02X:%02X:%02X\n        Name: %s\n        RSSI: %d\r\n",
                        //          param->scan_rst.bda[0], param->scan_rst.bda[1],
                        //          param->scan_rst.bda[2], param->scan_rst.bda[3],
                        //          param->scan_rst.bda[4], param->scan_rst.bda[5],
                        //          device_name, param->scan_rst.rssi);
                        // printf("\r\n");
                    } else {
                        // printf("Found device: %02X:%02X:%02X:%02X:%02X:%02X\n        Name: Unknown\n        RSSI: %d\r\n",
                        //          param->scan_rst.bda[0], param->scan_rst.bda[1],
                        //          param->scan_rst.bda[2], param->scan_rst.bda[3],
                        //          param->scan_rst.bda[4], param->scan_rst.bda[5],
                        //          param->scan_rst.rssi);
                        // printf("\r\n");
                    }
                }
            }
            break;
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            ESP_LOGI(GATTC_TAG, "Scan complete. Total devices found: %d (with names: %d)", BLE_NUM, num_devices_with_name);
            break;
        default:
            break;
    }
}

void BLE_Init(void *arg)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);                                            
    if (ret) {
        printf("%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));        
        return;}
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);                                            
    if (ret) {
        printf("%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));            
        return;}
    ret = esp_bluedroid_init();                                                                 
    if (ret) {
        printf("%s init bluetooth failed: %s\n", __func__, esp_err_to_name(ret));               
        return;}
    ret = esp_bluedroid_enable();                                                               
    if (ret) {
        printf("%s enable bluetooth failed: %s\n", __func__, esp_err_to_name(ret));             
        return;}

    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);                                            
    if (ret){
        printf("%s gap register error, error code = %x\n", __func__, ret);                      
        return;
    }
    BLE_Scan();
    // while(1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(150));
    // }
    
    vTaskDelete(NULL);

}
uint16_t BLE_Scan(void)
{
    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_RPA_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,     
        .scan_window = 0x30,        
        .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
    };
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));

    printf("Starting BLE scan...\n");
    ESP_ERROR_CHECK(esp_ble_gap_start_scanning(SCAN_DURATION));
    
    // Set scanning duration
    vTaskDelay(SCAN_DURATION * 1000 / portTICK_PERIOD_MS);
    
    printf("Stopping BLE scan...\n");
    // ESP_ERROR_CHECK(esp_ble_gap_stop_scanning());
    ESP_ERROR_CHECK(esp_ble_dtm_stop());
    BLE_Scan_Finish = 1;
    if(WiFi_Scan_Finish == 1)
        Scan_finish = 1;
    return BLE_NUM;
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