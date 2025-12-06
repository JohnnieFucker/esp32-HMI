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

/**
 * 打印详细的内存使用情况
 * 包括内部RAM、SPIRAM、最小剩余内存等信息
 */
void utils_print_memory_info(void);

/**
 * 打印详细的内存占用分解
 * 显示各个内存类型的占用情况、任务栈占用、组件内存占用等
 */
void utils_print_memory_breakdown(void);

/**
 * 获取内存使用情况的字符串表示
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 写入的字符数
 */
int utils_get_memory_info_string(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
