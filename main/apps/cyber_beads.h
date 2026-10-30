/* SPDX-License-Identifier: Apache-2.0
 * 中文：固定大小的拼豆状态与32步撤回，不依赖屏幕或文件系统。
 * English: Bounded bead state and 32-step undo, independent of display and storage.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BEADS_COLS 17
#define BEADS_ROWS 27
#define BEADS_COUNT (BEADS_COLS * BEADS_ROWS)
#define BEADS_UNDO 32
typedef struct {
    int8_t cells[BEADS_COUNT], saved[BEADS_COUNT], history[BEADS_UNDO][BEADS_COUNT];
    unsigned selected, steps;
    bool has_saved;
} cyber_beads_t;
/// 清空新画布。/ Initialize an empty canvas.
void cyber_beads_reset(cyber_beads_t *board);
/// 同色消除，异色替换；越界不修改。/ Toggle matching beads or replace colors; reject out-of-bounds input.
bool cyber_beads_place(cyber_beads_t *board, int index);
/// 清空作为一步撤回。/ Clear as one undo step.
bool cyber_beads_clear(cyber_beads_t *board);
/// 恢复最近一步。/ Restore the latest edit.
bool cyber_beads_undo(cyber_beads_t *board);
/// 判断相对已保存画面的变化。/ Compare against the last saved image.
bool cyber_beads_dirty(const cyber_beads_t *board);
/// 空白首次可存，已保存的相同画面不重复存。/ Allow the first blank save, but no unchanged duplicate.
bool cyber_beads_can_save(const cyber_beads_t *board);
/// 仅在文件提交成功后记录快照。/ Mark saved only after committing the file.
void cyber_beads_saved(cyber_beads_t *board);
