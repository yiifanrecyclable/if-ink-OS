/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline bool ttf_cjk_spacing_point(uint32_t cp) {
    return (cp >= 0x2e80 && cp <= 0xffef) || (cp >= 0x2018 && cp <= 0x201d);
}

/// 英文只扩展词间的普通空格。/ English expansion uses ordinary interword spaces.
static inline bool ttf_justify_gap(uint32_t previous, uint32_t next, bool word_spaces) {
    if (word_spaces) return previous == ' ' && next != ' ';
    return ttf_cjk_spacing_point(previous) || ttf_cjk_spacing_point(next);
}

/// 余量过大的短行保持自然宽度。/ Leave sparse lines at their natural width.
static inline bool ttf_justify_fits(int delta, int gaps, int px, bool word_spaces) {
    if (delta < 0) return gaps >= 2 && -delta <= gaps * 3;
    return gaps >= (word_spaces ? 1 : 2) && delta <= gaps * (word_spaces ? px / 2 : 12);
}
