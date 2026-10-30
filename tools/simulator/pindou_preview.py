#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：隔离模拟卡上的4位灰度PNG导出，不访问真实TF卡。/ English: Export 4-bit grayscale PNGs to an isolated simulator card, never real TF storage."""
import hashlib,os,struct,tempfile,zlib
from pathlib import Path
from urllib.parse import quote

WIDTH,HEIGHT=684,1216
FRAME_BYTES=WIDTH*HEIGHT//2

def png_from_pixels(pixels):
    if len(pixels)!=FRAME_BYTES:raise ValueError('画面尺寸不正确')
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    stride=WIDTH//2
    rows=b''.join(b'\0'+pixels[y*stride:(y+1)*stride] for y in range(HEIGHT))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',WIDTH,HEIGHT,4,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows,6))+chunk(b'IEND',b'')

class PindouStore:
    def __init__(self,card):self.card=Path(card).resolve();self.last_digest=None;self.last_path=None
    def save(self,pixels):
        digest=hashlib.sha256(pixels).digest()
        if digest==self.last_digest and self.last_path and self.last_path.is_file():return self.result(self.last_path,False)
        image=png_from_pixels(pixels);folder=self.card/'pictures'/'pindou'
        if not folder.resolve().is_relative_to(self.card):raise ValueError('保存目录无效')
        folder.mkdir(parents=True,exist_ok=True)
        fd,stage=tempfile.mkstemp(prefix='.pindou-',suffix='.part',dir=folder)
        reserved=None
        try:
            with os.fdopen(fd,'wb') as output:output.write(image);output.flush();os.fsync(output.fileno())
            # 中文：独占预留名称后原子替换自己的空文件，不覆盖任何已有作品。
            # English: Reserve a name exclusively, then atomically replace our placeholder, never an existing artwork.
            for number in range(1,100000):
                candidate=folder/f'pindou-{number:04d}.png'
                try:
                    fd=os.open(candidate,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o644);os.close(fd);reserved=candidate;break
                except FileExistsError:continue
            if reserved is None:raise ValueError('作品数量过多，请先整理文件夹')
            os.replace(stage,reserved);path=reserved;reserved=None
            self.last_digest=digest;self.last_path=path
            return self.result(path,True)
        finally:
            Path(stage).unlink(missing_ok=True)
            if reserved:reserved.unlink(missing_ok=True)
    def result(self,path,created):
        relative=path.relative_to(self.card).as_posix()
        return dict(ok=True,name=path.name,path=relative,url='/file?path='+quote(relative,safe=''),created=created)
