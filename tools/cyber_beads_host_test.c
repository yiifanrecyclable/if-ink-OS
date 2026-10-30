/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际C状态机的撤回、清空、保存与越界回归。
 * English: Test the production C state machine for undo, clearing, saving and bounds.
 */
#include "cyber_beads.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    cyber_beads_t board;cyber_beads_reset(&board);
    assert(!cyber_beads_dirty(&board)&&cyber_beads_can_save(&board));
    assert(!cyber_beads_place(&board,-1)&&!cyber_beads_place(&board,BEADS_COUNT));
    board.selected=6;assert(!cyber_beads_place(&board,0));board.selected=0;
    assert(cyber_beads_place(&board,0)&&board.cells[0]==0);
    assert(cyber_beads_place(&board,0)&&board.cells[0]==-1);
    assert(cyber_beads_undo(&board)&&board.cells[0]==0);
    board.selected=5;assert(cyber_beads_place(&board,0)&&board.cells[0]==5);
    cyber_beads_saved(&board);assert(!cyber_beads_can_save(&board));
    assert(cyber_beads_clear(&board)&&cyber_beads_dirty(&board));
    assert(!cyber_beads_clear(&board));
    assert(cyber_beads_undo(&board)&&!cyber_beads_dirty(&board));
    cyber_beads_reset(&board);
    for(int i=0;i<40;++i)assert(cyber_beads_place(&board,i));
    assert(board.steps==32);
    for(int i=0;i<32;++i)assert(cyber_beads_undo(&board));
    assert(!cyber_beads_undo(&board));
    for(int i=0;i<40;++i)assert(board.cells[i]==(i<8?0:-1));
    puts("PASS: C bead model, bounds, toggle/replace, 32-step undo, clear and saved-state equality");
}
