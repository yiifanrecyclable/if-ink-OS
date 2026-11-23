/* SPDX-License-Identifier: Apache-2.0
 * 中文：从已读取的正文判断字体档，保守处理混排与短文本。
 * English: Choose a font profile from text already in memory; conservative on mixed or short text.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CONTENT_LANGUAGE_UNKNOWN,
    CONTENT_LANGUAGE_ENGLISH,
    CONTENT_LANGUAGE_COMPATIBLE
} content_language_t;

typedef struct {
    uint32_t word, evidence;
    unsigned letters, length;
    bool incompatible;
} content_language_probe_t;

// At most four ASCII letters, packed for cheap comparison during a single text pass.
#define LANGUAGE_WORD(a,b,c,d) ((uint32_t)(a)<<24 | (uint32_t)(b)<<16 | (uint32_t)(c)<<8 | (d))
static inline void content_language_end_word(content_language_probe_t *p) {
    if (p->length >= 2 && p->length <= 4) {
        switch (p->word) {
        case LANGUAGE_WORD(0,'t','h','e'): p->evidence |= 1u << 0; break;
        case LANGUAGE_WORD(0,'a','n','d'): p->evidence |= 1u << 1; break;
        case LANGUAGE_WORD(0,'y','o','u'): p->evidence |= 1u << 2; break;
        case LANGUAGE_WORD(0,'w','a','s'): p->evidence |= 1u << 3; break;
        case LANGUAGE_WORD(0,'a','r','e'): p->evidence |= 1u << 4; break;
        case LANGUAGE_WORD(0,'f','o','r'): p->evidence |= 1u << 5; break;
        case LANGUAGE_WORD(0,0,'o','f'): p->evidence |= 1u << 6; break;
        case LANGUAGE_WORD(0,0,'i','s'): p->evidence |= 1u << 7; break;
        case LANGUAGE_WORD('t','h','i','s'): p->evidence |= 1u << 8; break;
        case LANGUAGE_WORD('t','h','a','t'): p->evidence |= 1u << 9; break;
        case LANGUAGE_WORD('w','i','t','h'): p->evidence |= 1u << 10; break;
        case LANGUAGE_WORD('f','r','o','m'): p->evidence |= 1u << 11; break;
        case LANGUAGE_WORD('w','e','r','e'): p->evidence |= 1u << 12; break;
        case LANGUAGE_WORD('h','a','v','e'): p->evidence |= 1u << 13; break;
        }
    }
    p->length = 0;
    p->word = 0;
}
#undef LANGUAGE_WORD

static inline void content_language_character(content_language_probe_t *p, uint32_t cp) {
    if (p->incompatible) return;
    unsigned ascii = cp | 32u;
    if (ascii >= 'a' && ascii <= 'z') {
        if (p->letters < 32) ++p->letters;
        if (p->length < 5) { ++p->length; p->word = (p->word << 8) | ascii; }
        return;
    }
    bool latin = (cp >= 0xc0 && cp <= 0x24f && cp != 0xd7 && cp != 0xf7) ||
                 (cp >= 0x1e00 && cp <= 0x1eff) || (cp >= 0xfb00 && cp <= 0xfb06);
    if (latin) { p->length = 5; return; }
    if (p->length) content_language_end_word(p);
    bool shared = cp < 0xc0 || cp == 0xd7 || cp == 0xf7 || cp == 0xfeff || cp == 0xfffc ||
                  (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x2000 && cp <= 0x206f) ||
                  (cp >= 0x2070 && cp <= 0x22ff) || (cp >= 0x2500 && cp <= 0x27ff) ||
                  (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0x1f300 && cp <= 0x1faff);
    p->incompatible = !shared;
}

static inline content_language_t content_language_finish(content_language_probe_t *p, bool english_hint) {
    if (p->incompatible) return CONTENT_LANGUAGE_COMPATIBLE;
    content_language_end_word(p);
    bool distinct = (p->evidence & (p->evidence - 1)) != 0;
    return ((p->letters >= 20 && distinct) || (english_hint && p->letters >= 8)) ?
        CONTENT_LANGUAGE_ENGLISH : CONTENT_LANGUAGE_UNKNOWN;
}

static inline content_language_t content_language_text(const char *text, size_t size, bool english_hint) {
    content_language_probe_t probe = {0};
    const unsigned char *bytes = (const unsigned char *)text;
    for (size_t i = 0; i < size && !probe.incompatible;) {
        uint32_t cp = bytes[i++];
        if (cp >= 0x80) {
            unsigned more = cp >= 0xc2 && cp <= 0xdf ? 1 : cp >= 0xe0 && cp <= 0xef ? 2 :
                            cp >= 0xf0 && cp <= 0xf4 ? 3 : 0;
            if (!more || size - i < more) return CONTENT_LANGUAGE_COMPATIBLE;
            cp &= 0x7f >> (more + 1);
            for (unsigned j = 0; j < more; ++j) {
                unsigned byte = bytes[i++];
                if ((byte & 0xc0) != 0x80) return CONTENT_LANGUAGE_COMPATIBLE;
                cp = (cp << 6) | (byte & 0x3f);
            }
            if (cp < (more == 1 ? 0x80u : more == 2 ? 0x800u : 0x10000u) ||
                (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return CONTENT_LANGUAGE_COMPATIBLE;
        }
        content_language_character(&probe, cp);
    }
    return content_language_finish(&probe, english_hint);
}
