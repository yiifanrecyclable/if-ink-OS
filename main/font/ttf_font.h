/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 可变 TTF 字形缓存：卡上字体或内置子集，按字重光栅化后画到 framebuffer。
 *
 * Variable TTF glyph cache: SD fonts or the built-in subset, rasterized
 * at the current weight onto the framebuffer.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "epdiy.h"
#include "esp_err.h"

#define TTF_SIZE_SMALL 0
#define TTF_SIZE_LARGE 1

#define TTF_FONT_NAME_MAX 64
#define TTF_FONT_PATH_MAX 160

typedef struct {
    char name[TTF_FONT_NAME_MAX];
    char path[TTF_FONT_PATH_MAX];
} ttf_font_item_t;

typedef struct {
    uint32_t glyphs;
    uint32_t hits;
    uint32_t misses;
    int64_t read_us;
    int64_t raster_us;
    int64_t total_us;
} ttf_bench_stats_t;

#define TTF_FONT_BUILTIN "builtin"
#define TTF_FONT_DEFAULT_FILE "PicoSans-Medium.ttf"

/// 灰阶字覆盖率伽马。小于 1 抬中间覆盖率，抗锯齿边缘更深；满墨仍是 0。
/// Coverage gamma for gray glyphs. Below 1 lifts mid coverage so AA edges are darker; full ink stays 0.
#ifndef TTF_COVER_GAMMA
#define TTF_COVER_GAMMA 0.5f
#endif

esp_err_t ttf_font_init(void);
esp_err_t ttf_font_open(const char* path);
esp_err_t ttf_font_open_builtin(void);
bool ttf_font_is_builtin(void);
bool ttf_font_path_is_builtin(const char* path);
/// 选择系统或阅读字体缓存；调用方须持有绘制锁，失败不丢失另一字体。/ Select a cached face under the draw lock, retaining the other face on failure.
esp_err_t ttf_font_select_context(bool system,const char *path);
/// 启用校准字号并返回之前的状态；与字体上下文共用绘制锁约束。
/// Toggle optical sizes and return the previous mode, under the same draw lock as face selection.
bool ttf_font_set_normalized_sizes(bool enabled);
/// 借用另一缓存槽预载，回调绘制期间临时切换活动字形。/ Preload in the other slot and temporarily select the face for a draw callback.
bool ttf_font_prepare_preview(const char *path);
bool ttf_font_with_preview(const char *path, void (*draw)(void *), void *ctx);
/// 字体列表只保留名称位图；空闲时提交，绘制只查缓存。/ Font lists submit on idle and paint from cached name masks only.
bool ttf_name_preview_request(const ttf_font_item_t *item, const char *label, int px, int width);
bool ttf_name_preview_draw(uint8_t *fb, const ttf_font_item_t *item, const char *label,
                           int px, EpdRect area, uint8_t background, bool replace);
void ttf_name_preview_revision(unsigned revision);
bool ttf_name_preview_idle(int64_t now_ms, bool input);
void ttf_name_preview_pause(void);
/// 停止读取 TF 卡，供退页和休眠使用。/ Stop SD reads before leaving a picker or sleeping.
void ttf_name_preview_stop(void);
/// 文件操作前释放匹配文件或目录的缓存句柄。/ Close cached handles for a file or directory before editing it.
bool ttf_font_release_path(const char *root);
/// 释放两套字体的来源及缓存；拔卡和USB存储切换前调用。/ Release both faces and caches before card loss or USB storage access.
void ttf_font_unload(void);
int ttf_font_scan(void);
int ttf_font_count(void);
const ttf_font_item_t* ttf_font_item(int index);
const char* ttf_font_path(void);
const char* ttf_font_display_name(void);
/// 已知随附字体的中文名称；未知名称原样返回。/ Chinese names for bundled fonts; unknown names pass through.
const char* ttf_font_localized_name(const char* stem);
bool ttf_font_ready(void);
// 查询当前实际字库的覆盖范围，不生成字形位图。
// Query actual glyph coverage without allocating raster bitmaps.
bool ttf_font_has_text(const char *text);
int ttf_ascender(int size);
int ttf_ascender_px(int pixel_height);

void ttf_draw_text(
    uint8_t* framebuffer, int x, int y, int size, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_draw_text_px(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);
/// 从左向右绘制，并在相邻字符之间加入固定像素间距。/ Draw left-aligned text with extra pixels between adjacent characters.
void ttf_draw_text_px_spaced(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, uint8_t fg, uint8_t bg
);
/// 在汉字间均摊行尾余量；标点溢出时可小幅压缩字距。/ Distribute spare width across CJK gaps or slightly compress for hanging punctuation.
void ttf_draw_text_px_fitted(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, int target_width, uint8_t fg, uint8_t bg
);
/// 覆盖率过半才落墨，像素只有 fg/bg。/ Ink only when coverage is over half; pixels are fg/bg only.
void ttf_draw_text_px_bw(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_measure_line(int size, const char* text, int* above, int* below);
void ttf_measure_line_px(int pixel_height, const char* text, int* above, int* below);
int ttf_text_width_px(int pixel_height, const char* text);
/// 首字符左侧留白，不生成位图；用于段首开标点的视觉对齐。
/// First-glyph left bearing without rasterizing; used for paragraph-opening punctuation alignment.
int ttf_text_left_bearing_px(int pixel_height, const char* text);
void ttf_set_weight(int wght);
int ttf_get_weight(void);

void ttf_font_cache_clear(void);
void ttf_bench_begin(void);
void ttf_bench_end(ttf_bench_stats_t* out);

/// Markdown 合成字形：1 加粗，2 倾斜；宽度含边缘留白，不切换全局字体。/ Synthetic Markdown glyphs: 1 bold, 2 italic; reserve edge space without changing the global face.
int ttf_text_width_styled_px(int px, const char *text, unsigned flags);
void ttf_draw_glyph_styled_px(uint8_t *fb, int x, int baseline, int px, const char *glyph, unsigned flags, uint8_t fg, uint8_t bg);

/// 回收双槽字形位图，保留字体；调用方持绘制锁。/ Reclaim both glyph caches, retaining loaded faces under the draw lock.
size_t ttf_font_cache_clear_all(void);
