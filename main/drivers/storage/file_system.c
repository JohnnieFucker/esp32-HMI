#include "file_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "FileSystem";

static bool g_initialized = false;

esp_err_t filesystem_init(const char *base_path,
                          const char *partition_label,
                          int max_files,
                          bool format_if_mount_failed) {
    if (g_initialized) {
        ESP_LOGW(TAG, "文件系统已经初始化");
        return ESP_OK;
    }

    if (base_path == NULL || partition_label == NULL) {
        ESP_LOGE(TAG, "base_path或partition_label不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = partition_label,
        .max_files = max_files > 0 ? max_files : 5,
        .format_if_mount_failed = format_if_mount_failed
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "挂载或格式化文件系统失败");
            ESP_LOGE(TAG, "请检查分区表是否正确烧录，分区名称: %s", partition_label);
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "未找到SPIFFS分区，分区名称: %s", partition_label);
            ESP_LOGE(TAG, "请检查：1. 分区表是否正确烧录 2. 分区名称是否匹配 3. 分区类型是否为 spiffs");
        } else {
            ESP_LOGE(TAG, "初始化SPIFFS失败 (%s), 错误代码: %d", esp_err_to_name(ret), ret);
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取SPIFFS分区信息失败 (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "分区大小: 总计: %d 字节, 已使用: %d 字节", total, used);
    }

    g_initialized = true;
    ESP_LOGI(TAG, "SPIFFS初始化成功，挂载点: %s", base_path);
    return ESP_OK;
}

esp_err_t filesystem_deinit(void) {
    if (!g_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_spiffs_unregister(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "卸载SPIFFS失败 (%s)", esp_err_to_name(ret));
        return ret;
    }

    g_initialized = false;
    ESP_LOGI(TAG, "SPIFFS已卸载");
    return ESP_OK;
}

bool filesystem_is_initialized(void) {
    return g_initialized;
}

esp_err_t filesystem_ensure_dir(const char *dir_path) {
    if (dir_path == NULL) {
        ESP_LOGE(TAG, "目录路径不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st = {0};
    if (stat(dir_path, &st) == -1) {
        // 目录不存在，创建它
        if (mkdir(dir_path, 0700) != 0) {
            ESP_LOGE(TAG, "创建目录失败: %s", dir_path);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "创建目录: %s", dir_path);
    }

    return ESP_OK;
}

esp_err_t filesystem_get_info(size_t *total, size_t *used) {
    if (total == NULL || used == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_initialized) {
        ESP_LOGE(TAG, "文件系统未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_spiffs_info(NULL, total, used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取SPIFFS分区信息失败 (%s)", esp_err_to_name(ret));
    }

    return ret;
}

