/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：系统与阅读使用独立保存的字体；页面边界切换活动字形。
 * English: System and reader fonts persist separately; page boundaries switch the active face.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

bool app_font_activate_system(void);
bool app_font_activate_reading(void);
bool app_font_reading_active(void);
/// 恢复阅读面板借用系统字体之前的实际字体。/ Restore the effective reader face after controls borrow the system face.
void app_font_restore_reading(const char *path);
typedef enum { APP_FONT_READING, APP_FONT_SYSTEM } app_font_role_t;
esp_err_t app_font_save_choice(app_font_role_t role, bool english, const char *path);
const char *app_font_reading_choice(void);
void app_font_set_english_mode(bool english);
const char *app_font_system_choice(void);
bool app_font_retry_active(void);
