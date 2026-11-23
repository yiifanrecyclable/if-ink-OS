/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借鉴阅读演示的逐码点折行，建立 PSRAM 页表并绘制章节。
 * English: Adapt the reading demo's codepoint wrapping into PSRAM chapter pagination.
 *
 * 冻结：原文由调用方持有；字体测量和绘制必须由调用方串行化。
 * Frozen: Caller owns source text and serializes all font measurement and drawing.
 */
#include "book_layout.h"
#include "book_hyphen.h"
#include "markdown_table.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "ttf_font.h"
#include "../font/ttf_justify.h"

#define PAGE_MAX 4096u
#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static const char* s_text;
static size_t s_len;
static size_t* s_pages;
static size_t s_count;
static size_t s_capacity;
static char* s_line;
static size_t s_line_capacity;
static EpdRect s_rect;
static int s_px;
static bool s_markdown;
static bool s_english_justification;

void book_layout_set_english_justification(bool enabled) { s_english_justification = enabled; }

static uint8_t s_heading_px[3] = {72, 64, 56};
static const text_style_t *s_styles;
static size_t s_style_count, s_line_source, s_line_source_length;
static const html_link_t *s_links;
static size_t s_link_count;
static bool s_has_image_notes;
static const markdown_cell_t *s_table_cells;
static size_t s_table_cell_count;
void book_layout_set_markdown_tables(const markdown_cell_t *cells, size_t count) {
    s_table_cells = cells; s_table_cell_count = count;
}
void book_layout_set_markdown(bool enabled, const uint8_t headings[3], const text_style_t *styles, size_t count) {
    s_markdown = enabled;
    if (headings) memcpy(s_heading_px, headings, sizeof(s_heading_px));
    s_styles = enabled ? styles : NULL; s_style_count = enabled ? count : 0;
    if (!enabled) book_layout_set_markdown_tables(NULL, 0);
}
static unsigned style_at(size_t offset) {
    size_t lo = 0, hi = s_style_count;
    while (lo < hi) { size_t mid = lo + (hi-lo)/2;
        if (s_styles[mid].offset <= offset) lo = mid+1; else hi = mid; }
    if (!lo) return 0;
    const text_style_t *run = &s_styles[lo-1];
    return offset - run->offset < run->len ? run->flags : 0;
}
static unsigned glyph_style(size_t offset, bool heading) {
    return s_markdown ? style_at(offset) | (heading ? TEXT_STYLE_BOLD : 0) : 0;
}
static unsigned s_line_percent = 150, s_paragraph_percent = 50;
static int s_tracking_px;
static unsigned s_first_line_indent_em = 2;
static unsigned s_reading_line;
static int s_reading_line_offset;
static bool s_images_visible = true;
static bool s_vertical;
static bool s_two_columns;
#define BOOK_COLUMN_GAP 36

void book_layout_set_two_columns(bool enabled) { s_two_columns = enabled && !s_markdown; }

EpdRect book_layout_column_rect(EpdRect body, unsigned column) {
    if (!s_two_columns) return body;
    body.width = (body.width - BOOK_COLUMN_GAP) / 2;
    if ((column != 0) != s_vertical) body.x += body.width + BOOK_COLUMN_GAP;
    return body;
}

void book_layout_set_vertical(bool enabled) { s_vertical = enabled && !s_markdown; }

static unsigned s_lead_height;

static int page_lead(size_t page, EpdRect rect) {
    if (page || !s_lead_height) return 0;
    if (!s_vertical) return (int)s_lead_height;
    return rect.width >= 300 ? BOOK_VERTICAL_HEADING_WIDTH : 0;
}
static size_t s_lead_skip;

static const blk_t* s_blocks;
static size_t s_block_count;
// 布局代次：重排或释放后自增，页内位图缓存靠它失效。/ Bumped on rebuild or free so page bitmap caches expire.
static uint32_t s_generation;

// 页内插图记录序号、栏号、页内 y 和显示宽高。平坦池按页顺序存放，每页用起点表切片，
// 这样大多数没有插图的页只花两个下标。
// Images placed on a page: index, column, y within the page, and display width/height. The pool is flat
// and ordered by page, sliced by a per-page start table, so a page without images costs two
// indices and nothing else.
typedef struct {
    int16_t image;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t column;
} layout_image_t;

static layout_image_t* s_images;
static size_t s_image_count;
static size_t s_image_capacity;
static uint32_t* s_page_img_start;

static book_layout_image_dims_fn s_dims_fn;
static void* s_dims_ctx;

// 块表是有序字节区间，二分查找当前行样式。/ Blocks are ordered byte ranges; binary-search the line style.
static const blk_t* block_at(size_t off) {
    if (!s_block_count) return NULL;
    size_t lo = 0, hi = s_block_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_blocks[mid].offset <= off) lo = mid;
        else hi = mid;
    }
    return &s_blocks[lo];
}
// 拒绝截断、过长编码、代理项和嵌入零字节。/ Reject truncation, overlong encodings, surrogates and embedded NUL.
static size_t codepoint_size(const char* text, size_t remaining) {
    if (!remaining) return 0;
    const unsigned char* p = (const unsigned char*)text;
    if (p[0] > 0 && p[0] < 0x80) return 1;
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
               p[0] >= 0xe0 && p[0] <= 0xef ? 3 :
               p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; i++) if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}

static uint32_t codepoint_value(const char* text, size_t n) {
    const unsigned char* p = (const unsigned char*)text;
    if (n == 1) return p[0];
    uint32_t cp = p[0] & (0x7f >> n);
    for (size_t i = 1; i < n; ++i) cp = (cp << 6) | (p[i] & 63);
    return cp;
}

static bool paragraph_padding(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0xa0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200b) || cp == 0x202f || cp == 0x205f ||
           cp == 0x2060 || cp == 0x3000 || cp == 0xfeff;
}

// 空白行只标记段落边界，仍沿用原文偏移。/ Blank lines only mark paragraph boundaries; source offsets are preserved.
static size_t skip_book_paragraph_padding(size_t off) {
    size_t next_line = off;
    while (off < s_len) {
        if (s_text[off] == '\n' || s_text[off] == '\r') {
            if (s_text[off++] == '\r' && off < s_len && s_text[off] == '\n') ++off;
            next_line = off;
            continue;
        }
        size_t bytes = codepoint_size(s_text + off, s_len - off);
        if (!bytes || !paragraph_padding(codepoint_value(s_text + off, bytes))) return next_line;
        off += bytes;
    }
    return off;
}

// 插图容器常带全角空白、零宽字符和换行实体；它们不是可阅读的独立段落。
// Image wrappers often contain fullwidth spaces, zero-width characters and line-break entities, not readable paragraphs.
static bool image_spacing_block(const blk_t* block) {
    if (!block || block->image >= 0 || block->table_columns) return false;
    size_t end = block->offset + block->len;
    for (size_t at = block->offset; at < end;) {
        size_t n = codepoint_size(s_text + at, end - at);
        if (!n) return false;
        uint32_t cp = codepoint_value(s_text + at, n);
        bool space = cp == ' ' || (cp >= '\t' && cp <= '\r') || cp == 0x85 || cp == 0xa0 ||
                     cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200b) || cp == 0x2028 ||
                     cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x2060 ||
                     cp == 0x3000 || cp == 0xfeff;
        if (!space) return false;
        at += n;
    }
    return true;
}

// 单独处理与插图相邻的空白块。/ Handle image-adjacent spacer blocks separately.
static size_t skip_image_spacing(size_t off) {
    while (off < s_len && s_block_count) {
        const blk_t* block = block_at(off);
        if (block->offset != off) break;
        size_t end = block->offset + block->len;
        if (block->image >= 0) {
            if (s_images_visible) break;
        } else {
            if (!image_spacing_block(block)) break;
            size_t first = (size_t)(block - s_blocks), last = first + 1;
            while (last < s_block_count && image_spacing_block(&s_blocks[last])) ++last;
            bool adjacent = (first > 0 && s_blocks[first - 1].image >= 0) ||
                            (last < s_block_count && s_blocks[last].image >= 0);
            if (!adjacent) break;
            end = s_blocks[last - 1].offset + s_blocks[last - 1].len;
        }
        off = end;
        if (off < s_len && s_text[off] == '\n') ++off;
    }
    return off;
}

// 中文排版禁则：右标点不得出现在行首，左标点不得停在行尾。
// CJK kinsoku: closing punctuation may not start a line; opening punctuation may not end one.
static bool prohibited_line_start(uint32_t cp) {
    switch (cp) {
        case 0x0021: case 0x0025: case 0x0029: case 0x002c: case 0x002e: case 0x003a:
        case 0x003b: case 0x003f: case 0x005d: case 0x007d:
        case 0x2019: case 0x201d: case 0x2026: case 0x3001: case 0x3002: case 0x3009:
        case 0x300b: case 0x300d: case 0x300f: case 0x3011: case 0x3015: case 0x3017:
        case 0x3019: case 0x301b: case 0xff01: case 0xff05: case 0xff09: case 0xff0c:
        case 0xff0e: case 0xff1a: case 0xff1b: case 0xff1f: case 0xff3d: case 0xff5d:
            return true;
        default: return false;
    }
}

static bool prohibited_line_end(uint32_t cp) {
    switch (cp) {
        case 0x0028: case 0x005b: case 0x007b: case 0x2018: case 0x201c: case 0x3008:
        case 0x300a: case 0x300c: case 0x300e: case 0x3010: case 0x3014: case 0x3016:
        case 0x3018: case 0x301a: case 0xff08: case 0xff3b: case 0xff5b:
            return true;
        default: return false;
    }
}

uint32_t book_layout_generation(void) { return s_generation; }

void book_layout_free(void) {
    ++s_generation;
    markdown_table_free();
    free(s_pages);
    free(s_line);
    free(s_images);
    free(s_page_img_start);
    s_pages = NULL;
    s_line = NULL; s_line_capacity=0;
    s_images = NULL;
    s_page_img_start = NULL;
    s_image_count = s_image_capacity = 0;
    s_text = NULL;
    s_count = s_capacity = s_len = 0;
    s_px = 0;
    s_blocks = NULL;
    s_block_count = 0;
}

void book_layout_set_spacing(unsigned line_percent, unsigned paragraph_percent) {
    s_line_percent = line_percent >= 110 && line_percent <= 200 ? line_percent : 150;
    s_paragraph_percent = paragraph_percent <= 100 ? paragraph_percent : 50;
}
static bool s_hyphenate = true;
void book_layout_set_hyphenation(bool enabled) { s_hyphenate = enabled; }

void book_layout_set_typography(int tracking_px) {
    s_tracking_px = tracking_px >= -4 && tracking_px <= 4 && tracking_px % 2 == 0 ? tracking_px : 0;
}
void book_layout_set_first_line_indent(unsigned em) {
    s_first_line_indent_em = em <= 3 ? em : 2;
}
EpdRect book_layout_balanced_rect(EpdRect outer, int px, int tracking_px) {
    if (px <= 0 || outer.width < px || tracking_px < -4 || tracking_px > 4) return outer;
    int step = px + tracking_px;
    if (step <= 0) return outer;
    int columns = 1 + (outer.width - px) / step;
    int used = px + (columns - 1) * step;
    outer.x += (outer.width - used) / 2;
    outer.width = used;
    return outer;
}
void book_layout_set_reading_line(unsigned style) {
    s_reading_line = style <= 2 ? style : 0;
}
void book_layout_set_reading_line_offset(int offset_px) {
    s_reading_line_offset = offset_px < -8 ? -8 : offset_px > 8 ? 8 : offset_px;
}
void book_layout_set_images_visible(bool visible) { s_images_visible = visible; }
void book_layout_set_chapter_lead(size_t skip_bytes, unsigned height_px) {
    s_lead_skip = skip_bytes;
    s_lead_height = height_px;
}

void book_layout_set_image_dims(book_layout_image_dims_fn fn, void* ctx) {
    s_dims_fn = fn;
    s_dims_ctx = ctx;
}

// 按栏宽等比缩放、不超过整页高、不放大——与参考实现同一套规则。
// Aspect-fit to the column width, never taller than the page, never upscaled, matching the
// reference implementations.
static bool image_display_size(int image, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!s_dims_fn) return false;
    int w = 0, h = 0;
    if (!s_dims_fn(s_dims_ctx, image, &w, &h) || w <= 0 || h <= 0) return false;
    int64_t cw = s_rect.width, ch = s_rect.height;
    if (cw <= 0 || ch <= 0) return false;
    if (w > cw) { h = (int)((int64_t)h * cw / w); w = (int)cw; }
    if (h > ch) { w = (int)((int64_t)w * ch / h); h = (int)ch; }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}

static bool record_page_image(int image, unsigned column, int y, int w, int h) {
    if (s_image_count == s_image_capacity) {
        size_t cap = s_image_capacity ? s_image_capacity * 2 : 16;
        if (cap > PAGE_MAX * 4) return false;
        layout_image_t* grown = heap_caps_realloc(s_images, cap * sizeof(*grown), PSRAM_CAPS);
        if (!grown) return false;
        s_images = grown;
        s_image_capacity = cap;
    }
    layout_image_t* rec = &s_images[s_image_count++];
    rec->image = (int16_t)image;
    rec->y = (int16_t)y;
    rec->width = (int16_t)w;
    rec->height = (int16_t)h;
    rec->column = (uint8_t)column;
    if (s_page_img_start && s_count) s_page_img_start[s_count] = (uint32_t)s_image_count;
    return true;
}

int book_layout_page_image_count(size_t page) {
    if (page >= s_count || !s_page_img_start) return 0;
    return (int)(s_page_img_start[page + 1] - s_page_img_start[page]);
}

bool book_layout_page_image_at(size_t page, int i, int* image, int* y, int* width, int* height,
                               unsigned* column) {
    if (page >= s_count || !s_page_img_start || i < 0) return false;
    uint32_t a = s_page_img_start[page];
    if ((uint32_t)i >= s_page_img_start[page + 1] - a) return false;
    const layout_image_t* rec = &s_images[a + (uint32_t)i];
    if (image) *image = rec->image;
    if (y) *y = rec->y;
    if (width) *width = rec->width;
    if (height) *height = rec->height;
    if (column) *column = rec->column;
    return true;
}

int book_layout_page_image(size_t page) {
    if (page >= s_count || !s_block_count) return -1;
    const blk_t* block = block_at(s_pages[page]);
    if (!block || block->offset != s_pages[page] || block->image < 0) return -1;
    // 图片流进正文之后，插图常常正好落在页首，只判断"页首是图片块"会把普通文字页也算成整页插图。
    // 这里要求这一页的范围【只覆盖这一个图片块】，才是真正需要强刷的整页灰阶图。
    // Once images flow into the page an inline image often sits at the top, so testing only the
    // page start also matched ordinary text pages. Require the page to span exactly this image
    // block: that is a whole-page gray bitmap, the case the forced refresh exists for.
    size_t after = block->offset + block->len;
    if (after < s_len && s_text[after] == '\n') ++after;
    size_t page_end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    // 忽略插图包装中的空白块，MD长图及未知尺寸图仍识别为整页。
    // Ignore image-wrapper spacing so Markdown long images and unknown dimensions remain whole-page images.
    return page_end == skip_image_spacing(after) ? block->image : -1;
}

static int line_height_for(int px) { return (int)((unsigned)px * s_line_percent / 100); }
static int gap_for(int height, bool heading) {
    return (int)((unsigned)height * s_paragraph_percent / (heading ? 150 : 100));
}

static bool append_page(size_t off) {
    if (s_count == PAGE_MAX) return false;
    if (s_count == s_capacity) {
        size_t cap = s_capacity ? s_capacity * 2 : 16;
        size_t* pages = heap_caps_realloc(s_pages, cap * sizeof(*pages), PSRAM_CAPS);
        if (!pages) return false;
        s_pages = pages;
        // 起点表要多一项：第 page 页的插图区间是 [start[page], start[page+1])。
        // The start table needs one extra slot: page `page` owns [start[page], start[page+1]).
        uint32_t* starts = heap_caps_realloc(s_page_img_start, (cap + 1) * sizeof(*starts), PSRAM_CAPS);
        if (!starts) return false;
        s_page_img_start = starts;
        s_capacity = cap;
    }
    s_pages[s_count] = off;
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    ++s_count;
    s_page_img_start[s_count] = (uint32_t)s_image_count;
    return true;
}

static bool table_next_page(void *ctx, size_t offset) {
    (void)ctx;
    return offset < s_len && (!s_count || offset > s_pages[s_count-1]) && append_page(offset);
}

// 折行时保留原文字节位置；CRLF 算一个段落边界。/ Preserve source offsets while wrapping; CRLF is one paragraph boundary.
// 行缓冲按需增长，不按整章长度常驻。/ Grow line scratch on demand instead of retaining chapter-sized scratch.
static bool line_reserve(size_t need) {
    if(need<=s_line_capacity)return true;
    if(need>s_len+1)return false;
    size_t capacity=s_line_capacity?s_line_capacity:256;
    while(capacity<need){if(capacity>(s_len+1)/2){capacity=s_len+1;break;}capacity*=2;}
    char *next=heap_caps_realloc(s_line,capacity,PSRAM_CAPS);
    if(!next)return false;
    s_line=next;s_line_capacity=capacity;return true;
}

static size_t first_link_ending_after(size_t offset) {
    size_t low = 0, high = s_link_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (s_links[mid].end <= offset) low = mid + 1;
        else high = mid;
    }
    return low;
}

static const html_link_t *image_note_at(size_t offset) {
    size_t i = first_link_ending_after(offset);
    return i < s_link_count && s_links[i].start == offset && s_links[i].image_label[0] ? &s_links[i] : NULL;
}

static bool image_note_is_badge(const html_link_t *link) {
    return link->image_label[0] == '*';
}

static int image_note_px(int px, const html_link_t *link, int *badge_top) {
    if (image_note_is_badge(link)) {
        int above = 0, below = 0;
        ttf_measure_line_px(px, ttf_font_has_text("中") ? "中" : "H", &above, &below);
        if (above + below < 2) { above = px; below = 0; }
        int height = above + below;
        int diameter = ((height * 9 / 10) - 1) | 1;
        if (badge_top) *badge_top = -above + (height - diameter) / 2;
        return diameter;
    }
    int size = px * 2 / 3;
    if (s_vertical) {
        int width = ttf_text_width_px(size, link->image_label);
        if (width > px) size = size * px / width;
    }
    return size > 0 ? size : 1;
}

// Leave room even at the tightest tracking setting. The larger trailing space groups the note with its source text.
static int image_note_leading(int px) { return px / 24 + 6; }
static int image_note_trailing(int px) { return px / 5 + 6; }

static int image_note_advance(int px, const html_link_t *link) {
    int size = image_note_px(px, link, NULL);
    int extent = s_vertical || image_note_is_badge(link) ? size : ttf_text_width_px(size, link->image_label);
    return image_note_leading(px) + extent + image_note_trailing(px);
}

static bool take_line(size_t off, size_t* next, bool* paragraph_end, int* px, bool* heading,
                      int* line_width, int* indent, uint8_t* align,
                      int* margin_before, int* margin_after) {
    const blk_t* block = block_at(off);
    *heading = block && block->heading;
    *px = s_px + (*heading && !s_markdown ? 8 : 0);
    if (s_markdown && block && block->heading_level >= 1 && block->heading_level <= 3)
        *px = s_heading_px[block->heading_level-1];
    bool first_line = block ? off == block->offset :
        off == 0 || s_text[off - 1] == '\n' || s_text[off - 1] == '\r';
    *align = block ? block->align : HTML_ALIGN_DEFAULT;
    if (!s_english_justification && *align == HTML_ALIGN_LEFT) *align = HTML_ALIGN_DEFAULT;
    bool left_edge = *align == HTML_ALIGN_DEFAULT || *align == HTML_ALIGN_LEFT;
    bool prose = !*heading && left_edge && !(block && block->literal);
    bool english_prose = prose && s_english_justification;
    // 首行缩进由阅读设置统一控制；书内标题和对齐块仍保持原本的位置。
    // The reader setting controls paragraph indent; headings and aligned blocks keep their placement.
    *indent = 0;
    if (first_line && prose) {
        // 缩进按当前字体真实全角字宽和字间距计算，不把行高当字宽。
        // Indent by actual full-width advances plus tracking, not by the font's line height.
        int advance = ttf_text_width_px(*px, "一");
        if (advance <= 0) advance = *px;
        advance += s_tracking_px;
        if (advance < 1) advance = 1;
        *indent = advance * (int)s_first_line_indent_em;
    }
    int extent = s_vertical ? s_rect.height : s_rect.width;
    if (*indent >= extent) *indent = extent > 1 ? extent - 1 : 0;
    if (*indent + *px > extent) *indent = 0;
    // 图书段距只由阅读设置决定。/ Book paragraph spacing comes from the reader preference.
    bool source_margins = s_markdown && block;
    *margin_before = source_margins && first_line ? (int)((unsigned)*px * block->margin_before_percent / 100) : 0;
    *margin_after = source_margins ? (int)((unsigned)*px * block->margin_after_percent / 100) : 0;
    size_t limit = block ? block->offset + block->len : s_len;
    // 纸书常把全角空格写进段首；统一由阅读设置决定缩进，原文偏移仍保留。
    // Printed-book source often contains leading spaces; the reader setting owns the visual indent.
    size_t visible_off = off;
    if (first_line && prose) {
        while (visible_off < limit) {
            size_t n = codepoint_size(s_text + visible_off, limit - visible_off);
            if (!n) return false;
            uint32_t cp = codepoint_value(s_text + visible_off, n);
            if (paragraph_padding(cp)) {
                visible_off += n; continue;
            }
            break;
        }
        if (!s_vertical && visible_off < limit && *indent > 0) {
            size_t n = codepoint_size(s_text + visible_off, limit - visible_off);
            if (!n) return false;
            uint32_t cp = codepoint_value(s_text + visible_off, n);
            if (prohibited_line_end(cp)) {
                // 段首开标点自身常有半字留白，向缩进区悬挂，保持可见左边缘对齐。
                // Hang an opener's own leading whitespace into the indent, keeping its ink edge aligned.
                char glyph[5]; memcpy(glyph, s_text + visible_off, n); glyph[n] = 0;
                int bearing = ttf_text_left_bearing_px(*px, glyph);
                if (bearing > *indent) bearing = *indent;
                if (bearing > 0) *indent -= bearing;
            }
        }
    } else if (english_prose) {
        while (visible_off < limit && s_text[visible_off] == ' ') ++visible_off;
    }
    s_line_source = visible_off;
    int available = extent - *indent;
    size_t end = visible_off;
    size_t last_start = visible_off;
    uint32_t last_cp = 0;
    int64_t width = 0;
    int64_t last_width = 0;
    int glyph_count = 0;
    size_t word_start = visible_off, split_at = 0;
    int64_t word_start_width = 0, split_width = 0;
    int dash_width = 0;
    unsigned dash_style = UINT32_MAX;
    bool synthetic_hyphen = false, previous_joiner = false, split_draws_dash = false;
    book_hyphen_range_t range = {0};
    bool hyphenate = s_hyphenate && !*heading && !(block && block->literal);
    s_line[0] = 0;
    *paragraph_end = false;
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        size_t n = codepoint_size(s_text + end, s_len - end);
        if (!n) return false;
        if(!line_reserve(end-visible_off+n+1))return false;
        uint32_t cp = codepoint_value(s_text + end, n);
        // 字体逐字取整后累加 advance；单字测量避免反复扫描整行前缀。
        // Font advances are rounded per glyph and summed; measure each glyph once instead of every prefix.
        char glyph[5];
        memcpy(glyph, s_text + end, n);
        glyph[n] = 0;
        const html_link_t *image_note = cp == HTML_NOTE_IMAGE_CP && s_has_image_notes ? image_note_at(end) : NULL;
        int advance = image_note ? image_note_advance(*px, image_note) : s_vertical ? ttf_vertical_advance_px(*px, cp)
            : s_markdown ? ttf_text_width_styled_px(*px, glyph, glyph_style(end, *heading))
            : ttf_text_width_px(*px, glyph);
        int64_t candidate = width + advance + (end > visible_off && !*heading ? s_tracking_px : 0);
        if (candidate < 0) return false;
        bool previous_part = book_word_part(last_cp);
        bool joiner = (previous_part || previous_joiner) &&
            book_word_joiner(cp, s_text + end + n, s_text + limit);
        bool word_continues = (previous_part || previous_joiner) && (book_word_part(cp) || joiner);
        if (hyphenate && word_continues && previous_joiner && last_cp == '-' && book_word_part(cp)) {
            split_at = end; split_width = width; split_draws_dash = false;
        } else if (word_continues && book_hyphen_fits_range(range, s_text + end)) {
            unsigned style = s_markdown ? glyph_style(end - 1, *heading) : 0;
            if (dash_style != style) {
                dash_style = style;
                dash_width = s_vertical ? ttf_vertical_advance_px(*px, '-') :
                    s_markdown ? ttf_text_width_styled_px(*px, "-", style) : ttf_text_width_px(*px, "-");
            }
            int64_t with_dash = width + dash_width + (*heading ? 0 : s_tracking_px);
            if (with_dash <= available) { split_at = end; split_width = with_dash; split_draws_dash = true; }
        }
        if (candidate > available && word_continues && word_start == visible_off &&
            *indent && !split_at) {
            // 仅为放不下的段首单词让出缩进。/ Reclaim the first-line indent for a leading word that needs it.
            *indent = 0;
            available = extent;
        }
        if (candidate > available) {
            if (end == visible_off) return false;
            // 先尝试容纳行尾标点，再决定是否在单词内部折行。
            // Try hanging a closing mark before splitting the preceding word.
            bool closing = !word_continues && (prohibited_line_start(cp) || (cp == '-' && previous_part) || image_note);
            if (closing && !s_vertical) {
                int hang = s_rect.x > 4 ? *px / 3 : 0;
                if (hang > s_rect.x - 4) hang = s_rect.x - 4;
                int squeeze = glyph_count > 1 ? (glyph_count - 1) * 3 : 0;
                if (candidate <= (int64_t)available + hang + squeeze ||
                    (glyph_count == 1 && candidate <= (int64_t)available + *px * 2)) {
                    memcpy(s_line + end - visible_off, glyph, n);
                    s_line[end - visible_off + n] = 0;
                    width = candidate;
                    end += n;
                    break;
                }
            }
            if (word_continues || (closing && previous_part)) {
                if (split_at > visible_off) {
                    end = split_at;
                    width = split_width;
                    if (split_draws_dash) {
                        s_line[end - visible_off] = '-';
                        s_line[end - visible_off + 1] = 0;
                        synthetic_hyphen = true;
                    }
                    break;
                }
                if (word_start > visible_off) {
                    end = word_start;
                    width = word_start_width;
                    break;
                }
                if (!word_continues) break;
            }
            if (closing) {
                if (last_start > visible_off) {
                    end = last_start;
                    s_line[end - visible_off] = 0;
                    width = last_width;
                    break;
                }
            }
            // 若最后一个字是左括号，将它回退到下一行；极窄行则让括号和首字成组悬挂。
            // Move a trailing opener to the next line; on a one-glyph line keep the pair together.
            if (prohibited_line_end(last_cp)) {
                if (last_start > visible_off) {
                    end = last_start;
                    s_line[end - visible_off] = 0;
                    width = last_width;
                } else if (!s_vertical && candidate <= (int64_t)available + *px * 2) {
                    memcpy(s_line + end - visible_off, glyph, n);
                    s_line[end - visible_off + n] = 0;
                    width = candidate;
                    end += n;
                }
            }
            break;
        }
        if (!word_continues) {
            split_at = 0;
            word_start = end;
            word_start_width = width;
            range = hyphenate && book_latin_letter(cp) &&
                (end > visible_off || !book_word_before(s_text + (block ? block->offset : 0), s_text + end))
                ? book_hyphen_range(s_text + end, s_text + limit) : (book_hyphen_range_t){0};
        }
        last_width = width;
        width = candidate;
        ++glyph_count;
        last_start = end;
        last_cp = cp;
        previous_joiner = joiner;
        memcpy(s_line + end - visible_off, glyph, n);
        s_line[end - visible_off + n] = 0;
        end += n;
    }
    s_line_source_length = end - visible_off;
    if (!synthetic_hyphen) {
        if (english_prose) {
            // 折行留下的空格不占行尾宽度，原文字节偏移仍由 end 保留。
            // Do not count wrap spaces at the right edge. Keep end in source coordinates.
            while (s_line_source_length && s_line[s_line_source_length - 1] == ' ') {
                --s_line_source_length;
                width -= s_markdown ? ttf_text_width_styled_px(*px, " ", glyph_style(visible_off + s_line_source_length, false))
                                    : ttf_text_width_px(*px, " ");
                if (s_line_source_length) width -= s_tracking_px;
            }
        }
        s_line[s_line_source_length] = 0;
    }
    *line_width = (int)width;
    *next = end;
    if (end < s_len && (s_text[end] == '\r' || s_text[end] == '\n')) {
        *next = end + 1;
        if (s_text[end] == '\r' && *next < s_len && s_text[*next] == '\n') (*next)++;
        // Markdown 块内换行只占行高，段间距在块尾计算。/ Markdown breaks inside a block take one line; paragraph spacing follows the block.
        *paragraph_end = !s_markdown || !block || end == limit;
        if (!s_markdown) *next = skip_book_paragraph_padding(*next);
    }
    return *next > off;
}

static bool next_column_or_page(size_t offset, unsigned* column) {
    if (s_two_columns && !*column) {
        *column = 1;
        return true;
    }
    *column = 0;
    return append_page(offset);
}

bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px) {
    return book_layout_build_blocks(utf8, len, NULL, 0, rect, px);
}

bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    book_layout_free();
    if ((!utf8 && len) || len == SIZE_MAX || s_lead_skip > len || s_lead_height >= (unsigned)rect.height ||
        px <= 0 || px > INT_MAX / 3 ||
        rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return false;
    int line_height = line_height_for(px);
    if (line_height > rect.height) return false;
    for (size_t off = 0; off < len;) {
        size_t n = codepoint_size(utf8 + off, len - off);
        if (!n) return false;
        off += n;
    }
    if (count) {
        if (!blocks || count > HTML_TEXT_MAX_BLOCKS) return false;
        size_t expected = 0;
        for (size_t i = 0; i < count; ++i) {
            const blk_t* b = &blocks[i];
            if (b->offset != expected || b->offset >= len || !b->len || b->len > len - b->offset ||
                ((unsigned char)utf8[b->offset] & 0xc0) == 0x80) return false;
            size_t end = b->offset + b->len;
            if (i + 1 < count) {
                if (end >= len || utf8[end] != '\n') return false;
                expected = end + 1;
            } else if (end != len) return false;
        }
    }
    if (s_markdown) {
        if (s_table_cell_count > 65536 || (s_table_cell_count && !s_table_cells)) return false;
        for (size_t i = 0; i < count; ++i) {
            const blk_t *b = &blocks[i];
            if (!b->table_columns) continue;
            if (!b->table_id || b->image >= 0 || b->table_cell > s_table_cell_count ||
                b->table_columns > s_table_cell_count - b->table_cell ||
                rect.width > INT16_MAX || rect.height > INT16_MAX) return false;
            if (i && b->table_id == blocks[i-1].table_id &&
                (b->table_columns != blocks[i-1].table_columns || b->table_header)) return false;
            size_t previous = b->offset;
            for (size_t c = b->table_cell; c < b->table_cell + b->table_columns; ++c) {
                const markdown_cell_t *cell = &s_table_cells[c];
                if (cell->offset < previous || cell->offset > b->offset+b->len ||
                    cell->len > b->offset+b->len-cell->offset || cell->align > 2 ||
                    (cell->offset < len && ((unsigned char)utf8[cell->offset]&0xc0)==0x80) ||
                    (cell->offset+cell->len < len && ((unsigned char)utf8[cell->offset+cell->len]&0xc0)==0x80)) return false;
                previous = cell->offset + cell->len;
            }
        }
        if (s_style_count > 16384 || (s_style_count && !s_styles)) return false;
        size_t end = 0;
        for (size_t i = 0; i < s_style_count; ++i) {
            const text_style_t *run = &s_styles[i];
            if (run->offset < end || run->offset >= len || !run->len || run->len > len-run->offset ||
                !run->flags || run->flags > 7 || ((unsigned char)utf8[run->offset]&0xc0)==0x80 ||
                (run->offset+run->len < len && ((unsigned char)utf8[run->offset+run->len]&0xc0)==0x80)) return false;
            end = run->offset + run->len;
        }
        for (int i = 0; i < 3; ++i) if (s_heading_px[i] < 24 || s_heading_px[i] > 96) return false;
    }
    s_blocks = blocks;
    s_block_count = count;
    s_text = utf8;
    s_len = len;
    s_px = px;
    s_rect = book_layout_column_rect(rect, 0);
    if (s_markdown) markdown_table_configure(utf8, s_table_cells, s_styles, s_style_count,
                                            rect, px, s_line_percent, s_tracking_px, s_hyphenate);
    s_line_capacity=len+1<256?len+1:256;
    s_line = heap_caps_malloc(s_line_capacity, PSRAM_CAPS);
    size_t off = s_lead_skip;
    if (!s_line) goto fail;
    size_t visible_off = skip_image_spacing(off);
    if (visible_off < len) off = visible_off;
    else if (!s_images_visible) {
        // 纯插图章节仍保留可检测的插图页，让跨章跳过逻辑继续前进。
        // Image-only chapters keep a detectable image page for the chapter-skip path.
        for (size_t i = 0; i < count; ++i)
            if (blocks[i].image >= 0 && blocks[i].offset >= off) { off = blocks[i].offset; break; }
    }
    if (!append_page(off)) goto fail;
    if (s_rect.width <= 0) goto fail;
    int64_t used = page_lead(0, s_rect);
    unsigned column = 0;
    while (off < len) {
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        const blk_t* block = block_at(off);
        if (s_markdown && block && block->table_columns && off == block->offset) {
            size_t first = (size_t)(block-s_blocks), last = first+1;
            while (last < s_block_count && s_blocks[last].table_id == block->table_id &&
                   s_blocks[last].table_columns) ++last;
            size_t after = s_blocks[last-1].offset+s_blocks[last-1].len;
            if (after < len && utf8[after] == '\n') ++after;
            if (!markdown_table_build(block, last-first, after, s_count-1, &used, table_next_page, NULL)) goto fail;
            off = after;
            continue;
        }
        if (block && block->image >= 0 && off == block->offset) {
            int img_w = 0, img_h = 0;
            bool image_fits = !s_vertical && image_display_size(block->image, &img_w, &img_h);
            // 占满栏高的插图保留独页；其他有尺寸的插图随双栏文字流动。
            // Keep full-height images on their own page and let other measured images flow with both columns.
            if (s_vertical || (s_two_columns && (!image_fits || img_h >= s_rect.height))) {
                if (s_pages[s_count - 1] != off && !append_page(off)) goto fail;
                off = block->offset + block->len;
                if (off < len && utf8[off] == '\n') ++off;
                off = skip_image_spacing(off);
                if (off < len && !append_page(off)) goto fail;
                used = 0;
                column = 0;
                continue;
            }
            if (image_fits) {
                // 图片下方接着排文字；空间不足时移到下一栏或下一页。/ Continue below the image or move it to the next column or page when needed.
                if (used && used + img_h > s_rect.height) {
                    if (!next_column_or_page(off, &column)) goto fail;
                    used = 0;
                }
                if (!record_page_image(block->image, column, (int)used, img_w, img_h)) goto fail;
                used += img_h + px / 2;  // 图下留半个行高 / half a line under the image
            } else {
                // 取不到尺寸时退回整页显示，保证纯插图章节仍可被跨章跳过逻辑识别。
                // Without dimensions, fall back to a whole-page image so an image-only chapter
                // stays detectable by the chapter-skip path.
                if (used && !append_page(off)) goto fail;
                used = rect.height;
            }
            off = block->offset + block->len;
            if (off < len && utf8[off] == '\n') ++off;
            continue;
        }
        size_t next;
        bool paragraph_end, heading;
        int line_px, line_width, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) goto fail;
        (void)line_width; (void)indent; (void)align;
        line_height = line_height_for(line_px);
        int stack_extent = s_vertical ? s_rect.width : s_rect.height;
        if (line_height > stack_extent) goto fail;
        int leading = used ? margin_before : 0;
        if (used + leading + line_height > stack_extent) {
            if (!next_column_or_page(off, &column)) goto fail;
            used = 0;
            leading = 0;
        }
        used += leading + line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
    }
    return true;
fail:
    book_layout_free();
    return false;
}

size_t book_layout_page_count(void) { return s_count; }

size_t book_layout_page_start_offset(size_t page) {
    return page < s_count ? s_pages[page] : s_len;
}

size_t book_layout_page_for_offset(size_t off) {
    size_t lo = 0, hi = s_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_pages[mid] <= off) lo = mid;
        else hi = mid;
    }
    return lo;
}

static bool layout_justify_gap(uint32_t previous, uint32_t next, bool image_notes) {
    if (image_notes && next == HTML_NOTE_IMAGE_CP) return false;
    if (!s_english_justification && image_notes && previous == HTML_NOTE_IMAGE_CP) return true;
    return ttf_justify_gap(previous, next, s_english_justification);
}

static int line_delta(size_t len, int natural, int target, int px, int *adjustable, bool image_notes) {
    if (!target || target == natural) { *adjustable = 0; return 0; }
    int glyphs = 0, stretchable = 0;
    uint32_t previous = 0;
    for (size_t at = 0; at < len;) {
        size_t n = codepoint_size(s_line+at, len-at); uint32_t cp = codepoint_value(s_line+at,n);
        if (glyphs && layout_justify_gap(previous, cp, image_notes)) ++stretchable;
        previous = cp; ++glyphs; at += n;
    }
    int delta = target - natural;
    *adjustable = delta < 0 ? glyphs - 1 : stretchable;
    return ttf_justify_fits(delta, *adjustable, px, s_english_justification) ? delta : 0;
}

static void draw_markdown_line(uint8_t *fb, int x, int baseline, int px, bool heading,
                               int natural, int target, int top, int height, EpdRect clip) {
    size_t len = strlen(s_line);
    int adjustable = 0;
    int delta = line_delta(len, natural, target, px, &adjustable, false);
    int quotient = adjustable > 0 ? delta/adjustable : 0;
    int remainder = adjustable > 0 ? delta%adjustable : 0;
    // 背景先整行画完，再画字形，防止负字距或倾斜的笔画被后一个底色覆盖。
    // Paint all backgrounds before glyphs so negative tracking or slanted ink is never erased by the next cell.
    for (int pass = 0; pass < 2; ++pass) {
        int cursor = x, adjusted = 0;
        for (size_t at = 0; at < len;) {
            size_t n = codepoint_size(s_line+at,len-at); char glyph[5]; memcpy(glyph,s_line+at,n); glyph[n]=0;
            unsigned flags = glyph_style(s_line_source+(at<s_line_source_length?at:s_line_source_length-1),heading);
            int width = ttf_text_width_styled_px(px,glyph,flags), gap = 0;
            uint32_t cp = codepoint_value(s_line+at,n);
            if (at+n < len) {
                gap = heading ? 0 : s_tracking_px;
                size_t nn = codepoint_size(s_line+at+n,len-at-n);
                uint32_t next = codepoint_value(s_line+at+n,nn);
                if (delta && (delta < 0 || layout_justify_gap(cp, next, false))) {
                    gap += quotient + (remainder > 0 && adjusted < remainder ? 1 : remainder < 0 && adjusted < -remainder ? -1 : 0);
                    ++adjusted;
                }
            }
            uint8_t bg = (flags & TEXT_STYLE_HIGHLIGHT) ? 12 : 15;
            if (!pass && bg != 15) {
                int right = cursor+width+(gap > 0 ? gap : 0);
                int left = cursor < clip.x ? clip.x : cursor;
                if (right > clip.x+clip.width) right = clip.x+clip.width;
                if (right > left) epd_fill_rect((EpdRect){left,top,right-left,height},bg<<4,fb);
            }
            if (pass) ttf_draw_glyph_styled_px(fb,cursor,baseline,px,glyph,flags,0,bg);
            cursor += width+gap; at += n;
        }
    }
}

void book_layout_set_links(const html_link_t *links, size_t count) {
    s_links = links;
    s_link_count = count;
    s_has_image_notes = false;
    for (size_t i = 0; i < count && !s_has_image_notes; ++i)
        s_has_image_notes = links[i].image_label[0] != 0;
}

typedef struct {
    int x, y, distance;
    const html_link_t *link;
    bool ambiguous;
} link_probe_t;

static bool number_link(const html_link_t *link) {
    const char *start = s_text + link->start;
    const char *end = s_text + link->end;
    if (end - start > 10) return false;
    if (end - start >= 2 && ((*start == '[' && end[-1] == ']') || (*start == '(' && end[-1] == ')'))) {
        ++start;
        --end;
    }
    if (start == end) return false;
    while (start < end && *start >= '0' && *start <= '9') ++start;
    return start == end;
}

static void present_link_span(uint8_t *fb, link_probe_t *probe, const html_link_t *link,
                              EpdRect line, int px, int first, int last, int ink_y, int baseline) {
    int extent = s_vertical ? line.height : line.width;
    if (last > extent) last = extent;
    if (first >= last) return;
    EpdRect hit = s_vertical ? (EpdRect){line.x,line.y+first,line.width,last-first} :
        (EpdRect){line.x+first,line.y,last-first,line.height};
    if (fb) {
        const int thickness = 3;
        EpdRect rule;
        if (s_vertical) {
            rule = (EpdRect){hit.x+hit.width-thickness,hit.y,thickness,hit.height};
        } else {
            if (link->image_label[0] || number_link(link)) {
                int below;
                ttf_measure_line_px(px, ttf_font_has_text("中") ? "中[0]" : "H[0]", NULL, &below);
                ink_y = baseline + below;
            }
            int bottom = line.y + line.height;
            if (s_reading_line && !s_reading_line_offset) {
                int guide = line.y + px + (line.height - px) / 2;
                if (guide - 2 < bottom) bottom = guide - 2;
            }
            int y = ink_y + 2;
            if (y + thickness > bottom) y = bottom - thickness;
            rule = (EpdRect){hit.x,y,hit.width,thickness};
        }
        epd_fill_rect(rule, 0x60, fb);
    }
    if (!probe) return;
    int dx = probe->x < hit.x ? hit.x-probe->x :
        probe->x >= hit.x+hit.width ? probe->x-hit.x-hit.width+1 : 0;
    int dy = probe->y < hit.y ? hit.y-probe->y :
        probe->y >= hit.y+hit.height ? probe->y-hit.y-hit.height+1 : 0;
    const int slop = image_note_is_badge(link) ? 15 : 9;
    if (dx > slop || dy > slop) return;
    int distance = dx*dx + dy*dy;
    if (distance < probe->distance) {
        probe->distance = distance; probe->link = link; probe->ambiguous = false;
    } else if (distance == probe->distance &&
               (probe->link->backlink != link->backlink || probe->link->noteref != link->noteref ||
                strcmp(probe->link->href, link->href))) {
        probe->ambiguous = true;
    }
}

static void link_line(uint8_t *fb, link_probe_t *probe, EpdRect line, int px, int tracking, int target,
                      int baseline) {
    if (!s_link_count) return;
    if (probe && (probe->x < line.x - 9 || probe->y < line.y - 9 ||
                  probe->x >= line.x + line.width + 9 || probe->y >= line.y + line.height + 9)) return;
    const size_t len = s_line_source_length;
    size_t i = first_link_ending_after(s_line_source);
    for (; i < s_link_count && s_links[i].start < s_line_source + len; ++i) {
        const html_link_t *link = &s_links[i];
        size_t start = link->start > s_line_source ? link->start - s_line_source : 0;
        size_t end = link->end - s_line_source;
        if (end > len) end = len;
        int first = 0, last = 0, ink_bottom = 0;
        if (s_vertical) {
            for (size_t at = 0; at < end;) {
                if (at == start) first = last;
                size_t n = codepoint_size(s_line + at, len - at);
                if (!n) return;
                last += ttf_vertical_advance_px(px, codepoint_value(s_line + at, n));
                at += n;
                if (at < end) last += tracking;
            }
        } else if (!ttf_text_range_px(px, s_line, tracking, target, s_english_justification, start, end, &first, &last,
                                      fb ? &ink_bottom : NULL)) continue;
        present_link_span(fb, probe, link, line, px, first, last, baseline + ink_bottom, baseline);
    }
}

static void draw_note_badge(uint8_t *fb, int x, int y, int diameter) {
    // 16px monochrome 注 from PicoSans. Keep this UI glyph available with any reading font.
    static const uint16_t glyph[16] = {
        0x2060, 0x7060, 0x1830, 0x07fe, 0x07ff, 0xc020, 0xf020, 0x3020,
        0x03fe, 0x03fe, 0x1820, 0x1820, 0x3020, 0x6060, 0x67ff, 0x47ff,
    };
    int radius = diameter / 2;
    epd_fill_circle(x + radius, y + radius, radius, 0x70, fb);
    int size = diameter * 2 / 3;
    int inset = (diameter - size) / 2;
    for (int row = 0; row < size; ++row) {
        uint16_t pattern = glyph[row * 16 / size];
        int start = -1;
        for (int col = 0; col <= size; ++col) {
            bool white = col < size && (pattern & (0x8000u >> (col * 16 / size)));
            if (white && start < 0) start = col;
            if (!white && start >= 0) {
                epd_fill_rect((EpdRect){x + inset + start,y + inset + row,col - start,1}, 0xf0, fb);
                start = -1;
            }
        }
    }
}

// Lines containing a replacement marker share one walk for painting and link hit geometry.
static void draw_image_note_line(uint8_t *fb, link_probe_t *probe, EpdRect line, int px,
                                 int baseline, int tracking, int natural, int target) {
    size_t len = strlen(s_line), index = first_link_ending_after(s_line_source);
    int adjustable = 0;
    int delta = line_delta(len, natural, target, px, &adjustable, true);
    int gap = delta ? delta / adjustable : 0, remainder = delta ? delta % adjustable : 0;
    int cursor = 0, adjusted = 0, link_first = 0, link_ink = 0;
    for (size_t at = 0; at < len;) {
        size_t n = codepoint_size(s_line + at, len - at);
        if (!n) return;
        uint32_t cp = codepoint_value(s_line + at, n);
        char glyph[5];
        memcpy(glyph, s_line + at, n);
        glyph[n] = 0;
        size_t offset = s_line_source + at;
        while (index < s_link_count && s_links[index].end <= offset) ++index;
        const html_link_t *link = at < s_line_source_length && index < s_link_count && s_links[index].start <= offset ? &s_links[index] : NULL;
        bool image_note = link && cp == HTML_NOTE_IMAGE_CP && link->image_label[0] && link->start == offset;
        int advance, ink_y = baseline;
        int center = line.x + line.width / 2;
        if (image_note) {
            bool badge = image_note_is_badge(link);
            int badge_top = 0;
            int size = image_note_px(px, link, &badge_top);
            int width = badge ? size : ttf_text_width_px(size, link->image_label);
            int extent = s_vertical ? size : width;
            int start = cursor + image_note_leading(px);
            advance = image_note_leading(px) + extent + image_note_trailing(px);
            int x = s_vertical ? center - width / 2 : line.x + start;
            if (fb && badge) {
                draw_note_badge(fb, x, s_vertical ? line.y + start : baseline + badge_top, size);
            } else if (fb && !badge) {
                ink_y = s_vertical ? line.y + start + ttf_ascender_px(size) : baseline - px / 4;
                ttf_draw_text_px(fb, x, ink_y, size, link->image_label, EPD_DRAW_ALIGN_LEFT, 0, 15);
            }
            present_link_span(fb, probe, link, line, px, start, start + extent, baseline, baseline);
        } else if (s_vertical) {
            advance = ttf_vertical_advance_px(px, cp);
            if (fb && cursor + advance <= line.height)
                ttf_draw_vertical_px(fb, center, line.y + cursor, advance, px, glyph, 0);
        } else {
            advance = ttf_text_width_px(px, glyph);
            if (fb) ttf_draw_text_px(fb, line.x + cursor, baseline, px, glyph, EPD_DRAW_ALIGN_LEFT, 0, 15);
        }
        if (link && !image_note) {
            if (!at || link->start == offset) { link_first = cursor; link_ink = ink_y; }
            if (fb && !s_vertical) {
                int first, last, bottom;
                if (ttf_text_range_px(px, glyph, 0, 0, false, 0, n, &first, &last, &bottom)) ink_y += bottom;
            }
            if (ink_y > link_ink) link_ink = ink_y;
            if (offset + n >= link->end || at + n == s_line_source_length)
                present_link_span(fb, probe, link, line, px, link_first, cursor + advance, link_ink, baseline);
        }
        at += n;
        cursor += advance;
        if (at < len) {
            cursor += tracking;
            size_t next_len = codepoint_size(s_line + at, len - at);
            if (delta && (delta < 0 || layout_justify_gap(cp, codepoint_value(s_line + at, next_len), true))) {
                cursor += gap + (remainder > 0 && adjusted < remainder ? 1 :
                                 remainder < 0 && adjusted < -remainder ? -1 : 0);
                ++adjusted;
            }
        }
    }
}

static int draw_slice(uint8_t* fb,size_t page,EpdRect body,int px,book_layout_cursor_t *cursor,
                      link_probe_t *probe) {
    EpdRect rect = book_layout_column_rect(body, cursor && cursor->started ? cursor->column : 0);
    if (!cursor || (!fb && !probe) || page >= s_count || px != s_px || rect.width != s_rect.width ||
        rect.height != s_rect.height || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return -1;
    // 走回退路径的"整页插图"没有图片记录：这里没有文字可画，那张图由调用方绘制，直接返回。
    // 有记录时说明图片已经流进正文，文字和图一起排。
    // A whole-page image from the fallback path has no record: there is no text to draw here and
    // the caller draws that image, so return. With records the image flowed into the page and the
    // text lays out alongside it.
    if(cursor->started&&(cursor->generation!=s_generation||cursor->page!=page))return -1;
    if (book_layout_page_image(page) >= 0 && book_layout_page_image_count(page) == 0) return 1;
    size_t off = cursor->started?cursor->offset:s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = cursor->started ? cursor->used : page_lead(page, rect);
    // 本页插图按出现顺序排在池里，游标走到哪个块就用哪条记录，不必再查表。
    // This page's images sit in the pool in encounter order, so the walk just consumes them in
    // turn instead of looking each one up.
    uint32_t img_at = cursor->started?cursor->image_at:s_page_img_start ? s_page_img_start[page] : 0;
    cursor->started=true;cursor->generation=s_generation;cursor->page=page;
    const uint32_t img_end = s_page_img_start ? s_page_img_start[page + 1] : 0;
    while (off < end) {
        size_t table_next;
        if (s_markdown && markdown_table_draw(fb, page, off, rect, &table_next, &used)) {
            if (table_next <= off) return -1;
            off = table_next;
            cursor->offset=off;cursor->used=used;cursor->image_at=img_at;return off>=end?1:0;
        }
        size_t visible = skip_image_spacing(off);
        if (visible != off) { off = visible; continue; }
        const blk_t* image_block = block_at(off);
        if (image_block && image_block->image >= 0 && off == image_block->offset) {
            while (img_at < img_end && s_images[img_at].image != image_block->image) ++img_at;
            if (img_at < img_end) {
                const layout_image_t* rec = &s_images[img_at++];
                if (cursor->column != rec->column) {
                    cursor->column = rec->column;
                    rect = book_layout_column_rect(body, cursor->column);
                }
                used = (int64_t)rec->y + rec->height + s_px / 2;
            }
            off = image_block->offset + image_block->len;
            if (off < end && s_text[off] == '\n') ++off;
            continue;
        }
        size_t next;
        bool paragraph_end, heading;
        int line_px, line_width, indent, margin_before, margin_after;
        uint8_t align;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading,
                       &line_width, &indent, &align, &margin_before, &margin_after)) return -1;
        int line_height = line_height_for(line_px);
        int leading = used ? margin_before : 0;
        int stack_extent = s_vertical ? rect.width : rect.height;
        if (used + leading + line_height > stack_extent) {
            if (!s_two_columns || cursor->column || line_height > stack_extent) return -1;
            cursor->column = 1;
            rect = book_layout_column_rect(body, 1);
            used = 0;
            leading = 0;
        }
        used += leading;
        bool image_notes = s_has_image_notes && strstr(s_line, "\xef\xbf\xbc");
        if (s_vertical && s_line[0]) {
            int x = rect.x + rect.width - (int)used - line_height;
            int y = rect.y + indent;
            if (align == 1) y += (rect.height - indent - line_width) / 2;
            else if (align == 2) y += rect.height - indent - line_width;
            if (fb && s_reading_line) {
                int dash = s_reading_line == 1 ? 19 : 2;
                int period = s_reading_line == 1 ? 31 : 13;
                int guide = x + (line_height - line_px) / 2 - s_reading_line_offset;
                if (guide < rect.x) guide = rect.x;
                if (guide > rect.x + rect.width - 2) guide = rect.x + rect.width - 2;
                for (int dy = 0; dy < line_width; dy += period)
                    epd_fill_rect((EpdRect){guide, y + dy, 2, line_width - dy < dash ? line_width - dy : dash}, 0x50, fb);
            }
            EpdRect line = {x,y,line_height,rect.y+rect.height-y};
            int tracking = heading ? 0 : s_tracking_px;
            if (image_notes) draw_image_note_line(fb, probe, line, line_px, 0, tracking, line_width, 0);
            else {
                if (fb) ttf_draw_vertical_px(fb, x + line_height / 2, y, line.height, line_px, s_line, tracking);
                if (s_link_count) link_line(fb, probe, line, line_px, tracking, 0, 0);
            }
        } else if (s_line[0]) {
            if (fb && s_reading_line) {
                int guide_y = rect.y + (int)used + line_px + (line_height - line_px) / 2 +
                              s_reading_line_offset;
                int dash = s_reading_line == 1 ? 19 : 2;
                int period = s_reading_line == 1 ? 31 : 13;
                for (int dx = 0; dx < rect.width; dx += period) {
                    int width = dx + dash <= rect.width ? dash : rect.width - dx;
                    epd_fill_rect((EpdRect){rect.x + dx, guide_y, width, 2}, 0x50, fb);
                }
            }
            int x = rect.x + indent;
            int available = rect.width - indent;
            if (align == 1) x += (available - line_width) / 2;
            else if (align == 2) x += available - line_width;
            int baseline = rect.y + (int)used + ttf_ascender_px(line_px);
            const blk_t *line_block = block_at(off);
            bool explicit_break = s_text[next - 1] == '\n' || s_text[next - 1] == '\r';
            bool final_line = explicit_break || next >= s_len ||
                (line_block && next >= line_block->offset + line_block->len);
            int target = 0;
            if (!heading && !align && !(line_block && line_block->literal) && (!final_line || line_width > available)) {
                // 容许行尾标点悬挂，不拉长已经超宽的英文行。/ Allow hanging punctuation without expanding an overwide English line.
                int hang = s_rect.x > 4 ? line_px / 3 : 0;
                if (hang > s_rect.x - 4) hang = s_rect.x - 4;
                target = line_width > available ? available + hang : available;
                if (s_english_justification && line_width > available && target > line_width) target = line_width;
            }
            if (s_markdown) {
                draw_markdown_line(fb,x,baseline,line_px,heading,line_width,target,rect.y+(int)used,line_height,rect);
            } else {
                EpdRect line = {x,rect.y+(int)used,rect.x+rect.width-x,line_height};
                int tracking = heading ? 0 : s_tracking_px;
                if (image_notes) draw_image_note_line(fb, probe, line, line_px, baseline, tracking, line_width, target);
                else {
                    if (fb && target) ttf_draw_text_px_fitted(fb, x, baseline, line_px, s_line, tracking, target, s_english_justification, 0, 15);
                    else if (fb && tracking) ttf_draw_text_px_spaced(fb, x, baseline, line_px, s_line, tracking, 0, 15);
                    else if (fb) ttf_draw_text_px(fb, x, baseline, line_px, s_line, EPD_DRAW_ALIGN_LEFT, 0, 15);
                    if (s_link_count) link_line(fb, probe, line, line_px, tracking, target, baseline);
                }
            }
        }
        used += line_height;
        if (paragraph_end) used += gap_for(line_height, heading) + margin_after;
        off = next;
        cursor->offset=off;cursor->used=used;cursor->image_at=img_at;return off>=end?1:0;
    }
    cursor->offset=off;cursor->used=used;cursor->image_at=img_at;return 1;
}

int book_layout_draw_slice(uint8_t *fb, size_t page, EpdRect rect, int px, book_layout_cursor_t *cursor) {
    return draw_slice(fb, page, rect, px, cursor, NULL);
}

const html_link_t *book_layout_link_at(size_t page, EpdRect rect, int px, int x, int y, bool *ambiguous) {
    *ambiguous = false;
    if (!s_link_count || s_markdown || page >= s_count) return NULL;
    size_t low = first_link_ending_after(s_pages[page]);
    if (low == s_link_count || s_links[low].start >= (page + 1 < s_count ? s_pages[page+1] : s_len)) return NULL;
    link_probe_t probe = {.x=x,.y=y,.distance=INT_MAX};
    book_layout_cursor_t cursor = {0};
    while (draw_slice(NULL, page, rect, px, &cursor, &probe) == 0) {}
    *ambiguous = probe.ambiguous;
    return probe.link;
}

void book_layout_draw_page(uint8_t* fb,size_t page,EpdRect rect,int px) {
    book_layout_cursor_t cursor={0};
    while(book_layout_draw_slice(fb,page,rect,px,&cursor)==0){}
}
