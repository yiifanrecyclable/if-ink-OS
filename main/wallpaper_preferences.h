/* SPDX-License-Identifier: Apache-2.0
 * 中文：壁纸选择与轮播状态的TF双槽持久化，不接触其他设置或阅读数据。
 * English: SD dual-slot wallpaper/rotation preferences, isolated from other settings and reading data.
 */
#pragma once
#include "esp_err.h"
#define WALLPAPER_PREF_PATH_BYTES 288
typedef struct { char path[WALLPAPER_PREF_PATH_BYTES],last[WALLPAPER_PREF_PATH_BYTES]; } wallpaper_preferences_t;
/// 仅访问已挂载TF；校验失败不返回损坏路径。/ Only access an already-mounted card; never return damaged paths.
esp_err_t wallpaper_preferences_load(wallpaper_preferences_t *out);
/// 写非当前槽并同步/读回校验，失败保留上一完整副本。/ Sync and verify the inactive slot, retaining the last complete copy on failure.
esp_err_t wallpaper_preferences_save(const wallpaper_preferences_t *value);
