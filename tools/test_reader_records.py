#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：真实阅读器的计时、文档进度和首页排序，全部使用隔离存储。
# English: Real-reader time, document progress and Home order over isolated storage.
from pathlib import Path
import json,os,struct,subprocess,tempfile,runpy
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='reader-records-',dir=R/'build') as tmp:
 w=Path(tmp);card=w/'card';out=w/'build/simulator';out.mkdir(parents=True)
 for folder in ('books','markdown','fonts'):(card/folder).mkdir(parents=True)
 (out/'titles.txt').write_text('')
 env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
 def frame(view,path='',page=-1,events=''):
  a=[str(R/'build/simulator/render'),view,path,str(page),'48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,'']
  p=subprocess.run(a,cwd=w,env=env,capture_output=True,text=True,timeout=30)
  assert not p.returncode,p.stderr[-2000:]
  return json.loads(p.stdout)
 paths=[f'/sdcard/markdown/{n}.md' for n in ('A','B','C')]
 for path in paths:
  (card/path.removeprefix('/sdcard/')).write_text('# 阅读记录\n\n'+('计时与进度测试正文。\n\n'*500))
  state=frame('book-1',path,3)
  assert state['reader_page']==3 and state['reader_progress']>0,state
 # 重开和主页顺序不能由文件名或导入时间决定。/ Resume and Home order follow reads, not names/import dates.
 assert frame('baseline-home')['home_list_paths'][:3]==list(reversed(paths))
 state=frame('book-1',paths[0]);assert state['reader_page']==3
 assert frame('baseline-home')['home_list_paths'][0]==paths[0]
 # 修改正文后清除旧锚点，新的阅读位置仍能保存。/ A revision resets old anchors but must retain new progress.
 with (card/'markdown/A.md').open('a') as f:f.write('\n修订后的正文。'*300)
 state=frame('book-1',paths[0],5);assert state['reader_progress']>0
 assert frame('book-1',paths[0])['reader_page']==5
 # 真实on_tick连续计时；两次进程启动模拟两次阅读会话。/ Real on_tick accounting across two reader sessions.
 events=';'.join(['h,0,0,0,0']*65)
 frame('book-1',paths[1],0,events)
 def total():return struct.unpack_from('<Q',(out/'nvs/rp_ticket/stats').read_bytes(),8)[0]
 first=total();assert first>=60,first
 frame('book-1',paths[2],0,events);assert total()>=first+60
 # 使用真正EPUB解析和每帧独立进程模拟重启；NVS提交失败不能再阻断阅读进度。
 # Use the real EPUB parser and fresh processes as reboot boundaries; NVS commit failures must not block progress.
 make_epub=runpy.run_path(str(R/'tools/gen_book_fixtures.py'))['make_epub']
 books=['/sdcard/books/New A.epub','/sdcard/books/New B.epub']
 for path in books:make_epub(card/path.removeprefix('/sdcard/'),'ncx')
 env['PICO_TEST_NVS_COMMIT_FAIL']='always'
 expected={}
 for path in books+paths[:2]:
  state=frame('book-1',path,3);assert state['reader_page']==3 and state['reader_progress']>0,state
  expected[path]=state['reader_progress']
  # Shelf and document progress values are the actual values rendered by production UI.
  listed=frame('book-8' if path.endswith('.md') else 'book-0')
  items=listed['document_items'] if path.endswith('.md') else listed['book_items']
  values=listed['document_progresses'] if path.endswith('.md') else listed['book_progresses']
  assert dict(zip(items,values))[path]==expected[path],(path,listed)
  home=frame('baseline-home')
  if path.endswith('.md'):
   assert home['home_list_paths'][0]==path and home['home_progresses'][1]==expected[path],home
  else:
   assert home['home_featured_path']==path and home['home_progresses'][0]==expected[path],home
 # Every fresh process must restore both the position and percentage, and move that title to the top.
 for path in reversed(books+paths[:2]):
  state=frame('book-1',path);assert state['reader_page']==3 and state['reader_progress']==expected[path],state
  home=frame('baseline-home')
  if path.endswith('.md'):assert home['home_list_paths'][0]==path
  else:assert home['home_featured_path']==path
 assert len(list((card/'.readpico/progress').glob('b_*')))>0
 print('PASS: real MD/EPUB fresh-process resume, matching shelf/doc/Home percentages and recency with NVS commits failing; MD revision saving and active time across sessions.')
