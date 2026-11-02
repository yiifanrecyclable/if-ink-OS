/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：文件管理与首页请求内容页打开指定文件。
 * English: File manager and home request a specific file in the content pages.
 */
#pragma once

#include <stdbool.h>

bool app_book_request_open(const char *path);
bool app_book_request_open_from_home(const char *path);
bool app_image_request_open(const char *path);
void app_files_request_folder(int folder);
/// 图片查看结束后，下一次文件页进入恢复原目录/页码。/ Restore the prior directory/page on the next Files entry after image viewing.
void app_files_request_image_return(void);
void app_book_request_manage(void);
bool app_book_reader_body_visible(void);
/// 首页长按请求统一格言编辑；返回首页并独立于状态栏签名。/ Home requests quote editing, separate from the status signature.
void app_device_settings_request_home_quote(void);
bool app_device_settings_home_quote_requested(void);
