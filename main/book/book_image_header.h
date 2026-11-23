/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- JPEG 帧头 / JPEG frame header ---- */

// 读 SOF 标记与帧尺寸，只走标记段、不解码。
// 0xC0 是基线，0xC2 是渐进式；ROM TJpgDec 只解基线，所以渐进式要交给 JPEGDEC。
// Walk the marker segments and read the SOF marker plus the frame size. 0xC0 is baseline and
// 0xC2 is progressive; the ROM TJpgDec decodes baseline only, so progressive frames go to
// JPEGDEC.
static inline bool book_jpeg_frame(const uint8_t *data, size_t size, uint8_t *sof,
                       unsigned *width, unsigned *height, uint8_t *components) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    size_t at = 2;
    while (at + 1 < size) {
        if (data[at] != 0xFF) { ++at; continue; }   // 段间的填充字节 / fill bytes between segments
        uint8_t marker = data[at + 1];
        if (marker == 0xFF) { ++at; continue; }
        // 无长度字段的标记。/ Markers without a length field.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) { at += 2; continue; }
        if (at + 4 > size) return false;
        size_t length = ((size_t)data[at + 2] << 8) | data[at + 3];
        if (length < 2) return false;
        // SOF0..SOF15，跳过 DHT(0xC4)、JPG(0xC8)、DAC(0xCC)。
        // SOF0..SOF15, skipping DHT (0xC4), JPG (0xC8) and DAC (0xCC).
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (at + 9 > size || (components && (length < 8 || at + 10 > size))) return false;
            if (height) *height = ((unsigned)data[at + 5] << 8) | data[at + 6];
            if (width) *width = ((unsigned)data[at + 7] << 8) | data[at + 8];
            if (sof) *sof = marker;
            if (components) *components = data[at + 9];
            return true;
        }
        at += 2 + length;
    }
    return false;
}

static inline bool book_png_frame(const uint8_t *data, size_t size, unsigned *width, unsigned *height) {
    static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};
    if (size < 24 || memcmp(data, signature, 8) || memcmp(data + 12, "IHDR", 4)) return false;
    *width = (uint32_t)data[16] << 24 | (uint32_t)data[17] << 16 | (uint32_t)data[18] << 8 | data[19];
    *height = (uint32_t)data[20] << 24 | (uint32_t)data[21] << 16 | (uint32_t)data[22] << 8 | data[23];
    return true;
}

static inline bool book_gif_signature(const uint8_t *data, size_t size) {
    return data && size >= 6 && (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6));
}

static inline bool book_gif_frame(const uint8_t *data, size_t size, unsigned *width, unsigned *height) {
    if (size < 13 || !book_gif_signature(data, size)) return false;
    unsigned w = data[6] | (unsigned)data[7] << 8;
    unsigned h = data[8] | (unsigned)data[9] << 8;
    // 限制解码工作量，不受 ZIP 压缩后大小影响。/ Bound decode work independently of the compressed ZIP entry size.
    if (!w || !h || w > 8192 || h > 8192 || w * h > 8u * 1024u * 1024u) return false;
    *width = w;
    *height = h;
    return true;
}
