#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create isolated, original-content virtual SD fixtures once per simulator."""
from pathlib import Path
import shutil
import subprocess
import sys

def prepare(root: Path):
    build = root / 'build/simulator'
    vfs = build / 'vfs'
    card = root.parent / 'TF卡'
    for name in ('books','pictures','fonts','notes','markdown/attachments','banner pic'):
        (card/name).mkdir(parents=True,exist_ok=True)
    if not (vfs/'.banner-sample-initialized').exists():
        sample=card/'banner pic/默认背景.png'
        if not sample.exists():shutil.copyfile(root/'assets/banner/default-banner.png',sample)
        vfs.mkdir(parents=True,exist_ok=True);(vfs/'.banner-sample-initialized').write_text('Original sample installed once.\n')
    if not (card/'markdown/.index/.simulator-demo').exists():
        if not (card/'markdown/Markdown 阅读示例.md').exists():
            (card/'markdown/Markdown 阅读示例.md').write_text('# 阅读与笔记\n\n这是 Markdown 阅读示例。标题、列表、引用和插图使用同一阅读器。\n\n## 日常阅读\n\n- 第一项：选择清晰的文字。\n- 第二项：保留阅读进度。\n\n### 阅读计划\n\n> 每天留一点时间阅读。\n\n## 长图查看\n\n点击插图放大，按住上下移动，再点击退出。\n\n![完整长图](attachments/长图示例-test.png)\n\n# 回顾\n\n文档的大纲可以展开，也可以跳转到对应段落。\n',encoding='utf-8')
        if not (card/'markdown/attachments/长图示例-test.png').exists():
            import struct,zlib
            def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
            width,height=684,5000
            rows=bytearray()
            for y in range(height):
                rows.append(0)
                for x in range(0,width,2):
                    a=(y//250+x//57)%16
                    b=(y//250+(x+1)//57)%16
                    if y%250<6 or x%114<6:a=b=0
                    rows.append((a<<4)|b)
            png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,4,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
            (card/'markdown/attachments/长图示例-test.png').write_bytes(png)
        (card/'markdown/.index').mkdir(exist_ok=True)
        (card/'markdown/.index/.simulator-demo').write_text('Original simulator demo generated once.\n')
    standard=root/'assets/fonts/PicoSans-Medium.ttf'
    if standard.exists() and not (vfs/'.standard-font-initialized').exists():
        if not (card/'fonts'/standard.name).exists():
            shutil.copyfile(standard,card/'fonts'/standard.name)
            shutil.copyfile(root/'assets/fonts/OFL.txt',card/'fonts/PicoSans-OFL.txt')
        vfs.mkdir(parents=True,exist_ok=True)
        (vfs/'.standard-font-initialized').write_text('Standard font installed once; preserve user replacements/deletions.\n')
    if (vfs / '.initialized').exists() and card.exists():
        return
    generated = build / 'generated-books'
    subprocess.run([sys.executable, str(root / 'tools/gen_book_fixtures.py'),
                    '--output', str(generated)],
                   cwd=root, check=True, capture_output=True)
    for name in ('books','pictures','fonts','notes','markdown/attachments','banner pic'):
        (card/name).mkdir(parents=True,exist_ok=True)
    (vfs/'flash/books').mkdir(parents=True,exist_ok=True)
    for source, target in (
        ('河边记事_UTF8.txt', 'sdcard/books/阅读示例.txt'),
        ('河边记事_GBK.txt', 'sdcard/books/阅读与时间.txt'),
        ('河边记事_nav.epub', 'sdcard/books/每日笔记.epub'),
        ('flash_small.txt', 'flash/books/内置阅读.txt')):
        dest=card/target[7:] if target.startswith('sdcard/') else vfs/target
        if not dest.exists():shutil.copyfile(generated/'books'/source,dest)
    if not (card/'pictures/灰阶测试.png').exists():shutil.copyfile(root/'main/assets/display-test.png',card/'pictures/灰阶测试.png')
    if not (card/'fonts/Hei.ttf').exists():shutil.copyfile(root/'main/assets/builtin.ttf',card/'fonts/Hei.ttf')
    (vfs / '.initialized').write_text('rc58 virtual storage v1\n')

if __name__ == '__main__':
    prepare(Path(__file__).resolve().parents[2])
