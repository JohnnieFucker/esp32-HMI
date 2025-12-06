#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>

/**
 * 初始化SPIFFS文件系统
 * @param base_path 挂载点路径
 * @param partition_label 分区标签
 * @param max_files 最大文件数
 * @param format_if_mount_failed 挂载失败时是否格式化
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t filesystem_init(const char *base_path, const char *partition_label,
                          int max_files, bool format_if_mount_failed);

/**
 * 反初始化SPIFFS文件系统
 * @return ESP_OK 成功
 */
esp_err_t filesystem_deinit(void);

/**
 * 检查文件系统是否已初始化
 * @return true 已初始化，false 未初始化
 */
bool filesystem_is_initialized(void);

/**
 * 确保目录存在（如果不存在则创建）
 * @param dir_path 目录路径
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t filesystem_ensure_dir(const char *dir_path);

/**
 * 获取文件系统信息
 * @param total 输出总大小（字节）
 * @param used 输出已使用大小（字节）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t filesystem_get_info(size_t *total, size_t *used);

#ifdef __cplusplus
}
#endif
