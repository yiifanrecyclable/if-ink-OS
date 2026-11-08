#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the native font preview, apply and guarded file edits over a private card."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]

def tap(x, y):
    return f't,{x},{y},{x},{y}'

def sequence(*events):
    return ';'.join(events)

def tone(frame, x, y):
    return (frame[y*342+x//2] >> (4 if x%2 else 0)) & 15

with tempfile.TemporaryDirectory(prefix='files-font-preview-', dir=ROOT/'build') as tmp:
    work = Path(tmp)
    card = work/'card'
    out = work/'build/simulator'
    settings = out/'nvs/read_pico'
    content = out/'nvs/if_content'
    for folder in (settings, content, card/'fonts', card/'archive', card/'books', card/'markdown'):
        folder.mkdir(parents=True)
    (out/'titles.txt').touch()
    font = ROOT/'assets/fonts/PicoSans-Medium.ttf'
    for name in ('A-Sample.ttf','PicoSans-Medium.ttf'):
        shutil.copyfile(font,card/'fonts'/name)
    (card/'fonts/Z-Bad.ttf').write_bytes(b'invalid font')
    (card/'books/Story.txt').write_text('A book reading sample.\n')
    (card/'markdown/Note.md').write_text('# A document reading sample\n')
    (settings/'sys_size').write_bytes(bytes([140]))
    (settings/'bk_px').write_bytes(bytes([60]))
    (content/'md_sizes').write_bytes(bytes([62,76,68,58]))
    original_sizes = {p:p.read_bytes() for p in (settings/'sys_size',settings/'bk_px',content/'md_sizes')}
    env = {**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1'}

    def render(events='',directory='/sdcard/fonts',fault=None,view='files-0',path='',aux='3'):
        p = subprocess.run([str(ROOT/'build/simulator/render'),view,path,aux,'48',
            str(out/'frame.bin'),str(out/'titles.txt'),'0','0','builtin',events,directory],
            cwd=work,env={**env,**(fault or {})},capture_output=True,text=True,timeout=30,check=True)
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()

    def fonts(state):
        return tuple(state[k] for k in ('system_font_path','reading_font_path','markdown_font_path'))

    initial, listing = render()
    row = initial['files_row_height']
    x, y = 180, 190+row//2
    open_font = tap(x,y)
    card_open = f'l,{x},{y},{x},{y}'
    close = tap(58,83)
    system, book, document = (tap(cx,1125) for cx in (134,342,550))
    p = '/sdcard/fonts/A-Sample.ttf'
    default_font = '/sdcard/fonts/PicoSans-Medium.ttf'
    preview, frame = render(open_font)
    assert preview['files_view']==5 and preview['files_font_preview_px']==48
    assert fonts(preview)==fonts(initial) and preview['font_path']==initial['font_path']
    assert frame[230*342:520*342] != b'\xff'*(290*342)
    # 固定样张的深色抗锯齿像素多于浅色像素。/ The fixed sample has more dark than light antialiasing pixels.
    shades=[tone(frame,x,y) for y in range(270,350) for x in range(100,580)]
    assert sum(1<=shade<=4 for shade in shades)>sum(8<=shade<=14 for shade in shades)
    assert all(tone(frame,cx,1080)==0 for cx in (134,342,550))
    assert all(tone(frame,cx,1125)==15 for cx in (238,446))
    smaller, small_frame = render(sequence(open_font,tap(64,802)))
    bigger, large_frame = render(sequence(open_font,'r,64,802,620,802'))
    assert smaller['files_font_preview_px']==24 and bigger['files_font_preview_px']==72
    assert fonts(smaller)==fonts(bigger)==fonts(initial)
    assert small_frame[230*342:520*342]!=large_frame[230*342:520*342]
    assert small_frame[:208*342]==large_frame[:208*342] and small_frame[868*342:]==large_frame[868*342:]
    for image in (listing,frame):
        assert all(tone(image,cx,cy)==0 for cx,cy in ((58,83),(50,75),(66,75),(50,91),(66,91)))
    cancelled,_ = render(sequence(open_font,close))
    assert cancelled['files_view']==0 and cancelled['files_dir']=='/sdcard/fonts' and fonts(cancelled)==fonts(initial)
    assert render(tap(342,83))[0]['files_dir']=='/sdcard/fonts'
    assert render(close)[0]['files_dir']=='/sdcard'
    bad,_ = render(tap(x,y+2*row))
    assert bad['files_view']==0 and bad['files_notice']=='字体无法加载' and fonts(bad)==fonts(initial)
    for action,key in ((system,'sys_font'),(book,'font'),(document,'md_font')):
        failed,_ = render(sequence(open_font,action),fault={'PICO_TEST_NVS_SET_FAIL':key})
        assert fonts(failed)==fonts(initial) and '保存失败' in failed['files_notice']
    assert fonts(render(sequence(open_font,tap(238,1125)))[0])==fonts(initial)
    applied,_ = render(sequence(open_font,tap(64,802),book),fault={'PICO_TEST_NVS_SET_FAIL':'md_font'})
    assert fonts(applied)==(fonts(initial)[0],p,'')
    md_read,_ = render(view='book-1',path='/sdcard/markdown/Note.md',aux='0')
    assert md_read['font_path']==default_font and md_read['markdown_font_path']==''
    txt_read,_ = render(view='book-1',path='/sdcard/books/Story.txt',aux='0')
    assert txt_read['font_path']==p
    applied,_ = render(sequence(tap(x,y+row),document),fault={'PICO_TEST_NVS_SET_FAIL':'font'})
    assert fonts(applied)==(fonts(initial)[0],p,default_font)
    applied,_ = render(sequence(open_font,tap(620,802),system))
    assert fonts(applied)==(p,p,default_font) and applied['font_path']==p
    assert fonts(render(sequence(open_font,document))[0])==(p,p,p)
    assert render(view='book-1',path='/sdcard/markdown/Note.md',aux='0')[0]['font_path']==p
    assert all(path.read_bytes()==value for path,value in original_sizes.items())

    sheet,_ = render(card_open)
    button_y = sheet['files_card_geometry'][8]+sheet['files_card_geometry'][10]//2
    move, delete = tap(428,button_y), tap(598,button_y)
    confirm, cancel = tap(500,button_y), tap(180,button_y)
    assert render(sequence(card_open,move))[0]['files_font_warning']==2
    assert render(sequence(card_open,delete))[0]['files_font_warning']==1
    assert fonts(render(sequence(card_open,delete,cancel))[0])==(p,p,p)
    assert (card/'fonts/A-Sample.ttf').is_file()
    assert render(sequence(card_open,move,confirm,close))[0]['files_dir']=='/sdcard/fonts'
    moving = sequence(card_open,move,confirm,tap(180,y),tap(500,895))
    rejected,_ = render(moving,fault={'PICO_TEST_FSYNC_FAIL':'1'})
    assert fonts(rejected)==(p,p,p) and (card/'fonts/A-Sample.ttf').is_file()
    moved,_ = render(moving)
    target = card/'archive/A-Sample.ttf'
    assert target.read_bytes()==font.read_bytes() and not (card/'fonts/A-Sample.ttf').exists()
    assert fonts(moved)==('','','builtin')
    new_path = '/sdcard/archive/A-Sample.ttf'
    assert fonts(render(sequence(open_font,system,book,document),'/sdcard/archive')[0])==(new_path,)*3
    deleted,_ = render(sequence(card_open,delete,confirm),'/sdcard/archive')
    assert not target.exists() and fonts(deleted)==('','','builtin')
    assert all(path.read_bytes()==value for path,value in original_sizes.items())
print('PASS native font preview, slider, close, independent sizes, checked apply, move/delete warning, cancellation, and default fallback')
