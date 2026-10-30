/* SPDX-License-Identifier: Apache-2.0
 * 中文：流式保存逻辑坐标画面，不分配第二块全屏缓冲。
 * English: Stream a logical screen to PNG without a second full framebuffer.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cyber_beads.h"
/// 保存684x1216、4-bit灰度PNG；不覆盖原图，失败清理临时文件。/ Save a 4-bit gray PNG, never overwrite an artwork, and clean failed staging files.
bool beads_png_save(const uint8_t *fb,char *path,size_t capacity);
/// 恢复最近保存的可编辑画布，校验失败不改内存。/ Restore the latest valid editable board; failed validation leaves memory unchanged.
bool beads_board_load(cyber_beads_t *board);
/// 双槽保存画布，失败时保留上一份有效状态。/ Save alternating slots, retaining the previous valid board on failure.
bool beads_board_save(const cyber_beads_t *board);
