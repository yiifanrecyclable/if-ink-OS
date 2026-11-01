#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：文件弹窗实际设为壁纸、持久化、取消轮播及损坏/写失败边界，均使用隔离卡。
# English: Real file-sheet wallpaper apply/persistence, rotation replacement and bad-image/write-failure cases on an isolated card.
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile,zlib
from PIL import Image
R=Path(__file__).resolve().parents[1]
def png(w,h,tone):
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([tone])*w)*h))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(prefix='files-wallpaper-',dir=R/'build') as tmp:
    work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('pictures/inner','banner pic'):(card/folder).mkdir(parents=True)
    (out/'titles.txt').touch()
    (card/'pictures/inner/a.png').write_bytes(png(684,1216,102))
    Image.new('RGB',(342,608),(170,170,170)).save(card/'pictures/inner/photo.JPG')
    (card/'pictures/inner/bad.png').write_bytes(b'broken')
    (card/'pictures/inner/plain.txt').write_text('ordinary file')
    (card/'banner pic/banner.png').write_bytes(png(684,300,85))
    original={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in card.rglob('*') if f.is_file()}
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(directory='/sdcard/pictures/inner',events='',grid=0,view='files-0',fault=False):
        a=[str(R/'build/simulator/render'),view,'','3','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,directory]
        e={**env,'PICO_SIM_FILES_GRID':str(grid)}
        if fault:e['PICO_TEST_NVS_COMMIT_FAIL']='always'
        p=subprocess.run(a,cwd=work,env=e,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr[-1800:]
        return json.loads(p.stdout)
    def select(path,grid=0):
        directory=str(Path(path).parent);state=frame(directory,grid=grid)
        index=next(i for i,item in enumerate(state['files_items']) if item['path']==path)
        if grid:
            cols,_,x,y,w,h,_=state['files_grid_geometry'];x+=(index%cols)*w+w//2;y+=(index//cols)*h+h//2
        else:x,y=180,190+index*state['files_row_height']+state['files_row_height']//2
        return directory,f'l,{x},{y},{x},{y}'
    def wallpaper_button(directory,events,grid=0):
        g=frame(directory,events,grid)['files_card_geometry'];assert g[2],g
        return f't,{g[3]+g[5]//2},{g[4]+g[6]//2},{g[3]+g[5]//2},{g[4]+g[6]//2}'
    chosen='/sdcard/pictures/inner/a.png'
    directory,events=select(chosen)
    before=frame()['wallpaper_path'];assert before==''
    apply=wallpaper_button(directory,events)
    state=frame(directory,events+';'+apply)
    assert state['wallpaper_path']==chosen and not state['wallpaper_rotating'] and state['files_view']==1,state
    assert state['files_message']=='已设为息屏壁纸'
    assert frame()['wallpaper_path']==chosen
    frame(view='lock-wallpaper');actual=(out/'frame.bin').read_bytes()
    frame(view='lock-default');assert (out/'frame.bin').read_bytes()!=actual
    # Closing the card dismisses without modifying the selected wallpaper.
    g=state['files_card_geometry'];x,y=g[11]+g[13]//2,g[12]+g[14]//2
    assert frame(directory,events+f';t,{x},{y},{x},{y}')['files_view']==0
    # Directories, non-images, damaged sources and wide banners have no wallpaper action.
    for path in ('/sdcard/pictures/inner','/sdcard/pictures/inner/plain.txt','/sdcard/pictures/inner/bad.png','/sdcard/banner pic/banner.png'):
        parent,opened=select(path);assert not frame(parent,opened)['files_card_geometry'][2]
    # Selecting a file replaces rotation, and works from the thumbnail grid and another image root.
    ns=out/'nvs/read_pico';ns.mkdir(parents=True,exist_ok=True);(ns/'wallpaper').write_bytes(b'/sdcard/pictures/inner/\0')
    assert frame()['wallpaper_rotating']
    parent,opened=select('/sdcard/pictures/inner/photo.JPG',grid=1)
    state=frame(parent,opened+';'+wallpaper_button(parent,opened,grid=1),grid=1)
    assert state['wallpaper_path'].endswith('photo.JPG') and not state['wallpaper_rotating']
    previous=state['wallpaper_path']
    parent,opened=select(chosen)
    apply=wallpaper_button(parent,opened)
    state=frame(parent,opened+';'+apply,fault=True)
    assert state['files_message']=='保存失败，请检查设置存储空间' and frame()['wallpaper_path']==previous,state
    # English confirmation uses the same action and durable field, without changing image bytes.
    (ns/'language').write_bytes(b'\1')
    state=frame(parent,opened+';'+apply);assert state['wallpaper_path']==chosen and state['files_message']=='Sleep wallpaper applied'
    assert all(hashlib.sha256(f.read_bytes()).hexdigest()==h for f,h in original.items())
print('PASS: image file sheet applies persisted wallpaper in list/grid/subfolders, displays it on lock, replaces rotation, handles dismissal/ratio eligibility/NVS failure and English; source files unchanged.')
