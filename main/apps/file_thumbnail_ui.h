/* SPDX-License-Identifier: Apache-2.0
 * 中文：文件浏览和图片选择器共用的灰阶缩略图绘制。
 * English: Shared grayscale thumbnail painting for file browsing and image pickers.
 */
#pragma once
#include "book_cover.h"
#include "epdiy.h"

static inline bool file_thumbnail_draw(uint8_t *fb, EpdRect box, const uint8_t *pixels,
                                       unsigned source_width, unsigned source_height) {
    if (!pixels || !source_width || !source_height || box.width <= 0 || box.height <= 0) return false;
    unsigned width, height;
    book_cover_fit_size(source_width, source_height, (unsigned)box.width, (unsigned)box.height, &width, &height);
    int left = box.x + (box.width - (int)width) / 2;
    int top = box.y + (box.height - (int)height) / 2;
    for (unsigned y = 0; y < height; ++y) {
        size_t row = (size_t)(y * source_height / height) * source_width;
        for (unsigned x = 0; x < width; ++x) {
            size_t pixel = row + x * source_width / width;
            uint8_t byte = pixels[pixel / 2];
            uint8_t shade = pixel & 1 ? byte & 15 : byte >> 4;
            epd_draw_pixel(left + (int)x, top + (int)y, (uint8_t)(shade << 4), fb);
        }
    }
    return true;
}
