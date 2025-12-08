#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * 开始录音业务逻辑
 * 每隔30秒录音文件压缩为mp3文件，然后调用API_UPLOAD接口上传录音文件
 *
 * @return 0 成功，非0 失败
 */
int start_note_recording(void);

/**
 * 停止录音业务逻辑
 *
 * @param duration_sec 录音持续时间（秒），用于判断是否提交数据
 *                     如果 >= 60 秒则上传数据并生成笔记
 *                     如果 < 60 秒则只停止录音，不提交数据
 * @return 0 成功，非0 失败
 */
int stop_note_recording(uint32_t duration_sec);

/**
 * 生成笔记
 * 调用API_NOTE接口生成笔记
 *
 * @param note_id 笔记ID（从上传接口返回）
 * @param device 设备类型（如"mac", "esp32"等）
 * @param is_voice 是否为语音笔记
 * @param type 笔记类型
 * @param version 版本号
 * @return 0 成功，非0 失败
 */
int generate_note(const char *note_id, const char *device, bool is_voice,
                  int type, const char *version);

/**
 * 检查录音是否正在进行
 *
 * @return true 正在录音，false 未在录音
 */
bool is_recording(void);

#ifdef __cplusplus
}
#endif
