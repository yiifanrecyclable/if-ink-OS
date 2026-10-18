#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实字体、区域绘制及主循环取消语义回归。/ Native font, dirty-area and loop-cancellation regression."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='reader-previews-',dir=ROOT/'build') as folder:
    base=Path(folder)
    nvs=base/'build/simulator/nvs/read_pico'
    nvs.mkdir(parents=True)
    card=base/'card'
    (card/'markdown').mkdir(parents=True)
    (card/'font').mkdir()
    (card/'markdown/preview.md').write_text('\n\n'.join(f'# 章节 {i}\n\n阅读测试。'*20 for i in range(60)))
    for name in ('Reader','System'):
        shutil.copyfile(ROOT/'main/assets/builtin.ttf',card/f'font/{name}.ttf')
    for key,name in (('font','Reader'),('sys_font','System')):
        (nvs/key).write_bytes(f'/sdcard/font/{name}.ttf\0'.encode())
    (base/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_VERIFY_AREAS':'1'}
    def frame(events='',panel=1,view=1):
        args=[str(ROOT/'build/simulator/render'),f'book-{view}','/sdcard/markdown/preview.md','1','48','build/simulator/frame.bin','titles.txt',str(panel),','.join(['0']*30),'builtin',events,'']
        result=subprocess.run(args,cwd=base,env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stderr[-2000:]
        return json.loads(result.stdout)
    raw=lambda kind,x,y:f'{kind},{x},{y},{x},{y}'
    state=frame()
    assert state['system_font_path']=='/sdcard/font/System.ttf',state
    assert state['reading_font_path']=='/sdcard/font/Reader.ttf' and state['font_path']=='/sdcard/font/Reader.ttf',state
    py=state['reader_progress_geometry'][0]
    rh=state['reader_controls_geometry'][1]
    # 未松手时必须复用正文及两套字体；声明的区域覆盖所有像素改动。
    # Before release, reuse prose and both faces; the dirty hint must contain every pixel change.
    md_y=state['markdown_slider_y']
    for panel,y in ((1,py),(2,md_y[0]),(3,220+rh-40),(11,md_y[1]),(2,md_y[2]),(2,md_y[3])):
        events=';'.join(raw(kind,x,y) for kind,x in (('b',240),('v',300),('v',420),('v',500)))
        preview=frame(events,panel=panel)
        assert preview['reader_event_draws']==0,preview
        assert preview['reader_event_font_loads']==0,preview
        assert preview['reader_area_updates']>=2,preview
        assert preview['reader_area_violations']==0,preview
    # 目录PRESS若返回PAGE，模拟的真实主循环会立刻取消拖动，此断言必须失败。
    # A PAGE on TOC PRESS would be cancelled immediately by the mirrored main loop.
    events=';'.join(raw(kind,x,641) for kind,x in (('b',242),('v',492)))
    toc=frame(events,view=2)
    assert toc['toc_jump'] and toc['toc_drag'] and toc['toc_percent']==80,toc
    assert toc['reader_event_font_loads']==0 and toc['reader_area_violations']==0,toc
    released=frame(events+';'+raw('e',492,641),view=2)
    assert not released['toc_drag'] and released['toc_percent']==80,released
    held=frame(';'.join(raw(kind,x,641) for kind,x in (('b',242),('h',242),('v',542),('e',542))),view=2)
    assert not held['toc_drag'] and held['toc_percent']==90,held
    cancelled=frame(events+';c,242,641,492,641',view=2)
    assert not cancelled['toc_drag'] and cancelled['toc_percent']==45,cancelled
print('reader previews: cached faces, zero prose redraws while dragging, dirty-area bounds and hardware-loop TOC release/hold/cancel passed')
