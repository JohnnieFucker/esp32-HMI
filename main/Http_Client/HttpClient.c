#include "HttpClient.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

// 如果启用了CRT Bundle，包含相关头文件
// 在 ESP-IDF v5.x 中，通过 menuconfig 启用：
// Component config -> ESP-TLS -> Allow HTTP Client to use World Wide Web Certificates Bundle
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) || defined(CONFIG_ESP_TLS_INSECURE)
// 尝试包含 CRT Bundle 头文件，如果不存在则定义宏为0
#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define HTTP_CLIENT_CRT_BUNDLE_AVAILABLE 1
#else
#define HTTP_CLIENT_CRT_BUNDLE_AVAILABLE 0
#endif
#else
#define HTTP_CLIENT_CRT_BUNDLE_AVAILABLE 0
#endif

static const char *TAG = "HttpClient";

// HTTP事件处理器
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_request_config_t *config = (http_request_config_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (config && config->response_cb) {
                config->response_cb((const char *)evt->data, evt->data_len, config->user_data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t http_client_post_json(const http_request_config_t *config,
                                int *status_code,
                                char *response_buffer,
                                size_t response_buffer_size) {
    if (config == NULL || config->url == NULL) {
        ESP_LOGE(TAG, "配置或URL不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    http_request_config_t json_config = *config;
    if (json_config.content_type == NULL) {
        json_config.content_type = "application/json";
    }

    return http_client_request(&json_config, status_code, response_buffer, response_buffer_size);
}

esp_err_t http_client_post_multipart(const http_request_config_t *config,
                                     const char *file_path,
                                     const char *file_field_name,
                                     const char *file_name_field_name,
                                     int *status_code,
                                     char *response_buffer,
                                     size_t response_buffer_size) {
    if (config == NULL || config->url == NULL || file_path == NULL) {
        ESP_LOGE(TAG, "配置、URL或文件路径不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    const char *field_name = file_field_name ? file_field_name : "file";
    const char *name_field_name = file_name_field_name ? file_name_field_name : "fileName";

    // 读取文件
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "无法打开文件: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (file_data == NULL) {
        ESP_LOGE(TAG, "无法分配内存读取文件");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    fread(file_data, 1, file_size, file);
    fclose(file);

    // 提取文件名（不含路径）
    const char *filename_only = strrchr(file_path, '/');
    if (filename_only == NULL) {
        filename_only = file_path;
    } else {
        filename_only++;  // 跳过 '/'
    }

    // 构建multipart边界
    char boundary[64] = "----WebKitFormBoundaryzrFQBJBH1leZOl25";
    char content_type[256];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    // 计算body大小
    size_t body_start_size = snprintf(NULL, 0,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        boundary, field_name, filename_only);
    
    size_t body_end_size = snprintf(NULL, 0,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, name_field_name, filename_only, boundary);
    
    size_t body_size = body_start_size + file_size + body_end_size;
    
    // 优先从PSRAM分配内存（大文件需要大量内存）
    uint8_t *body = NULL;
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_spiram >= body_size) {
        body = (uint8_t *)heap_caps_malloc(body_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (body != NULL) {
            ESP_LOGI(TAG, "从PSRAM分配multipart请求体内存: %d KB", body_size / 1024);
        }
    }
    
    // 如果PSRAM分配失败，尝试普通内存
    if (body == NULL) {
        body = (uint8_t *)heap_caps_malloc(body_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (body != NULL) {
            ESP_LOGW(TAG, "从普通内存分配multipart请求体: %d KB (PSRAM不可用或不足)", body_size / 1024);
        }
    }
    
    // 如果仍然失败，尝试标准malloc
    if (body == NULL) {
        body = (uint8_t *)malloc(body_size);
    }
    
    if (body == NULL) {
        ESP_LOGE(TAG, "无法分配内存构建请求体 (%d KB)，可用内存不足", body_size / 1024);
        ESP_LOGE(TAG, "PSRAM空闲: %d KB, 需要: %d KB", free_spiram / 1024, body_size / 1024);
        free(file_data);
        return ESP_ERR_NO_MEM;
    }

    // 构建body
    size_t offset = 0;
    offset += snprintf((char *)body + offset, body_size - offset,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        boundary, field_name, filename_only);
    
    memcpy(body + offset, file_data, file_size);
    offset += file_size;
    
    offset += snprintf((char *)body + offset, body_size - offset,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, name_field_name, filename_only, boundary);

    free(file_data);

    // 创建请求配置
    http_request_config_t multipart_config = *config;
    multipart_config.content_type = content_type;
    multipart_config.body = (const char *)body;
    multipart_config.body_len = body_size;

    // 发送请求
    esp_err_t ret = http_client_request(&multipart_config, status_code, response_buffer, response_buffer_size);

    free(body);
    return ret;
}

esp_err_t http_client_post_multipart_from_memory(const http_request_config_t *config,
                                                 const uint8_t *file_data,
                                                 size_t file_data_size,
                                                 const char *file_name,
                                                 const char *file_field_name,
                                                 const char *file_name_field_name,
                                                 int *status_code,
                                                 char *response_buffer,
                                                 size_t response_buffer_size) {
    if (config == NULL || config->url == NULL || file_data == NULL || file_name == NULL) {
        ESP_LOGE(TAG, "配置、URL、文件数据或文件名不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    const char *field_name = file_field_name ? file_field_name : "file";
    const char *name_field_name = file_name_field_name ? file_name_field_name : "fileName";

    // 构建multipart边界
    char boundary[64] = "----WebKitFormBoundaryzrFQBJBH1leZOl25";
    char content_type[256];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    // 计算body大小
    size_t body_start_size = snprintf(NULL, 0,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        boundary, field_name, file_name);
    
    size_t body_end_size = snprintf(NULL, 0,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, name_field_name, file_name, boundary);
    
    size_t body_size = body_start_size + file_data_size + body_end_size;
    
    // 优先从PSRAM分配内存（大文件需要大量内存）
    uint8_t *body = NULL;
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_spiram >= body_size) {
        body = (uint8_t *)heap_caps_malloc(body_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (body != NULL) {
            ESP_LOGI(TAG, "从PSRAM分配multipart请求体内存: %d KB", body_size / 1024);
        }
    }
    
    // 如果PSRAM分配失败，尝试普通内存
    if (body == NULL) {
        body = (uint8_t *)heap_caps_malloc(body_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (body != NULL) {
            ESP_LOGW(TAG, "从普通内存分配multipart请求体: %d KB (PSRAM不可用或不足)", body_size / 1024);
        }
    }
    
    // 如果仍然失败，尝试标准malloc
    if (body == NULL) {
        body = (uint8_t *)malloc(body_size);
    }
    
    if (body == NULL) {
        ESP_LOGE(TAG, "无法分配内存构建请求体 (%d KB)，可用内存不足", body_size / 1024);
        ESP_LOGE(TAG, "PSRAM空闲: %d KB, 需要: %d KB", free_spiram / 1024, body_size / 1024);
        return ESP_ERR_NO_MEM;
    }

    // 构建body
    size_t offset = 0;
    offset += snprintf((char *)body + offset, body_size - offset,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        boundary, field_name, file_name);
    
    memcpy(body + offset, file_data, file_data_size);
    offset += file_data_size;
    
    offset += snprintf((char *)body + offset, body_size - offset,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, name_field_name, file_name, boundary);

    // 创建请求配置
    http_request_config_t multipart_config = *config;
    multipart_config.content_type = content_type;
    multipart_config.body = (const char *)body;
    multipart_config.body_len = body_size;

    // 发送请求
    esp_err_t ret = http_client_request(&multipart_config, status_code, response_buffer, response_buffer_size);

    free(body);
    return ret;
}

esp_err_t http_client_post_multipart_streaming(const http_request_config_t *config,
                                                const uint8_t *file_data,
                                                size_t file_data_size,
                                                const char *file_name,
                                                const char *file_field_name,
                                                const char *file_name_field_name,
                                                int *status_code,
                                                char *response_buffer,
                                                size_t response_buffer_size) {
    if (config == NULL || config->url == NULL || file_data == NULL || file_name == NULL) {
        ESP_LOGE(TAG, "配置、URL、文件数据或文件名不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    const char *field_name = file_field_name ? file_field_name : "file";
    const char *name_field_name = file_name_field_name ? file_name_field_name : "fileName";

    // 构建multipart边界
    char boundary[64] = "----WebKitFormBoundaryzrFQBJBH1leZOl25";
    char content_type[256];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    // 计算multipart头部和尾部大小（不包含文件数据）
    size_t body_start_size = snprintf(NULL, 0,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        boundary, field_name, file_name);
    
    size_t body_end_size = snprintf(NULL, 0,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, name_field_name, file_name, boundary);
    
    // 总大小（用于Content-Length）
    size_t total_body_size = body_start_size + file_data_size + body_end_size;
    
    ESP_LOGI(TAG, "流式上传: 文件大小 %zu KB, 总body大小 %zu KB (仅头部尾部占用约 %zu 字节内存)",
             file_data_size / 1024, total_body_size / 1024, body_start_size + body_end_size);

    // 准备响应缓冲区
    if (response_buffer != NULL && response_buffer_size > 0) {
        response_buffer[0] = '\0';
    }

    // 创建HTTP客户端配置（复用http_client_request的逻辑）
    esp_http_client_config_t client_config = {0};
    client_config.url = config->url;
    client_config.method = HTTP_METHOD_POST;
    client_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000;
    client_config.event_handler = config->response_cb ? http_event_handler : NULL;
    client_config.user_data = (void *)config;

    // HTTPS SSL/TLS 配置
    http_ssl_verify_mode_t verify_mode = config->ssl_verify_mode;
    if (verify_mode == 0) {
#if HTTP_CLIENT_CRT_BUNDLE_AVAILABLE
        verify_mode = HTTP_SSL_VERIFY_CRT_BUNDLE;
#else
        verify_mode = HTTP_SSL_VERIFY_NONE;
#endif
    }
    
    if (verify_mode == HTTP_SSL_VERIFY_NONE) {
        // 警告：HTTPS证书验证已禁用，连接不安全！
        // 减少不必要的日志打印
        // ESP_LOGW(TAG, "警告：HTTPS证书验证已禁用，连接不安全！");
        client_config.skip_cert_common_name_check = true;
        client_config.cert_pem = NULL;
        client_config.client_cert_pem = NULL;
        client_config.client_key_pem = NULL;
        client_config.crt_bundle_attach = NULL;
#ifdef ESP_HTTP_CLIENT_CONFIG_USE_GLOBAL_CA_STORE
        client_config.use_global_ca_store = false;
#endif
        // 显式禁用 mbedTLS 内存优化配置，可能会导致握手失败
        // 如果内存足够，建议不设置此项或设置为false
        // client_config.keep_alive_enable = true; 
    } else if (verify_mode == HTTP_SSL_VERIFY_CRT_BUNDLE) {
#if HTTP_CLIENT_CRT_BUNDLE_AVAILABLE
        client_config.crt_bundle_attach = esp_crt_bundle_attach;
        ESP_LOGI(TAG, "使用CRT Bundle进行HTTPS证书验证");
#else
        ESP_LOGW(TAG, "CRT Bundle不可用，回退到不验证模式");
        client_config.skip_cert_common_name_check = true;
#endif
    }

    // 初始化HTTP客户端
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "无法初始化HTTP客户端");
        return ESP_ERR_NO_MEM;
    }

    // 设置头部
    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_header(client, "Accept", "application/json, text/plain, */*");
    if (config->token != NULL) {
        esp_http_client_set_header(client, "token", config->token);
    }
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%zu", total_body_size);
    esp_http_client_set_header(client, "Content-Length", content_length_str);

    // 打开连接
    esp_err_t err = esp_http_client_open(client, total_body_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "无法打开HTTP连接: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    // 流式写入multipart头部
    char body_start[512];
    int start_len = snprintf(body_start, sizeof(body_start),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        boundary, field_name, file_name);
    
    int written = esp_http_client_write(client, body_start, start_len);
    if (written < 0 || written != start_len) {
        ESP_LOGE(TAG, "写入multipart头部失败: %d", written);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // 流式写入文件数据（分块写入，避免大块内存操作）
    const size_t chunk_size = 64 * 1024;  // 64KB块大小
    size_t remaining = file_data_size;
    const uint8_t *file_ptr = file_data;
    
    while (remaining > 0) {
        size_t to_write = (remaining > chunk_size) ? chunk_size : remaining;
        written = esp_http_client_write(client, (const char *)file_ptr, to_write);
        
        if (written < 0) {
            ESP_LOGE(TAG, "写入文件数据失败: %d", written);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        
        if (written != (int)to_write) {
            ESP_LOGW(TAG, "部分写入: 期望 %zu 字节，实际写入 %d 字节", to_write, written);
        }
        
        file_ptr += written;
        remaining -= written;
    }

    // 流式写入multipart尾部
    char body_end[512];
    int end_len = snprintf(body_end, sizeof(body_end),
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, name_field_name, file_name, boundary);
    
    written = esp_http_client_write(client, body_end, end_len);
    if (written < 0 || written != end_len) {
        ESP_LOGE(TAG, "写入multipart尾部失败: %d", written);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // 完成请求并读取响应
    err = esp_http_client_fetch_headers(client);
    if (err == ESP_OK) {
        int code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP请求状态码 = %d, 内容长度 = %d", code, content_length);
        
        if (status_code != NULL) {
            *status_code = code;
        }

        // 读取响应
        if (response_buffer != NULL && response_buffer_size > 0 && !config->response_cb) {
            if (content_length > 0) {
                char *response = (char *)malloc(content_length + 1);
                if (response != NULL) {
                    int data_read = esp_http_client_read_response(client, response, content_length);
                    response[data_read] = '\0';
                    
                    if (data_read < (int)response_buffer_size) {
                        strncpy(response_buffer, response, response_buffer_size - 1);
                        response_buffer[response_buffer_size - 1] = '\0';
                    } else {
                        strncpy(response_buffer, response, response_buffer_size - 1);
                        response_buffer[response_buffer_size - 1] = '\0';
                    }
                    
                    free(response);
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    ESP_LOGI(TAG, "流式上传完成，内存占用仅multipart头部和尾部（约 %zu 字节）", body_start_size + body_end_size);
    return err;
}

esp_err_t http_client_request(const http_request_config_t *config,
                              int *status_code,
                              char *response_buffer,
                              size_t response_buffer_size) {
    if (config == NULL || config->url == NULL) {
        ESP_LOGE(TAG, "配置或URL不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 打印请求参数
    ESP_LOGI(TAG, "========== HTTP请求参数 ==========");
    ESP_LOGI(TAG, "URL: %s", config->url);
    ESP_LOGI(TAG, "Method: %s", config->method ? config->method : "POST");
    ESP_LOGI(TAG, "Content-Type: %s", config->content_type ? config->content_type : "未设置");
    ESP_LOGI(TAG, "Timeout: %d ms", config->timeout_ms > 0 ? config->timeout_ms : 30000);
    if (config->token != NULL) {
        ESP_LOGI(TAG, "Token: %s", config->token);
    }
    if (config->body != NULL && config->body_len > 0) {
        ESP_LOGI(TAG, "Body长度: %d 字节 (%.2f KB)", config->body_len, config->body_len / 1024.0f);
        // 如果是JSON，打印前256个字符
        if (config->content_type && strstr(config->content_type, "json")) {
            size_t print_len = config->body_len > 256 ? 256 : config->body_len;
            char body_preview[257] = {0};
            memcpy(body_preview, config->body, print_len);
            ESP_LOGI(TAG, "Body预览: %s%s", body_preview, config->body_len > 256 ? "..." : "");
        }
    } else if (config->body_len > 0) {
        ESP_LOGI(TAG, "Body长度: %d 字节 (二进制数据)", config->body_len);
    }
    ESP_LOGI(TAG, "==================================");

    // 准备响应缓冲区
    if (response_buffer != NULL && response_buffer_size > 0) {
        response_buffer[0] = '\0';
    }

    // 创建HTTP客户端配置
    esp_http_client_config_t client_config = {0};  // 初始化为0
    client_config.url = config->url;
    client_config.method = config->method ? 
                  (strcmp(config->method, "GET") == 0 ? HTTP_METHOD_GET :
                   strcmp(config->method, "POST") == 0 ? HTTP_METHOD_POST :
                   strcmp(config->method, "PUT") == 0 ? HTTP_METHOD_PUT :
                   strcmp(config->method, "DELETE") == 0 ? HTTP_METHOD_DELETE :
                   HTTP_METHOD_POST) : HTTP_METHOD_POST;
    client_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000;
    client_config.event_handler = config->response_cb ? http_event_handler : NULL;
    client_config.user_data = (void *)config;
    
    // 注意：HTTP头部缓冲区大小通过menuconfig配置项 CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN 设置
    // 默认值为512字节，对于包含长Token的请求可能不够
    // 建议在 sdkconfig.defaults 中设置：CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN=4096
    
    // HTTPS SSL/TLS 配置
    // 根据配置的验证模式设置相应的选项
    // 如果未设置验证模式，默认尝试使用CRT Bundle（如果可用），否则不验证
    http_ssl_verify_mode_t verify_mode = config->ssl_verify_mode;
    
    // 如果验证模式为0（未初始化），设置默认值
    if (verify_mode == 0) {
#if HTTP_CLIENT_CRT_BUNDLE_AVAILABLE
        verify_mode = HTTP_SSL_VERIFY_CRT_BUNDLE;  // 默认使用CRT Bundle
#else
        verify_mode = HTTP_SSL_VERIFY_NONE;  // CRT Bundle不可用时，默认不验证
#endif
    }
    
    if (verify_mode == HTTP_SSL_VERIFY_NONE) {
        // 模式1：不验证证书（仅用于开发环境，不安全）
        ESP_LOGW(TAG, "警告：HTTPS证书验证已禁用，连接不安全！");
        
        // 在 ESP-IDF v5.x 中，需要显式设置跳过证书验证
        // 关键：必须同时设置以下选项才能成功建立不安全的 HTTPS 连接
        
        // 步骤1：跳过通用名（域名）检查
        client_config.skip_cert_common_name_check = true;
        
        // 步骤2：清除所有证书相关配置（让 mbedTLS 知道不使用证书验证）
        client_config.cert_pem = NULL;
        client_config.client_cert_pem = NULL;
        client_config.client_key_pem = NULL;
        client_config.crt_bundle_attach = NULL;
        
        // 步骤3：在 ESP-IDF v5.x 中，需要显式禁用全局CA存储
        // 这可以避免 mbedTLS 尝试使用未配置的证书存储
        #ifdef ESP_HTTP_CLIENT_CONFIG_USE_GLOBAL_CA_STORE
        client_config.use_global_ca_store = false;
        #endif
        
        // 注意：在 ESP-IDF v5.x 中，还需要在 sdkconfig 中设置以下选项：
        // CONFIG_ESP_TLS_INSECURE=y
        // CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y
        // 
        // 这些配置项会告诉底层的 mbedTLS 库允许不安全的连接
        // 如果仍然报错，请确保：
        // 1. 已重新构建项目（idf.py build）
        // 2. sdkconfig 中确实设置了上述两个选项
        // 3. 检查是否有足够的内存（mbedTLS需要一定内存）
    }
#if HTTP_CLIENT_CRT_BUNDLE_AVAILABLE
    else if (verify_mode == HTTP_SSL_VERIFY_CRT_BUNDLE) {
        // 模式2：使用CRT Bundle验证（推荐方式）
        // CRT Bundle包含了全球大多数主流Root CA证书
        ESP_LOGI(TAG, "使用CRT Bundle进行HTTPS证书验证");
        extern void esp_crt_bundle_attach(void *conf);
        client_config.crt_bundle_attach = esp_crt_bundle_attach;
        client_config.skip_cert_common_name_check = false;
    }
#endif
    else if (verify_mode == HTTP_SSL_VERIFY_CUSTOM_CERT) {
        // 模式3：使用自定义证书验证
        if (config->cert_pem != NULL) {
            ESP_LOGI(TAG, "使用自定义证书进行HTTPS证书验证");
            client_config.cert_pem = config->cert_pem;
            client_config.skip_cert_common_name_check = config->skip_cert_common_name_check;
        } else {
            ESP_LOGW(TAG, "自定义证书模式已选择但未提供证书，回退到不验证模式");
            client_config.skip_cert_common_name_check = true;
            client_config.cert_pem = NULL;
        }
        client_config.client_cert_pem = NULL;
        client_config.client_key_pem = NULL;
    }
    else {
        // 未知的验证模式，回退到不验证
        ESP_LOGW(TAG, "未知的SSL验证模式，回退到不验证模式");
        client_config.skip_cert_common_name_check = true;
        client_config.cert_pem = NULL;
        client_config.client_cert_pem = NULL;
        client_config.client_key_pem = NULL;
        client_config.crt_bundle_attach = NULL;
    }

    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "无法初始化HTTP客户端");
        return ESP_ERR_NO_MEM;
    }

    // 设置请求头
    if (config->content_type != NULL) {
        esp_http_client_set_header(client, "Content-Type", config->content_type);
    }
    esp_http_client_set_header(client, "Accept", "application/json, text/plain, */*");
    if (config->token != NULL) {
        esp_http_client_set_header(client, "token", config->token);
    }

    // 计算并打印HTTP头部总长度
    size_t header_total_len = 0;
    if (config->content_type != NULL) {
        header_total_len += strlen("Content-Type: ") + strlen(config->content_type) + 2; // +2 for \r\n
    }
    header_total_len += strlen("Accept: application/json, text/plain, */*") + 2; // +2 for \r\n
    if (config->token != NULL) {
        header_total_len += strlen("token: ") + strlen(config->token) + 2; // +2 for \r\n
    }
    // 添加其他可能的头部（Host, User-Agent等由ESP-IDF自动添加）
    // 估算URL相关的头部
    if (config->url != NULL) {
        const char *host_start = strstr(config->url, "://");
        if (host_start != NULL) {
            host_start += 3; // 跳过 "://"
            const char *host_end = strchr(host_start, '/');
            if (host_end == NULL) {
                host_end = host_start + strlen(host_start);
            }
            size_t host_len = host_end - host_start;
            header_total_len += strlen("Host: ") + host_len + 2; // +2 for \r\n
        }
    }
    // 估算User-Agent头部（ESP-IDF默认添加）
    header_total_len += strlen("User-Agent: ESP32 HTTP Client/1.0") + 2; // +2 for \r\n
    // 如果有请求体，添加Content-Length头部
    if (config->body != NULL && config->body_len > 0) {
        char content_length_str[32];
        snprintf(content_length_str, sizeof(content_length_str), "%zu", config->body_len);
        header_total_len += strlen("Content-Length: ") + strlen(content_length_str) + 2; // +2 for \r\n
    }
    // 添加最后的空行（\r\n）
    header_total_len += 2;
    
    ESP_LOGI(TAG, "========== HTTP头部信息 ==========");
    ESP_LOGI(TAG, "计算的总头部长度: %zu 字节", header_total_len);
    if (config->content_type != NULL) {
        ESP_LOGI(TAG, "  - Content-Type: %zu 字节", strlen("Content-Type: ") + strlen(config->content_type) + 2);
    }
    ESP_LOGI(TAG, "  - Accept: %zu 字节", strlen("Accept: application/json, text/plain, */*") + 2);
    if (config->token != NULL) {
        ESP_LOGI(TAG, "  - token: %zu 字节", strlen("token: ") + strlen(config->token) + 2);
    }
    // 计算其他头部的长度
    size_t other_headers_len = header_total_len;
    if (config->content_type != NULL) {
        other_headers_len -= (strlen("Content-Type: ") + strlen(config->content_type) + 2);
    }
    other_headers_len -= (strlen("Accept: application/json, text/plain, */*") + 2);
    if (config->token != NULL) {
        other_headers_len -= (strlen("token: ") + strlen(config->token) + 2);
    }
    other_headers_len -= 2; // 最后的空行
    ESP_LOGI(TAG, "  - 其他头部（Host, User-Agent, Content-Length等）: 约 %zu 字节", other_headers_len);
    // 尝试获取配置的缓冲区大小（如果宏已定义）
    #if defined(CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN)
    int configured_buffer_size = CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN;
    ESP_LOGI(TAG, "当前配置的缓冲区大小: %d 字节 (通过 CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN 设置)", 
             configured_buffer_size);
    if (header_total_len > configured_buffer_size) {
        ESP_LOGW(TAG, "警告：计算的头部长度 (%zu) 超过配置的缓冲区大小 (%d)，建议增加 CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN", 
                 header_total_len, configured_buffer_size);
    }
    #elif defined(CONFIG_ESP_HTTP_CLIENT_MAX_HEADER_LEN)
    int configured_buffer_size = CONFIG_ESP_HTTP_CLIENT_MAX_HEADER_LEN;
    ESP_LOGI(TAG, "当前配置的缓冲区大小: %d 字节 (通过 CONFIG_ESP_HTTP_CLIENT_MAX_HEADER_LEN 设置)", 
             configured_buffer_size);
    if (header_total_len > configured_buffer_size) {
        ESP_LOGW(TAG, "警告：计算的头部长度 (%zu) 超过配置的缓冲区大小 (%d)，建议增加配置值", 
                 header_total_len, configured_buffer_size);
    }
    #else
    ESP_LOGI(TAG, "注意：无法读取 HTTP 头部缓冲区配置，请检查 sdkconfig 配置");
    ESP_LOGI(TAG, "建议在 sdkconfig 或 sdkconfig.defaults 中设置: CONFIG_ESP_HTTP_CLIENT_MAX_HTTP_HEADER_LEN=%zu", 
             header_total_len + 512); // 建议值：实际长度 + 512字节余量
    #endif
    ESP_LOGI(TAG, "==================================");

    // 设置请求体
    if (config->body != NULL && config->body_len > 0) {
        esp_http_client_set_post_field(client, config->body, config->body_len);
    }

    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP请求状态码 = %d, 内容长度 = %d", code, content_length);
        
        if (status_code != NULL) {
            *status_code = code;
        }

        // 读取响应（如果没有使用回调）
        if (response_buffer != NULL && response_buffer_size > 0 && !config->response_cb) {
            if (content_length > 0) {
                char *response = (char *)malloc(content_length + 1);
                if (response != NULL) {
                    int data_read = esp_http_client_read_response(client, response, content_length);
                    response[data_read] = '\0';
                    
                    if (data_read < (int)response_buffer_size) {
                        strncpy(response_buffer, response, response_buffer_size - 1);
                        response_buffer[response_buffer_size - 1] = '\0';
                    } else {
                        strncpy(response_buffer, response, response_buffer_size - 1);
                        response_buffer[response_buffer_size - 1] = '\0';
                    }
                    
                    ESP_LOGI(TAG, "响应内容: %s", response);
                    free(response);
                }
            }
        }

        esp_http_client_cleanup(client);
        return (code >= 200 && code < 300) ? ESP_OK : ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
}

