/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 自定义固件修改：Markdown 大纲使用可折叠父子标题，图书目录保留原行为。
 * Modified derivative: collapsible Markdown headings; retain book TOC behavior.
 * 中文：目录只呈现章节导航，和书架列表、旧示例菜单完全分离。
 * English: The directory only navigates chapters and shares no shelf or demo-menu rendering.
 */
#include "book_toc.h"

#include <stdio.h>
#include <string.h>
#include "book_source.h"
#include "book_markdown.h"
#include "ui_kit.h"
#include "ui_nav.h"

enum { TOC_LEFT = 36, TOC_RIGHT = 648, TOC_ROW_TOP = UI_PAGE_Y(252),
       TOC_ROW_STEP = 108, TOC_ROW_HEIGHT = 94 };

static int toc_step(void){int height=ui_list_row_height(true);return height<108?108:height;}
int book_toc_visible_rows(void){int rows=(1000-TOC_ROW_TOP)/toc_step();return rows>0?rows:1;}

int book_toc_pages(size_t chapters) {
    return chapters ? (int)(1 + (chapters - 1) / BOOK_TOC_ROWS) : 1;
}

EpdRect book_toc_row_rect(int row) {
    return (EpdRect){TOC_LEFT, TOC_ROW_TOP + row * toc_step(),
                     TOC_RIGHT - TOC_LEFT, toc_step()-1};
}

// 把 EPUB/TXT 中的换行、制表符和控制符收敛为一个空格，防止绘制越过本行。
// Collapse line breaks, tabs and controls from EPUB/TXT to one space so glyphs stay in one row.
static void one_line(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t out = 0;
    bool gap = false;
    for (const unsigned char *p = (const unsigned char *)src; *p && out + 1 < cap;) {
        unsigned char c = *p;
        if (c <= 0x20 || c == 0x7f) { gap = out != 0; ++p; continue; }
        if (gap && out + 2 < cap) dst[out++] = ' ';
        gap = false;
        int bytes = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (out + (size_t)bytes >= cap) break;
        for (int i = 0; i < bytes && *p; ++i) dst[out++] = (char)*p++;
    }
    while (out && dst[out - 1] == ' ') --out;
    dst[out] = 0;
}

static void fit_line(char *text, int px, int width) {
    size_t len = strlen(text);
    bool trimmed = false;
    while (len && (ui_text_fixed_width_px(px, text) > width || len > 96)) {
        do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
        text[len] = 0;
        trimmed = true;
    }
    if (trimmed && len > 3) {
        // Cut a little more before adding the ellipsis so its glyph cannot cross the status area.
        // 添加省略号前预留字形宽度，避免跨过右侧当前章节标记。
        while (len && ui_text_fixed_width_px(px, text) + ui_text_fixed_width_px(px, "…") > width) {
            do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
            text[len] = 0;
        }
        if (len + sizeof("…") <= 160) strcat(text, "…");
    }
}

int book_toc_hit(uint16_t x, uint16_t y, int page, size_t chapters) {
    if (x < 100 && y >= 76 && y < 151) return BOOK_TOC_BACK;
    if (y >= 1018 && y < 1086) {
        if (x >= 36 && x < 136) return BOOK_TOC_SKIP_PREV;
        if (x >= 146 && x < 258) return BOOK_TOC_PREV;
        if (x >= 426 && x < 538) return BOOK_TOC_NEXT;
        if (x >= 548 && x < 648) return BOOK_TOC_SKIP_NEXT;
    }
    if (x >= 268 && x < 416 && y >= 1018 && y < 1086) return BOOK_TOC_JUMP;
    for (int row = 0; row < BOOK_TOC_ROWS; ++row) {
        size_t index = (size_t)page * BOOK_TOC_ROWS + row;
        if (index >= chapters) break;
        if (ui_rect_hit(book_toc_row_rect(row), x, y)) return (int)index;
    }
    return -1;
}

int book_toc_jump_hit(uint16_t x, uint16_t y) {
    if (x >= 75 && x < 330 && y >= 850 && y < 907) return BOOK_TOC_JUMP_CANCEL;
    if (x >= 354 && x < 609 && y >= 850 && y < 907) return BOOK_TOC_JUMP_CONFIRM;
    if (x >= 75 && x < 609 && y >= 600 && y < 711) return BOOK_TOC_JUMP_TRACK;
    return -1;
}

int book_toc_jump_percent(uint16_t x) {
    if (x <= 92) return 0;
    if (x >= 592) return 100;
    return ((int)x - 92) * 100 / 500;
}

int book_toc_page_from_percent(int percent, int pages) {
    if (pages <= 1 || percent <= 0) return 0;
    if (percent >= 100) return pages - 1;
    int page = (percent * pages + 99) / 100 - 1;
    return page < pages ? page : pages - 1;
}

// 双像素深色轮廓让白色卡片在墨水屏上仍有清晰边界。/ Dark two-pixel outlines keep white cards legible on e-paper.
static void toc_border(uint8_t *fb,EpdRect rect,int radius,bool selected){
    (void)radius;(void)selected;epd_fill_rect((EpdRect){rect.x,rect.y+rect.height-1,rect.width,1},UI_GRAY_ITEM_LINE,fb);
}

static void toc_pill(uint8_t *fb, EpdRect rect, const char *text, int px, bool selected) {
    epd_fill_rect(rect,selected?0xee:UI_GRAY_WHITE,fb);
    toc_border(fb, rect, 20, selected);
    char label[160];
    one_line(label, sizeof(label), text);
    px=ui_text_effective_px(UI_TEXT_CAPTION);
    fit_line(label, px, rect.width - 16);
    ui_text_fixed(fb, rect.x + rect.width / 2,
                  rect.y + (rect.height - px) / 2, px, label,
                  EPD_DRAW_ALIGN_CENTER, false);
}

void book_toc_render(uint8_t *fb, const char *book_title, size_t chapters,
                     size_t current, int page, const char *message) {
    ui_clear_page(fb);
    epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    ui_nav_back(fb, TOC_LEFT, UI_PAGE_Y(79));
    ui_text_fixed(fb, 342, UI_PAGE_Y(93), ui_text_effective_px(UI_TEXT_TITLE), UI_MSG("目录"), EPD_DRAW_ALIGN_CENTER, false);

    char title[160];
    one_line(title, sizeof(title), book_title ? book_title : "");
    if (!title[0]) strcpy(title, UI_MSG("当前书籍"));
    ui_text_title_line(fb,(EpdRect){TOC_LEFT,UI_PAGE_Y(160),445,48},ui_text_effective_px(UI_TEXT_CAPTION),title,UI_GRAY_WHITE);
    char total[48];
    ui_snprintf(total, sizeof(total), UI_MSG("%u 节"), (unsigned)chapters);
    ui_text_fixed(fb, TOC_RIGHT, UI_PAGE_Y(169), ui_text_effective_px(UI_TEXT_CAPTION), total, EPD_DRAW_ALIGN_RIGHT, false);
    epd_fill_rect((EpdRect){TOC_LEFT, UI_PAGE_Y(213), TOC_RIGHT - TOC_LEFT, 2}, 0x68, fb);

    if (message && *message) {
        char info[160]; one_line(info, sizeof(info), message); fit_line(info, 23, 540);
        ui_text_fixed(fb, 342, 408, 23, info, EPD_DRAW_ALIGN_CENTER, false);
    } else if (!chapters) {
        ui_text_fixed(fb, 342, 408, 25, UI_MSG("这本书没有可用目录"), EPD_DRAW_ALIGN_CENTER, false);
    } else {
        for (int row = 0; row < BOOK_TOC_ROWS; ++row) {
            size_t index = (size_t)page * BOOK_TOC_ROWS + row;
            if (index >= chapters) break;
            size_t source_index = book_navigation_chapter(index);
            if (source_index == SIZE_MAX) break;
            EpdRect r = book_toc_row_rect(row);
            bool selected = index == current;
            epd_fill_rect(r,selected?0xee:UI_GRAY_WHITE,fb);
            toc_border(fb, r, 20, selected);
            char number[16]; ui_snprintf(number, sizeof(number), "%02u", (unsigned)index + 1);
            int indent=book_kind()==BOOK_KIND_MARKDOWN?(int)book_markdown_navigation_depth(index)*24:0;
            if(book_kind()==BOOK_KIND_MARKDOWN){
                if(book_markdown_navigation_children(index))ui_text_fixed(fb,r.x+22+indent,r.y+(r.height-ui_text_effective_px(UI_TEXT_BODY))/2,ui_text_effective_px(UI_TEXT_BODY),book_markdown_navigation_expanded(index)?"−":"+",EPD_DRAW_ALIGN_LEFT,false);
            }else ui_text_fixed(fb, r.x + 22, r.y+(r.height-ui_text_effective_px(UI_TEXT_CAPTION))/2, ui_text_effective_px(UI_TEXT_CAPTION), number, EPD_DRAW_ALIGN_LEFT, false);
            char raw[160] = "", label[160];
            if (book_navigation_title(index, raw, sizeof(raw)) != ESP_OK || !raw[0])
                ui_snprintf(raw, sizeof(raw), UI_MSG("第 %u 节"), (unsigned)index + 1);
            char spine_default[32];
            ui_snprintf(spine_default, sizeof(spine_default), UI_MSG("第 %u 节"), (unsigned)source_index + 1);
            if (strcmp(raw, spine_default) == 0)
                ui_snprintf(raw, sizeof(raw), UI_MSG("第 %u 节"), (unsigned)index + 1);
            one_line(label, sizeof(label), raw);
            if (!label[0]) ui_snprintf(label, sizeof(label), UI_MSG("第 %u 节"), (unsigned)index + 1);
            ui_text_title_line(fb,(EpdRect){r.x+79+indent,r.y,(selected?410:495)-indent,r.height},ui_text_effective_px(UI_TEXT_BODY),label,selected?0xee:UI_GRAY_WHITE);
            if (selected)
                ui_text_fixed(fb, r.x + r.width - 22, r.y+(r.height-ui_text_effective_px(UI_TEXT_CAPTION))/2, ui_text_effective_px(UI_TEXT_CAPTION), UI_MSG("当前"),
                              EPD_DRAW_ALIGN_RIGHT, false);
        }
    }
    const int pages = book_toc_pages(chapters);
    toc_pill(fb, (EpdRect){36, 1018, 100, 68}, "«10", 24, false);
    toc_pill(fb, (EpdRect){146, 1018, 112, 68}, "‹", 26, false);
    char position[32]; ui_snprintf(position, sizeof(position), "%02d / %02d", page + 1, pages);
    toc_pill(fb, (EpdRect){268, 1018, 148, 68}, position, 25, true);
    toc_pill(fb, (EpdRect){426, 1018, 112, 68}, "›", 26, false);
    toc_pill(fb, (EpdRect){548, 1018, 100, 68}, "10»", 24, false);

}

EpdRect book_toc_jump_preview_area(void){return (EpdRect){66,495,552,329};}
void book_toc_render_jump_preview(uint8_t *fb,size_t chapters,int percent){
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    epd_fill_rect((EpdRect){66,495,552,92},UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){66,600,552,112},UI_GRAY_WHITE,fb);
    char value[24]; ui_snprintf(value, sizeof(value), "%d%%", percent);
    ui_text_fixed(fb, 342, 515, 55, value, EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_round_rect(fb, (EpdRect){92, 637, 500, 8}, 4, 0x80);
    int width = 500 * percent / 100;
    if (width) ui_fill_round_rect(fb, (EpdRect){92, 637, width, 8}, 4, 0x40);
    int knob = 92 + width;
    epd_fill_circle(knob, 641, 25, UI_GRAY_WHITE, fb);
    epd_draw_circle(knob, 641, 25, 0x40, fb);
    static const char *ticks[] = {"0", "25", "50", "75", "100%"};
    for (int i = 0; i < 5; ++i)
        ui_text_fixed(fb, 92 + i * 125, 678, 24, ticks[i], EPD_DRAW_ALIGN_CENTER, false);
    EpdRect target_card = {75, 740, 534, 84};
    epd_fill_rect(target_card,0xee,fb);
    toc_border(fb, target_card, 19, false);
    ui_text_fixed(fb, 99, 751, 23, UI_MSG("目标位置"), EPD_DRAW_ALIGN_LEFT, false);
    int pages = book_toc_pages(chapters);
    int target = book_toc_page_from_percent(percent, pages);
    size_t first = (size_t)target * BOOK_TOC_ROWS + 1;
    size_t last = first + BOOK_TOC_ROWS - 1;
    if (last > chapters) last = chapters;
    char summary[160];
    if (chapters)
        ui_snprintf(summary, sizeof(summary), UI_MSG("约第 %d / %d 页 · 第 %u～%u 节"),
                 target + 1, pages, (unsigned)first, (unsigned)last);
    else ui_snprintf(summary, sizeof(summary), UI_MSG("这本书没有可用目录"));
    fit_line(summary, 25, 486);
    ui_text_fixed(fb, 99, 786, 25, summary, EPD_DRAW_ALIGN_LEFT, false);
}

void book_toc_render_jump(uint8_t *fb, size_t chapters, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    EpdRect card = {44, 356, 596, 572};
    epd_fill_rect(card,UI_GRAY_WHITE,fb);
    toc_border(fb, card, 30, true);
    ui_text_fixed(fb, 342, 393, 32, UI_MSG("跳到目录位置"), EPD_DRAW_ALIGN_CENTER, false);
    ui_text_fixed(fb, 342, 450, 24, UI_MSG("选择百分比，先预览目标位置"),
                  EPD_DRAW_ALIGN_CENTER, false);
    book_toc_render_jump_preview(fb,chapters,percent);
    toc_pill(fb, (EpdRect){75, 850, 255, 57}, UI_MSG("取消"), 26, false);
    epd_fill_rect((EpdRect){354,850,255,57},0x88,fb);
    ui_text_fixed(fb, 481, 865, 26, UI_MSG("跳转"), EPD_DRAW_ALIGN_CENTER, true);
}
