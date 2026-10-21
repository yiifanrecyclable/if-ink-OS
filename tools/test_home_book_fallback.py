#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：隔离TF/NVS验证无MD首页图书回退、排重、未读文档及点击续读。/ Isolated native checks for empty-MD fallback, deduplication, unread documents and resume routing.
from pathlib import Path
import json,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='home-book-fallback-',dir=ROOT/'build') as directory:
    work=Path(directory);card=work/'card';(card/'books').mkdir(parents=True);(card/'markdown').mkdir()
    (work/'build/simulator/nvs').mkdir(parents=True)
    (work/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(view='baseline-home',path='',page=0,events=''):
        args=[str(ROOT/'build/simulator/render'),view,path,str(page),'48','build/simulator/frame.bin','titles.txt','0',','.join(['0']*30),'builtin',events,'']
        result=subprocess.run(args,cwd=work,env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stderr[-2000:]
        return json.loads(result.stdout)
    names=[f'/sdcard/books/Book{i}.txt' for i in range(5)]
    for i,path in enumerate(names):
        (card/f'books/Book{i}.txt').write_text('阅读测试正文。'*1500)
        assert frame('book-1',path,1)['reader_progress']>0
    state=frame();assert not state['home_has_documents']
    assert state['home_featured_path']==names[-1]
    assert state['home_list_paths']==list(reversed(names[:-1])),state['home_list_paths']
    y,step,rows=state['home_geometry'][:3]
    # 每行触区必须打开对应书籍，不能重复左列或仍走空文档数组。/ Each row routes to its book, excluding the cover and stale document slots.
    for i,path in enumerate(reversed(names[:-1])):
        opened=frame(events=f't,400,{y+i*step+step//2},400,{y+i*step+step//2}')
        assert opened['route']==1 and opened['book_path']==path,opened
    # 有MD但尚未阅读时，仍是文档列。隐藏文件、图片和目录不算MD。
    # An unread MD keeps the document column; hidden files, images and directories do not count.
    (card/'markdown/.hidden.md').write_text('hidden');(card/'markdown/image.png').write_text('image');(card/'markdown/folder.md').mkdir()
    assert not frame()['home_has_documents']
    doc=card/'markdown/Unread.MD';doc.write_text('# 未读\n\n正文')
    unread=frame();assert unread['home_has_documents'] and unread['home_list_paths']==['']*4
    frame('book-1','/sdcard/markdown/Unread.MD',0)
    state=frame();assert state['home_has_documents'] and state['home_list_paths'][0]=='/sdcard/markdown/Unread.MD'
    # 最近一篇是MD时，左列来自recent[0]，右列应再跳过它。/ When last-path is MD, skip recent[0] already shown on the left.
    doc.unlink();state=frame();assert not state['home_has_documents']
    assert state['home_featured_path']==names[-1] and state['home_list_paths']==list(reversed(names[:-1]))
    # 特大字号沿用三行，其余行不挤占布局。/ XL retains the existing three-row layout.
    prefs=work/'build/simulator/nvs/read_pico';prefs.mkdir(parents=True,exist_ok=True);(prefs/'sys_size').write_bytes(bytes([160]))
    xl=frame();assert xl['home_geometry'][2]==3 and xl['home_list_paths'][:3]==list(reversed(names[:-1]))[:3]
    for path in (card/'books').glob('Book[0-3].txt'):path.unlink()
    single=frame();assert single['home_featured_path']==names[-1] and single['home_list_paths']==['']*4
    tapped=frame(events=f't,400,{y+step//2},400,{y+step//2}')
    assert tapped['route']==-1 and tapped['book_path']==''
    (card/'books/Book4.txt').unlink();empty=frame();assert not empty['home_featured_path'] and empty['home_list_paths']==['']*4
print('PASS: native empty-MD fallback, recency order, featured deduplication, four/XL-three rows, unread-MD distinction, add/delete transition, row routes and empty/one-book safety')
