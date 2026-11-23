/* SPDX-License-Identifier: Apache-2.0
 * 中文：Markdown 书源、折叠大纲和安全的本地附件定位。
 * English: Markdown source, collapsible outline and safe local attachment resolution.
 */
#pragma once
#include "html_text.h"
#include "content_language.h"
esp_err_t book_markdown_open(const char *path);
void book_markdown_close(void);
bool book_markdown_active(void);
content_language_t book_markdown_language(void);
esp_err_t book_markdown_load(html_text_t *out);
size_t book_markdown_length(void);
size_t book_markdown_navigation_count(void);
esp_err_t book_markdown_navigation_title(size_t index, char *out, size_t cap);
size_t book_markdown_navigation_offset(size_t index);
unsigned book_markdown_navigation_depth(size_t index);
bool book_markdown_navigation_children(size_t index);
bool book_markdown_navigation_expanded(size_t index);
bool book_markdown_navigation_toggle(size_t index);
bool book_markdown_attachment(const char *src, char *out, size_t cap);
bool markdown_attachment_path(const char *document, const char *src, char *out, size_t cap);
