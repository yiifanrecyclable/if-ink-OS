/* SPDX-License-Identifier: Apache-2.0
 * 中文：UI任务上的网页个性化校验、预览与完整快照保存。
 * English: UI-owned web personalization validation, preview and snapshot commit.
 */
#pragma once
#include "read_pico_transfer_editor.h"
esp_err_t transfer_appearance_dispatch(const char *operation,const char *json,
    read_pico_transfer_editor_reply_t *reply,read_pico_transfer_cancel_fn cancelled,void *context);
bool transfer_appearance_path_writable(const char *path);
/// 停止HTTP后清理本次会话临时字体。/ Remove staged fonts after HTTP has stopped.
void transfer_appearance_cleanup(void);
