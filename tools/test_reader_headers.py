#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify reader headers and their navigation with isolated simulator storage."""
from pathlib import Path
import tempfile,subprocess,os,json,struct
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=root/'build') as d:
    w=Path(d)
    card=w/'card'
    nvs=w/'build/simulator/nvs/read_pico'
    for p in (card/'books',card/'markdown',card/'fonts',nvs):p.mkdir(parents=True)
    title='测试书籍顶部显示书名很长很长'
    (card/'books'/f'{title}.txt').write_text('第一章 正文\n'+'沉浸阅读内容示例。'*450)
    (card/'markdown/文档顶部标题.md').write_text('# 文档顶部标题\n\n'+'测试文档。'*160)
    (nvs/'rd_fullscreen').write_bytes(b'\1')
    (nvs/'rd_immersive').write_bytes(b'\1')
    sim=w/'build/simulator'
    (sim/'titles.txt').touch()
    def draw(events='', panel=0, toc=False, doc=False):
        path='/sdcard/markdown/文档顶部标题.md' if doc else '/sdcard/books/'+title+'.txt'
        a=[str(root/'build/simulator/render'),'book-2' if toc else 'book-1',path,'1','48','build/simulator/frame.bin','build/simulator/titles.txt',str(panel),'0','builtin',events,'']
        r=subprocess.run(a,cwd=w,env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)},capture_output=True,text=True,check=True)
        return json.loads(r.stdout),(sim/'frame.bin').read_bytes()
    def tap(x,y):return f't,{x},{y},{x},{y}'
    def row(f,y):return f[y*342:(y+1)*342]
    # The live clock at the left may change between simulator calls.
    def top(f):return bytes(f[y*342+x] for y in range(56) for x in range(115,342))
    body=draw()[1]
    toc=draw(toc=True)[1]
    bar=top(toc)
    assert bar!=top(body)
    for x0,x1 in ((36,220),(250,430),(555,654)):
        assert any(toc[y*342+x//2]!=255 for y in range(12,47) for x in range(x0,x1))
    for panel in (7,2,3,9,0):
        is_toc=panel==0
        _, f=draw(panel=panel,toc=is_toc)
        assert top(f)==bar
        assert row(f,127)==row(toc,127) and row(f,127)[0]!=255 and row(f,127)[-1]!=255
        assert not any(b!=255 for y in range(58,120) for b in row(f,y)[280:])
        left=draw(tap(58,90),panel=panel,toc=is_toc)[0]
        assert left['reader_panel']==1 and left['book_view']==1, (panel,left['reader_panel'],left['book_view'])
        right=draw(tap(626,90),panel=panel,toc=is_toc)[0]
        assert right['reader_panel']==panel and right['book_view']==(2 if is_toc else 1)
        key=draw('k,1,0,0,0',panel=panel,toc=is_toc)[0]
        assert key['reader_panel']==0 and key['book_view']==1
    (nvs/'rd_immersive').write_bytes(b'\0')
    assert top(draw()[1])==bar
    assert top(draw(doc=True)[1])!=bar
    # Give the system font visibly different advances while keeping the actual
    # reader face fixed. Compare the same label as a signature and a title.
    face = bytearray((root/'main/assets/builtin.ttf').read_bytes())
    tables = {face[i:i+4].decode():struct.unpack_from('>I',face,i+8)[0]
              for i in range(12,12+16*struct.unpack_from('>H',face,4)[0],16)}
    metrics = struct.unpack_from('>H',face,tables['hhea']+34)[0]
    for i in range(metrics):
        at=tables['hmtx']+4*i
        advance=struct.unpack_from('>H',face,at)[0]
        struct.pack_into('>H',face,at,advance*3//2)
    (card/'fonts/System.ttf').write_bytes(face)
    (nvs/'sys_font').write_bytes(b'/sdcard/fonts/System.ttf\0')
    (nvs/'status_sig').write_bytes(title.encode()+b'\0')
    (nvs/'rd_fullscreen').write_bytes(b'\0')
    normal, signed=draw()
    (nvs/'rd_fullscreen').write_bytes(b'\1')
    full, titled=draw()
    def center(f):
        return bytes(f[y*342+x] for y in range(8,52) for x in range(104,237))
    assert center(signed)==center(titled) and center(titled)!=center(toc)
    assert normal['font_path']==full['font_path']=='builtin'
    (nvs/'book_screen').write_bytes(b'\1')
    def wide_pixel(frame,x,y):
        px=683-y
        return (frame[x*342+px//2]>>(4*(px%2)))&15
    wide_state, widened=draw()
    def wide_title(frame):
        return bytes(wide_pixel(frame,x,y) for y in range(8,52) for x in range(622,1020))
    (nvs/'sys_font').unlink()
    _, default=draw()
    assert wide_title(widened)!=wide_title(default)
    assert wide_state['font_path']=='builtin'
    print('PASS immersive and regular fullscreen, five shared headers, title bar, left ×, right inactive and KEY2')
