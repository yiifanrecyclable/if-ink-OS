#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Native books batch gestures, selection, safety, and file mutations on a private card."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def tap(x, y):
    return f't,{x},{y},{x},{y}'

def events(*parts):
    return ';'.join(parts)

def tone(frame, x, y):
    return (frame[y*342+x//2] >> (4 if x%2 else 0)) & 15

with tempfile.TemporaryDirectory(prefix='books-batch-', dir=ROOT/'build') as tmp:
    work = Path(tmp)
    card = work/'card'
    out = work/'build/simulator'
    settings = out/'nvs/read_pico'
    for directory in (settings, card/'books/Sub', card/'archive', card/'markdown'):
        directory.mkdir(parents=True)
    (out/'titles.txt').touch()
    (settings/'sys_size').write_bytes(bytes([120]))
    (card/'books/A.txt').write_bytes(b'A'*2048)
    (card/'books/B.epub').write_bytes(b'B'*4096)
    (card/'archive/B.epub').write_bytes(b'target must survive')
    (card/'books/Untouched.md').write_text('# Markdown\n')
    for i in range(20):
        (card/f'books/Z{i:02}.txt').write_text(f'Spare {i}\n')
    (card/'books/Sub/One.txt').write_text('Nested one\n')
    (card/'books/Sub/Two.txt').write_text('Nested two\n')
    env = {**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1'}

    def render(sequence='', directory='/sdcard/books', fault=None):
        result = subprocess.run([str(ROOT/'build/simulator/render'),'files-0','','3','48',
            str(out/'frame.bin'),str(out/'titles.txt'),'0','0','builtin',sequence,directory],
            cwd=work,env={**env,**(fault or {})},capture_output=True,text=True,timeout=30,check=True)
        return json.loads(result.stdout),(out/'frame.bin').read_bytes()

    def progress(action, path):
        result = subprocess.run([str(ROOT/'build/simulator/cloud-host'),action,path],
            cwd=work,env=env,capture_output=True,text=True,timeout=30,check=True)
        data=json.loads(result.stdout)
        assert not data.get('error'), data
        return data

    def row_y(state, path):
        row=next(i for i,item in enumerate(state['files_items']) if item['path']==path)
        step=state['files_row_height']
        return 190+row*step+step//2

    state,original=render()
    ay,by=(row_y(state,f'/sdcard/books/{name}') for name in ('A.txt','B.epub'))
    a,b=tap(150,ay),tap(625,by)
    footer=tap(405,1086)
    assert tone(original,405,1062)==9
    assert render(footer)[0]['files_view']==0
    one,checked=render(a)
    assert one['route']==-1 and one['files_batch_count']==1 and one['files_view']==0
    assert tone(original,625,ay)==15 and tone(checked,625,ay)<15
    assert tone(checked,405,1062)==0
    one_sheet,_=render(events(a,footer))
    assert one_sheet['files_view']==1 and one_sheet['files_batch_count']==1
    cross_page,_=render(events(a,tap(626,1086),footer))
    assert cross_page['files_view']==1 and cross_page['files_page']==1 and cross_page['files_batch_count']==1
    selected=events(a,b)
    selection,background=render(selected)
    assert selection['files_batch_count']==2 and selection['files_batch_bytes']==6144
    assert tone(background,405,1062)==0
    assert render(events(selected,b,footer))[0]['files_view']==1
    modal=events(selected,footer)
    sheet,foreground=render(modal)
    top=sheet['files_batch_top']
    assert sheet['files_view']==6 and top>700
    assert foreground[:56*342]==background[:56*342] and foreground[1136*342:]==background[1136*342:]
    assert foreground[(top-1)*342:top*342]!=background[(top-1)*342:top*342]
    closed,_=render(events(modal,tap(630,top+50)))
    assert closed['files_view']==0 and closed['files_batch_count']==2
    single,_=render(events(selected,f'l,150,{ay},150,{ay}'))
    assert single['files_view']==1 and single['files_batch_count']==2
    assert render(events(a,tap(626,1086),tap(625,190+state['files_row_height']//2),footer))[0]['files_batch_count']==2
    assert render(events(selected,tap(58,83)))[0]['files_batch_count']==0
    assert render(tap(150,ay),'/sdcard/books/Sub')[0]['files_dir']=='/sdcard/books/Sub'

    move,delete=tap(182,1080),tap(500,1080)
    move_here=tap(500,890)
    root,_=render(directory='/sdcard')
    archive=tap(160,row_y(root,'/sdcard/archive'))
    origin=tap(160,row_y(root,'/sdcard/books'))
    assert render(events(modal,move,origin,move_here))[0]['files_notice']=='已在此文件夹'
    aborted,_=render(events(modal,move,tap(180,890)))
    assert aborted['files_view']==0 and aborted['files_batch_count']==2
    assert (card/'books/A.txt').is_file() and (card/'books/B.epub').is_file()
    progress('progress-save','/sdcard/books/A.txt')
    failed,_=render(events(modal,move,archive,move_here),fault={'PICO_TEST_RENAME_FAIL_TO':'/sdcard/archive/A.txt'})
    assert failed['files_notice']=='成功 0 个，失败 2 个' and failed['files_batch_count']==2
    assert progress('progress-load','/sdcard/books/A.txt')['present']
    moved,_=render(events(modal,move,archive,move_here))
    assert moved['files_notice']=='成功 1 个，失败 1 个' and moved['files_batch_count']==1
    assert (card/'archive/A.txt').read_bytes()==b'A'*2048 and not (card/'books/A.txt').exists()
    assert (card/'books/B.epub').read_bytes()==b'B'*4096
    assert (card/'archive/B.epub').read_bytes()==b'target must survive'
    assert progress('progress-load','/sdcard/archive/A.txt')['present']
    (card/'books/A.txt').write_bytes(b'A'*2048)
    assert not progress('progress-load','/sdcard/books/A.txt')['present']
    (card/'books/A.txt').unlink()

    state,_=render()
    by,zy=(row_y(state,f'/sdcard/books/{name}') for name in ('B.epub','Z00.txt'))
    pending=events(tap(625,by),tap(625,zy),footer,delete)
    assert render(pending)[0]['files_batch_state']==2
    assert render(events(pending,move))[0]['files_batch_state']==0
    assert render(events(pending,'t,500,1080,220,1080'))[0]['files_batch_state']==2
    assert (card/'books/B.epub').is_file() and (card/'books/Z00.txt').is_file()
    progress('progress-save','/sdcard/books/Z00.txt')
    result,_=render(events(pending,delete))
    assert result['files_view']==0 and result['files_batch_count']==0
    assert result['files_notice']=='成功 2 个，失败 0 个'
    assert not (card/'books/B.epub').exists() and not (card/'books/Z00.txt').exists()
    (card/'books/Z00.txt').write_text('Spare 0\n')
    assert not progress('progress-load','/sdcard/books/Z00.txt')['present']
    (card/'books/Z00.txt').unlink()
    assert (card/'books/Untouched.md').exists() and (card/'books/Z01.txt').exists() and (card/'books/Sub').is_dir()

    nested,_=render(directory='/sdcard/books/Sub')
    rows=[row_y(nested,f'/sdcard/books/Sub/{name}') for name in ('One.txt','Two.txt')]
    result,_=render(events(*(tap(150,y) for y in rows),footer,delete,delete),'/sdcard/books/Sub')
    assert result['files_notice']=='成功 2 个，失败 0 个' and not list((card/'books/Sub').iterdir())
    empty,empty_frame=render(footer,'/sdcard/books/Sub')
    assert empty['files_view']==0 and tone(empty_frame,405,1062)==9
print('PASS books-only checkboxes, cross-page selection, batch sheet, guarded move/delete, progress, and collision safety')
