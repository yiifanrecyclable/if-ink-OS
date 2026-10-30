#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：验证4位灰度PNG及隔离保存，不使用真实TF卡。/ English: Validate 4-bit PNG export and isolated saving without real TF storage."""
import importlib.util,struct,tempfile,zlib
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pindou_preview',ROOT/'tools/simulator/pindou_preview.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
pixels=bytes(range(256))*(m.FRAME_BYTES//256)+bytes(range(m.FRAME_BYTES%256))
png=m.png_from_pixels(pixels);assert png[:8]==b'\x89PNG\r\n\x1a\n'
pos=8;compressed=b''
while pos<len(png):
    length=struct.unpack_from('>I',png,pos)[0];kind=png[pos+4:pos+8];data=png[pos+8:pos+8+length]
    assert zlib.crc32(kind+data)&0xffffffff==struct.unpack_from('>I',png,pos+8+length)[0]
    if kind==b'IHDR':assert struct.unpack('>IIBBBBB',data)==(684,1216,4,0,0,0,0)
    if kind==b'IDAT':compressed+=data
    pos+=12+length
assert pos==len(png)
rows=zlib.decompress(compressed);stride=342
assert len(rows)==(stride+1)*1216
for y in range(1216):assert rows[y*(stride+1)]==0 and rows[y*(stride+1)+1:(y+1)*(stride+1)]==pixels[y*stride:(y+1)*stride]
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='pindou-') as tmp:
    card=Path(tmp)/'card';card.mkdir();store=m.PindouStore(card)
    a=store.save(pixels);path=card/a['path'];assert a['created'] and path.read_bytes()==png
    again=store.save(pixels);assert not again['created'] and again['name']==a['name']
    other=store.save(bytes(m.FRAME_BYTES));assert other['created'] and other['name']!=a['name']
    reverted=store.save(pixels);assert reverted['created'] and reverted['name'] not in (a['name'],other['name'])
    count=len(list(path.parent.glob('*.png')))
    try:store.save(b'bad')
    except ValueError:pass
    else:raise AssertionError('invalid frame accepted')
    with patch.object(m.os,'replace',side_effect=OSError('test write failure')):
        try:store.save(bytes([0x77])*m.FRAME_BYTES)
        except OSError:pass
        else:raise AssertionError('failed commit accepted')
    assert len(list(path.parent.iterdir()))==count and path.read_bytes()==png
    outside=Path(tmp)/'outside';outside.mkdir();other_card=Path(tmp)/'other';other_card.mkdir();(other_card/'pictures').symlink_to(outside,target_is_directory=True)
    try:m.PindouStore(other_card).save(pixels)
    except ValueError:pass
    else:raise AssertionError('symlink escape accepted')
print('PASS: full-resolution 4-bit grayscale PNG/CRC/rows, no duplicate/overwrite, failed-save cleanup and isolated card paths')
