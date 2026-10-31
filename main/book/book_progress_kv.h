/*
 * SPDX-FileCopyrightText: 2026 ifRecyclable
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读进度的持久键值层；TF双槽优先，旧NVS只在无覆盖记录时读取。
 * Durable progress keys: prefer SD dual slots, reading legacy NVS only without an override.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nvs.h"

typedef struct {
    bool sd; ///< 此句柄使用TF卡。/ This handle uses SD.
    nvs_handle_t legacy; ///< 无TF时沿用NVS。/ NVS fallback without SD.
} progress_kv_handle_t;

/// 进度写入与遗忘仍须由调用方串行化；不会创建或挂载TF根目录。
/// Callers still serialize writes/forget; never create or mount the SD root.
esp_err_t progress_kv_open(int mode, progress_kv_handle_t *out);
void progress_kv_close(progress_kv_handle_t h);
esp_err_t progress_kv_get_blob(progress_kv_handle_t h, const char *key, void *out, size_t *len);
esp_err_t progress_kv_get_str(progress_kv_handle_t h, const char *key, char *out, size_t *len);
esp_err_t progress_kv_get_u32(progress_kv_handle_t h, const char *key, uint32_t *out);
esp_err_t progress_kv_set_blob(progress_kv_handle_t h, const char *key, const void *data, size_t len);
esp_err_t progress_kv_set_str(progress_kv_handle_t h, const char *key, const char *data);
esp_err_t progress_kv_set_u32(progress_kv_handle_t h, const char *key, uint32_t data);
esp_err_t progress_kv_erase_key(progress_kv_handle_t h, const char *key);
esp_err_t progress_kv_commit(progress_kv_handle_t h);

/// 导出有效键，合并TF与未被覆盖的旧NVS；false提前结束，不重复路径。
/// Export usable keys, merging SD with unshadowed NVS; false stops early, without duplicates.
typedef bool (*progress_kv_visit_fn)(const char *key, nvs_type_t type, const void *data, size_t len, void *ctx);
esp_err_t progress_kv_list(progress_kv_visit_fn visit, void *ctx);
/// 恢复备份时替换进度库；TF墓碑阻止旧NVS复活，失败返回调用方。
/// Replace the progress library during restore; SD tombstones suppress legacy resurrection, returning failures.
esp_err_t progress_kv_reset(void);
