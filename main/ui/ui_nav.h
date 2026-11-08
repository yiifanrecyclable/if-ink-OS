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

/// 导航绘制和刷新共用同一实际边界。/ Share exact geometry between navigation paint and refresh.
EpdRect ui_nav_rect(void);

void ui_nav_draw(uint8_t *fb, int active);
void ui_nav_status(uint8_t *fb);
/// 状态栏短暂通知；到期由页面tick清除并仅重绘状态栏，避免整屏闪烁。
/// Transient status notice; page ticks expire it and repaint only the status strip.
void ui_nav_notice_show(const char *text, int64_t now_ms);
void ui_nav_notice_clear(void);
bool ui_nav_notice_tick(int64_t now_ms);
const char *ui_nav_notice_text(void);
/// 内容和文件共用底部分页。/ Shared content/file pagination above the navigation.
EpdRect ui_nav_pager_rect(int direction);
void ui_nav_draw_pager(uint8_t *fb,int count,const char *unit,int page,int pages);
/// 共用导航上沿的操作卡几何和绘制。/ Shared action card anchored above the navigation bar.
typedef struct {
    int top, header_height, path_height;
    EpdRect close;
} ui_action_card_t;
int ui_nav_action_height(void);
ui_action_card_t ui_nav_action_card(int extra_height);
EpdRect ui_nav_action_button(int index, int count);
void ui_nav_draw_action_card(uint8_t *fb, ui_action_card_t card);
void ui_nav_draw_action_row(uint8_t *fb, const char *const *labels, int count);
/// 绘制设置页与状态栏共用的 WiFi 标志。/ Draw the WiFi mark shared by Settings and the status bar.
void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray);
/// 二级页面共用的圆形返回按钮。/ Shared circular back control for secondary pages.
void ui_nav_back(uint8_t *fb, int x, int y);
/// 设置子页共用的无圈退出按钮及紧凑触区；目录上级箭头使用相同3px圆头笔画。
/// Borderless Settings close control with a bounded hit box; parent arrows share its 3px round stroke.
EpdRect ui_nav_close_rect(void);
void ui_nav_close(uint8_t *fb);
void ui_nav_close_in(uint8_t *fb, EpdRect area);
void ui_nav_parent(uint8_t *fb,EpdRect rect,bool enabled);
int ui_nav_hit(uint16_t x, uint16_t y);
void ui_nav_request(app_ctx_t *ctx, int index);
