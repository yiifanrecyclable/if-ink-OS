/* SPDX-License-Identifier: Apache-2.0
 * 中文：设置页借用只读文件浏览器选择壁纸；保留设置页导航来源。
 * English: Settings embeds the read-only file browser for wallpaper selection, retaining Settings navigation.
 */
#pragma once
#include "app.h"
#include "ui_gesture.h"
void app_files_wallpaper_open(app_ctx_t *ctx,bool rotating);
void app_files_wallpaper_close(void);
void app_files_wallpaper_render(app_ctx_t *ctx,uint8_t *fb);
app_redraw_t app_files_wallpaper_gesture(app_ctx_t *ctx,const ui_gesture_event_t *event);
app_redraw_t app_files_wallpaper_key(app_ctx_t *ctx,int key);
app_redraw_t app_files_wallpaper_tick(app_ctx_t *ctx);
bool app_files_wallpaper_finished(void);

void app_files_wallpaper_media_changed(app_ctx_t *ctx,bool ready);
