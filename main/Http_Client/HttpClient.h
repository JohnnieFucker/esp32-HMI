#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_http_client.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * HTTPS证书验证模式
 */
typedef enum {
  HTTP_SSL_VERIFY_NONE = 0,   // 不验证证书（仅用于开发环境，不安全）
  HTTP_SSL_VERIFY_CRT_BUNDLE, // 使用CRT Bundle验证（推荐，包含主流CA证书）
  HTTP_SSL_VERIFY_CUSTOM_CERT // 使用自定义证书验证
} http_ssl_verify_mode_t;

/**
 * HTTP响应回调函数类型
 * @param data 响应数据
 * @param len 数据长度
 * @param user_data 用户数据
 * @return 返回ESP_OK继续，其他值停止
 */
typedef esp_err_t (*http_response_cb_t)(const char *data, int len,
                                        void *user_data);

/**
 * HTTP请求配置
 */
typedef struct {
  const char *url;                // 请求URL
  const char *method;             // 请求方法（GET, POST等）
  const char *content_type;       // Content-Type头
  const char *token;              // 认证token（可选）
  const char *body;               // 请求体（可选）
  size_t body_len;                // 请求体长度
  int timeout_ms;                 // 超时时间（毫秒）
  http_response_cb_t response_cb; // 响应回调（可选）
  void *user_data;                // 用户数据

  // HTTPS SSL/TLS 配置（可选）
  http_ssl_verify_mode_t
      ssl_verify_mode; // SSL验证模式（默认：HTTP_SSL_VERIFY_CRT_BUNDLE）
  const char *
      cert_pem; // 自定义服务器证书（PEM格式，仅在HTTP_SSL_VERIFY_CUSTOM_CERT模式下使用）
  bool
      skip_cert_common_name_check; // 跳过证书通用名称检查（仅在HTTP_SSL_VERIFY_CUSTOM_CERT模式下使用）
} http_request_config_t;

/**
 * 发送HTTP请求（JSON格式）
 * @param config 请求配置
 * @param status_code 输出状态码（可选，传NULL则忽略）
 * @param response_buffer 响应缓冲区（可选）
 * @param response_buffer_size 响应缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t http_client_post_json(const http_request_config_t *config,
                                int *status_code, char *response_buffer,
                                size_t response_buffer_size);

/**
 * 发送HTTP请求（multipart/form-data格式，用于文件上传）
 * @param config 请求配置
 * @param file_path 要上传的文件路径
 * @param file_field_name 文件字段名（默认为"file"）
 * @param file_name_field_name 文件名字段名（默认为"fileName"）
 * @param status_code 输出状态码（可选，传NULL则忽略）
 * @param response_buffer 响应缓冲区（可选）
 * @param response_buffer_size 响应缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t http_client_post_multipart(const http_request_config_t *config,
                                     const char *file_path,
                                     const char *file_field_name,
                                     const char *file_name_field_name,
                                     int *status_code, char *response_buffer,
                                     size_t response_buffer_size);

/**
 * 发送HTTP请求（multipart/form-data格式，从内存缓冲区上传）
 * @param config 请求配置
 * @param file_data 要上传的文件数据（内存缓冲区）
 * @param file_data_size 文件数据大小（字节）
 * @param file_name 文件名（用于上传）
 * @param file_field_name 文件字段名（默认为"file"）
 * @param file_name_field_name 文件名字段名（默认为"fileName"）
 * @param status_code 输出状态码（可选，传NULL则忽略）
 * @param response_buffer 响应缓冲区（可选）
 * @param response_buffer_size 响应缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t http_client_post_multipart_from_memory(
    const http_request_config_t *config, const uint8_t *file_data,
    size_t file_data_size, const char *file_name, const char *file_field_name,
    const char *file_name_field_name, int *status_code, char *response_buffer,
    size_t response_buffer_size);

/**
 * 发送HTTP请求（multipart/form-data格式，流式上传，避免文件数据重复占用内存）
 * @param config 请求配置
 * @param file_data 要上传的文件数据（内存缓冲区）
 * @param file_data_size 文件数据大小（字节）
 * @param file_name 文件名（用于上传）
 * @param file_field_name 文件字段名（默认为"file"）
 * @param file_name_field_name 文件名字段名（默认为"fileName"）
 * @param status_code 输出状态码（可选，传NULL则忽略）
 * @param response_buffer 响应缓冲区（可选）
 * @param response_buffer_size 响应缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 *
 * 注意：此函数使用流式上传，不会将文件数据复制到multipart body中，
 * 而是直接写入HTTP连接，从而避免文件数据在内存中重复占用。
 * 内存占用：仅multipart头部和尾部（约200-300字节），而不是文件大小+头部+尾部。
 */
esp_err_t http_client_post_multipart_streaming(
    const http_request_config_t *config, const uint8_t *file_data,
    size_t file_data_size, const char *file_name, const char *file_field_name,
    const char *file_name_field_name, int *status_code, char *response_buffer,
    size_t response_buffer_size);

/**
 * 发送HTTP请求（通用方法）
 * @param config 请求配置
 * @param status_code 输出状态码（可选，传NULL则忽略）
 * @param response_buffer 响应缓冲区（可选）
 * @param response_buffer_size 响应缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t http_client_request(const http_request_config_t *config,
                              int *status_code, char *response_buffer,
                              size_t response_buffer_size);

#ifdef __cplusplus
}
#endif
