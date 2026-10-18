#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""隔离TF/NVS验证分页、分区及淡化，不做截图巡检。/ Private native regression for pagination, tap regions and fading, without screenshot inspection."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
tap=lambda x,y:f't,{x},{y},{x},{y}'
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='feedback-011-') as directory:
    work=Path(directory);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for name in ('books','markdown','fonts'):(card/name).mkdir(parents=True)
    for i in range(24):
        (card/f'books/Book{i:02}.txt').write_text('阅读体验测试。This is a reading example.\n'*350)
        (card/f'markdown/Doc{i:02}.md').write_text('# 一级标题\n\n## 二级标题\n\n### 三级标题\n\n'+'文档阅读体验测试。\n'*350)
    shutil.copyfile(ROOT/'main/assets/builtin.ttf',card/'fonts/Document.ttf')
    (out/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_VERIFY_AREAS':'1'}
    def frame(view='book-1',panel=0,events='',path='/sdcard/books/Book00.txt',page=1,extra=''):
        run=subprocess.run([str(ROOT/'build/simulator/render'),view,path,str(page),'48','build/simulator/frame.bin','build/simulator/titles.txt',str(panel),'0,0,0,0,0,0,0','builtin',events,extra],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert run.returncode==0,run.stderr[-2000:]
        return json.loads(run.stdout),(out/'frame.bin').read_bytes()
    # 扩展触区的边缘仍可翻页；回刷区域覆盖整颗箭头。/ Enlarged edges work and the dirty region includes the complete arrow.
    for view in ('book-0','book-8'):
        assert frame(view=view,page=0)[0]['pages']>1
        state,_=frame(view=view,page=0,events=tap(665,1105))
        assert state['reader_page']==1 and state['route']==-1 and state['reader_area_violations']==0,state
        state,_=frame(view=view,page=0,events=tap(665,1105)+';'+tap(18,1050))
        assert state['reader_page']==0 and state['reader_area_violations']==0,state
        assert frame(view=view,page=0,events='k,2,0,0,0')[0]['reader_page']==1
        assert frame(view=view,page=1,events='k,0,0,0,0')[0]['reader_page']==0
    for events,wanted in ((tap(665,1105),1),('k,2,0,0,0',1),('k,2,0,0,0;k,0,0,0,0',0)):
        state,_=frame(view='files-0',page=3,extra='/sdcard/books',events=events)
        assert state['files_page']==wanted and state['files_folder']==3 and state['route']==-1,state
    # 管理列表实体键分页时不清掉所选项或离开列表。/ Side keys paginate management lists without leaving them.
    for view in ('book-4','book-9'):
        state,_=frame(view=view,page=0,events='k,2,0,0,0')
        assert state['reader_page']==1 and state['book_view']==int(view[5:]),state
    # 黑色字淡到9级，关面板精确恢复；控件拖动不再次淡化正文。/ Dark ink fades to level 9, closes exactly, and never compounds while dragging.
    baseline,plain=frame();state,faded=frame(panel=1);top=state['reader_controls_geometry'][0]
    assert plain[180*342:(top-7)*342]!=faded[180*342:(top-7)*342]
    grays=[(b>>shift)&15 for b in faded[180*342:(top-7)*342] for shift in (0,4)]
    assert min(grays)>=9 and 9 in grays
    _,restored=frame(events='k,1,0,0,0',panel=1)
    assert restored[180*342:(top-7)*342]==plain[180*342:(top-7)*342]
    py=state['reader_progress_geometry'][0]
    _,dragged=frame(panel=1,events=f'p,200,{py},200,{py};m,200,{py},400,{py}')
    assert dragged[180*342:(top-7)*342]==faded[180*342:(top-7)*342]
    # 缩略图、实际点击区、跨内容偏好使用同一行布局。/ Thumbnail choice, actual tap actions and cross-content preferences share rows.
    state,_=frame(panel=9,events=tap(200,270));assert state['reader_panel']==15
    state,_=frame(panel=15,events=tap(500,500));assert state['reader_tap_layout']==1
    assert frame(events=tap(342,300))[0]['reader_page']==0
    assert frame(events=tap(40,600))[0]['reader_panel']==1
    assert frame(events=tap(40,900))[0]['reader_page']==2
    assert frame(path='/sdcard/markdown/Doc00.md')[0]['reader_tap_layout']==1
    frame(panel=15,events=tap(100,500))
    assert frame(events=tap(40,600))[0]['reader_page']==0
    # 文档选字形不会改变图书字体；重新打开仍使用已选字体。/ A document face persists without changing the book preference.
    path='/sdcard/markdown/Doc00.md'
    state,_=frame(path=path,panel=2);rh=state['reader_controls_geometry'][1]
    state,_=frame(path=path,panel=2,events=tap(300,250)+';'+tap(200,220+rh+rh//2))
    assert state['reader_panel']==2 and state['markdown_font_path']=='/sdcard/fonts/Document.ttf',state
    assert state['reading_font_path']==baseline['reading_font_path']
    assert frame(path=path)[0]['font_path']=='/sdcard/fonts/Document.ttf'
    assert frame()[0]['font_builtin']
print('PASS: complete pagination arrows/keys, manager pages, non-compounding fade/restore, shared rows/columns and independent persistent Markdown font.')
