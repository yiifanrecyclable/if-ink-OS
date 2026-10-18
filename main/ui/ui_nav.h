/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：四个产品主页面共用的底部导航；阅读页自行隐藏。
 * English: Shared four-tab product navigation; the reader hides it.
 */
#pragma once

#include "app.h"

#define UI_NAV_HEIGHT 80
#define UI_NAV_TOP (UI_LOCK_HEIGHT - UI_NAV_HEIGHT)
// 分页触区与导航栏间留出安全间隔。/ Leave a safe gap between pagination targets and navigation.
#define UI_NAV_PAGER_TOP (UI_NAV_TOP - 88)
#define UI_NAV_SHADOW_DEPTH 7
#define UI_NAV_ACTIVE_GRAY 0x44
#define UI_NAV_INACTIVE_GRAY 0x88
#define UI_NAV_BACK_BORDER_GRAY 0x58

void ui_nav_draw(uint8_t *fb, int active);
void ui_nav_status(uint8_t *fb);
/// 内容和文件共用底部分页。/ Shared content/file pagination above the navigation.
EpdRect ui_nav_pager_rect(int direction);
void ui_nav_draw_pager(uint8_t *fb,int count,const char *unit,int page,int pages);
/// 绘制设置页与状态栏共用的 WiFi 标志。/ Draw the WiFi mark shared by Settings and the status bar.
void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray);
/// 二级页面共用的圆形返回按钮。/ Shared circular back control for secondary pages.
void ui_nav_back(uint8_t *fb, int x, int y);
int ui_nav_hit(uint16_t x, uint16_t y);
void ui_nav_request(app_ctx_t *ctx, int index);
