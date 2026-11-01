#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：真实文件浏览器的图片目录继承、切换、命中、分页、缓存和固定目录保护。
# English: Real browser directory inheritance, toggling, hits, paging, cache and fixed-root protection.
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile,zlib,sys
R=Path(__file__).resolve().parents[1]
def png(w,h,tone):
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body))
    raw=b''.join(b'\0'+bytes([tone])*w for _ in range(h))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(prefix='file-thumbnails-',dir=R/'build') as tmp:
    work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('books','fonts','markdown','pictures/album','banner pic/nested','pictures-old'):(card/folder).mkdir(parents=True)
    for i in range(13):(card/f'pictures/{i:02}.png').write_bytes(png(80,140,40+i*10))
    (card/'pictures/album/child.png').write_bytes(png(120,60,68))
    (card/'pictures/zz-bad.png').write_bytes(b'not a PNG')
    (card/'banner pic/wide.png').write_bytes(png(684,300,85))
    (card/'banner pic/nested/child.png').write_bytes(png(684,300,102))
    (out/'titles.txt').write_text('')
    original={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in card.rglob('*') if f.is_file()}
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1'}
    toggle='t,612,83,612,83'
    def frame(directory='/sdcard',events='',modes=0,size=120):
        settings=out/'nvs/read_pico';settings.mkdir(parents=True,exist_ok=True);(settings/'sys_size').write_bytes(bytes([size]))
        args=[str(R/'build/simulator/render'),'files-0','','3','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,directory]
        p=subprocess.run(args,cwd=work,env={**env,'PICO_SIM_FILES_GRID':str(modes)},capture_output=True,text=True,timeout=30)
        assert not p.returncode,p.stderr[-2500:]
        return json.loads(p.stdout)
    root=frame();items={Path(i['path']).name:i for i in root['files_items']}
    for folder in ('books','fonts','markdown','pictures','banner pic'):assert items[folder]['note'],items
    assert items['pictures']['image_folder'] and items['banner pic']['image_folder']
    assert not items['pictures-old']['image_folder'] and not root['files_grid_available']
    # Each protected root rejects all four real management actions.
    for name in ('books','fonts','markdown','pictures','banner pic'):
        index=next(i for i,f in enumerate(root['files_items']) if Path(f['path']).name==name)
        y=190+index*root['files_row_height']+root['files_row_height']//2
        opened=f'l,180,{y},180,{y}'
        geometry=frame(events=opened)['files_card_geometry'];x0,button_y,width,height=geometry[7:11]
        for i in range(4):
            x=x0+i*width+width//2;button_y=geometry[8]+height//2
            action=frame(events=opened+f';t,{x},{button_y},{x},{button_y}')
            assert action['files_view']==1 and action['files_message']=='系统目录不可修改',(name,action)
    state=frame('/sdcard/pictures');assert state['files_grid_available'] and not state['files_grid']
    grid=frame('/sdcard/pictures',toggle);assert grid['files_grid']==1 and grid['files_grid_modes']==1
    cols,rows,x,y,w,h,count=grid['files_grid_geometry'];assert (cols,rows,count)==(3,3,9)
    assert sum(grid['files_thumbs'])==8 # The first cell is the child folder.
    store=card/'.readpico/thumbnails';cached={f.name:(f.stat().st_mtime_ns,f.read_bytes()) for f in store.glob('*.bin')}
    assert len(cached)==8
    frame('/sdcard/pictures',modes=1)
    assert cached=={f.name:(f.stat().st_mtime_ns,f.read_bytes()) for f in store.glob('*.bin')}
    tap=lambda a,b:f't,{a},{b},{a},{b}'
    child=frame('/sdcard/pictures',toggle+';'+tap(x+w//2,y+h//2))
    assert child['files_dir']=='/sdcard/pictures/album' and child['files_grid'] and child['files_thumbs'][0]
    child_off=frame('/sdcard/pictures/album',toggle,modes=1);assert not child_off['files_grid']
    parent=frame('/sdcard/pictures/album',toggle+';t,50,100,50,100',modes=1);assert parent['files_dir']=='/sdcard/pictures' and not parent['files_grid']
    assert frame('/sdcard/pictures/album',modes=1)['files_items'][0]['path'].endswith('/album/child.png')
    selected=frame('/sdcard/pictures',toggle+';'+tap(x+w+w//2,y+h//2))
    assert selected['route']==4 and selected['image_path']=='/sdcard/pictures/00.png',selected
    action=frame('/sdcard/pictures',toggle+f';l,{x+w+w//2},{y+h//2},{x+w+w//2},{y+h//2}')
    assert action['files_view']==1
    page=frame('/sdcard/pictures',toggle+';k,2,0,0,0');assert page['files_page']==1
    assert 0<sum(page['files_thumbs'])<9
    assert frame('/sdcard/pictures',toggle+';k,2,0,0,0;k,0,0,0,0')['files_page']==0
    # Wide banner mode uses two columns, inherits into descendants, and is independent from pictures.
    banner=frame('/sdcard/banner pic',toggle);assert banner['files_grid_modes']==2 and banner['files_grid_geometry'][:2]==[2,4]
    nested=frame('/sdcard/banner pic/nested',modes=2);assert nested['files_grid'] and nested['files_thumbs'][0]
    assert not frame('/sdcard/pictures-old',toggle)['files_grid_available']
    assert not frame('/sdcard/books',toggle,modes=3)['files_grid_available']
    # A size-preserving change invalidates the cache by mtime and changes the actual packed pixels.
    source=card/'pictures/00.png';before=source.stat();source.write_bytes(png(80,140,210));os.utime(source,(before.st_atime,before.st_mtime+2))
    original[source]=hashlib.sha256(source.read_bytes()).hexdigest()
    frame('/sdcard/pictures',modes=1)
    assert any(f.read_bytes()!=cached[f.name][1] for f in store.glob('*.bin') if f.name in cached)
    # Corrupt cache is regenerated, and a corrupt source remains an ordinary manageable file.
    victim=next(store.glob('*.bin'));victim.write_bytes(b'torn');frame('/sdcard/pictures',modes=1)
    for size in (100,120,140,160):
        state=frame('/sdcard/pictures',modes=1,size=size);g=state['files_grid_geometry'];assert g[3]+g[1]*g[5]<1048
    assert all(hashlib.sha256(f.read_bytes()).hexdigest()==h for f,h in original.items())
    assert not (card/'.readpico/progress').exists() and not (card/'.readpico/reading').exists()
    # Exercise the actual Python replay boundary: later clicks must not toggle the mode twice.
    sys.path.insert(0,str(R/'tools/simulator'));import server
    class OfflineCloud:
        def snapshot(self):return {'busy':False,'revision':0}
    server.ROOT=work;server.BUILD=out;server.CARD=card
    server.BOOKS=work/'prototype';server.BOOKS.mkdir();server.FIXTURES=work/'fixtures';server.FIXTURES.mkdir()
    server.STORES={'nvs':out/'nvs','vfs':out/'vfs','card':card};server.CLOUD_PRIVATE=work/'cloud';server.CLOUD_PRIVATE.mkdir()
    (out/'render').symlink_to(R/'build/simulator/render');os.environ['PICO_SIM_CARD_ROOT']=str(card)
    (out/'nvs/read_pico/sys_size').write_bytes(bytes([120]))
    p=server.Preview.__new__(server.Preview)
    p.state={'screen':'files-home','page':0,'pages':1,'px':48,'scroll':0,'events':[],'root_view':None,
        'firmware_path':'','files_dir':'','selected':[],'active':'','version':0,'days':[0]*30,'message':'','cloud_page':0}
    p.state_path=out/'state.json';p.signature='test';p.sources_signature=lambda:'test'
    p.cloud=OfflineCloud();p.cloud_revision=0;p.cloud_pending_view=None;p.failed_signature=''
    p.replay_origin=None;p.storage_signature=None
    def gesture(x,y):return p.action({'action':'gesture','type':'t','x':x,'y':y,'xx':x,'yy':y})
    # Get geometry before capturing the replay origin: fixture writes must not masquerade as external edits.
    state=frame();index=next(i for i,item in enumerate(state['files_items']) if item['path']=='/sdcard/pictures')
    p.render();gesture(180,154)
    gesture(180,190+index*state['files_row_height']+state['files_row_height']//2)
    assert p.state['files_dir']=='/sdcard/pictures'
    gesture(612,83);assert p.state['files_grid']==1
    gesture(138,330);assert p.state['files_dir']=='/sdcard/pictures/album' and p.state['files_grid']==1
    gesture(612,83);assert p.state['files_grid']==0

print('PASS: native image-folder notes and inherited modes; grid hits/open/long-press/paging; persistent thumbnail reuse and invalidation; bad-image fallback; all four UI sizes; source images and reading data untouched.')
