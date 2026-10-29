/* SPDX-License-Identifier: Apache-2.0
 * 中文：网页个性化请求的有界跨任务契约；HTTP不操作字体和屏幕。
 * English: Bounded web-personalization handoff; HTTP never touches fonts or the display.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    int status; ///< HTTP状态 / HTTP status
    uint8_t *data; ///< malloc所有权交给组件 / malloc ownership transferred to the component
    size_t size;
    bool pixels,changed; ///< 684×300低位在前4bpp；配置已提交 / Low-nibble-first 684×300 4bpp; committed configuration
} read_pico_transfer_editor_reply_t;
typedef bool (*read_pico_transfer_cancel_fn)(void *context);
typedef esp_err_t (*read_pico_transfer_editor_fn)(const char *operation,const char *json,
    read_pico_transfer_editor_reply_t *reply,read_pico_transfer_cancel_fn cancelled,void *context);
/// 只由UI任务调用；处理最多一份草稿，不推屏。/ UI task only: process at most one draft without presenting the display.
void read_pico_transfer_editor_poll(void);
