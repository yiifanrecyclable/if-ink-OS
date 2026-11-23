/* SPDX-License-Identifier: Apache-2.0
 * 中文：自定义固件新增 Markdown 书源；文件读取只在打开/加载时发生。
 * English: Derivative Markdown source;
 file I/O occurs only during open/load.
 */
#include "book_markdown.h"
#include "markdown_text.h"
#include "book_index_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef PICO_HOST_SIMULATOR
// TF卡使用不支持符号链接的FatFS；主机保留lstat路径防护。/ FatFS has no symlinks; the host keeps lstat checks.
#define lstat stat
#endif
typedef struct {
    size_t block, parent;
    bool expanded;
} heading_t;
static html_text_t s_text;
static heading_t *s_headings;
static size_t *s_visible, s_count, s_visible_count;
static bool s_active;
static content_language_t s_language;
static char s_path[512];
static void visibility(void) {
    s_visible_count = 0;
    for (size_t i = 0; i < s_count; i++) {
        bool show = true;
        for (size_t p = s_headings[i].parent; p != SIZE_MAX; p = s_headings[p].parent)
            if (!s_headings[p].expanded) {
                show = false;
                break;
            }
        if (show)
            s_visible[s_visible_count++] = i;
    }
}
void book_markdown_close(void) {
    html_text_free(&s_text);
    free(s_headings);
    free(s_visible);
    s_headings = NULL;
    s_visible = NULL;
    s_count = s_visible_count = 0;
    s_active = false;
    s_language = CONTENT_LANGUAGE_UNKNOWN;
    s_path[0] = 0;
}
esp_err_t book_markdown_open(const char *path) {
    book_markdown_close();
    struct stat st;
    if (!path || strlen(path) >= sizeof(s_path) || stat(path, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || st.st_size > MARKDOWN_INPUT_MAX)
        return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(path, "rb");
    if (!f)
        return ESP_FAIL;
    char *input = malloc((size_t)st.st_size + 1);
    if (!input) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t n = fread(input, 1, (size_t)st.st_size, f);
    bool bad = ferror(f) || n != (size_t)st.st_size;
    fclose(f);
    esp_err_t err = bad ? ESP_FAIL : markdown_to_blocks(input, n, &s_text);
    free(input);
    if (err != ESP_OK)
        return err;
    for (size_t i = 0; i < s_text.count; i++)
        if (s_text.blocks[i].heading_level)
            s_count++;
    if (s_count) {
        s_headings = calloc(s_count, sizeof(*s_headings));
        s_visible = malloc(s_count * sizeof(*s_visible));
        if (!s_headings || !s_visible) {
            book_markdown_close();
            return ESP_ERR_NO_MEM;
        }
        size_t k = 0, stack[6], depth = 0;
        for (size_t i = 0; i < s_text.count; i++) {
            unsigned level = s_text.blocks[i].heading_level;
            if (!level)
                continue;
            while (depth && s_text.blocks[s_headings[stack[depth - 1]].block].heading_level >= level)
                depth--;
            s_headings[k] = (heading_t){i, depth ? stack[depth - 1] : SIZE_MAX, false};
            stack[depth++] = k++;
        }
        visibility();
    }
    strcpy(s_path, path);
    if (!book_index_cache_read_language(path, &s_language))
        s_language = content_language_text(s_text.utf8, s_text.len, false);
    s_active = true;
    return ESP_OK;
}
bool book_markdown_active(void) {
    return s_active;
}
content_language_t book_markdown_language(void) {
    return s_language;
}
size_t book_markdown_length(void) {
    return s_text.len;
}
esp_err_t book_markdown_load(html_text_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!s_active)
        return ESP_ERR_INVALID_ARG;
    out->utf8 = malloc(s_text.len + 1);
    if (!out->utf8)
        return ESP_ERR_NO_MEM;
    memcpy(out->utf8, s_text.utf8, s_text.len + 1);
    out->len = s_text.len;
    if (s_text.cell_count) {
        out->cells = malloc(s_text.cell_count * sizeof(*out->cells));
        if (!out->cells) goto fail;
        memcpy(out->cells, s_text.cells, s_text.cell_count * sizeof(*out->cells));
        out->cell_count = s_text.cell_count;
    }
    if (s_text.count) {
        out->blocks = malloc(s_text.count * sizeof(blk_t));
        if (!out->blocks)
            goto fail;
        memcpy(out->blocks, s_text.blocks, s_text.count * sizeof(blk_t));
        out->count = s_text.count;
    }
    if (s_text.style_count) {
        out->styles = malloc(s_text.style_count * sizeof(text_style_t));
        if (!out->styles) goto fail;
        memcpy(out->styles, s_text.styles, s_text.style_count * sizeof(text_style_t));
        out->style_count = s_text.style_count;
    }
    if (s_text.image_count) {
        out->images = calloc(s_text.image_count, sizeof(char *));
        if (!out->images)
            goto fail;
        for (size_t i = 0; i < s_text.image_count; i++) {
            out->images[i] = strdup(s_text.images[i]);
            if (!out->images[i])
                goto fail;
            out->image_count++;
        }
    }
    return ESP_OK;
fail:
    html_text_free(out);
    return ESP_ERR_NO_MEM;
}
size_t book_markdown_navigation_count(void) {
    return s_count ? s_visible_count : 1;
}
static size_t entry(size_t index) {
    return index < s_visible_count ? s_visible[index] : SIZE_MAX;
}
esp_err_t book_markdown_navigation_title(size_t index, char *out, size_t cap) {
    if (!out || !cap)
        return ESP_ERR_INVALID_ARG;
    size_t i = entry(index);
    if (!s_count && index == 0) {
        snprintf(out, cap, "全文起点");
        return ESP_OK;
    }
    if (i == SIZE_MAX)
        return ESP_ERR_INVALID_ARG;
    blk_t *b = &s_text.blocks[s_headings[i].block];
    size_t n = b->len < cap - 1 ? b->len : cap - 1;
    while (n < b->len && n && ((unsigned char)s_text.utf8[b->offset + n] & 0xc0) == 0x80)
        n--;
    memcpy(out, s_text.utf8 + b->offset, n);
    out[n] = 0;
    return ESP_OK;
}
size_t book_markdown_navigation_offset(size_t index) {
    size_t i = entry(index);
    return i == SIZE_MAX ? 0 : s_text.blocks[s_headings[i].block].offset;
}
unsigned book_markdown_navigation_depth(size_t index) {
    size_t i = entry(index);
    unsigned d = 0;
    if (i != SIZE_MAX)
        for (size_t p = s_headings[i].parent; p != SIZE_MAX; p = s_headings[p].parent)
            d++;
    return d;
}
bool book_markdown_navigation_children(size_t index) {
    size_t i = entry(index);
    return i != SIZE_MAX && i + 1 < s_count && s_headings[i + 1].parent == i;
}
bool book_markdown_navigation_expanded(size_t index) {
    size_t i = entry(index);
    return i != SIZE_MAX && s_headings[i].expanded;
}
bool book_markdown_navigation_toggle(size_t index) {
    size_t i = entry(index);
    if (!book_markdown_navigation_children(index))
        return false;
    s_headings[i].expanded = !s_headings[i].expanded;
    visibility();
    return true;
}
static int hex(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
bool markdown_attachment_path(const char *document, const char *src, char *out, size_t cap) {
    if (!document || !src || strncmp(document, "/sdcard/markdown/", 17))
        return false;
    char decoded[MARKDOWN_PATH_MAX + 1];
    size_t n = 0;
    if (*src == '<' && src[strlen(src) - 1] == '>')
        src++;
    while (*src && *src != '>' && n < sizeof(decoded) - 1) {
        unsigned char c = *src++;
        if (c == '%') {
            int a = hex(src[0]);
            if (a < 0 || !src[0])
                return false;
            int b = hex(src[1]);
            if (b < 0)
                return false;
            c = (a << 4) | b;
            src += 2;
        }
        if (c < 32 || c == 127 || c == '\\')
            return false;
        decoded[n++] = c;
    }
    if (*src && *src != '>')
        return false;
    decoded[n] = 0;
    if (strncmp(decoded, "attachments/", 12) || !decoded[12] || strchr(decoded + 12, '/') ||
        !strcmp(decoded + 12, ".") || !strcmp(decoded + 12, ".."))
        return false;
    int k = snprintf(out, cap, "/sdcard/markdown/%s", decoded);
    if (k < 0 || (size_t)k >= cap)
        return false;
    struct stat st;
    if (lstat(out, &st) == 0 && !S_ISREG(st.st_mode))
        return false;
    return true;
}
bool book_markdown_attachment(const char *src, char *out, size_t cap) {
    return markdown_attachment_path(s_path, src, out, cap);
}
