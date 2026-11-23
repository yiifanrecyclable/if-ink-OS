/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的字体契约。/ English: Font contract for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include "epdiy.h"
int ttf_text_width_px(int px, const char* text);
bool ttf_text_range_px(int px, const char *text, int tracking, int target, bool word_spaces,
                       size_t start, size_t end, int *first, int *last, int *ink_bottom);
int ttf_text_left_bearing_px(int px, const char* text);
int ttf_ascender_px(int px);
static inline bool ttf_font_has_text(const char *text) { return text != NULL; }
static inline void ttf_measure_line_px(int px, const char *text, int *above, int *below) {
    (void)text;
    if (above) *above = px;
    if (below) *below = 0;
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text, enum EpdFontFlags align, uint8_t fg, uint8_t bg);
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg);
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, bool word_spaces, uint8_t fg, uint8_t bg);

int ttf_text_width_styled_px(int px,const char *text,unsigned flags);
int ttf_vertical_advance_px(int px, uint32_t cp);
void ttf_draw_vertical_px(uint8_t *fb, int center_x, int top, int height, int px, const char *text, int tracking);
void ttf_draw_glyph_styled_px(uint8_t *fb,int x,int y,int px,const char *text,unsigned flags,uint8_t fg,uint8_t bg);
