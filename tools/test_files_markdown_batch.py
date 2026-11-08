#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the shared file gestures and Markdown-only storage paths on a private card."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def tap(x,y):
    return f't,{x},{y},{x},{y}'

def events(*parts):
    return ';'.join(parts)

with tempfile.TemporaryDirectory(prefix='markdown-batch-',dir=ROOT/'build') as temp:
    work=Path(temp); card=work/'card'; md=card/'markdown'; archive=md/'Archive'
    attachments=md/'attachments'; out=work/'build/simulator'; out.mkdir(parents=True)
    (out/'titles.txt').touch()
    archive.mkdir(parents=True); attachments.mkdir(); (card/'books').mkdir()
    root='/sdcard/markdown'
    for name in ('shared.png','only-a.png','only-c.png'):
        shutil.copyfile(ROOT/'plugins/obsidian-pico/tests/fixtures/sample-image.png',attachments/name)
    a='# A\n\n![shared](attachments/shared.png)\n\n![own](attachments/only-a.png)\n'
    (md/'A.md').write_text(a)
    (md/'B.md').write_text('# B\n\n![shared](attachments/shared.png)\n')
    (md/'C.md').write_text('# C\n\n![own](attachments/only-c.png)\n')
    (archive/'B.md').write_text('Existing destination.\n')
    for i in range(15):
        (md/f'Z{i:02}.md').write_text(f'# Extra {i}\n')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1'}

    def run(executable,args,fault=None):
        p=subprocess.run([str(ROOT/'build/simulator'/executable),*args],
                         cwd=work,env={**env,**(fault or {})},capture_output=True,text=True,
                         check=True,timeout=30)
        return json.loads(p.stdout)

    def render(sequence='',directory=root,view='files-0',path='',fault=None):
        return run('render',[view,path,'3' if view=='files-0' else '0','48',
                   str(out/'frame.bin'),str(out/'titles.txt'),'0','0','builtin',sequence,directory],fault)

    def bridge(*args):
        result=run('cloud-host',args)
        assert result['error']==0,result
        return result

    def row(state,path):
        i=next(i for i,item in enumerate(state['files_items']) if item['path']==path)
        height=state['files_row_height']
        return 190+i*height+height//2

    initial=render()
    a_row,b_row=(row(initial,f'{root}/{name}.md') for name in ('A','B'))
    pick_a,pick_b=tap(150,a_row),tap(625,b_row)
    footer,move,delete=tap(342,1105),tap(182,1080),tap(500,1080)
    single=render(f'l,150,{a_row},150,{a_row}')
    assert single['files_view']==1
    assert render(pick_a)['files_view']==0 and render(pick_a)['route']==-1
    second_page=events(pick_a,tap(626,1086),tap(625,190+initial['files_row_height']//2),footer)
    assert render(second_page)['files_view']==6 and render(second_page)['files_batch_count']==2
    modal=events(pick_a,pick_b,footer)
    sheet=render(modal)
    assert sheet['files_batch_count']==2 and sheet['files_batch_bytes']==(md/'A.md').stat().st_size+(md/'B.md').stat().st_size
    choosing=render(events(modal,move))
    assert choosing['files_dir']==root and choosing['files_batch_state']==1
    assert [x['path'] for x in choosing['files_items']]==[f'{root}/Archive']
    back=render(events(modal,move,tap(58,83)))
    assert back['files_view']==0 and back['files_batch_count']==2

    # Refresh the index, seed progress and pin A before moving it. / 生成索引、进度与置顶记录。
    for name in ('A','B','C'):
        bridge('refresh',f'{root}/{name}.md')
    bridge('progress-save',f'{root}/A.md')
    index=md/'.index'
    original_index=(index/'A.md.pico').read_bytes()
    (index/'A.md.prefs').write_text('pico-md-prefs-1\n1\n')
    (index/'A.md.sync').write_text('1794114000000000\n')
    destination=f'{root}/Archive/A.md'
    target_dir=tap(160,row(choosing,f'{root}/Archive'))
    moving=events(modal,move,target_dir,tap(500,890))
    cancelled=render(moving,fault={'PICO_TEST_RENAME_FAIL_TO':destination})
    assert cancelled['files_batch_count']==2 and not (index/'.move').exists()
    assert (md/'A.md').read_text()==a and not (archive/'A.md').exists()
    failed=render(moving,fault={'PICO_TEST_RENAME_FAIL_TO':f'{root}/Archive/.pico-index/A.md.pico'})
    assert failed['files_notice']=='成功 0 个，失败 2 个' and failed['files_batch_count']==1
    assert (index/'.move').is_file() and (archive/'A.md').read_text()==a
    assert (md/'B.md').is_file() and (archive/'B.md').read_text()=='Existing destination.\n'
    assert bridge('recover')['error']==0 and not (index/'.move').exists()
    assert (archive/'.pico-index/A.md.pico').read_bytes()==original_index
    assert (archive/'.pico-index/A.md.prefs').read_text()=='pico-md-prefs-1\n1\n'
    assert (archive/'.pico-index/A.md.sync').is_file() and not (index/'A.md.sync').exists()
    assert bridge('progress-load',destination)['pct']==33
    listing=render(view='book-8',path=destination)
    assert destination in listing['document_items'] and listing['document_count']==19
    assert render(view='book-1',path=destination)['book_view']==1

    # The valid destination conflict still preserves the selected source. / 同名冲突保留源文件与勾选。
    current=render()
    b_row,c_row=(row(current,f'{root}/{name}.md') for name in ('B','C'))
    only_one=render(events(tap(625,b_row),tap(625,c_row),footer,move,target_dir,tap(500,890)))
    assert only_one['files_notice']=='成功 1 个，失败 1 个' and only_one['files_batch_count']==1
    assert (md/'B.md').exists() and (archive/'C.md').exists()
    (md/'D.md').write_text('# D\n')
    current=render()
    b_row,d_row=(row(current,f'{root}/{name}.md') for name in ('B','D'))
    pending=events(tap(625,b_row),tap(625,d_row),footer,delete)
    assert render(pending)['files_batch_state']==2
    assert render(events(pending,move))['files_batch_state']==0
    removed=render(events(pending,delete))
    assert removed['files_notice']=='成功 2 个，失败 0 个'
    assert not (md/'B.md').exists() and (attachments/'shared.png').exists()

    inside=render(directory=f'{root}/Archive')
    rows=[row(inside,f'{root}/Archive/{name}.md') for name in ('A','C')]
    nested=render(events(*(tap(150,y) for y in rows),footer,delete,delete),f'{root}/Archive')
    assert nested['files_notice']=='成功 2 个，失败 0 个'
    assert not (archive/'A.md').exists() and not (archive/'C.md').exists()
    assert (archive/'B.md').is_file() and not list(attachments.glob('*.png'))
print('PASS shared Markdown batch UI, nested reading and deletion, collision safety, interrupted move recovery, indexes, and shared attachments')
