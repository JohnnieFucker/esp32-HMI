/**
 * @file network_monitor.h
 * @brief 网络连接监控服务
 *
 * 功能：
 * - 持续监控 WiFi 连接状态
 * - 网络断开时播放语音提示
 * - 支持配置检测间隔和提示音间隔
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 启动网络监控任务
 *
 * 需要在 WiFi 和音频系统初始化之后调用
 */
void network_monitor_start(void);

/**
 * 停止网络监控任务
 */
void network_monitor_stop(void);

/**
 * 检查网络监控是否运行中
 * @return true 运行中，false 未运行
 */
bool network_monitor_is_running(void);

#ifdef __cplusplus
}
#endif
