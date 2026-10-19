#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""验证完整封面比例、独立缓存、7px圆角与网格位置；不操作用户卡。/ Verify aspect-preserved covers, separate caches, 7px corners and grid positions on a private card."""
from pathlib import Path
import json,os,struct,subprocess,tempfile,zipfile,zlib
R=Path(__file__).resolve().parents[1]
def png(w,h):
    def chunk(t,d):return struct.pack('!I',len(d))+t+d+struct.pack('!I',zlib.crc32(t+d)&0xffffffff)
    pixels=b''.join(b'\0'+bytes([64])*w for _ in range(h))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(prefix='shelf-fit-',dir=R/'build') as tmp:
    base=Path(tmp);card=base/'card';out=base/'build/simulator';out.mkdir(parents=True)
    for n in ('books','markdown','fonts'):(card/n).mkdir(parents=True)
    sizes={'A-tall.epub':(320,480),'B-square.epub':(400,400),'C-wide.epub':(600,300)}
    for name,(w,h) in sizes.items():
        with zipfile.ZipFile(card/'books'/name,'w') as z:
            z.writestr('mimetype','application/epub+zip')
            z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/content.opf"/></rootfiles></container>')
            z.writestr('OPS/content.opf','<package xmlns="http://www.idpf.org/2007/opf" version="2.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>'+name+'</dc:title><meta name="cover" content="cover"/></metadata><manifest><item id="cover" href="cover.png" media-type="image/png"/><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="chapter"/></spine></package>')
            z.writestr('OPS/cover.png',png(w,h));z.writestr('OPS/chapter.xhtml','<html><body><p>Reading test.</p></body></html>')
    (card/'books/D-coverless.txt').write_text('Coverless book.');sizes['D-coverless.txt']=(176,240)
    (out/'titles.txt').write_text('');env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(view='book-0',path=''):
        p=subprocess.run([str(R/'build/simulator/render'),view,path,'0','48','build/simulator/frame.bin','build/simulator/titles.txt','0',','.join(['0']*30),'builtin',''],cwd=base,env=env,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()
    def tone(data,x,y):return (data[y*342+x//2]>>(4 if x%2 else 0))&15
    state,data=frame();geometries=state['shelf_geometry'];assert geometries[0][1]==148
    assert geometries[0][0]==12 and geometries[2][0]+geometries[2][2]==672,geometries[:3]
    centers={};tracks=[]
    for name,g in zip(state['book_items'],geometries):
        source_w,source_h=sizes[Path(name).name];x,y,w,h=g[4:8]
        assert abs(w*source_h-h*source_w)<=max(source_w,source_h),(name,g)
        assert g[8]<=176 and g[9]<=240 and w<=g[2]-12
        assert tone(data,x,y)==15 and tone(data,x+w-1,y)==15 and tone(data,x,y+h-1)==15 and tone(data,x+w-1,y+h-1)==15,(name,g)
        assert tone(data,x+7,y)==0 and tone(data,x+10,y+10)<15,(name,g)
        assert tone(data,x+2,y)==15,(name,g)
        centers.setdefault(g[1],set()).add(y+h//2)
        tracks.append(g[10:14])
        assert g[10]==g[0]+(g[2]-g[12])//2 and g[12]<g[2]-12 and g[13]==4,g
        assert tone(data,g[10],g[11])<15 and tone(data,g[10]+g[12]-1,g[11])<15,g
    assert all(len(c)==1 for c in centers.values()),geometries[:4]
    assert len({t[2] for t in tracks})==1,tracks
    assert tracks[0][2]==geometries[0][7]*176//240,tracks
    assert geometries[0][7]>geometries[1][7]>geometries[2][7]
    assert tracks[0][1]==geometries[0][1]+geometries[0][7]+8
    cache=card/'.readpico/covers';fitted=list(cache.glob('*-fit2.bin'));assert len(fitted)==4
    before={p.name:(p.stat().st_mtime_ns,p.read_bytes()) for p in fitted};frame()
    assert before=={p.name:(p.stat().st_mtime_ns,p.read_bytes()) for p in fitted}
    # 首页使用同一个完整比例缓存，覆盖三种比例及无封面书。/ Home shares full-cover caches across portrait, square, landscape and coverless books.
    for name,(source_w,source_h) in sizes.items():
        frame('book-1','/sdcard/books/'+name)
        home,pixels=frame('baseline-home');x,y,w,h,sw,sh=home['home_cover_geometry']
        assert sw>0 and sh>0,(name,home['home_cover_geometry'])
        assert abs(w*source_h-h*source_w)<=max(source_w,source_h),(name,w,h)
        assert 28<=x and x+w<=216 and 0<h<=246,(name,x,y,w,h)
        assert tone(pixels,x,y)==15 and tone(pixels,x+w-1,y+h-1)==15
        assert tone(pixels,x+2,y)==15 and tone(pixels,x+7,y)==0
        assert tone(pixels,x+w//2,y+h//2)<15
    assert not [p for p in cache.glob('*.bin') if not p.name.endswith('-fit2.bin')]
    assert before=={p.name:(p.stat().st_mtime_ns,p.read_bytes()) for p in fitted}
    # 大字模式仍使用两列并限制封面不超出单元格。/ Large type retains two columns and bounded covers.
    nvs=out/'nvs/read_pico';nvs.mkdir(parents=True,exist_ok=True);(nvs/'sys_size').write_bytes(bytes([140]))
    for scale in (140,160):
        (nvs/'sys_size').write_bytes(bytes([scale]))
        large,pixels=frame();grid=large['shelf_geometry']
        assert grid[0][0]==40 and grid[4]==[0]*14
        for g in grid[:4]:
            x,y,w,h=g[4:8]
            assert x>=g[0] and x+w<=g[0]+g[2]
            assert tone(pixels,x,y)==15 and tone(pixels,x+w-1,y+h-1)==15
            assert tone(pixels,x+2,y)==15 and tone(pixels,x+7,y)==0
            assert g[12]==300*176//240,g
            # 整行封面/进度/两行标题均向下留白，末尾仍有空间。/ Each large-cover block is inset below the row top and leaves room at the bottom.
            assert y>g[1] and g[11]+4<g[1]+g[3],g
        assert grid[0][5]+grid[0][7]//2==grid[1][5]+grid[1][7]//2
        assert grid[2][5]+grid[2][7]//2==grid[3][5]+grid[3][7]//2
        assert grid[0][12]==grid[1][12]==grid[2][12]==grid[3][12]

print('shelf covers: portrait/square/landscape aspect ratios, bounded cached pixels, repeat cache hits, vertically centered covers, uniform caption/progress columns, 7px corners, shared full-aspect Home covers and 3x3/2x2 geometry passed')
