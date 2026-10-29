/* SPDX-License-Identifier: Apache-2.0
 * 中文：16 灰阶 PNG 流式解码，磁盘缓存和有界视口读取。
 * English: Streaming 16-gray PNG decoding, disk cache and bounded viewport reads.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t width,height; char cache[512]; } document_image_t;
/// 仅读取经CRC校验的尺寸头；分页时不解压或生成缓存。/ Probe CRC-checked dimensions without decoding or creating caches during pagination.
bool document_image_dimensions(const char *path, uint32_t *width, uint32_t *height);
void document_image_remove_cache(const char *source_path);
bool document_image_open(const char *path, document_image_t *image);
bool document_image_sample(const document_image_t *image, uint32_t top, uint32_t span,
                            unsigned width, unsigned height, uint8_t *gray);
#ifdef PICO_HOST_SIMULATOR
// 宿主观察缓存是否命中，不影响设备接口。/ Host-only observation of actual cache decoding.
unsigned document_image_decode_count(void);
#endif
