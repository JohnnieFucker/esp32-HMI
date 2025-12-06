#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * 生成UUID（简化版）
 * @param uuid 输出缓冲区
 * @param uuid_size 缓冲区大小（至少37字节）
 * @return 0 成功，-1 失败
 */
int utils_generate_uuid(char *uuid, size_t uuid_size);

#ifdef __cplusplus
}
#endif
