#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local HTTP frontend/native integration using an isolated temporary card, never real hardware.
中文：双栏网页与原生后端的隔离HTTP验收，不使用真实TF卡。/ English: Isolated HTTP/native acceptance without real TF storage.
"""
from pathlib import Path
import json,os,socket,struct,subprocess,tempfile,time,urllib.request,urllib.error,zlib
ROOT=Path(__file__).resolve().parents[1]
OPENER=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def png(w,h):
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([160])*w)*h))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(prefix='web-http-',dir=ROOT/'build') as tmp:
 session=Path(tmp);card=session/'card';card.mkdir();(card/'books').mkdir();(card/'books/资料.html').write_bytes(b'<script>must download as attachment</script>')
 with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 proc=subprocess.Popen([os.environ.get('PICO_TEST_PYTHON',os.sys.executable),str(ROOT/'tools/simulator/transfer_preview.py'),'--port',str(port),'--session',str(session)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 def request(path,body=None,method=None):
  req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=body,method=method,headers={'Content-Type':'application/json'} if method=='POST' else {})
  try:
   with OPENER.open(req,timeout=25) as reply:return reply.status,reply.read(),dict(reply.headers)
  except urllib.error.HTTPError as reply:return reply.code,reply.read(),dict(reply.headers)
 try:
  for _ in range(100):
   try:code,page,_=request('/');break
   except (urllib.error.URLError,ConnectionError):time.sleep(.05)
  else:raise AssertionError('preview server failed to start')
  assert code==200 and b'<title>if-ink OS</title>' in page
  code,body,_=request('/appearance');assert code==200;settings=json.loads(body)
  image=png(684,300);code,_,_=request('/banner-upload?name=test.png',image,'PUT');assert code==200
  draft={**settings['banner'],'revision':settings['revision'],'background':True,'image':'/sdcard/banner pic/test.png','font':'','text':'HTTP native preview','size':48,'text_y':100,'date_y':16}
  code,pixels,_=request('/appearance?op=preview',json.dumps(draft).encode(),'POST');assert code==200 and len(pixels)==102600
  code,body,_=request('/appearance?op=banner',json.dumps(draft).encode(),'POST');assert code==200 and json.loads(body)['banner']['text']==draft['text']
  code,_,_=request('/appearance?op=banner',json.dumps(draft).encode(),'POST');assert code==409
  code,copy,headers=request('/file?path=banner%20pic/test.png');assert code==200 and copy==image and headers['Content-Disposition']=='attachment' and headers['X-Content-Type-Options']=='nosniff'
  code,_,_=request('/file?path=books/%E8%B5%84%E6%96%99.html');assert code==200
  code,_,_=request('/file?path=markdown/.index/secret');assert code==400
  code,_,_=request('/files',json.dumps({'action':'delete','path':'banner pic/test.png'}).encode(),'POST');assert code==409
  code,_,_=request('/files',json.dumps({'action':'delete','path':'markdown/test.md'}).encode(),'POST');assert code==403
  code,_,_=request('/document-upload?name=test.md',b'# HTTP Markdown\n\nText only.','PUT');assert code==200
  assert (card/'markdown/test.md').read_text()=='# HTTP Markdown\n\nText only.'
  code,body,_=request('/appearance');assert code==200;current=json.loads(body)
  image=png(684,1216);assert request('/image-upload?name=lock.png',image,'PUT')[0]==200
  code,body,_=request('/appearance?op=wallpaper',json.dumps({'revision':current['revision'],'path':'/sdcard/pictures/lock.png'}).encode(),'POST');assert code==200 and json.loads(body)['wallpaper'].endswith('/lock.png')
  for directory in ('books','markdown','fonts','pictures','banner pic'):
   for action in ('rename','copy','delete'):
    request_body={'action':action,'path':directory,'target':'other'}
    assert request('/files',json.dumps(request_body).encode(),'POST')[0]==403
  assert b'id="bannerConfirm"' not in page and b'id="bannerLocalFont"' in page
  font=(ROOT/'main/assets/builtin.ttf').read_bytes();token='d'*32
  assert request('/font-stage?name='+token+'.ttf',font,'PUT')[0]==200
  staged='/sdcard/.web-fonts/'+token+'.ttf'
  assert request('/file?path=.web-fonts/'+token+'.ttf')[0]==400
  code,body,_=request('/appearance');current=json.loads(body)
  draft={**current['banner'],'revision':current['revision'],'font':staged,'text':'native font','size':48,'text_y':100}
  assert request('/appearance?op=preview',json.dumps(draft).encode(),'POST')[0]==200
  assert request('/appearance?op=banner',json.dumps(draft).encode(),'POST')[0]==422
  draft.update(sync_font=True,font_name='Computer.ttf')
  code,body,_=request('/appearance?op=banner',json.dumps(draft).encode(),'POST');assert code==200
  result=json.loads(body);assert result['banner']['font'].startswith('/sdcard/fonts/Computer-')
  assert request('/appearance?op=font-release',json.dumps({'path':staged}).encode(),'POST')[0]==200
  assert not (card/'.web-fonts'/f'{token}.ttf').exists()
  print('PASS: actual embedded page HTTP/native integration, new image upload, native preview, commit/readback/conflict, protected resources/MD, safe binary download, document import and wallpaper validation')
 finally:
  proc.terminate()
  try:proc.communicate(timeout=3)
  except subprocess.TimeoutExpired:proc.kill();proc.communicate()
