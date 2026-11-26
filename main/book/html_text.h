/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 自定义固件修改：块表新增 Markdown 标题层级与样式区间，原 HTML 字段行为保留。
 * Modified derivative: add Markdown heading levels and style runs while retaining HTML field behavior.
 *
 * 中文：把章节 HTML 转为 PSRAM UTF-8 文本与非空块表。
 * English: Convert chapter HTML into PSRAM UTF-8 text and nonempty blocks.
 *
 * 冻结：不加载网络资源、不执行脚本；解释有界的阅读排版 CSS 子集；输入只借用。
 * Frozen: Never load network resources or execute scripts; interpret a bounded reading CSS subset; input is borrowed.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define HTML_TEXT_MAX_BYTES (4u * 1024u * 1024u)
#define HTML_TEXT_MAX_BLOCKS 16384u
#define HTML_LINK_HREF_CAP 512u
#define HTML_LINK_ANCHOR_CAP 128u
#define HTML_LINK_MAX 512u
#define HTML_NOTE_LABEL_CAP 6u
#define HTML_NOTE_IMAGE_CP 0xfffcu

enum { HTML_ALIGN_DEFAULT, HTML_ALIGN_CENTER, HTML_ALIGN_RIGHT, HTML_ALIGN_LEFT };

typedef struct {
    size_t offset; ///< UTF-8 字节起点 / UTF-8 byte start
    size_t len; ///< 不含段间换行的字节数 / Bytes excluding the block separator
    bool heading; ///< h1–h3 标题块 / h1-h3 heading block
    bool literal; ///< 保留 Markdown 代码/列表缩进 / Preserve Markdown code/list indentation
    uint8_t heading_level; ///< Markdown H1–H6，其他为零 / Markdown H1–H6, otherwise zero
    int image; ///< 图片序号，文字为 -1 / Image index, -1 for text
    uint8_t align; ///< 段落对齐方式 / Paragraph alignment from HTML_ALIGN_*
    uint8_t indent_percent; ///< 首行缩进，相对字号百分比 / First-line indent as a percentage of font size
    uint8_t margin_before_percent; ///< 段前距，相对字号百分比 / Leading margin as a percentage of font size
    uint8_t margin_after_percent; ///< 段后距，相对字号百分比 / Trailing margin as a percentage of font size
    uint32_t table_cell; ///< Markdown 行的首单元格下标 / First cell of a Markdown row
    uint16_t table_columns, table_id; ///< 列数与非零表格编号 / Column count and nonzero table identity
    bool table_header; ///< Markdown 表头行 / Markdown header row
} blk_t;

/// Markdown 单元格借用归一化正文区间，允许空内容。/ Markdown cell range in normalized text, including empty cells.
typedef struct { size_t offset, len; uint8_t align; } markdown_cell_t;

enum { TEXT_STYLE_BOLD = 1, TEXT_STYLE_ITALIC = 2, TEXT_STYLE_HIGHLIGHT = 4 };
/// 非重叠有序文字样式区间，偏移沿用归一化 UTF-8。/ Ordered nonoverlapping style runs in normalized UTF-8.
typedef struct { size_t offset, len; uint8_t flags; } text_style_t;

/// 区间使用归一化 UTF-8 字节偏移。/ Range uses normalized UTF-8 byte offsets.
typedef struct {
    uint32_t start, end;
    char *href;
    const char *anchor, *block_anchor;
    bool backlink, noteref, navigation;
    char image_label[HTML_NOTE_LABEL_CAP];
} html_link_t;

typedef enum { HTML_NOTE_NONE, HTML_NOTE_PARAGRAPH, HTML_NOTE_EXPLICIT } html_note_kind_t;

typedef struct { uint32_t stored, layout; } html_position_t;
typedef struct {
    html_position_t small[4], *large;
    size_t count;
} html_positions_t;

typedef struct {
    html_positions_t positions; ///< Sparse correspondence with the 0.2.3 position format.
    html_link_t *links;
    size_t link_count;
    size_t note_start, note_end; ///< Normalized text range enclosing the requested anchor.
    html_note_kind_t note_kind;
    bool anchor_in_note; ///< 锚点落在有语义标注的脚注内。/ Anchor is inside a semantic footnote, even without a usable card.
    markdown_cell_t *cells; ///< 自有 Markdown 单元格表，普通图书为空 / Owned Markdown cells, absent in ordinary books
    size_t cell_count;
    text_style_t* styles; ///< 自有样式表，普通图书为空 / Owned style table, empty for ordinary books
    size_t style_count;
    char* utf8; ///< 模块分配且零结尾的文本 / Owned NUL-terminated text
    size_t len; ///< 不含结尾零的字节数 / Bytes excluding terminating NUL
    blk_t* blocks; ///< 模块分配的非空块表 / Owned nonempty block table
    size_t count; ///< 块数 / Block count
    char** images; ///< 相对图片路径 / Relative image paths
    size_t image_count; ///< 图片数 / Image count
} html_text_t;

/// Convert between persisted EPUB positions and this chapter's layout offsets.
size_t html_position_convert(const html_positions_t *positions, size_t offset, bool to_layout);

/// 输出须为空；成功交出所有权，失败清空输出；空输入成功且零块。/ Output must be empty; success transfers ownership, failure clears output; empty input succeeds with zero blocks.
esp_err_t html_to_blocks(const char* html, size_t len, html_text_t* out);
/// 先应用书内 CSS，再应用页面内样式；两段输入均只借用。/ Apply in-book CSS before page styles; both inputs are borrowed.
esp_err_t html_to_blocks_with_css(const char* html, size_t len,
                                   const char* css, size_t css_len, html_text_t* out);
/// 同一次解析记录 EPUB #fragment 在规范化正文中的字节位置；找不到时为 SIZE_MAX。
/// Record a fragment's normalized text offset during parsing; missing anchors map to SIZE_MAX.
esp_err_t html_to_blocks_with_css_anchor(const char* html, size_t len,
                                          const char* css, size_t css_len,
                                          const char* anchor, size_t* anchor_offset,
                                          html_text_t* out);
typedef bool (*html_note_image_probe_fn)(void *ctx, const char *path, unsigned max_side);
/// 目录标题没有 id 时，以原 XHTML 标签位置定位，仍在同一次排版解析中换算正文偏移。
/// Resolve a source tag position to normalized text during the same parse.
esp_err_t html_to_blocks_with_css_target(const char* html, size_t len,
                                          const char* css, size_t css_len,
                                          const char* anchor, size_t source_offset,
                                          size_t* text_offset, html_note_image_probe_fn probe, void *probe_ctx,
                                          html_text_t* out);
/// 释放文本与块表并清零；可重复调用。/ Free text and blocks and reset; safe to repeat.
void html_text_free(html_text_t* text);
