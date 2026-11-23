# SPDX-License-Identifier: Apache-2.0
"""隔离的EPUB小注图、切书及封面按下回归。 / Isolated EPUB note, switching and cover-press regressions."""
from pathlib import Path
import json, os, shutil, struct, subprocess, tempfile, zipfile, zlib

ROOT = Path(__file__).resolve().parents[1]

def png48():
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    rows = b''.join(b'\0' + bytes([100]) * 48 for _ in range(48))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 48, 48, 8, 0, 0, 0, 0))
            + chunk(b'tEXt', b'Comment\0' + b'x' * 8192) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def epub(path, image, wrapped=False):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as book:
        book.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="content.opf"/></rootfiles></container>')
        book.writestr('content.opf', '<package><metadata><title>' + path.stem + '</title></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/>' + ('<item id="note" href="note.png" media-type="image/png"/>' if image else '') + '</manifest><spine><itemref idref="body"/></spine></package>')
        source = 'wrapper.svg' if wrapped else 'note.png'
        body = f'<p>正文之前。</p><img src="{source}"/><p>正文之后。</p>' if image else '<p>' + '普通正文，连续翻页。' * 500 + '</p>'
        book.writestr('body.xhtml', '<html><body>' + body + '</body></html>')
        if image:
            book.writestr('note.png', png48())
        if wrapped:
            book.writestr('wrapper.svg', '<svg><image href="note.png"/></svg>')

with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='epub-flow-') as temp:
    work = Path(temp); card = work / 'card'; output = work / 'build/simulator'
    output.mkdir(parents=True)
    for folder in ('books', 'fonts', 'markdown/attachments'):
        (card / folder).mkdir(parents=True)
    shutil.copyfile(ROOT / 'assets/fonts/PicoSans-Medium.ttf', card / 'fonts/PicoSans-Medium.ttf')
    for name in ('Note', 'One', 'Two'):
        epub(card / ('books/' + name + '.epub'), name == 'Note')
    (output / 'titles.txt').write_text('')
    env = {**os.environ, 'PICO_SIM_CARD_ROOT': str(card), 'ASAN_OPTIONS': 'detect_leaks=0'}
    def render(view, path='/sdcard/books/Note.epub', events=''):
        result = subprocess.run([str(ROOT / 'build/simulator/render'), 'book-' + str(view), path, '0', '48', 'build/simulator/frame.bin', 'build/simulator/titles.txt', '0', ','.join(['0'] * 30), 'builtin', events], cwd=work, env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stderr
        assert 'runtime error:' not in result.stderr and 'AddressSanitizer' not in result.stderr
        return json.loads(result.stdout), (output / 'frame.bin').read_bytes()
    note, _ = render(1)
    assert note['book_view'] == 1 and note['pages'] == 1, note
    baseline, before = render(0)
    held, after = render(0, events='p,130,320,130,320')
    assert held['book_view'] == 0 and before[224*342:1010*342] == after[224*342:1010*342]
    # 同一进程反复关闭并打开不同EPUB，最后仍可打开小注图书。
    # Repeated close/open transitions in one process must still open the note book.
    events = []
    for _ in range(10):
        for column in range(3):
            x = (130, 342, 554)[column]
            events += [f't,{x},320,{x},320', 't,620,720,620,720', 't,62,116,62,116']
    events += ['t,130,320,130,320']
    final, _ = render(0, events=';'.join(events))
    assert final['book_view'] == 1 and final['book_path'].endswith('Note.epub') and final['pages'] == 1, final
    epub(card / 'books/Wrapped.epub', True, wrapped=True)
    settings = output / 'nvs/read_pico'
    settings.mkdir(parents=True, exist_ok=True)
    (settings / 'book_screen').write_bytes(b'\x02')
    for name in ('Note', 'Wrapped'):
        layout, _ = render(1, path=f'/sdcard/books/{name}.epub')
        assert layout['pages']==1, layout
        assert len(layout['reader_images'])==1 and layout['reader_images'][0][2:]==[48,48], layout
    print('PASS: EPUB prefix and SVG fallback flow inline in two columns; cover press and 30 book switches.')
