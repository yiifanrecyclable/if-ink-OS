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
        zoom, enlarged = frame(name, tap(342,600))
        assert zoom['document_zoom'] and not zoom['document_image_ready'], zoom
        assert enlarged != preview
        if name == '长图':
            moved, _ = frame(name, tap(342,600)+';k,2,0,0,0')
            assert moved['document_zoom'] and moved['image_top'] > 0
        returned, returned_frame = frame(name, tap(342,600)+';k,1,0,0,0')
        assert not returned['document_zoom'] and returned['document_image_ready']
        assert returned_frame == preview
        assert frame(name)[0]['document_image_ready']
        assert frame(name, 'k,2,0,0,0')[0]['reader_page'] == 1
    # 图文操作后仍可读取相同的纯文字页及其书签/进度状态。
    # Text remains identical after image operations, with unchanged bookmark/position semantics.
    after, unchanged = frame('纯文本', page=1)
    assert after['reader_offset'] == plain['reader_offset'] and not after['document_image_ready']
    assert unchanged == original
print('PASS: text-only turns/panel/bookmarks are unchanged; ordinary/tall image preview, zoom, key segments, return and repeated cache opens work under FAT semantics.')
