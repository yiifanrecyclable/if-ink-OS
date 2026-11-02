/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"

esp_err_t app_settings_take_save_error(void);
void app_settings_wallpaper_media_ready(void);
void app_settings_trace_storage(const char *stage);
#ifndef UI_MSG
#define UI_MSG(s) (s)
#endif

esp_err_t app_settings_load_status(void);
void app_settings_trace_storage_result(const char *stage,esp_err_t error);
