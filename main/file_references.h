/* SPDX-License-Identifier: Apache-2.0
 * 文件编辑的设置引用事务；不改逐本进度或固定目录。
 * Settings-reference intent for file edits; book progress and fixed roots stay separate.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
// 调用方必须串行执行 begin → 文件操作 → finish，不在 render 中调用。
// Serialize begin -> filesystem edit -> finish; never call from render.
// 新操作前重试上一份intent。重复同一已完成文件操作时 done=true，禁止再次执行文件编辑。
// Retry the previous intent first. done=true means a repeated edit is already physically complete.
esp_err_t file_references_begin(const char *source,const char *target,bool *done);
esp_err_t file_references_finish(void);
