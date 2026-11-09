#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check optical sizing, fixed row geometry and reflow with production rendering."""
from pathlib import Path
import json
import os
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def table(font, tag):
    for at in range(12, 12 + 16 * struct.unpack_from('>H', font, 4)[0], 16):
        if font[at:at+4] == tag:
            return struct.unpack_from('>I', font, at+8)[0]
    raise AssertionError(tag)


def padded_metrics(data):
    """Keep every glyph and its advance, but add 25% to the vertical metrics."""
    font = bytearray(data)
    offset = table(font, b'hhea')
    ascent, descent = struct.unpack_from('>hh', font, offset+4)
    struct.pack_into('>hh', font, offset+4, ascent*5//4, descent*5//4)
    return font


def wider_advances(data):
    font = bytearray(data)
    metrics = struct.unpack_from('>H', font, table(font, b'hhea')+34)[0]
    offset = table(font, b'hmtx')
    for i in range(metrics):
        advance = struct.unpack_from('>H', font, offset+4*i)[0]
        struct.pack_into('>H', font, offset+4*i, advance*6//5)
    return font


def bands(frame, left, right, top, bottom):
    ranges = []
    for y in range(top, bottom):
        row = frame[y*342 + left//2:y*342 + right//2]
        if all(byte == 255 for byte in row):
            continue
        if ranges and ranges[-1][1] == y:
            ranges[-1][1] += 1
        else:
            ranges.append([y, y+1])
    return ranges


with tempfile.TemporaryDirectory(dir=ROOT/'build', prefix='optical-font-') as temp:
    work = Path(temp)
    card = work/'card'
    output = work/'build/simulator'
    nvs = output/'nvs'
    for folder in (card/'fonts', card/'books', card/'markdown', nvs/'read_pico', nvs/'if_content'):
        folder.mkdir(parents=True)
    (output/'titles.txt').touch()
    reference = (ROOT/'main/assets/builtin.ttf').read_bytes()
    (card/'fonts/Padded.ttf').write_bytes(padded_metrics(reference))
    body = ('国田永 HEN gjy。国田永 HEN gjy。\n\n') * 200
    (card/'books/Book.txt').write_text(body)
    (card/'markdown/Doc.md').write_text(body)
    env = {**os.environ, 'PICO_SIM_CARD_ROOT': str(card)}
    face = '/sdcard/fonts/Padded.ttf'

    def set_font(key, value):
        (nvs/key).write_bytes(value.encode()+b'\0')

    def render(screen, path='', panel=0, events='', page=1):
        args = [str(ROOT/'build/simulator/render'), screen, path, str(page), '48',
                'build/simulator/frame.bin', 'build/simulator/titles.txt', str(panel), '0', 'builtin', events, '']
        result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True, timeout=30, check=True)
        return json.loads(result.stdout), (output/'frame.bin').read_bytes()

    for size in (100, 120, 140, 160):
        (nvs/'read_pico/sys_size').write_bytes(bytes([size]))
        set_font('read_pico/sys_font', '')
        baseline, before = render('settings-5')
        set_font('read_pico/sys_font', face)
        calibrated, after = render('settings-5')
        assert baseline['settings_geometry'] == calibrated['settings_geometry']
        a, b = (bands(frame, 230, 454, 350, 417) for frame in (before, after))
        assert len(a) == len(b) == 1 and all(abs(x-y) <= 2 for x, y in zip(a[0], b[0])), (size, a, b)

    set_font('read_pico/sys_font', '')
    for path, key in (('/sdcard/books/Book.txt', 'read_pico/font'), ('/sdcard/markdown/Doc.md', 'if_content/md_font')):
        set_font(key, '')
        original, before = render('book-1', path)
        set_font(key, face)
        normalized, after = render('book-1', path)
        for field in ('pages', 'reader_page', 'reader_px', 'reader_offset'):
            assert original[field] == normalized[field], (path, field, original[field], normalized[field])
        a, b = (bands(frame, 36, 648, 170, 1080) for frame in (before, after))
        assert a == b and len(a) >= 5, (path, a, b)
        set_font(key, '')
        geometry, _ = render('book-1', path, panel=5)
        y = 196 + geometry['reader_controls_geometry'][1]//2
        selected, _ = render('book-1', path, panel=5, events=f't,300,{y},300,{y}')
        assert selected['reader_panel'] == 2 and selected['reader_offset'] == original['reader_offset']
        assert selected['font_path'] == face

    # 字宽变化时分页可以改变，切换前的正文字节位置仍应保留。
    # A font may change wrapping while preserving the source byte being read.
    (card/'fonts/Wide.ttf').write_bytes(wider_advances(reference))
    (card/'books/Wrapping.txt').write_text('国田永春江秋月夜，静听远山雨。'*350)
    set_font('read_pico/font', '')
    path = '/sdcard/books/Wrapping.txt'
    before, _ = render('book-1', path, panel=5, page=4)
    y = 196 + before['reader_controls_geometry'][1]*3//2
    after, _ = render('book-1', path, panel=5, page=4, events=f't,300,{y},300,{y}')
    assert after['font_path'] == '/sdcard/fonts/Wide.ttf' and after['pages'] != before['pages']
    assert after['reader_offset'] == before['reader_offset']

print('PASS fixed system rows across four sizes, matched glyph bounds, TXT/Markdown line bands, pagination and font-change anchor')
