/* SPDX-License-Identifier: Apache-2.0
 * 中文：md4c事件归一化为连续非空阅读块，保留代码、列表、图片块和 H1–H6 层级。
 * English: Normalize md4c events into contiguous nonempty reading blocks, preserving code, lists, image
 blocks and H1–H6 levels.
 * 中文：根据用户确认的文档阅读方案，保留粗体、斜体及 Obsidian 高亮样式。
 * English: Preserve emphasis and Obsidian highlights per the approved document reading design.
 * 冻结：代码、转义和图片描述不解释高亮；不解码图片；所有错误释放本次输出。
 * Frozen: Do not interpret highlights in code, escapes or image descriptions; no image decoding; release all output on error.
 */
#include "markdown_text.h"
#include "esp_heap_caps.h"
#include "md4c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
typedef struct {
    html_text_t text;
    size_t capacity, block_capacity, start;
    uint8_t *style_bytes;
    unsigned strong, emphasis;
    bool protected_text;
    const char *source;
    size_t source_len;
    unsigned depth, list_depth, quote_depth, image_count;
    unsigned table_column, table_id, table_columns;
    size_t cell_capacity, row_cell;
    bool table_header, in_cell, table_image;
    unsigned ordered[MARKDOWN_DEPTH_MAX], counters[MARKDOWN_DEPTH_MAX];
    bool open, heading, code, in_image, pending_soft_break;
    uint8_t heading_level;
    int image_index;
    esp_err_t error;
} writer_t;

static bool utf8_valid(const char *s, size_t len) {
    for (size_t i = 0; i < len;) {
        const unsigned char *p = (const unsigned char *)s + i;
        size_t n = p[0] > 0 && p[0] < 0x80        ? 1
                   : p[0] >= 0xc2 && p[0] <= 0xdf ? 2
                   : p[0] >= 0xe0 && p[0] <= 0xef ? 3
                   : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4
                                                  : 0;
        if (!n || n > len - i)
            return false;
        for (size_t j = 1; j < n; ++j)
            if ((p[j] & 0xc0) != 0x80)
                return false;
        if (n > 1 && ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
                      (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)))
            return false;
        i += n;
    }
    return true;
}
static int append(writer_t *w, const char *s, size_t len) {
    if (w->error)
        return 1;
    if (len > MARKDOWN_TEXT_MAX - w->text.len) {
        w->error = ESP_ERR_INVALID_SIZE;
        return 1;
    }
    size_t need = w->text.len + len + 1;
    if (need > w->capacity) {
        size_t cap = w->capacity ? w->capacity : 256;
        while (cap < need)
            cap = cap > (MARKDOWN_TEXT_MAX + 1) / 2 ? MARKDOWN_TEXT_MAX + 1 : cap * 2;
        char *p = heap_caps_realloc(w->text.utf8, cap, CAPS);
        if (!p) {
            w->error = ESP_ERR_NO_MEM;
            return 1;
        }
        w->text.utf8 = p;
        uint8_t *styles = heap_caps_realloc(w->style_bytes, cap, CAPS);
        if (!styles) { w->error = ESP_ERR_NO_MEM; return 1; }
        w->style_bytes = styles;
        w->capacity = cap;
    }
    if (len) {
        if (w->pending_soft_break) {
            w->text.utf8[w->text.len - 1] = '\n';
            w->pending_soft_break = false;
        }
        memcpy(w->text.utf8 + w->text.len, s, len);
        memset(w->style_bytes + w->text.len,
               (w->strong ? TEXT_STYLE_BOLD : 0) | (w->emphasis ? TEXT_STYLE_ITALIC : 0) |
               ((w->protected_text || w->code || w->in_image) ? 0x80 : 0), len);
    }
    w->text.len += len;
    w->text.utf8[w->text.len] = 0;
    return 0;
}
static int flush(writer_t *w) {
    if (!w->open)
        return 0;
    // 代码块最后一个换行是事件终止符，其他空行仍保留。
    // The last code newline is an event terminator; preserve other blank lines.
    if (w->code && w->text.len > w->start && w->text.utf8[w->text.len - 1] == '\n')
        w->text.utf8[--w->text.len] = 0;
    w->open = false;
    w->pending_soft_break = false;
    if (w->text.len == w->start) {
        if (w->start && w->text.utf8[w->start - 1] == '\n')
            w->text.utf8[--w->text.len] = 0;
        return 0;
    }
    if (w->text.count == MARKDOWN_BLOCK_MAX) {
        w->error = ESP_ERR_INVALID_SIZE;
        return 1;
    }
    if (w->text.count == w->block_capacity) {
        size_t cap = w->block_capacity ? w->block_capacity * 2 : 32;
        if (cap > MARKDOWN_BLOCK_MAX)
            cap = MARKDOWN_BLOCK_MAX;
        blk_t *p = heap_caps_realloc(w->text.blocks, cap * sizeof(*p), CAPS);
        if (!p) {
            w->error = ESP_ERR_NO_MEM;
            return 1;
        }
        w->text.blocks = p;
        w->block_capacity = cap;
    }
    w->text.blocks[w->text.count++] = (blk_t){.offset = w->start,
                                              .len = w->text.len - w->start,
                                              .heading = w->heading,
                                              .literal = w->code || w->list_depth || w->quote_depth,
                                              .heading_level = w->heading_level,
                                              .table_id = w->table_columns ? w->table_id : 0,
                                              .table_columns = w->table_columns,
                                              .table_cell = w->row_cell,
                                              .table_header = w->table_header,
                                              .image = w->in_image ? w->image_index : -1};
    return 0;
}
static int begin(writer_t *w, bool heading) {
    if (flush(w))
        return 1;
    if (w->text.count && append(w, "\n", 1))
        return 1;
    w->start = w->text.len;
    w->open = true;
    w->heading = heading;
    w->heading_level = 0;
    if (w->quote_depth && append(w, "> ", 2))
        return 1;
    return 0;
}
static int enter_block(MD_BLOCKTYPE type, void *detail, void *ctx) {
    writer_t *w = ctx;
    if (++w->depth > MARKDOWN_DEPTH_MAX) {
        w->error = ESP_ERR_INVALID_SIZE;
        return 1;
    }
    switch (type) {
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (flush(w))
            return 1;
        if (w->list_depth == MARKDOWN_DEPTH_MAX) {
            w->error = ESP_ERR_INVALID_SIZE;
            return 1;
        }
        w->ordered[w->list_depth] = type == MD_BLOCK_OL;
        w->counters[w->list_depth] = type == MD_BLOCK_OL ? ((MD_BLOCK_OL_DETAIL *)detail)->start : 0;
        ++w->list_depth;
        break;
    case MD_BLOCK_QUOTE:
        if (flush(w))
            return 1;
        ++w->quote_depth;
        break;
    case MD_BLOCK_H:
        if (begin(w, true))
            return 1;
        w->heading_level = ((MD_BLOCK_H_DETAIL *)detail)->level;
        break;
    case MD_BLOCK_CODE:
        if (begin(w, false))
            return 1;
        w->code = true;
        break;
    case MD_BLOCK_P:
    if (!w->open && begin(w, false))
            return 1;
        break;
    case MD_BLOCK_LI: {
        if (begin(w, false))
            return 1;
        for (unsigned i = 1; i < w->list_depth; ++i)
            if (append(w, "  ", 2))
                return 1;
        char marker[40];
        MD_BLOCK_LI_DETAIL *li = detail;
        if (li->is_task)
            snprintf(marker, sizeof(marker), "[%c] ", li->task_mark == ' ' ? ' ' : 'x');
        else if (w->list_depth && w->ordered[w->list_depth - 1])
            snprintf(marker, sizeof(marker), "%u. ", w->counters[w->list_depth - 1]++);
        else
            snprintf(marker, sizeof(marker), "- ");
        if (append(w, marker, strlen(marker)))
            return 1;
        break;
    }
    case MD_BLOCK_HR:
        if (begin(w, false) || append(w, "---", 3) || flush(w))
            return 1;
        break;
    case MD_BLOCK_TABLE:
        if (flush(w)) return 1;
        if (++w->table_id > UINT16_MAX) { w->error = ESP_ERR_INVALID_SIZE; return 1; }
        w->table_columns = ((MD_BLOCK_TABLE_DETAIL *)detail)->col_count;
        if (w->table_columns > UINT16_MAX) { w->error = ESP_ERR_INVALID_SIZE; return 1; }
        break;
    case MD_BLOCK_TR:
        w->table_column = 0;
        w->row_cell = w->text.cell_count;
        if (begin(w, false))
            return 1;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        w->table_header = type == MD_BLOCK_TH;
        if (w->table_column++ && append(w, " | ", 3))
            return 1;
        if (w->text.cell_count >= 65536) { w->error = ESP_ERR_INVALID_SIZE; return 1; }
        if (w->text.cell_count == w->cell_capacity) {
            size_t cap = w->cell_capacity ? w->cell_capacity * 2 : 32;
            markdown_cell_t *cells = heap_caps_realloc(w->text.cells, cap * sizeof(*cells), CAPS);
            if (!cells) { w->error = ESP_ERR_NO_MEM; return 1; }
            w->text.cells = cells; w->cell_capacity = cap;
        }
        MD_ALIGN a = ((MD_BLOCK_TD_DETAIL *)detail)->align;
        w->text.cells[w->text.cell_count++] = (markdown_cell_t){.offset = w->text.len,
            .align = a == MD_ALIGN_CENTER ? 1 : a == MD_ALIGN_RIGHT ? 2 : 0};
        w->in_cell = true;
        break;
    default:
        break;
    }
    return 0;
}
static int leave_block(MD_BLOCKTYPE type, void *detail, void *ctx) {
    (void)detail;
    writer_t *w = ctx;
    int err = 0;
    switch (type) {
    case MD_BLOCK_TABLE:
        err = flush(w); w->table_columns = 0; w->table_header = false;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        w->text.cells[w->text.cell_count-1].len = w->text.len - w->text.cells[w->text.cell_count-1].offset;
        if (w->table_columns == 1 && w->text.len == w->start && append(w, " ", 1)) return 1;
        w->in_cell = false;
        break;
    case MD_BLOCK_DOC:
    case MD_BLOCK_P:
    case MD_BLOCK_H:
    case MD_BLOCK_LI:
    case MD_BLOCK_TR:
        err = flush(w);
        break;
    case MD_BLOCK_CODE:
        err = flush(w);
        w->code = false;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        err = flush(w);
        if (w->list_depth)
            --w->list_depth;
        break;
    case MD_BLOCK_QUOTE:
        err = flush(w);
        if (w->quote_depth)
            --w->quote_depth;
        break;
    default:
        break;
    }
    if (w->depth)
        --w->depth;
    return err;
}
static int enter_span(MD_SPANTYPE type, void *detail, void *ctx) {
    writer_t *w = ctx;
    if (++w->depth > MARKDOWN_DEPTH_MAX) {
        w->error = ESP_ERR_INVALID_SIZE;
        return 1;
    }
    if (type == MD_SPAN_STRONG) ++w->strong;
    if (type == MD_SPAN_EM) ++w->emphasis;
    if (type == MD_SPAN_IMG) {
        if (w->in_cell) { w->table_image = true; return append(w, "[图片: ", strlen("[图片: ")); }
        MD_SPAN_IMG_DETAIL *image = detail;
        if (++w->image_count > MARKDOWN_IMAGE_MAX || image->src.size > MARKDOWN_PATH_MAX) {
            w->error = ESP_ERR_INVALID_SIZE;
            return 1;
        }
        if (begin(w, false))
            return 1;
        char **paths = heap_caps_realloc(w->text.images, w->image_count * sizeof(char *), CAPS);
        if (!paths) {
            w->error = ESP_ERR_NO_MEM;
            return 1;
        }
        w->text.images = paths;
        char *path = heap_caps_malloc(image->src.size + 1, CAPS);
        if (!path) {
            w->error = ESP_ERR_NO_MEM;
            return 1;
        }
        if (image->src.size)
            memcpy(path, image->src.text, image->src.size);
        path[image->src.size] = 0;
        w->image_index = (int)w->text.image_count;
        w->text.images[w->text.image_count++] = path;
        w->in_image = true;
        return append(w, "[图片: ", strlen("[图片: "));
    }
    return 0;
}
static int leave_span(MD_SPANTYPE type, void *detail, void *ctx) {
    (void)detail;
    writer_t *w = ctx;
    if (w->depth)
        --w->depth;
    if (type == MD_SPAN_STRONG && w->strong) --w->strong;
    if (type == MD_SPAN_EM && w->emphasis) --w->emphasis;
    if (type != MD_SPAN_IMG)
        return 0;
    if (w->table_image) { w->table_image = false; return append(w, "]", 1); }
    int result = append(w, "]", 1) || flush(w);
    w->in_image = false;
    return result;
}
static int on_text(MD_TEXTTYPE type, const MD_CHAR *s, MD_SIZE len, void *ctx) {
    writer_t *w = ctx;
    w->protected_text = type == MD_TEXT_CODE || type == MD_TEXT_ENTITY || w->table_image;
    if ((uintptr_t)s >= (uintptr_t)w->source && (uintptr_t)s < (uintptr_t)w->source + w->source_len) {
        size_t pos = (size_t)(s - w->source), slashes = 0;
        while (pos && w->source[--pos] == '\\') ++slashes;
        if (slashes % 2) w->protected_text = true;
    }
    if (!w->open && begin(w, false))
        return 1;
    if (type == MD_TEXT_BR)
        return append(w, "\n", 1);
    if (type == MD_TEXT_SOFTBR) {
        // 同一文字块有后续内容时才换行，避免在独立图片块旁多留空行。
        // Break only when more text follows in the same block, avoiding extra rows beside images.
        bool within_prose = w->text.len > w->start && !w->heading && !w->in_image && !w->in_cell;
        int result = append(w, " ", 1);
        w->pending_soft_break = within_prose && !result;
        return result;
    }
    if (type == MD_TEXT_NULLCHAR) {
        w->error = ESP_ERR_INVALID_ARG;
        return 1;
    }
    if (type == MD_TEXT_ENTITY) {
        // 哨兵保留实体前后空格；HTML解析器会修剪孤立空白。
        // Sentinels retain entity whitespace otherwise trimmed by the HTML parser.
        // 未收录的实体原样保留。/ Preserve names outside the EPUB entity vocabulary.
        if (len > 34)
            return append(w, s, len);
        char wrapped[37];
        wrapped[0] = 'A';
        memcpy(wrapped + 1, s, len);
        wrapped[len + 1] = 'Z';
        html_text_t entity = {0};
        esp_err_t err = html_to_blocks(wrapped, len + 2, &entity);
        if (err != ESP_OK) {
            w->error = err;
            return 1;
        }
        int result = entity.len >= 2 ? append(w, entity.utf8 + 1, entity.len - 2) : 1;
        if (result && !w->error)
            w->error = ESP_ERR_INVALID_ARG;
        html_text_free(&entity);
        return result;
    }
    if (type == MD_TEXT_CODE) {
        for (size_t i = 0; i < len; ++i)
            if (s[i] == '\t' ? append(w, "    ", 4) : append(w, s + i, 1))
                return 1;
        return 0;
    }
    return append(w, s, len);
}
// 高亮在归一化后配对，先保留未闭合标记；有界临时字节表最终压缩成样式区间。
// Pair highlights after normalization, retain unmatched markers and compress the bounded temporary byte map into runs.
static void pair_highlights(writer_t *w, size_t start, size_t end) {
    size_t opener = SIZE_MAX;
    for (size_t i = start; i + 1 < end; ++i) {
        if (w->text.utf8[i] != '=' || w->text.utf8[i+1] != '=' ||
            (w->style_bytes[i] & 0x80) || (w->style_bytes[i+1] & 0x80)) continue;
        if ((i > start && w->text.utf8[i-1] == '=') || (i+2 < end && w->text.utf8[i+2] == '=')) continue;
        if (opener == SIZE_MAX) opener = i;
        else {
            if (i > opener + 2) {
                w->style_bytes[opener] = w->style_bytes[opener+1] = 0x40;
                w->style_bytes[i] = w->style_bytes[i+1] = 0x40;
                for (size_t k = opener + 2; k < i; ++k) w->style_bytes[k] |= TEXT_STYLE_HIGHLIGHT;
            }
            opener = SIZE_MAX;
        }
        ++i;
    }
}
static void copy_styled_range(writer_t *w, size_t start, size_t end, size_t *write) {
    for (size_t i = start; i < end; ++i) {
        if (w->style_bytes[i] & 0x40) continue;
        w->text.utf8[*write] = w->text.utf8[i];
        w->style_bytes[(*write)++] = w->style_bytes[i] & 7;
    }
}
static int finish_styles(writer_t *w) {
    size_t write = 0;
    for (size_t b = 0; b < w->text.count; ++b) {
        blk_t *block = &w->text.blocks[b];
        size_t start = block->offset, end = start + block->len;
        if (block->table_columns) {
            for (size_t c = block->table_cell; c < block->table_cell + block->table_columns; ++c) {
                markdown_cell_t *cell = &w->text.cells[c];
                pair_highlights(w, cell->offset, cell->offset + cell->len);
            }
        } else pair_highlights(w, start, end);
        if (b) { w->text.utf8[write] = '\n'; w->style_bytes[write++] = 0; }
        block->offset = write;
        size_t at = start;
        for (size_t c = block->table_cell; block->table_columns &&
             c < block->table_cell + block->table_columns; ++c) {
            markdown_cell_t *cell = &w->text.cells[c];
            size_t cell_end = cell->offset + cell->len;
            copy_styled_range(w, at, cell->offset, &write);
            at = cell->offset;
            cell->offset = write;
            copy_styled_range(w, at, cell_end, &write);
            cell->len = write - cell->offset;
            at = cell_end;
        }
        copy_styled_range(w, at, end, &write);
        block->len = write - block->offset;
    }
    w->text.len = write; w->text.utf8[write] = 0;
    size_t count = 0;
    for (size_t i = 0; i < write;) {
        uint8_t flags = w->style_bytes[i++];
        while (i < write && w->style_bytes[i] == flags) ++i;
        if (flags && ++count > 16384) { w->error = ESP_ERR_INVALID_SIZE; return 1; }
    }
    if (count) {
        w->text.styles = heap_caps_malloc(count * sizeof(text_style_t), CAPS);
        if (!w->text.styles) { w->error = ESP_ERR_NO_MEM; return 1; }
        for (size_t i = 0; i < write;) {
            size_t start = i; uint8_t flags = w->style_bytes[i++];
            while (i < write && w->style_bytes[i] == flags) ++i;
            if (flags) w->text.styles[w->text.style_count++] = (text_style_t){start, i-start, flags};
        }
    }
    return 0;
}
static size_t frontmatter_end(const char *s, size_t len) {
    if (len < 4 || memcmp(s, "---", 3) || (s[3] != '\n' && s[3] != '\r'))
        return 0;
    size_t pos = 3;
    while (pos < len && (s[pos] == '\r' || s[pos] == '\n'))
        ++pos;
    while (pos < len) {
        size_t end = pos;
        while (end < len && s[end] != '\r' && s[end] != '\n')
            ++end;
        if (end - pos == 3 && (!memcmp(s + pos, "---", 3) || !memcmp(s + pos, "...", 3))) {
            while (end < len && (s[end] == '\r' || s[end] == '\n'))
                ++end;
            return end;
        }
        pos = end;
        while (pos < len && (s[pos] == '\r' || s[pos] == '\n'))
            ++pos;
    }
    return 0;
}
esp_err_t markdown_to_blocks(const char *input, size_t len, html_text_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if ((!input && len) || len > MARKDOWN_INPUT_MAX)
        return len > MARKDOWN_INPUT_MAX ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
    if (!input)
        input = "";
    if (!utf8_valid(input, len))
        return ESP_ERR_INVALID_ARG;
    if (len >= 3 && !memcmp(input, "\xef\xbb\xbf", 3)) {
        input += 3;
        len -= 3;
    }
    size_t skip = frontmatter_end(input, len);
    input += skip;
    len -= skip;
    writer_t w = {.source = input, .source_len = len};
    const MD_PARSER parser = {.flags = MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS | MD_FLAG_TABLES |
                                       MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH,
                              .enter_block = enter_block,
                              .leave_block = leave_block,
                              .enter_span = enter_span,
                              .leave_span = leave_span,
                              .text = on_text};
    int result = md_parse(input, (MD_SIZE)len, &parser, &w);
    if (!result)
        result = flush(&w);
    if (!result)
        result = append(&w, "", 0);
    if (!result) result = finish_styles(&w);
    free(w.style_bytes);
    if (result) {
        html_text_free(&w.text);
        return w.error ? w.error : ESP_ERR_NO_MEM;
    }
    *out = w.text;
    return ESP_OK;
}
