#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""隔离TF上的图文阅读与纯文本交互回归。 / Isolated image reading and text-only interactions."""
from pathlib import Path
import json, os, subprocess, tempfile, struct, zlib

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/simulator/render'
def png(width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + bytes([((y // 24) % 16) * 17]) * ((width + 1) // 2) for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 4, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')

with tempfile.TemporaryDirectory(prefix='md-images-', dir=ROOT/'build') as directory:
    work = Path(directory); card = work/'card'; store = work/'build/simulator'
    (card/'markdown/attachments').mkdir(parents=True); store.mkdir(parents=True)
    (work/'titles.txt').write_text('')
    (card/'markdown/纯文本.md').write_text('# 第一章\n\n' + ('正文 **粗体** *斜体* ==高亮==，继续阅读。\n\n' * 200))
    for name, width, height in [('普通图', 200, 120), ('长图', 684, 5000)]:
        (card/f'markdown/attachments/{name}.png').write_bytes(png(width, height))
        (card/f'markdown/{name}.md').write_text(f'![示例](attachments/{name}.png)\n\n图片之后的正文。')
    env = {**os.environ, 'PICO_SIM_CARD_ROOT': str(card), 'PICO_TEST_FATFS_RENAME': '1'}
    def frame(path, events='', page=0, panel=0):
        result = subprocess.run([str(BINARY), 'book-1', '/sdcard/markdown/' + path + '.md', str(page), '48',
            'build/simulator/frame.bin', 'titles.txt', str(panel), ','.join(['0']*30), 'builtin', events, ''],
            cwd=work, env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr[-1000:]
        return json.loads(result.stdout), (store/'frame.bin').read_bytes()
    tap = lambda x,y: f't,{x},{y},{x},{y}'
    plain, original = frame('纯文本', page=1)
    assert plain['reader_offset'] > 0 and not plain['document_image_ready'] and not plain['document_zoom']
    assert frame('纯文本', tap(550,500), page=1)[0]['reader_page'] == 2
    assert frame('纯文本', tap(100,500), page=1)[0]['reader_page'] == 0
    opened = frame('纯文本', tap(342,500), page=1)[0]
    assert opened['reader_panel'] == 1 and opened['reader_offset'] == plain['reader_offset']
    ay, height = plain['reader_progress_geometry'][1:]
    bookmark = tap(425, ay+height//2)
    assert frame('纯文本', bookmark, page=1, panel=1)[0]['reader_bookmark_count'] == 1
    assert frame('纯文本', tap(205,1180), page=1, panel=1)[0]['reader_panel'] == 7
    assert frame('纯文本', bookmark, page=1, panel=1)[0]['reader_bookmark_count'] == 0
    assert not list((card/'markdown').glob('.cache/*.gray'))
    for name in ('普通图', '长图'):
        state, preview = frame(name)
        assert state['document_image_ready'] and not state['document_zoom'], state
        slots=state['reader_images'];x,y,w,h=slots[0];image_tap=tap(x+w//2,y+h//2)
        clicked, panel_frame = frame(name, image_tap)
        assert not clicked['document_zoom'] and clicked['reader_panel']==1 and clicked['reader_page']==state['reader_page']
        returned, returned_frame = frame(name, 'k,1,0,0,0', panel=1)
        assert not returned['document_zoom'] and returned['document_image_ready']
        assert returned_frame == preview
        assert frame(name)[0]['document_image_ready']
        if state['pages']>1:assert frame(name, 'k,2,0,0,0')[0]['reader_page'] == 1
        else:assert name=='普通图' and state['pages']==1
    # 普通图前后文字、同页两图与各自点击区域。/ Text around images, two images per page and independent hit regions.
    (card/'markdown/attachments/另一图.png').write_bytes(png(240,160))
    (card/'markdown/混排.md').write_text('上方正文。\n\n![一](attachments/普通图.png)\n\n图间正文。\n\n![二](attachments/另一图.png)\n\n下方继续阅读。')
    mixed,mixed_frame=frame('混排');assert mixed['pages']==1 and len(mixed['reader_images'])==2
    boxes=mixed['reader_images'];assert boxes[0][1]>56 and boxes[1][1]>boxes[0][1]+boxes[0][3]
    def dark(frame,x,y):return ((frame[y*342+x//2]>>(4 if x%2 else 0))&15)<8
    assert any(dark(mixed_frame,x,y) for y in range(boxes[-1][1]+boxes[-1][3]+24,1000) for x in range(40,640))
    for x,y,w,h in boxes:
        press=tap(x+w//2,y+h//2);state,_=frame('混排',press)
        assert not state['document_zoom'] and state['reader_panel']==1
        state,returned=frame('混排','k,1,0,0,0',panel=1)
        assert not state['document_zoom'] and returned==mixed_frame
    # 宽图区域仍遵循左中右点击与实体键翻页。/ Wide images retain fixed tap zones and physical page turns.
    (card/'markdown/attachments/宽图.png').write_bytes(png(684,200))
    (card/'markdown/操作测试.md').write_text('![宽图](attachments/宽图.png)\n\n'+('后续正文。\n\n'*150))
    wide,_=frame('操作测试');x,y,w,h=wide['reader_images'][0];assert wide['pages']>1
    next_page,_=frame('操作测试',tap(x+w-20,y+h//2))
    assert next_page['reader_page']==1 and not next_page['document_zoom']
    back,_=frame('操作测试',tap(x+w-20,y+h//2)+';'+tap(100,500))
    assert back['reader_page']==0
    middle,_=frame('操作测试',tap(x+w//2,y+h//2));assert middle['reader_panel']==1
    assert frame('混排',tap(342,1000))[0]['reader_panel']==1
    assert frame('混排')[1]==mixed_frame
    # 图文操作后仍可读取相同的纯文字页及其书签/进度状态。
    # Text remains identical after image operations, with unchanged bookmark/position semantics.
    after, unchanged = frame('纯文本', page=1)
    assert after['reader_offset'] == plain['reader_offset'] and not after['document_image_ready']
    assert unchanged == original
print('PASS: text-only turns/panel/bookmarks are unchanged; ordinary/tall image previews, fixed tap zones, physical turns, panel return and repeated cache opens work under FAT semantics.')
