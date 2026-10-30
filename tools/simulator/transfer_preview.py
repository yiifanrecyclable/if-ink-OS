#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Loopback preview of the embedded management page using real native appearance/document code.
中文：仅映射显式隔离会话，不接触真实TF卡。/ English: Explicit isolated session only, never the real TF card.
"""
import argparse,json,mimetypes,os,shutil,subprocess,threading,uuid
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit,parse_qs,unquote
from pindou_preview import PindouStore,FRAME_BYTES
ROOT=Path(__file__).resolve().parents[2]
LOCK=threading.RLock()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--port',type=int,default=8768);parser.add_argument('--session',type=Path,default=ROOT/'build/os016-preview/session');args=parser.parse_args()
    session=args.session.resolve();card=session/'card';build=session/'build/simulator';build.mkdir(parents=True,exist_ok=True)
    if not card.is_dir() or not str(session).startswith(str(ROOT/'build')+'/'):raise SystemExit('An existing isolated build session is required')
    for name in ('books','markdown','pictures','banner pic','fonts'):(card/name).mkdir(exist_ok=True)
    pindou=PindouStore(card)
    def path_for(relative,root=False):
        parts=relative.split('/')
        if not relative and root:return card
        if not relative or len(relative.encode())>238 or any(not p or p.startswith('.') or p.endswith((' ','.')) or p.startswith(' ') or any(c in p for c in '\\:*?"<>|') for p in parts):raise ValueError('文件路径无效或受保护')
        target=card.joinpath(*parts).resolve()
        if not target.is_relative_to(card):raise ValueError('文件路径越界')
        return target
    def native(op='get',data=None,staging=None,destination=None):
        reply=build/('editor-'+uuid.uuid4().hex+'.bin');titles=build/'editor-titles.txt';titles.touch(exist_ok=True)
        env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_EDITOR_OP':op,'PICO_SIM_EDITOR_JSON':json.dumps(data or {},ensure_ascii=False)}
        command=[str(ROOT/'build/simulator/render'),'web-document' if staging else 'web-appearance',staging or '/sdcard/books/preview.txt',destination or '0','48',str(reply),str(titles),'0','','builtin']
        process=subprocess.run(command,cwd=session,env=env,capture_output=True,text=True,timeout=25)
        if process.returncode:raise ValueError('原生处理失败：'+process.stderr[-300:])
        meta=json.loads(process.stdout);body=reply.read_bytes() if reply.exists() else b'';reply.unlink(missing_ok=True)
        if staging:return meta['status'],json.dumps({'ok':meta['status']==200,'error':'文档验证或入库失败' if meta['status']!=200 else ''}).encode(),'application/json'
        return meta['status'],body,'application/octet-stream' if meta['pixels'] else 'application/json; charset=utf-8'
    def writable(path):
        status,body,_=native();current=json.loads(body);cfg=current['banner'];absolute='/sdcard/'+str(path.relative_to(card));assets=[cfg['image'] if cfg['background'] else '',cfg['font'] if cfg['show_text'] else '']
        return not any(a and (a.casefold()==absolute.casefold() or a.casefold().startswith(absolute.casefold()+'/')) for a in assets)
    def managed(relative):return relative.split('/')[0].lower() in ('markdown','.pico-sync','.pico-cloud','.index')
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*a):pass
        def respond(self,code,body,kind='application/json; charset=utf-8',attachment=False):
            if not isinstance(body,bytes):body=json.dumps(body,ensure_ascii=False).encode()
            self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
            if attachment:self.send_header('Content-Disposition','attachment');self.send_header('Content-Security-Policy','sandbox')
            self.end_headers();self.wfile.write(body)
        def query(self):
            url=urlsplit(self.path);return url.path,{k:v[-1] for k,v in parse_qs(url.query).items()}
        def do_GET(self):
            try:
                with LOCK:
                    route,q=self.query()
                    if route=='/':
                        page=(ROOT/'components/read_pico_transfer/upload.html').read_text().replace('</header>','</header><p style="margin:-10px 0 20px"><a class="download tiny" href="/pindou">电子拼豆 · 独立预览</a></p>',1)
                        return self.respond(200,page.encode(),'text/html; charset=utf-8')
                    if route in ('/pindou','/pindou/'):
                        return self.respond(200,(ROOT/'tools/simulator/pindou/index.html').read_bytes(),'text/html; charset=utf-8')
                    if route in ('/pindou/app.js','/pindou/core.mjs','/pindou/style.css','/pindou/system.ttf'):
                        asset=ROOT/'main/assets/builtin.ttf' if route.endswith('system.ttf') else ROOT/'tools/simulator/pindou'/route.rsplit('/',1)[1]
                        kind='font/ttf' if route.endswith('.ttf') else 'text/css' if route.endswith('.css') else 'text/javascript'
                        return self.respond(200,asset.read_bytes(),kind)
                    if route=='/appearance':return self.respond(*native())
                    if route=='/info':return self.respond(200,{'mode':'sta','is_flash':False,'free_bytes':shutil.disk_usage(card).free,'file_limit':0})
                    if route=='/files':
                        folder=path_for(q.get('path',''),True);items=sorted((p for p in folder.iterdir() if not p.name.startswith('.')),key=lambda p:(not p.is_dir(),p.name.casefold()));page=max(0,int(q.get('page','0')))
                        return self.respond(200,{'total':len(items),'pages':(len(items)+19)//20,'items':[{'name':p.name,'directory':p.is_dir(),'size':p.stat().st_size} for p in items[page*20:(page+1)*20]]})
                    if route=='/file':
                        path=path_for(q.get('path',''))
                        if not path.is_file():return self.respond(404,{'error':'文件不存在'})
                        return self.respond(200,path.read_bytes(),'application/octet-stream',True)
                    self.respond(404,{'error':'未知接口'})
            except (ValueError,OSError,subprocess.SubprocessError) as e:self.respond(400,{'error':str(e)})
        def do_POST(self):
            try:
                with LOCK:
                    route,q=self.query();length=int(self.headers.get('Content-Length','0'))
                    if route=='/pindou/save':
                        origin=self.headers.get('Origin')
                        if origin and origin!='http://'+self.headers.get('Host',''):return self.respond(403,{'error':'请从本机预览页面保存'})
                        if length!=FRAME_BYTES:return self.respond(400,{'error':'画面尺寸不正确'})
                        return self.respond(200,pindou.save(self.rfile.read(length)))
                    if length>3071:return self.respond(400,{'error':'请求过长'})
                    body=json.loads(self.rfile.read(length))
                    if route=='/appearance':return self.respond(*native(q.get('op',''),body))
                    if route!='/files':return self.respond(404,{'error':'未知接口'})
                    relative=body.get('path','');target=body.get('target','');action=body.get('action');source=path_for(relative,action=='mkdir')
                    if (action!='mkdir' and relative.lower() in ('books','markdown','fonts','pictures','banner pic')) or target.lower() in ('books','markdown','fonts','pictures','banner pic'):return self.respond(403,{'error':'默认文件夹不可更改，请管理文件夹内的文件'})
                    if managed(relative) or (target and managed(target)):return self.respond(403,{'error':'此目录由文档服务管理，请在设备端操作'})
                    dest=path_for(target) if action!='delete' else None
                    if action not in ('copy','mkdir') and not writable(source):return self.respond(409,{'error':'资源正在被首页 Banner 使用，请先更换设置'})
                    if dest and dest.exists():return self.respond(409,{'error':'同名文件已存在'})
                    if action=='mkdir':dest.mkdir()
                    elif action=='rename':source.rename(dest)
                    elif action=='copy':shutil.copytree(source,dest) if source.is_dir() else shutil.copy2(source,dest)
                    elif action=='delete':shutil.rmtree(source) if source.is_dir() else source.unlink()
                    else:raise ValueError('不支持的操作')
                    self.respond(200,{'ok':True})
            except (ValueError,OSError,subprocess.SubprocessError) as e:self.respond(400,{'error':str(e)})
        def do_PUT(self):
            try:
                with LOCK:
                    route,q=self.query();length=int(self.headers.get('Content-Length','0'))
                    roots={'/upload':'books','/document-upload':'markdown','/font-upload':'fonts','/image-upload':'pictures','/banner-upload':'banner pic','/font-stage':'.web-fonts'}
                    relative=q.get('path','') if route=='/file-upload' else roots.get(route,'')+'/'+q.get('name','')
                    if route=='/font-stage':
                        import re
                        if not re.fullmatch(r'[0-9a-f]{32}\.ttf',q.get('name','')):return self.respond(400,{'error':'临时字体名称无效'})
                        target=card/'.web-fonts'/q['name']
                    else:target=path_for(relative)
                    document=route=='/document-upload'
                    if managed(relative) and not document:return self.respond(403,{'error':'请使用上传文档板块'})
                    if not writable(target):return self.respond(409,{'error':'文件正在被 Banner 使用'})
                    if target.exists() and q.get('overwrite')!='1':return self.respond(409,{'error':'同名文件已存在'})
                    limit=262144 if document else 2097152 if route=='/banner-upload' else 32*1024*1024
                    if length<=0 or length>limit:return self.respond(413,{'error':'文件超过上限'})
                    target.parent.mkdir(parents=True,exist_ok=True);stage=card/('.web-stage-'+uuid.uuid4().hex+('.md' if document else '.tmp'))
                    try:
                        with stage.open('wb') as output:
                            remaining=length
                            while remaining:
                                chunk=self.rfile.read(min(16384,remaining))
                                if not chunk:raise ValueError('上传中断，原文件已保留')
                                output.write(chunk);remaining-=len(chunk)
                        if document:return self.respond(*native(staging='/sdcard/'+stage.name,destination='/sdcard/'+relative))
                        stage.replace(target);self.respond(200,{'ok':True})
                    finally:stage.unlink(missing_ok=True)
            except (ValueError,OSError,subprocess.SubprocessError) as e:self.respond(400,{'error':str(e)})
    print(f'Isolated embedded management preview: http://127.0.0.1:{args.port}/',flush=True)
    ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
if __name__=='__main__':main()
