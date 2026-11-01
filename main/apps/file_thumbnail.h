/* SPDX-License-Identifier: Apache-2.0
 * 中文：文件浏览的小型灰阶缩略图；缓存可丢弃，不接触阅读记录。
 * English: Small grayscale file thumbnails; caches are disposable and never touch reading records.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FILE_THUMB_W 288
#define FILE_THUMB_H 240
#define FILE_THUMB_BYTES ((FILE_THUMB_W*FILE_THUMB_H+1)/2)
/// 等比缩小JPG/PNG，输出紧密高半字节优先的4bpp；仅在UI推屏前调用，非render内。
/// Contain JPG/PNG as tightly packed high-nibble-first 4bpp; call before presentation, outside render.
bool file_thumbnail_load(const char *path,uint8_t out[FILE_THUMB_BYTES],unsigned *width,unsigned *height);
