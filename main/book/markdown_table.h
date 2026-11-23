/* SPDX-License-Identifier: Apache-2.0
 * 中文：Markdown 表格的有界列排版、分页与绘制；不刷新屏幕、不读取文件。
 * English: Bounded Markdown table layout, pagination and drawing; no display presents or file I/O.
 */
#pragma once
#include "book_layout.h"
typedef bool (*markdown_table_page_fn)(void *ctx, size_t offset);
/// 借用当前章节；普通图书不调用。/ Borrow the current chapter; never used for ordinary books.
void markdown_table_configure(const char *text, const markdown_cell_t *cells,
    const text_style_t *styles, size_t style_count, EpdRect rect, int px, unsigned line_percent, int tracking, bool hyphenate);
/// 只释放表格布局记录，不释放借用的章节。/ Free table layout records, never borrowed chapter data.
void markdown_table_free(void);
/// 在当前页流中排入完整表格，必要时创建续页。/ Place a table in page flow, creating continuation pages when needed.
bool markdown_table_build(const blk_t *rows, size_t count, size_t after, size_t page,
    int64_t *used, markdown_table_page_fn next_page, void *ctx);
/// 绘制该偏移对应的表格片段并跳到片段末尾；false 表示没有匹配。/ Draw the matching table fragment and advance; false means no match.
bool markdown_table_draw(uint8_t *fb, size_t page, size_t offset, EpdRect rect, size_t *next, int64_t *used);
