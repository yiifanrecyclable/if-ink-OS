/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：无封面书籍统一使用深灰底、白色书名与作者的极简版式。
 * English: Use one minimal dark-gray cover with a white title and author for coverless books.
 */
#include "book_cover_auto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "ttf_font.h"
#include "ui_font.h"

#define COVER_LINE_MAX 4
#define COVER_TEXT_CAP 256

typedef struct {
    uint8_t *fb;
    unsigned width, height, stride;
} cover_canvas_t;

static bool physical_pixel(int x, int y, int *physical_x, int *physical_y) {
    int pw = epd_width(), ph = epd_height();
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE: *physical_x = x; *physical_y = y; break;
        case EPD_ROT_PORTRAIT: *physical_x = pw - y - 1; *physical_y = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            *physical_x = pw - x - 1; *physical_y = ph - y - 1; break;
        default: *physical_x = y; *physical_y = ph - x - 1; break;
    }
    return *physical_x >= 0 && *physical_y >= 0 && *physical_x < pw && *physical_y < ph;
}

static int xy(unsigned coordinate, unsigned size, unsigned reference) {
    return (int)((uint64_t)coordinate * size / reference);
}

static int font_px(const cover_canvas_t *c, unsigned base) {
    unsigned by_width = base * c->width / 176u;
    unsigned by_height = base * c->height / 240u;
    unsigned px = by_width < by_height ? by_width : by_height;
    return px < 10 ? 10 : px > 72 ? 72 : (int)px;
}

static void put(cover_canvas_t *c, int x, int y, uint8_t gray4) {
    if (x < 0 || y < 0 || x >= (int)c->width || y >= (int)c->height) return;
    int px, py;
    if (!physical_pixel(x, y, &px, &py)) return;
    uint8_t *at = c->fb + (size_t)py * c->stride + (unsigned)px / 2;
    gray4 &= 15;
    if (px & 1) *at = (uint8_t)((*at & 0x0f) | (gray4 << 4));
    else *at = (uint8_t)((*at & 0xf0) | gray4);
}

static uint8_t get(const cover_canvas_t *c, int x, int y) {
    int px, py;
    if (!physical_pixel(x, y, &px, &py)) return 15;
    uint8_t value = c->fb[(size_t)py * c->stride + (unsigned)px / 2];
    return px & 1 ? value >> 4 : value & 15;
}

static void fill(cover_canvas_t *c, int x0, int y0, int x1, int y1, uint8_t gray4) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)c->width) x1 = (int)c->width;
    if (y1 > (int)c->height) y1 = (int)c->height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) put(c, x, y, gray4);
}

static size_t cp_bytes(const char *p) {
    unsigned char c = (unsigned char)*p;
    if (!c) return 0;
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) return 2;
    if (c >= 0xe0 && c <= 0xef) return 3;
    if (c >= 0xf0 && c <= 0xf4) return 4;
    return 1;
}

static size_t prior_cp(const char *text, size_t length) {
    if (!length) return 0;
    --length;
    while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    return length;
}

static int text_width(int px, const char *text) {
    if (!text || !*text) return 0;
    return ui_font_has_text(text) ? ui_font_text_width_px(px, text) : ttf_text_width_px(px, text);
}

static void draw_text(cover_canvas_t *c, int x, int top, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    if (!text || !*text) return;
    if (ui_font_has_text(text))
        ui_font_draw_text_px(c->fb, x, top + ui_font_ascender_px(px), px,
                             text, align, fg, bg, false);
    else if (ttf_font_ready())
        ttf_draw_text_px(c->fb, x, top + ttf_ascender_px(px), px,
                         text, align, fg, bg);
}

static void trim_right(char *text) {
    size_t n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t')) text[--n] = 0;
}

static int wrap_title(const char *title, int px, int max_width, int max_lines,
                      char lines[COVER_LINE_MAX][COVER_TEXT_CAP]) {
    const char *cursor = title;
    int count = 0;
    while (*cursor && count < max_lines) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
        if (!*cursor) break;
        char *line = lines[count];
        size_t used = 0, break_at = 0;
        const char *break_src = NULL;
        while (*cursor && *cursor != '\n') {
            size_t bytes = cp_bytes(cursor);
            if (used + bytes >= COVER_TEXT_CAP) break;
            if (bytes > 1) for (size_t i = 1; i < bytes; ++i)
                if (!cursor[i] || ((unsigned char)cursor[i] & 0xc0) != 0x80) { bytes = 1; break; }
            bool whitespace = bytes == 1 && (*cursor == ' ' || *cursor == '\t');
            memcpy(line + used, cursor, bytes);
            line[used + bytes] = 0;
            if (text_width(px, line) > max_width && used) {
                line[break_at ? break_at : used] = 0;
                if (break_src) cursor = break_src;
                break;
            }
            used += bytes;
            cursor += bytes;
            if (whitespace) { break_at = used - 1; break_src = cursor; }
        }
        if (*cursor == '\n') ++cursor;
        trim_right(line);
        if (line[0]) ++count;
        else if (*cursor) cursor += cp_bytes(cursor);
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
    if (*cursor && count) {
        char *last = lines[count - 1];
        size_t length = strlen(last);
        while (length && text_width(px, last) + text_width(px, "…") > max_width) {
            length = prior_cp(last, length);
            last[length] = 0;
        }
        if (length + strlen("…") < COVER_TEXT_CAP) strcat(last, "…");
    }
    return count;
}

static int title_lines(const char *title, int max_px, int min_px, int max_width,
                       int max_height, int gap, int max_lines,
                       char lines[COVER_LINE_MAX][COVER_TEXT_CAP]) {
    for (int px = max_px; px >= min_px; --px) {
        memset(lines, 0, sizeof(char) * COVER_LINE_MAX * COVER_TEXT_CAP);
        int count = wrap_title(title, px, max_width, max_lines, lines);
        if (!count) continue;
        const char *last = lines[count - 1];
        size_t len = strlen(last);
        bool shortened = len >= strlen("…") && !strcmp(last + len - strlen("…"), "…");
        if ((!shortened && count * px + (count - 1) * gap <= max_height) || px == min_px) return px;
    }
    return min_px;
}

static void draw_author(cover_canvas_t *c, const char *author, int x, int y,
                        int max_width, int px, enum EpdFontFlags align,
                        uint8_t fg, uint8_t bg) {
    if (!author || !*author) return;
    char shown[COVER_TEXT_CAP];
    snprintf(shown, sizeof(shown), "%s", author);
    while (px > font_px(c, 12) && text_width(px, shown) > max_width) --px;
    size_t len = strlen(shown);
    bool clipped = false;
    while (len && text_width(px, shown) > max_width) {
        len = prior_cp(shown, len);
        shown[len] = 0;
        clipped = true;
    }
    if (clipped) {
        while (len && text_width(px, shown) + text_width(px, "…") > max_width) {
            len = prior_cp(shown, len); shown[len] = 0;
        }
        if (len + strlen("…") < sizeof(shown)) strcat(shown, "…");
    }
    draw_text(c, x, y, px, shown, align, fg, bg);
}

static void minimal_dark_cover(cover_canvas_t *c, const char *title, const char *author) {
    const uint8_t bg=4;
    fill(c,0,0,(int)c->width,(int)c->height,bg);
    char lines[COVER_LINE_MAX][COVER_TEXT_CAP];
    int gap=xy(6,c->height,240);
    int px=title_lines(title,font_px(c,30),font_px(c,17),
                       (int)c->width-xy(32,c->width,176),
                       xy(104,c->height,240),gap,3,lines);
    int count=0;while(count<3&&lines[count][0])++count;
    int block_height=count*px+(count>0?count-1:0)*gap;
    int top=xy(94,c->height,240)-block_height/2;
    for(int i=0;i<count;++i)
        draw_text(c,(int)c->width/2,top+i*(px+gap),px,lines[i],EPD_DRAW_ALIGN_CENTER,15,bg);
    if(author&&*author)
        draw_author(c,author,(int)c->width/2,xy(196,c->height,240),
                    (int)c->width-xy(32,c->width,176),font_px(c,15),EPD_DRAW_ALIGN_CENTER,15,bg);
}

bool book_auto_cover_render(const char *identity, const char *title, const char *author,
                            unsigned width, unsigned height, uint8_t *out) {
    (void)identity;
    if (!out || !title || !*title || width < 96 || height < 128 ||
        width > 320 || height > 440 || width + 104 >= (unsigned)epd_rotated_display_width() ||
        height > (unsigned)epd_rotated_display_height()) return false;
    unsigned stride = (unsigned)epd_width() / 2;
    uint8_t *scratch = heap_caps_malloc((size_t)stride * epd_height(),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch) return false;
    memset(scratch, 0xff, (size_t)stride * epd_height());
    cover_canvas_t canvas = {.fb = scratch, .width = width, .height = height, .stride = stride};
    minimal_dark_cover(&canvas,title,author);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            out[(size_t)y * width + x] = (uint8_t)(get(&canvas, (int)x, (int)y) * 17u);
    free(scratch);
    return true;
}
