/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 将首帧解码为不透明灰阶图。/ Decode the first frame into an opaque grayscale preview.
bool book_gif_gray(const uint8_t *data, size_t size,
                   unsigned out_width, unsigned out_height, uint8_t *out);
