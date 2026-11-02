#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real Files → Image → Files callbacks and browser round trips retain directory/page/mode.
中文：真实跨页回调与网页重放均保留图片来源目录、分页和显示模式。
"""
from pathlib import Path
import os,json,subprocess,tempfile,sys
from PIL import Image
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='image-return-',dir=R/'build') as tmp:
 work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
 for tree in ('pictures','banner pic'):
  folder=card/tree/'album';folder.mkdir(parents=True)
  for i in range(15):Image.new('L',(684,1216),80+i*5).save(folder/f'{i:02}.png')
 for f in ('books','fonts','markdown'):(card/f).mkdir()
 (out/'titles.txt').touch();ns=out/'nvs/read_pico';ns.mkdir(parents=True)
 def frame(directory,mode=0,page=0,events='',point=None,size=120):
  (ns/'sys_size').write_bytes(bytes([size]));env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_FILES_GRID':str(mode),'PICO_SIM_FILES_PAGE':str(page)}
  if point:env['PICO_TEST_IMAGE_FILE_RETURN']=point
  p=subprocess.run([str(R/'build/simulator/render'),'files-0','','3','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,directory],cwd=work,env=env,capture_output=True,text=True,timeout=30)
  assert p.returncode==0,p.stderr[-1200:]
  return json.loads(p.stdout),(out/'frame.bin').read_bytes()
 for size in (100,160):
  for tree,mode in (('pictures',0),('pictures',1),('banner pic',0),('banner pic',2)):
   directory='/sdcard/'+tree+'/album';base,fb=frame(directory,mode,1,size=size)
   if mode:
    _,_,left,top,w,h,_=base['files_grid_geometry'];x,y=left+w//2,top+h//2
   else:x,y=180,190+base['files_row_height']//2
   for close in ('0,0','342,608','683,1215'):
    state,after=frame(directory,mode,1,f't,{x},{y},{x},{y}',close,size)
    assert state['files_dir']==directory and state['files_page']==1 and state['files_grid_modes']==mode,state
    assert after[56*342:]==fb[56*342:] # Same directory layout and active Apps tab after a real return.
 # Exercise independent renderer processes through the real browser bridge, not just stored UI metadata.
 sys.path.insert(0,str(R/'tools/simulator'));import server
 class OfflineCloud:
  def snapshot(self):return {'busy':False,'revision':0}
 server.ROOT=work;server.BUILD=out;server.CARD=card;server.BOOKS=work/'prototype';server.BOOKS.mkdir();server.FIXTURES=work/'fixtures';server.FIXTURES.mkdir();server.CLOUD_PRIVATE=work/'cloud';server.CLOUD_PRIVATE.mkdir();server.STORES={'nvs':out/'nvs','vfs':out/'vfs','card':card}
 (out/'render').symlink_to(R/'build/simulator/render');os.environ['PICO_SIM_CARD_ROOT']=str(card)
 server.prepare=lambda root:None;server.CloudController=lambda **kwargs:OfflineCloud();server.Preview.sources_signature=lambda self:'image-return'
 root,_=frame('/sdcard');pictures,_=frame('/sdcard/pictures');grid,_=frame('/sdcard/pictures/album',1,1)
 p=server.Preview(build_native=False)
 def gesture(x,y):return p.action({'action':'gesture','type':'t','x':x,'y':y,'xx':x,'yy':y})
 index=next(i for i,item in enumerate(root['files_items']) if item['path']=='/sdcard/pictures')
 p.action({'action':'view','view':'files-card'});gesture(180,190+index*root['files_row_height']+root['files_row_height']//2)
 gesture(180,190+pictures['files_row_height']//2)
 gesture(612,83);p.action({'action':'key','key':2});assert p.state['files_page']==1
 _,_,x,y,w,h,_=grid['files_grid_geometry'];gesture(x+w//2,y+h//2);assert p.state['screen']=='image-view'
 gesture(683,1215);assert p.state['screen']=='files-card' and p.state['files_dir']=='/sdcard/pictures/album' and p.state['files_page']==1 and p.state['files_grid']==1,p.state
print('PASS: arbitrary image taps return through real callbacks and browser processes to the original directory, page and list/thumbnail mode; Apps navigation stays selected.')
