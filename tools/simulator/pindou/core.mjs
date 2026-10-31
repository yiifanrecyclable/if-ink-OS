/* SPDX-License-Identifier: Apache-2.0
 * 中文：拼豆预览的纯状态规则；不访问页面、网络或真实TF卡。
 * English: Pure bead-preview state rules without DOM, networking or real TF access.
 */
export const WIDTH=684, HEIGHT=1216, TOOLBAR_TOP=1100;
export const COLS=17, ROWS=27, PITCH=39, ORIGIN_X=30, ORIGIN_Y=34;
export const BEAD_GAP=2, BEAD_RADIUS=(PITCH-BEAD_GAP)/2;
export const SIGNATURE_GRAY=6, SIGNATURE_Y=(TOOLBAR_TOP+HEIGHT)/2;
export const SHADES=Object.freeze([13,11,8,5,2,0]);
export const UNDO_LIMIT=32;
export function createBoard(){return {cells:new Int8Array(COLS*ROWS).fill(-1),selected:0,saved:null,history:[]};}
export function equal(a,b){return !!a&&!!b&&a.length===b.length&&a.every((v,i)=>v===b[i]);}
export function dirty(board){return board.saved?!equal(board.cells,board.saved):board.cells.some(v=>v>=0);}
export function canSave(board){return board.saved===null||!equal(board.cells,board.saved);}
export function select(board,index){if(!Number.isInteger(index)||index<0||index>=SHADES.length)return false;board.selected=index;return true;}
export function cellAt(x,y){
  if(!Number.isFinite(x)||!Number.isFinite(y)||x<0||x>=WIDTH||y<0||y>=TOOLBAR_TOP)return -1;
  const col=Math.round((x-ORIGIN_X)/PITCH),row=Math.round((y-ORIGIN_Y)/PITCH);
  if(col<0||col>=COLS||row<0||row>=ROWS)return -1;
  return row*COLS+col;
}
export function place(board,index){
  if(!Number.isInteger(index)||index<0||index>=board.cells.length)return false;
  remember(board,{index,previous:board.cells[index]});
  board.cells[index]=board.cells[index]===board.selected?-1:board.selected;return true;
}
function remember(board,entry){board.history.push(entry);if(board.history.length>UNDO_LIMIT)board.history.shift();}
export function canUndo(board){return board.history.length>0;}
export function canClear(board){return board.cells.some(value=>value>=0);}
export function clearBoard(board){
  if(!canClear(board))return false;
  // 清空是一步撤回；普通落豆只记录索引和旧颜色，限定最多32条历史。
  // Clearing is one undo step; ordinary edits store only an index/color pair, with at most 32 records.
  remember(board,{cells:new Int8Array(board.cells)});board.cells.fill(-1);return true;
}
export function undo(board){
  const entry=board.history.pop();if(!entry)return false;
  if(entry.cells)board.cells.set(entry.cells);else board.cells[entry.index]=entry.previous;
  return true;
}
export function markSaved(board,snapshot=board.cells){board.saved=new Int8Array(snapshot);}
