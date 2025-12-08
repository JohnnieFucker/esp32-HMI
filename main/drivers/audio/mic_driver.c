#include "mic_driver.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"

static const char *TAG = "MIC";

static i2s_chan_handle_t rx_handle = NULL;
static bool mic_enabled = false;
static bool mic_initialized = false;

esp_err_t MIC_Init(void)
{
    if (mic_initialized) {
        ESP_LOGW(TAG, "MIC already initialized");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;

    // 创建 I2S 通道（使用与 koi_esp32 相同的 DMA 配置）
    i2s_chan_config_t chan_cfg = {
        .id = MIC_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 I2S 标准模式
    i2s_std_config_t std_cfg = I2S_CONFIG_DEFAULT(MIC_SAMPLE_RATE, I2S_SLOT_MODE_MONO, MIC_BITS_PER_SAMPLE);
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    
    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S std mode: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
        return ret;
    }

    mic_initialized = true;
    ESP_LOGI(TAG, "MIC initialized, sample rate: %d Hz, DMA: %d desc x %d frames", 
             MIC_SAMPLE_RATE, chan_cfg.dma_desc_num, chan_cfg.dma_frame_num);
    
    return ESP_OK;
}

void MIC_Deinit(void)
{
    if (!mic_initialized) {
        return;
    }

    MIC_Enable(false);
    
    if (rx_handle != NULL) {
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
    }

    mic_initialized = false;
    ESP_LOGI(TAG, "MIC deinitialized");
}

void MIC_Enable(bool enable)
{
    if (!mic_initialized || rx_handle == NULL) {
        ESP_LOGE(TAG, "MIC not initialized");
        return;
    }

    if (enable == mic_enabled) {
        return;
    }

    if (enable) {
        esp_err_t ret = i2s_channel_enable(rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "MIC enabled");
    } else {
        esp_err_t ret = i2s_channel_disable(rx_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable I2S channel: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "MIC disabled");
    }

    mic_enabled = enable;
}

bool MIC_IsEnabled(void)
{
    return mic_enabled;
}

// 静态缓冲区，避免频繁 malloc/free
#define MIC_TEMP_BUFFER_SAMPLES 512
static int32_t s_mic_temp_buffer[MIC_TEMP_BUFFER_SAMPLES];

esp_err_t MIC_Read(int16_t *buffer, size_t samples, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!mic_enabled || rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 限制读取样本数，不超过静态缓冲区大小
    if (samples > MIC_TEMP_BUFFER_SAMPLES) {
        samples = MIC_TEMP_BUFFER_SAMPLES;
    }

    // I2S 读取的是 32 位数据，需要转换为 16 位
    size_t bytes_to_read = samples * sizeof(int32_t);

    size_t actual_bytes_read = 0;
    
    esp_err_t ret = i2s_channel_read(rx_handle, s_mic_temp_buffer, bytes_to_read, 
                                      &actual_bytes_read, pdMS_TO_TICKS(timeout_ms));
    
    if (ret == ESP_OK && actual_bytes_read > 0) {
        size_t actual_samples = actual_bytes_read / sizeof(int32_t);
        
        // 将 32 位转换为 16 位，并应用增益
        for (size_t i = 0; i < actual_samples; i++) {
            // 32 位数据的有效位在高位，右移 14 位得到 16 位数据
            int32_t sample = s_mic_temp_buffer[i] >> 14;
            
            // 应用增益
            sample = sample * MIC_GAIN;
            
            // 限幅防止溢出
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            
            buffer[i] = (int16_t)sample;
        }
        
        if (bytes_read != NULL) {
            *bytes_read = actual_samples * sizeof(int16_t);
        }
    } else if (bytes_read != NULL) {
        *bytes_read = 0;
    }

    return ret;
}
