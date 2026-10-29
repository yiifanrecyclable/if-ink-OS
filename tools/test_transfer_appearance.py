#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production appearance handler with real fonts, decoder, renderer and persistent NVS on an isolated card.
中文：真实配置/字体/图片/排版与持久化回归，仅用隔离卡。/ English: Real configuration/font/image/layout persistence on an isolated card.
"""
from pathlib import Path
import json,os,shutil,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
def png(w,h):
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([180])*w)*h))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(prefix='appearance-',dir=ROOT/'build') as tmp:
 work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True);(out/'titles.txt').touch()
 for folder in ['fonts','banner pic','pictures','markdown']:(card/folder).mkdir(parents=True)
 shutil.copyfile(ROOT/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
 (card/'banner pic/test.png').write_bytes(png(684,300));(card/'pictures/lock.png').write_bytes(png(684,1216));(card/'banner pic/bad.png').write_bytes(b'not a picture')
 def call(op='get',body=None,raw=None,fault=False,cancel=False):
  env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_EDITOR_OP':op,'PICO_SIM_EDITOR_JSON':raw if raw is not None else json.dumps(body or {},ensure_ascii=False)}
  if fault:env['PICO_TEST_NVS_COMMIT_FAIL']='always'
  if cancel:env['PICO_SIM_EDITOR_CANCEL']='1'
  p=subprocess.run([str(ROOT/'build/simulator/render'),'web-appearance','/sdcard/test.txt','0','48','build/simulator/reply.bin','build/simulator/titles.txt','0','','builtin'],cwd=work,env=env,capture_output=True,text=True,timeout=30)
  assert p.returncode==0,p.stderr
  meta=json.loads(p.stdout);reply=(out/'reply.bin').read_bytes()
  return meta['status'],reply if meta['pixels'] else json.loads(reply)
 code,first=call();assert code==200 and first['banner']['size']==96
 payload={**first['banner'],'revision':first['revision'],'text':'自由阅读','font':'/sdcard/fonts/PicoSans-Medium.ttf','background':True,'image':'/sdcard/banner pic/test.png','date_y':20,'text_y':90,'valign':0}
 code,preview=call('preview',payload);assert code==200 and len(preview)==102600
 # 中文：日期和格言移动必须改变真实输出，而不是只改滑块数值。/ English: moving date/text must change native pixels.
 moved_date={**payload,'date_y':120};moved_text={**payload,'text_y':170}
 assert call('preview',moved_date)[1]!=preview
 assert call('preview',moved_text)[1]!=preview
 assert call()[1]['revision']==first['revision']
 code,saved=call('banner',payload);assert code==200 and saved['banner']['text']=='自由阅读' and saved['revision']!=first['revision']
 assert call('banner',payload)[0]==409
 baseline=call()[1];payload.update(revision=baseline['revision'])
 for changes,status in [({'size':121},400),({'text_y':297},400),({'date_y':3},400),({'align':4},400),({'valign':3},400),({'text':'一\n二\n三\n四\n五'},400),({'text':'好'*64},400),({'font':'/sdcard/fonts/missing.ttf'},422),({'font':'/sdcard/fonts/bad.ttf'},422),({'image':'/sdcard/banner pic/bad.png'},422),({'image':'/sdcard/../outside.png'},400)]:
  (card/'fonts/bad.ttf').write_bytes(b'not a font');body={**payload,**changes};assert call('banner',body)[0]==status,(changes,call('banner',body));assert call()[1]['revision']==baseline['revision']
 assert call('banner',raw=json.dumps(payload)[:-1]+',"size":96}')[0]==400
 assert call('banner',raw=json.dumps(payload)[:-1]+',"text":"bad\\u0000text"}')[0]==400
 body={**payload,'text':'一二三四五六七八九十'*6,'size':120};assert call('preview',body)[0]==422
 assert call()[1]['revision']==baseline['revision']
 assert call('banner',{**payload,'text':'失败时不应保存'},fault=True)[0]==500
 assert call()[1]['revision']==baseline['revision']
 assert call('banner',{**payload,'text':'取消时不应保存'},cancel=True)[0]==503
 assert call()[1]['revision']==baseline['revision']
 assert call('banner',{**payload,'text':'\U0010ffff'})[0]==422
 (card/'fonts/font.otf').write_bytes(b'not supported');assert not any(f['name'].endswith('.otf') for f in call()[1]['fonts'])
 code,wall=call('wallpaper',{'revision':baseline['revision'],'path':'/sdcard/pictures/lock.png'});assert code==200 and wall['wallpaper']=='/sdcard/pictures/lock.png'
 (card/'pictures/second.png').write_bytes(png(684,1216))
 assert call('wallpaper',{'revision':wall['revision'],'path':'/sdcard/pictures/second.png'},fault=True)[0]==500
 assert call()[1]['wallpaper']==wall['wallpaper']
 assert call('wallpaper',{'revision':wall['revision'],'path':'/sdcard/banner pic/bad.png'})[0]==422
 assert call()[1]['wallpaper']=='/sdcard/pictures/lock.png'
 assert call('banner',payload)[0]==409
 # New fonts remain hidden until explicit Save; compare actual file contents, not filenames.
 temp=card/'.web-fonts';temp.mkdir();token='a'*32;stage=temp/(token+'.ttf');staged='/sdcard/.web-fonts/'+stage.name
 shutil.copyfile(ROOT/'main/assets/builtin.ttf',stage)
 current=call()[1];body={**current['banner'],'revision':current['revision'],'font':staged,'text':'自由阅读','size':48,'text_y':90}
 assert call('preview',body)[0]==200
 assert call()[1]['revision']==current['revision']
 assert call('banner',body)[0]==422
 code,installed=call('banner',{**body,'sync_font':True,'font_name':'本地字体.ttf'});assert code==200,installed
 final=installed['banner']['font'];assert final.startswith('/sdcard/fonts/') and '.web-fonts' not in final
 assert (card/final.removeprefix('/sdcard/')).read_bytes()==stage.read_bytes()
 assert call('font-match',{'path':staged})[1]['path']==final
 assert call('font-release',{'path':staged})[0]==200 and not stage.exists()
 assert call('font-release',{'path':final})[0]==400
 duplicate=temp/('b'*32+'.ttf');shutil.copyfile(card/'fonts/PicoSans-Medium.ttf',duplicate)
 assert call('font-match',{'path':'/sdcard/.web-fonts/'+duplicate.name})[1]['path']=='/sdcard/fonts/PicoSans-Medium.ttf'
 stage.write_bytes(b'corrupt font');body.update(revision=installed['revision'])
 assert call('preview',body)[0]==422
 assert call('preview',{**body,'show_text':False})[0]==422
 assert call()[1]['revision']==installed['revision']
 print('PASS: real native appearance roundtrip, stable no-write preview/4bpp, font identity/coverage, damaged images, bounds/UTF8/duplicate keys, stale revision, wallpaper validation and old-setting preservation')
