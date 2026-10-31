/* SPDX-License-Identifier: Apache-2.0
 * 中文：电子拼豆独立网页预览，保存只访问隔离模拟卡。
 * English: Independent bead-app preview; saves target the isolated simulator card only.
 */
import {WIDTH,HEIGHT,COLS,ROWS,PITCH,ORIGIN_X,ORIGIN_Y,BEAD_RADIUS,SIGNATURE_GRAY,SIGNATURE_Y,SHADES,createBoard,dirty,canSave,select,cellAt,place,markSaved,canUndo,canClear,undo,clearBoard} from './core.mjs';
const $=id=>document.getElementById(id),canvas=$('board'),ctx=canvas.getContext('2d');
let board=createBoard(),active=true,busy=false,contact=null,toastTimer=null,previousFocus=null,dialogMode=null;
const colors=[];
function gray(level){return `rgb(${level*17},${level*17},${level*17})`;}
function drawCell(context,index,cells){
  const x=ORIGIN_X+(index%COLS)*PITCH,y=ORIGIN_Y+Math.floor(index/COLS)*PITCH;
  context.clearRect(x-Math.floor(PITCH/2),y-Math.floor(PITCH/2),PITCH,PITCH);context.fillStyle='#fff';context.fillRect(x-Math.floor(PITCH/2),y-Math.floor(PITCH/2),PITCH,PITCH);
  if(cells[index]<0){context.fillStyle='#999999';context.beginPath();context.arc(x,y,2.5,0,Math.PI*2);context.fill();return;}
  context.fillStyle=gray(SHADES[cells[index]]);
  context.beginPath();context.arc(x,y,BEAD_RADIUS,0,Math.PI*2);context.fill();
  context.fillStyle='#fff';
  context.beginPath();context.arc(x,y,7.5,0,Math.PI*2);context.fill();
}
function drawBoard(context,cells,exporting=false){
  context.fillStyle='#fff';context.fillRect(0,0,WIDTH,HEIGHT);
  for(let i=0;i<COLS*ROWS;i++)drawCell(context,i,cells);
  if(exporting){context.fillStyle=gray(SIGNATURE_GRAY);context.font='24px Pico, sans-serif';context.textAlign='center';context.textBaseline='middle';context.fillText('if-ink OS @ Pico',WIDTH/2,SIGNATURE_Y);}
}
function update(){
  for(let i=0;i<colors.length;i++){colors[i].setAttribute('aria-pressed',String(i===board.selected));colors[i].disabled=busy||!active;}
  $('save').disabled=busy||!active||!canSave(board);$('exit').disabled=busy||!active;
  $('undo').disabled=busy||!active||!canUndo(board);$('clear').disabled=busy||!active||!canClear(board);
  $('stateLabel').textContent=busy?'正在保存…':dirty(board)?'尚未保存 · 点按同色移除，换色替换':'点按放置 · 同色再点移除 · 换色点按替换';
  for(const id of ['saveExit','discardExit','continue'])$(id).disabled=busy;
}
for(let i=0;i<SHADES.length;i++){
  const button=document.createElement('button');button.className='color';button.type='button';button.title=`灰度 ${i+1}`;button.setAttribute('aria-label',`选择第${i+1}阶灰度`);
  button.innerHTML=`<svg viewBox="0 0 64 64" aria-hidden="true"><circle cx="32" cy="32" r="27" fill="${gray(SHADES[i])}"/><circle cx="32" cy="32" r="11" fill="white"/></svg>`;
  button.onclick=()=>{if(!busy&&active){select(board,i);update();}};$('palette').append(button);colors.push(button);
}
function point(event){const r=canvas.getBoundingClientRect();return {x:(event.clientX-r.left)*WIDTH/r.width,y:(event.clientY-r.top)*HEIGHT/r.height};}
function clearContact(){const c=contact;contact=null;if(c&&canvas.hasPointerCapture(c.id))canvas.releasePointerCapture(c.id);return c;}
canvas.onpointerdown=event=>{
  if(busy||!active||!$('exitDialog').hidden||contact||event.button!==0||!event.isPrimary)return;
  const p=point(event),index=cellAt(p.x,p.y);if(index<0)return;event.preventDefault();
  contact={...p,id:event.pointerId,index,moved:false};canvas.setPointerCapture(event.pointerId);
};
canvas.onpointermove=event=>{if(contact?.id!==event.pointerId)return;const p=point(event);if(Math.hypot(p.x-contact.x,p.y-contact.y)>24)contact.moved=true;};
canvas.onpointerup=event=>{
  if(contact?.id!==event.pointerId)return;const p=point(event),c=clearContact();
  if(c.moved||Math.hypot(p.x-c.x,p.y-c.y)>24||cellAt(p.x,p.y)!==c.index)return;
  if(place(board,c.index)){drawCell(ctx,c.index,board.cells);update();}
};
canvas.onpointercancel=canvas.onlostpointercapture=event=>{if(contact?.id===event.pointerId)clearContact();};
canvas.oncontextmenu=event=>event.preventDefault();window.addEventListener('blur',clearContact);
function toast(message){clearTimeout(toastTimer);$('toast').textContent=message;$('toast').hidden=false;toastTimer=setTimeout(()=>$('toast').hidden=true,3500);}
async function save(){
  if(busy||!active)return false;if(!canSave(board))return true;
  busy=true;clearContact();update();const snapshot=new Int8Array(board.cells);
  try{
    await document.fonts.ready;
    const image=document.createElement('canvas');image.width=WIDTH;image.height=HEIGHT;drawBoard(image.getContext('2d'),snapshot,true);
    const pixels=image.getContext('2d').getImageData(0,0,WIDTH,HEIGHT).data,packed=new Uint8Array(WIDTH*HEIGHT/2);
    for(let i=0;i<WIDTH*HEIGHT;i++){const shade=Math.max(0,Math.min(15,Math.round(pixels[i*4]/17)));packed[i>>1]|=shade<<(i%2?0:4);}
    const response=await fetch('/pindou/save',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:packed});
    const result=await response.json();if(!response.ok||!result.ok)throw Error(result.error||'保存失败，请重试');
    markSaved(board,snapshot);$('savedName').textContent=result.path;$('download').href=result.url;$('download').download=result.name;$('savedFile').hidden=false;
    toast('已保存');return true;
  }catch(error){toast(error.message||'保存失败，作品已保留');$('dialogMessage').textContent='保存失败，作品已保留，请重试。';return false;}
  finally{busy=false;update();}
}
function closeDialog(){if(busy)return;$('exitDialog').hidden=true;dialogMode=null;previousFocus?.focus();}
function leave(){clearContact();active=false;dialogMode=null;$('exitDialog').hidden=true;$('launcher').hidden=false;$('tools').hidden=true;$('toast').hidden=true;board=createBoard();update();$('start').focus();}
function openDialog(mode){
  clearContact();dialogMode=mode;previousFocus=document.activeElement;
  $('dialogTitle').textContent=mode==='clear'?'清空画布？':'作品尚未保存';
  $('dialogMessage').textContent=mode==='clear'?'清空后可用撤回恢复。':'保存后再退出吗？';
  $('saveExit').hidden=mode==='clear';$('discardExit').textContent=mode==='clear'?'清空':'不保存退出';
  $('continue').textContent=mode==='clear'?'取消':'继续编辑';$('exitDialog').hidden=false;$('continue').focus();
}
function requestExit(){
  if(busy||!active)return;if(!dirty(board)){leave();return;}
  openDialog('exit');
}
$('save').onclick=save;$('exit').onclick=$('backKey').onclick=$('homeKey').onclick=requestExit;
$('undo').onclick=()=>{if(busy||!active||!$('exitDialog').hidden)return;clearContact();if(undo(board)){drawBoard(ctx,board.cells);update();}};
$('clear').onclick=()=>{if(!busy&&active&&canClear(board))openDialog('clear');};
$('continue').onclick=closeDialog;$('discardExit').onclick=()=>{
  if(busy)return;
  if(dialogMode==='clear'){clearBoard(board);drawBoard(ctx,board.cells);closeDialog();update();}
  else leave();
};$('saveExit').onclick=async()=>{if(await save())leave();};
$('start').onclick=()=>{board=createBoard();active=true;$('launcher').hidden=true;$('tools').hidden=false;drawBoard(ctx,board.cells);update();};
document.addEventListener('keydown',event=>{
  if(event.key==='Escape'){event.preventDefault();if(!$('exitDialog').hidden)closeDialog();else requestExit();}
  if(event.key==='Tab'&&!$('exitDialog').hidden){const options=[$('saveExit'),$('discardExit'),$('continue')].filter(button=>!button.hidden),index=options.indexOf(document.activeElement);event.preventDefault();options[(index+(event.shiftKey?-1:1)+options.length)%options.length].focus();}
});
window.addEventListener('beforeunload',event=>{if(active&&(busy||dirty(board))){event.preventDefault();event.returnValue='';}});
drawBoard(ctx,board.cells);update();
