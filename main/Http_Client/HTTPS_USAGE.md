# HttpClient HTTPS 使用指南

## 概述

HttpClient 模块已升级支持安全的 HTTPS 连接，提供三种证书验证模式：

1. **CRT Bundle 验证**（推荐）：使用 ESP-IDF 内置的全球主流 CA 证书包
2. **自定义证书验证**：使用您自己的服务器证书
3. **不验证**（仅开发环境）：跳过证书验证（不安全）

## 配置 CRT Bundle（推荐方式）

### 步骤 1：启用 CRT Bundle

运行 menuconfig 配置：

```bash
idf.py menuconfig
```

导航到：
```
Component config → ESP-TLS → Allow HTTP Client to use World Wide Web Certificates Bundle
```

启用该选项并保存。

### 步骤 2：使用 CRT Bundle（默认）

如果启用了 CRT Bundle，代码会自动使用它（默认行为）：

```c
#include "HttpClient.h"

http_request_config_t config = {
    .url = "https://api.example.com/data",
    .method = "POST",
    .content_type = "application/json",
    .body = json_data,
    .body_len = strlen(json_data),
    .timeout_ms = 30000,
    // ssl_verify_mode 未设置时，默认使用 CRT Bundle（如果可用）
};

int status_code = 0;
esp_err_t err = http_client_post_json(&config, &status_code, NULL, 0);
```

### 步骤 3：显式指定 CRT Bundle

您也可以显式指定使用 CRT Bundle：

```c
http_request_config_t config = {
    .url = "https://api.example.com/data",
    .method = "POST",
    .ssl_verify_mode = HTTP_SSL_VERIFY_CRT_BUNDLE,  // 显式使用 CRT Bundle
    // ... 其他配置
};
```

## 使用自定义证书

如果您需要验证特定的服务器证书：

```c
// 定义服务器证书（PEM 格式）
const char *server_cert = \
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7svZGOnn2iLw5QY+s4wGkR+Q/Awe0F\n"
// ... 证书的其他行 ...
"-----END CERTIFICATE-----\n";

http_request_config_t config = {
    .url = "https://api.example.com/data",
    .method = "POST",
    .ssl_verify_mode = HTTP_SSL_VERIFY_CUSTOM_CERT,
    .cert_pem = server_cert,
    .skip_cert_common_name_check = false,  // 验证证书的通用名称
    // ... 其他配置
};
```

### 获取服务器证书

1. 使用浏览器访问目标网站
2. 点击地址栏的锁图标
3. 查看证书详情
4. 导出 Root CA 证书（PEM 格式）
5. 将证书内容粘贴到代码中

## 开发环境：跳过证书验证（不推荐）

⚠️ **警告**：仅用于开发环境，生产环境不安全！

```c
http_request_config_t config = {
    .url = "https://api.example.com/data",
    .method = "POST",
    .ssl_verify_mode = HTTP_SSL_VERIFY_NONE,  // 不验证证书
    // ... 其他配置
};
```

## 向后兼容性

现有代码无需修改即可继续工作：

- 如果 CRT Bundle 已启用，将自动使用安全的证书验证
- 如果 CRT Bundle 未启用，将回退到不验证模式（保持原有行为）

## 完整示例

### 示例 1：使用 CRT Bundle 发送 POST 请求

```c
#include "HttpClient.h"

void send_https_request(void) {
    char json_data[256];
    snprintf(json_data, sizeof(json_data), "{\"key\":\"value\"}");
    
    http_request_config_t config = {
        .url = "https://jsonplaceholder.typicode.com/posts",
        .method = "POST",
        .content_type = "application/json",
        .body = json_data,
        .body_len = strlen(json_data),
        .timeout_ms = 30000,
        .ssl_verify_mode = HTTP_SSL_VERIFY_CRT_BUNDLE,  // 使用 CRT Bundle
    };
    
    int status_code = 0;
    char response[512] = {0};
    esp_err_t err = http_client_post_json(&config, &status_code, response, sizeof(response));
    
    if (err == ESP_OK && status_code == 201) {
        ESP_LOGI("APP", "请求成功: %s", response);
    } else {
        ESP_LOGE("APP", "请求失败: %d", status_code);
    }
}
```

### 示例 2：上传文件（multipart）

```c
#include "HttpClient.h"

void upload_file_https(const char *file_path) {
    http_request_config_t config = {
        .url = "https://api.example.com/upload",
        .method = "POST",
        .token = "your-auth-token",
        .timeout_ms = 60000,  // 文件上传可能需要更长时间
        .ssl_verify_mode = HTTP_SSL_VERIFY_CRT_BUNDLE,
    };
    
    int status_code = 0;
    char response[512] = {0};
    esp_err_t err = http_client_post_multipart(
        &config,
        file_path,
        "file",           // 文件字段名
        "fileName",       // 文件名字段名
        &status_code,
        response,
        sizeof(response)
    );
    
    if (err == ESP_OK) {
        ESP_LOGI("APP", "上传成功: %s", response);
    }
}
```

## 故障排除

### 问题 1：编译错误 "esp_crt_bundle_attach undeclared"

**解决方案**：确保已在 menuconfig 中启用 CRT Bundle。

### 问题 2：HTTPS 连接失败

**可能原因**：
1. CRT Bundle 未启用
2. 服务器证书不在 CRT Bundle 中
3. 网络连接问题

**解决方案**：
1. 检查 menuconfig 配置
2. 尝试使用自定义证书模式
3. 检查网络连接和 URL

### 问题 3：证书验证失败

**解决方案**：
- 如果使用自定义证书，确保证书格式正确（PEM 格式）
- 检查证书是否过期
- 验证证书是否与服务器匹配

## 最佳实践

1. **生产环境**：始终使用 CRT Bundle 或自定义证书验证
2. **开发环境**：可以使用不验证模式进行快速测试
3. **性能**：CRT Bundle 验证比自定义证书稍慢，但更通用
4. **安全性**：CRT Bundle 包含主流 CA，适合大多数场景

## 相关配置

在 `sdkconfig` 或 `sdkconfig.defaults` 中可以添加：

```
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

这将启用完整的 CRT Bundle（包含更多 CA 证书，但占用更多 Flash 空间）。

