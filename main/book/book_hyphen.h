/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 拉丁字母及常见重音字母。/ Latin letters, including common precomposed accented letters.
static inline bool book_latin_letter(uint32_t cp) {
    uint32_t ascii = cp | 32u;
    return (ascii >= 'a' && ascii <= 'z') ||
           (cp >= 0xc0 && cp <= 0x24f && cp != 0xd7 && cp != 0xf7) ||
           (cp >= 0x1e00 && cp <= 0x1eff) || (cp >= 0xfb00 && cp <= 0xfb06);
}

static inline bool book_latin_apostrophe(uint32_t cp) {
    return cp == '\'' || cp == 0x2019;
}

static inline bool book_word_part(uint32_t cp) {
    return book_latin_letter(cp) || (cp >= '0' && cp <= '9') || (cp >= 0x300 && cp <= 0x36f);
}

// 输入已由正文解析器校验；仍检查当前字段的边界。/ Input is validated prose; still respect the current field's bounds.
static inline uint32_t book_word_cp(const char *p, const char *end, size_t *bytes) {
    unsigned char lead = (unsigned char)*p;
    size_t n = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    if ((size_t)(end - p) < n) { *bytes = 1; return 0; }
    uint32_t cp = n == 1 ? lead : lead & (0x7f >> n);
    for (size_t i = 1; i < n; ++i) cp = (cp << 6) | ((unsigned char)p[i] & 0x3f);
    *bytes = n;
    return cp;
}

static inline bool book_word_connector(uint32_t cp) {
    switch (cp) {
    case '-': case '_': case '.': case ':': case '/': case '@': case '?':
    case '&': case '=': case '%': case '+': case '#': case 0xad: return true;
    default: return book_latin_apostrophe(cp);
    }
}

static inline bool book_word_joiner(uint32_t cp, const char *after, const char *end) {
    if (!book_word_connector(cp)) return false;
    for (unsigned steps = 0; steps < 4 && after < end; ++steps) {
        size_t bytes;
        uint32_t next = book_word_cp(after, end, &bytes);
        if (book_word_part(next)) return true;
        if (book_latin_apostrophe(cp) || !book_word_connector(next)) break;
        after += bytes;
    }
    return false;
}

static inline bool book_word_before(const char *start, const char *pos) {
    if (pos <= start) return false;
    const char *p = pos - 1;
    while (p > start && ((unsigned char)*p & 0xc0) == 0x80) --p;
    size_t bytes;
    uint32_t cp = book_word_cp(p, pos, &bytes);
    return book_word_part(cp) || book_word_connector(cp);
}

typedef struct { const char *first, *last; } book_hyphen_range_t;

static inline book_hyphen_range_t book_hyphen_range(const char *start, const char *end) {
    enum { MIN_SIDE = 3, MAX_LETTERS = 70, MAX_BYTES = 160 };
    size_t letters = 0;
    const char *first = NULL, *tail[MIN_SIDE] = {NULL};
    for (const char *p = start; p < end;) {
        size_t bytes;
        uint32_t cp = book_word_cp(p, end, &bytes);
        if (!book_latin_letter(cp)) {
            if (book_word_part(cp) || book_word_joiner(cp, p + bytes, end)) return (book_hyphen_range_t){0};
            break;
        }
        if (letters == MAX_LETTERS || (size_t)(p - start) + bytes > MAX_BYTES)
            return (book_hyphen_range_t){0};
        tail[letters++ % MIN_SIDE] = p;
        p += bytes;
        if (letters == MIN_SIDE) first = p;
    }
    return letters >= 2 * MIN_SIDE ? (book_hyphen_range_t){first, tail[letters % MIN_SIDE]} : (book_hyphen_range_t){0};
}

static inline bool book_hyphen_fits_range(book_hyphen_range_t range, const char *pos) {
    return range.first && pos >= range.first && pos <= range.last;
}
