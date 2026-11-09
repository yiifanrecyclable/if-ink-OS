/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：系统与阅读使用独立保存的字体；页面边界切换活动字形。
 * English: System and reader fonts persist separately; page boundaries switch the active face.
 */
#pragma once
#include <stdbool.h>

bool app_font_activate_system(void);
bool app_font_activate_reading(void);
/// 恢复阅读面板借用系统字体之前的实际字体。/ Restore the effective reader face after controls borrow the system face.
void app_font_restore_reading(const char *path);
/// 开书前选择图书或文档的独立字体配置。/ Select the independent book/document font preference before opening.
void app_font_set_document_mode(bool document);
bool app_font_retry_active(void);
