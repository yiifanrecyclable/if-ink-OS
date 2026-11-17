# SPDX-License-Identifier: Apache-2.0
"""Exercise EPUB annotation hit testing, anchor resolution and saved progress."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile
import zlib


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('PICO_SIM_OUTPUT', ROOT / 'build/simulator/render')).resolve()
IMAGE_NOTE_SAMPLE = 'Inline image notes.epub'
SHORT_NOTE_SAMPLE = 'Short note card.epub'


def png_square(side):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    header = struct.pack('>IIBBBBB', side, side, 8, 0, 0, 0, 0)
    rows = (b'\x00' + b'\x40' * side) * side
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def write_image_epub(path, source, note, image_size, png_name='badge.png'):
    with ZipFile(path, 'w', ZIP_DEFLATED) as epub:
        epub.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>')
        epub.writestr('OPS/book.opf', f'''<package><metadata><title>{path.stem}</title></metadata>
          <manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/>
          <item id="icon" href="{png_name}" media-type="image/png"/></manifest>
          <spine><itemref idref="body"/></spine></package>''')
        filler = '<p>继续阅读，章末有注释。 The note follows the text.</p>' * 22
        epub.writestr('OPS/body.xhtml', '<html><body>' + source + filler + note + '</body></html>')
        epub.writestr('OPS/' + png_name, png_square(image_size))


def write_image_note_sample(path):
    write_image_epub(path, '''<p>正文中的图片角标<a id="call" href="#note_1">
        <sup><img src="badge.png" alt="[12]"/></sup></a>可以点按。<a href="#other">[2]</a></p>''',
        '''<p id="note_1"><a href="#call"><img src="badge.png" title="12"/></a> 这是第一条注释。</p>
        <p id="other">这是普通文字链接的目标。</p>''', 20)


def write_short_note_sample(path):
    write_image_epub(path, '''<p>这段正文有一条短注释<a id="card-ref" href="#note-body" epub:type="noteref">
        <img src="badge.png" alt="1"/></a>，点按角标查看。</p>''',
        '''<aside epub:type="footnote"><p id="note-body">
        <a role="doc-backlink" href="#card-ref">[1]</a> 这是短注释的第一段。</p>
        <p>这是短注释的第二段。</p></aside><p>这句在注释范围之外。</p>''', 20)


def write_epub(path, href, section_boundary=False, marker='[1]'):
    with ZipFile(path, 'w', ZIP_DEFLATED) as epub:
        epub.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>')
        epub.writestr('OPS/book.opf', '''<package><metadata><title>Annotations</title></metadata>
          <manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/>
          <item id="notes" href="appendix/notes.xhtml" media-type="application/xhtml+xml"/></manifest>
          <spine><itemref idref="body"/><itemref idref="notes"/></spine></package>''')
        filler = '<p>正文一直继续。 Paragraph after paragraph.</p>' * 96
        lead = '<p>正文开始。 Before the note. </p>' * 19 if section_boundary else ''
        role = ' role="doc-backlink"' if section_boundary else ''
        epub.writestr('OPS/body.xhtml', '<html><body>' +
                      lead + f'<p id="passage">正文 Marker <a id="call" href="{href}">{marker}</a> continues.</p>' + filler +
                      '<p id="same"><a href="#passage">[R]</a> Same chapter note.</p>'
                      '<p>Untyped continuation.</p></body></html>')
        epub.writestr('OPS/appendix/notes.xhtml', '<html><body><p id="fn1">' +
                      f'<a{role} href="../body.xhtml#{"same" if role else "call"}">[R]</a> Cross chapter note.</p>' +
                      filler + '</body></html>')


def raw(x, y, wide, reverse):
    if not wide:
        return x, y
    return (y, 1215 - x) if reverse else (683 - y, x)


def pixel(frame, x, y, wide=False, reverse=False):
    px, py = raw(x, y, wide, reverse)
    return (frame[py * 342 + px // 2] >> (4 * (px % 2))) & 15


def find_marker(frame, wide, reverse, vertical, exclude=None):
    width, height = (1216, 684) if wide else (684, 1216)

    def marked(x, y):
        return pixel(frame, x, y, wide, reverse) == 6

    # Follow the actual pixels from the simulator rather than assuming font metrics.
    start_range = range(36, width - 36) if vertical else range(80, height - 54)
    for start in start_range:
        consecutive = 0
        for step in range(36, (height if vertical else width) - 36):
            x, y = (start, step) if vertical else (step, start)
            dx, dy = (1, 0) if vertical else (0, 1)
            consecutive = consecutive + 1 if marked(x, y) and marked(x + dx, y + dy) else 0
            if consecutive >= 9 and marked(x + 2 * dx, y + 2 * dy):
                point = (x - 3, y - 4) if vertical else (x - 4, y - 3)
                if exclude is None or abs(point[0] - exclude[0]) > 60 or abs(point[1] - exclude[1]) > 60:
                    return point
    return None


def main():
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='epub-notes-') as temp:
        work = Path(temp)
        card = work / 'card'
        out = work / 'build/simulator'
        nvs = out / 'nvs/read_pico'
        for folder in (card / 'books', card / 'fonts', card / 'markdown', nvs):
            folder.mkdir(parents=True)
        (out / 'titles.txt').touch()
        for book, href in (('Cross', 'appendix/notes.xhtml#fn%31'), ('Broken', '#absent'),
                           ('Same', '#same')):
            write_epub(card / f'books/{book}.epub', href)
        write_epub(card / 'books/Later.epub', 'appendix/notes.xhtml#fn1', section_boundary=True)
        write_epub(card / 'books/External.epub', 'https://example.invalid/notes#fn1')
        write_epub(card / 'books/Superscript.epub', 'appendix/notes.xhtml#fn1', marker='¹²³')
        write_image_note_sample(card / 'books' / IMAGE_NOTE_SAMPLE)
        write_short_note_sample(card / 'books' / SHORT_NOTE_SAMPLE)
        write_image_epub(card / 'books/ShortParagraph.epub',
                         '<p>Text<a id="ref" href="#note">[1]</a> continues.</p>',
                         '<p><a id="note" href="#ref">[1]</a> A short reciprocal note.</p>', 20)
        card_source = '<p>Text<a id="ref" epub:type="noteref" href="#note">[1]</a> continues.</p>'
        for name, content in (('LongNote', '<p>More text.</p>' * 13),
                              ('IllustratedNote', '<p>See the figure.</p><img src="badge.png"/>'),
                              ('InlineImageNote', '<p>See the figure.</p><img src="data:image/png;base64,AA=="/>'),
                              ('LinkedNote', '<p>See <a href="#ref">other text</a> or <a href="#outside">outside</a>.</p>')):
            write_image_epub(card / f'books/{name}.epub', card_source,
                             f'<aside id="note" epub:type="footnote">{content}</aside><p id="outside">Outside.</p>', 20)
        for name, side in (('ProbedIcon', 14), ('LinkedFigure', 220)):
            write_image_epub(card / f'books/{name}.epub',
                             '<p>前<a href="#details"><img src="badge.png" alt="3"/></a>后</p>',
                             '<p id="details">Details</p>', side)
        write_image_epub(card / 'books/Unnumbered.epub',
                         '<p>中<a href="#fn"><img src="data:image/png;base64,AA=="/></a>中'
                         '<a href="#fn">[12]</a><a href="#fn"><img src="badge.png" alt="3"/></a></p>',
                         '<p id="fn">Note without a number</p>', 14)

        def render(book, *points, page='0', wide=False, reverse=False, key=None, px=48):
            env = {**os.environ, 'PICO_SIM_CARD_ROOT': str(card), 'ASAN_OPTIONS': 'detect_leaks=0'}
            if reverse:
                env['PICO_SIM_READER_REVERSE'] = '1'
            events = []
            for point in points:
                x, y = raw(*point, wide, reverse)
                events.append(f't,{x},{y},{x},{y}')
            if key is not None:
                events.append(f'k,{key},0,0,0')
            args = [str(BINARY), 'book-1', f'/sdcard/books/{book}.epub', page, str(px),
                    'build/simulator/frame.bin', 'build/simulator/titles.txt', '0', '0',
                    'builtin', ';'.join(events), '']
            result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stderr
            assert 'runtime error:' not in result.stderr, result.stderr
            return json.loads(result.stdout), (out / 'frame.bin').read_bytes()

        for wide, reverse, vertical in ((False, False, False), (True, False, False),
                                        (True, True, False), (False, False, True)):
            (nvs / 'book_screen').write_bytes(bytes([2 if wide else 0]))
            (nvs / 'book_vertical').write_bytes(bytes([vertical]))
            for book in ('Cross', 'Same', 'Broken'):
                original, frame = render(book, wide=wide, reverse=reverse)
                marker = find_marker(frame, wide, reverse, vertical)
                assert marker is not None, (book, wide, reverse, vertical)
                target, note_frame = render(book, marker, wide=wide, reverse=reverse)
                if book == 'Broken':
                    assert not target['reader_note_open'], target
                    assert target['reader_chapter'] == 0 and target['reader_offset'] == original['reader_offset'], target
                    continue
                assert target['reader_note_open'], (book, wide, reverse, vertical, marker, target)
                assert target['reader_note_return_label'] == '返回正文', target
                if book == 'Cross':
                    assert target['reader_chapter'] == 1, target
                else:
                    assert target['reader_chapter'] == 0 and target['reader_page'] > original['reader_page'], target
                resumed, _ = render(book, page='-1', wide=wide, reverse=reverse)
                assert resumed['reader_chapter'] == 0 and resumed['reader_offset'] == original['reader_offset'], resumed
                width, height = (1216, 684) if wide else (684, 1216)
                center = (width // 2, height - (29 if wide else 44))
                for x in (18, width - 18):
                    assert all(pixel(note_frame, x, y, wide, reverse) == 14
                               for y in range(center[1] - 18, center[1] + 18))
                returned, button_frame = render(book, marker, center, wide=wide, reverse=reverse)
                assert not returned['reader_note_open'] and returned['reader_chapter'] == 0, returned
                assert returned['reader_offset'] == original['reader_offset'], returned
                backlink = find_marker(note_frame, wide, reverse, vertical)
                assert backlink is not None, (book, wide, reverse, vertical)
                linked, linked_frame = render(book, marker, backlink, wide=wide, reverse=reverse)
                assert not linked['reader_note_open'] and linked['reader_chapter'] == 0, linked
                assert not linked['reader_note_return_label'], linked
                assert linked['reader_offset'] == original['reader_offset'], linked
                for x in range(0, width, 4):
                    for y in range(height - 56, height, 4):
                        assert pixel(linked_frame, x, y, wide, reverse) == pixel(button_frame, x, y, wide, reverse)
                assert pixel(linked_frame, 18, center[1], wide, reverse) == 15

            book = Path(IMAGE_NOTE_SAMPLE).stem
            origin, frame = render(book, wide=wide, reverse=reverse)
            assert not origin['reader_images'], origin
            mark = find_marker(frame, wide, reverse, vertical)
            assert mark is not None, (wide, reverse, vertical)
            note, note_frame = render(book, mark, wide=wide, reverse=reverse)
            assert note['reader_note_return_label'] == '返回正文' and not note['reader_images'], note
            backlink = find_marker(note_frame, wide, reverse, vertical)
            assert backlink is not None, (wide, reverse, vertical)
            linked, _ = render(book, mark, backlink, wide=wide, reverse=reverse)
            assert not linked['reader_note_open'] and linked['reader_offset'] == origin['reader_offset'], linked
            if not wide and not vertical:
                text_link = find_marker(frame, wide, reverse, vertical, exclude=mark)
                assert text_link is not None
                reached, _ = render(book, text_link)
                assert reached['reader_note_open'] and reached['reader_offset'] > note['reader_offset'], reached
                endnote, frame = render(book, page=str(origin['pages'] - 1))
                reverse_mark = find_marker(frame, False, False, False)
                assert reverse_mark is not None
                prose, _ = render(book, reverse_mark, page=str(endnote['reader_page']))
                assert prose['reader_note_return_label'] == '返回注释', prose

            book = Path(SHORT_NOTE_SAMPLE).stem
            origin, frame = render(book, wide=wide, reverse=reverse)
            marker = find_marker(frame, wide, reverse, vertical)
            assert marker is not None
            opened, card_frame = render(book, marker, wide=wide, reverse=reverse)
            top = opened['reader_note_card_top']
            assert top and not opened['reader_note_open'], opened
            assert opened['reader_note_card_title'] in ('注释1', 'Note 1'), opened
            assert opened['reader_note_card_px'] == origin['reader_px'] * 9 // 10
            assert opened['reader_page'] == origin['reader_page'] and opened['reader_offset'] == origin['reader_offset']
            assert ' '.join(opened['reader_note_card_text'].split()) == '这是短注释的第一段。 这是短注释的第二段。'
            width, height = (1216, 684) if wide else (684, 1216)
            assert top >= height // 2
            assert pixel(card_frame, 16, top, wide, reverse) == 5 and pixel(frame, 16, top, wide, reverse) == 15
            within, _ = render(book, marker, (width//2, top + 90), key=2, wide=wide, reverse=reverse)
            assert within['reader_note_card_top'] == top and within['reader_offset'] == origin['reader_offset'], within
            close_actions = [([(width//2, top-20)], None), ([], 1)]
            corner = (width-48, top+32)
            if wide:
                assert pixel(card_frame, 176, top+10, wide, reverse) == 15
                assert pixel(card_frame, 176, top+16, wide, reverse) == 11
                assert pixel(card_frame, 176, height-10, wide, reverse) == 15
                assert pixel(card_frame, *corner, wide, reverse) == 15
                still_open, _ = render(book, marker, corner, wide=wide, reverse=reverse)
                assert still_open['reader_note_card_top'] == top, still_open
            else:
                close_actions.append(([corner], None))
            for options in close_actions:
                points, key = options
                closed, restored = render(book, marker, *points, key=key, wide=wide, reverse=reverse)
                assert not closed['reader_note_card_top'] and closed['reader_offset'] == origin['reader_offset'], closed
                assert all(pixel(restored, x, y, wide, reverse) == pixel(frame, x, y, wide, reverse)
                           for y in range(top, height, 8) for x in range(0, width, 8))

        (nvs / 'book_screen').write_bytes(b'\x00')
        (nvs / 'book_vertical').write_bytes(b'\x00')
        _, frame = render('External')
        assert find_marker(frame, False, False, False) is None
        _, frame = render('ShortParagraph')
        mark = find_marker(frame, False, False, False)
        short, _ = render('ShortParagraph', mark)
        assert short['reader_note_card_top'] and short['reader_note_card_text'].strip() == 'A short reciprocal note.', short
        assert short['reader_note_card_title'] in ('注释[1]', 'Note [1]'), short
        for book in ('LongNote', 'IllustratedNote', 'InlineImageNote', 'LinkedNote'):
            _, frame = render(book)
            mark = find_marker(frame, False, False, False)
            fallback, _ = render(book, mark)
            assert fallback['reader_note_open'] and not fallback['reader_note_card_top'], fallback
        for wide in (False, True):
            (nvs / 'book_screen').write_bytes(bytes([2 if wide else 0]))
            _, frame = render('LongNote', wide=wide, px=28)
            mark = find_marker(frame, wide, False, False)
            small, _ = render('LongNote', mark, wide=wide, px=28)
            if wide:
                assert small['reader_note_open'] and not small['reader_note_card_top'], small
            else:
                assert small['reader_note_card_top'] >= 608 and not small['reader_note_open'], small
                assert small['reader_note_card_text'].count('More text.') == 13
        (nvs / 'book_screen').write_bytes(b'\x00')
        origin, frame = render('ProbedIcon')
        mark = find_marker(frame, False, False, False)
        assert not origin['reader_images'] and mark is not None, origin
        assert render('ProbedIcon', mark)[0]['reader_note_open']
        for px in (32, 48, 72):
            book = 'Unnumbered'
            origin, frame = render(book, px=px)
            mark = find_marker(frame, False, False, False)
            assert not origin['reader_images'] and mark is not None, origin
            assert render(book, mark, px=px)[0]['reader_note_open']
            x, y = mark
            underline = y + 3
            left = x
            while pixel(frame, left - 1, underline) == 6:
                left -= 1
            right = x
            while pixel(frame, right + 1, underline) == 6:
                right += 1
            scan = range(underline - 2 * px, underline)
            circle_rows = [row for row in scan if any(pixel(frame, col, row) == 7
                                                     for col in range(left, right + 1))]
            body_rows = [row for row in scan if any(pixel(frame, col, row) < 5
                                                   for col in range(left - px - 8, left))]
            top, bottom = min(circle_rows), max(circle_rows)
            body_top, body_bottom = min(body_rows), max(body_rows)
            diameter = right - left + 1
            assert diameter == bottom - top + 1, (px, diameter, top, bottom)
            assert 0.80 * (body_bottom - body_top + 1) <= diameter < body_bottom - body_top + 1
            assert abs(top + bottom - body_top - body_bottom) <= 4
            assert underline > max(bottom, body_bottom)
            assert sum(pixel(frame, col, underline) == 6 and pixel(frame, col - 1, underline) != 6
                       for col in range(left, 648)) == 3
            cx, cy = (left + right) // 2, (top + bottom) // 2
            assert any(pixel(frame, cx + dx, cy + dy) == 15
                       for dx in range(-5, 6) for dy in range(-5, 6))
            previous = max(col for col in range(left) if pixel(frame, col, cy) < 5)
            following = min(col for col in range(right + 1, 684) if pixel(frame, col, cy) < 5)
            assert 1 < left - previous < following - right
            assert render(book, (right + 13, cy), px=px)[0]['reader_note_open']
        figure, frame = render('LinkedFigure')
        assert figure['reader_images'] and find_marker(frame, False, False, False) is None, figure

        # Reach the annotation through pagination, then exercise the reverse trip.
        for book in ('Same', 'Cross'):
            original, _ = render(book)
            last = str(original['pages'] - 1)
            if book == 'Cross':
                next_chapter, _ = render(book, page=last, key=2)
                assert next_chapter['reader_chapter'] == 1, next_chapter
                last = '-1'
            note, note_frame = render(book, page=last)
            assert not note['reader_note_open'] and not note['reader_note_return_label'], note
            backlink = find_marker(note_frame, False, False, False)
            assert backlink is not None, (book, note)
            prose, prose_frame = render(book, backlink, page=last)
            assert prose['reader_chapter'] == 0 and prose['reader_note_return_label'] == '返回注释', prose
            mark = find_marker(prose_frame, False, False, False)
            assert mark is not None
            for finish in (mark, (342, 1172)):
                returned, frame = render(book, backlink, finish, page=last)
                assert returned['reader_chapter'] == note['reader_chapter'], returned
                assert returned['reader_offset'] == note['reader_offset'], returned
                assert not returned['reader_note_open'] and not returned['reader_note_return_label'], returned
                assert pixel(frame, 18, 1172) == 15
            if book == 'Cross':
                # Each replay starts from saved progress. Restore the source for later tests.
                render(book, page='0', key=0)
        for index in range(1, 6):
            page = str(index)
            original, frame = render('Later', page=page)
            marker = find_marker(frame, False, False, False)
            if marker is not None:
                assert original['reader_offset'] > 0, original
                target, note_frame = render('Later', marker, page=page)
                assert target['reader_note_open'] and target['reader_chapter'] == 1, target
                explicit = find_marker(note_frame, False, False, False)
                assert explicit is not None
                linked, _ = render('Later', marker, explicit, page=page)
                assert not linked['reader_note_open'] and linked['reader_offset'] == original['reader_offset'], linked
                resumed, _ = render('Later', page='-1')
                assert resumed['reader_chapter'] == 0 and resumed['reader_offset'] == original['reader_offset'], resumed
                break
        else:
            raise AssertionError('later annotation must appear beyond the first page')

        (nvs / 'bk_rule').write_bytes(b'\x01')
        for book in ('Cross', 'Superscript'):
            _, frame = render(book)
            x, y = find_marker(frame, False, False, False)
            underline = y + 3
            assert all(pixel(frame, x, row) == 6 for row in range(underline, underline + 3))
            ink = [row for row in range(underline - 20, underline)
                   if any(pixel(frame, col, row) < 5 for col in range(x - 5, x + 20))]
            assert ink and underline - max(ink) <= 7, book
            assert any(pixel(frame, col, row) == 5 for col in range(x - 16, x + 16)
                       for row in range(underline + 4, underline + 40)), book

        (nvs / 'rd_fullscreen').write_bytes(b'\x01')
        (nvs / 'rd_immersive').write_bytes(b'\x01')
        _, frame = render('Cross')
        marker = find_marker(frame, False, False, False)
        note, frame = render('Cross', marker)
        assert note['reader_note_open']
        assert all(pixel(frame, x, y) == 14 for x in (8, 100, 540, 675) for y in range(1159, 1216))
        assert all(pixel(frame, x, y) == 15 for x in range(36, 650, 4) for y in range(1150, 1158))
        shutil.copyfile(ROOT / 'assets/fonts/PicoSans-Medium.ttf', card / 'fonts/PicoSans-Medium.ttf')
        (nvs / 'font').write_bytes(b'/sdcard/fonts/PicoSans-Medium.ttf\0')
        _, frame = render(Path(SHORT_NOTE_SAMPLE).stem, px=64)
        mark = find_marker(frame, False, False, False)
        sized, _ = render(Path(SHORT_NOTE_SAMPLE).stem, mark, px=64)
        assert sized['reader_note_card_title'] == '注释1' and sized['reader_note_card_px'] == 57, sized
        print('PASS: note titles preserve markers; both layouts use the real half-screen height; landscape has no close hotspot')


if __name__ == '__main__':
    main()
