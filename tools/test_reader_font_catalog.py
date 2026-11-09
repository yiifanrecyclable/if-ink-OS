#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Select the last font through the native reader with a large TF catalog."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/simulator/render'

with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='reader-fonts-') as tmp:
    work = Path(tmp)
    card = work / 'card'
    sim = work / 'build/simulator'
    nvs = sim / 'nvs/read_pico'
    for folder in (card / 'books', card / 'markdown', card / 'fonts', nvs):
        folder.mkdir(parents=True)
    (nvs / 'sys_size').write_bytes(bytes([160]))
    (sim / 'titles.txt').touch()
    content = 'Reader font test with several pages.\n\n' * 250
    (card / 'books/Book.txt').write_text(content)
    (card / 'markdown/Doc.md').write_text('# Document\n\n' + content)
    source = ROOT / 'main/assets/builtin.ttf'
    for i in range(63):
        os.link(source, card / 'fonts' / f'Face-{i:03d}.ttf')
    os.link(source, card / 'fonts/Face-063.TTF')
    # 旧目录以截断后的显示名去重；两个路径都要保留。
    # Both paths must remain selectable even when their displayed names match.
    for suffix in ('a', 'b'):
        os.link(source, card / 'fonts' / ('A' + '字' * 26 + suffix + '.ttf'))
    os.link(source, card / 'fonts/._Hidden.ttf')
    os.link(source, card / 'fonts' / ('a' * 148 + '.ttf'))
    (card / 'fonts/A-Folder.ttf').mkdir()
    env = {**os.environ, 'PICO_SIM_CARD_ROOT': str(card)}

    def render(path, events='', panel=5, **faults):
        args = [str(BINARY), 'book-1', path, '1', '48', 'build/simulator/frame.bin',
                'build/simulator/titles.txt', str(panel), '0', 'builtin', events, '']
        result = subprocess.run(args, cwd=work, env={**env, **faults}, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)

    book = '/sdcard/books/Book.txt'
    doc = '/sdcard/markdown/Doc.md'
    _, row_height, rows = render(book)['reader_controls_geometry']
    assert row_height == 73 and rows == 11
    last_page, last_row = divmod(65, rows)
    assert last_page >= 5
    next_page = ('t,620,1050,620,1050', 'u,340,900,340,350', 'k,2,0,0,0')
    turns = [next_page[i % len(next_page)] for i in range(last_page)]
    y = 196 + last_row * row_height + row_height // 2
    tap_last = f't,300,{y},300,{y}'
    font = '/sdcard/fonts/Face-063.TTF'
    selected = render(book, ';'.join(turns + [tap_last]))
    assert selected['reader_panel'] == 2 and selected['reading_font_path'] == font, selected
    assert render(book, panel=0)['font_path'] == font
    assert render(doc, panel=0)['markdown_font_path'] != font

    # At the end, Next cannot skip to a blank page and Previous still works.
    selected = render(doc, ';'.join(turns + [next_page[0], 'k,0,0,0,0', next_page[1], tap_last]))
    assert selected['reader_panel'] == 2 and selected['markdown_font_path'] == font, selected
    assert render(doc, panel=0)['font_path'] == font

    (card/'fonts/00-Broken.ttf').write_bytes(bytes(768))
    os.link(source, card/'fonts/01-Valid.ttf')
    baseline = render(doc)
    def tap_row(row):
        y = 196 + row*row_height + row_height//2
        return f't,300,{y},300,{y}'

    for row, faults in ((0, {}), (1, {'PICO_TEST_NVS_SET_FAIL': 'md_font'})):
        failed = render(doc, tap_row(row), **faults)
        assert failed['reader_panel'] == 5 and failed['font_path'] == font, failed
        assert failed['reader_offset'] == baseline['reader_offset'], failed
        assert failed['markdown_font_path'] == font, failed
        back = render(doc, tap_row(row) + ';k,1,0,0,0', **faults)
        assert back['reader_panel'] == 0 and back['font_path'] == font, back

print('PASS 66 TF fonts, XL pagination, independent font persistence, and Markdown recovery after load or save failure')
