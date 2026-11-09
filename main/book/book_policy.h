/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读进度换算，不访问硬件；晃动翻页按用户要求移除。/ Reading position mapping without hardware access; shake turning removed at user request.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOK_PX_MIN 36
#define BOOK_PX_MAX 100

static inline bool book_text_size_valid(unsigned px) {
    return px >= BOOK_PX_MIN && px <= BOOK_PX_MAX;
}

/// UTF8章内偏移映射回文件进度，避免GBK字节单位混用。/ Map UTF8 chapter offsets to source bytes, including GBK.
static inline uint32_t book_position_bytes(uint32_t start, uint32_t end, size_t off, size_t len) {
    if (end < start || !len) return start;
    if (off > len) off = len;
    return start + (uint64_t)(end - start) * off / len;
}
