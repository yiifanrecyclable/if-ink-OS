/* SPDX-License-Identifier: Apache-2.0
 * 中文：拼豆状态机，所有历史记录均有上限。
 * English: Bead state machine with strictly bounded edit history.
 */
#include "cyber_beads.h"
#include <string.h>
void cyber_beads_reset(cyber_beads_t *b) { memset(b,0,sizeof(*b)); memset(b->cells,-1,BEADS_COUNT); }
static void remember(cyber_beads_t *b) {
    if(b->steps==BEADS_UNDO) {
        memmove(b->history,b->history+1,(BEADS_UNDO-1)*BEADS_COUNT);
        --b->steps;
    }
    memcpy(b->history[b->steps++],b->cells,BEADS_COUNT);
}
bool cyber_beads_place(cyber_beads_t *b,int index) {
    if(index<0||index>=BEADS_COUNT||b->selected>=6)return false;
    remember(b); b->cells[index]=b->cells[index]==(int)b->selected?-1:(int8_t)b->selected; return true;
}
bool cyber_beads_clear(cyber_beads_t *b) {
    bool any=false;for(int i=0;i<BEADS_COUNT;++i)if(b->cells[i]>=0){any=true;break;}
    if(!any)return false;
    remember(b);memset(b->cells,-1,BEADS_COUNT);return true;
}
bool cyber_beads_undo(cyber_beads_t *b) {
    if(!b->steps)return false;
    memcpy(b->cells,b->history[--b->steps],BEADS_COUNT);return true;
}
bool cyber_beads_dirty(const cyber_beads_t *b) {
    if(b->has_saved)return memcmp(b->cells,b->saved,BEADS_COUNT)!=0;
    for(int i=0;i<BEADS_COUNT;++i)if(b->cells[i]>=0)return true;
    return false;
}
bool cyber_beads_can_save(const cyber_beads_t *b) { return !b->has_saved||cyber_beads_dirty(b); }
void cyber_beads_saved(cyber_beads_t *b) { memcpy(b->saved,b->cells,BEADS_COUNT);b->has_saved=true; }
