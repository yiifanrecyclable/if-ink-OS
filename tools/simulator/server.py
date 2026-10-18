#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Loopback-only interactive host preview. Cloud actions use local fixtures."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import threading
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
from views import VIEWS
from fixtures import prepare
from cloud_client import CloudController,CloudError,PRIVATE as CLOUD_PRIVATE

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BUILD = ROOT / "build/simulator"
BOOKS = BUILD / "books"
CARD = ROOT.parent / "TF卡"
STORES = {"nvs":BUILD/"nvs","vfs":BUILD/"vfs","card":CARD}
FIXTURES = HERE / "fixtures"
MAX_INPUT = 256 * 1024
LOCK = threading.RLock()
DEMO_HISTORY = [0, 5, 25, 0, 50, 120, 70, 15, 0, 40, 80, 0, 95, 130, 20, 60, 0, 30, 75, 105, 5, 0, 90, 0, 15, 30, 60, 90, 120, 45]


class Preview:
    def __init__(self, build_native=True):
        BOOKS.mkdir(parents=True, exist_ok=True)
        prepare(ROOT)
        self.state_path = BUILD / "state.json"
        self.state = {"screen": "baseline-home", "page": 0, "pages": 1, "px": 48,
                      "scroll": 0,
                      'events':[], 'root_view':None,
                      'firmware_path':'/sdcard/books/阅读示例.txt','files_dir':'',
                      "selected": [], "active": "", "version": 0,
                      "days": DEMO_HISTORY.copy(), "message": ""}
        if self.state_path.exists():
            try:
                saved = json.loads(self.state_path.read_text())
                for key in ("px", "days", "active"):
                    if key in saved:
                        self.state[key] = saved[key]
                if self.state["px"] not in (32,36,38,40,44,48,50,56,64,68,72):
                    self.state["px"] = 48
                if isinstance(self.state["days"], list) and len(self.state["days"]) == 7:
                    self.state["days"] = DEMO_HISTORY[:23] + self.state["days"]
                if not isinstance(self.state["days"], list) or len(self.state["days"]) != 30 or any(type(n) is not int or not 0<=n<=1440 for n in self.state["days"]):
                    self.state["days"] = DEMO_HISTORY.copy()
                if not isinstance(self.state['active'],str) or Path(self.state['active']).name != self.state['active']:
                    self.state['active']=''
            except (ValueError, OSError):
                pass
        self.signature = "" if build_native else self.sources_signature()
        self.cloud=CloudController(storage_lock=LOCK)
        self.cloud_revision=0
        self.cloud_pending_view=None
        self.state['cloud_page']=0
        self.failed_signature=''
        self.replay_origin=None
        self.storage_signature=None
        self.render()

    def documents(self, cloud=False):
        folder = FIXTURES if cloud else BOOKS
        paths = sorted(folder.glob("*.md"), key=lambda p: (-p.stat().st_mtime_ns, p.name))
        return [{"id": p.name, "title": p.stem, "bytes": p.stat().st_size} for p in paths]

    def sources_signature(self):
        inputs = [HERE / "render.c", HERE / "pixels.c", HERE / "build.sh",
                  ROOT / "main/font/ttf_font.c", ROOT / "main/assets/builtin.ttf", ROOT / "main/ota_online.h", ROOT / "tools/cloud_host.c", ROOT / "tools/build_cloud_host.sh",
                  ROOT / "main/book/book_layout.c", ROOT / "main/book/html_text.c",
                  ROOT / "main/book/markdown_text.c", ROOT / "main/book/markdown_text.h"]
        inputs.extend((HERE / "include").glob("*.h"))
        inputs.extend(HERE.glob('*.c'))
        inputs.extend(HERE.glob('*.h'))
        inputs.extend((HERE/'vendor').glob('*'))
        inputs.extend((ROOT/'main/book').glob('*.c'))
        inputs.extend((ROOT/'main/book').glob('*.h'))
        inputs.extend((HERE/'include').rglob('*.h'))
        inputs.append(HERE/'hardware.c')
        inputs.extend([HERE/'nvs.c',HERE/'replay.h'])
        inputs.extend((ROOT/'main/ui').glob('*.c'))
        inputs.extend((ROOT/'main/ui').glob('*.h'))
        inputs.extend((ROOT/'main/apps').glob('*.c'))
        inputs.extend((ROOT/'main/apps').glob('*.h'))
        inputs.extend((ROOT/'main/cloud').glob('*.c'))
        inputs.extend((ROOT/'main/cloud').glob('*.h'))
        inputs.extend((ROOT/'main/power').glob('*'))
        inputs.extend((HERE/'vendor/cjson').glob('*'))
        inputs.extend((ROOT/'main/assets').glob('*.h'))
        inputs.extend([ROOT/'main/settings.c',ROOT/'main/settings.h',ROOT/'main/font/ui_font.c',ROOT/'components/md4c/md4c.c',ROOT/'components/md4c/md4c.h'])
        return hashlib.sha256("|".join(f"{p}:{p.stat().st_mtime_ns}" for p in inputs).encode()).hexdigest()

    def storage_digest(self):
        entries=[]
        for name,folder in STORES.items():
            if folder.exists():
                for p in sorted(folder.rglob("*")):
                    st=p.lstat()
                    entries.append((name+"/"+str(p.relative_to(folder)),st.st_size,st.st_mtime_ns,st.st_mode))
        return hashlib.sha256(repr(entries).encode()).hexdigest()

    def capture_origin(self):
        self.replay_origin = {}
        for folder,base in STORES.items():
            if base.exists():
                for path in base.rglob('*'):
                    if path.is_symlink():
                        raise ValueError('模拟目录含符号链接，请移除后重试')
                    if path.is_dir() or path.is_file():
                        self.replay_origin[folder+"/"+str(path.relative_to(base))] = (None if path.is_dir() else path.read_bytes(), path.stat().st_mtime_ns)

    def restore_origin(self):
        if self.replay_origin is None:
            return
        if self.storage_signature is not None and self.storage_digest()!=self.storage_signature:
            raise ValueError("TF卡文件已在外部修改，请点击重新载入 TF卡，避免覆盖你的更改")
        for name,folder in STORES.items():
            if folder.exists():
                shutil.rmtree(folder)
            folder.mkdir(parents=True)
        for name, (content, modified) in self.replay_origin.items():
            store,relative=name.split("/",1)
            target = STORES[store]/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if content is None:target.mkdir(parents=True,exist_ok=True)
            else:target.write_bytes(content)
            import os
            os.utime(target, ns=(modified, modified))

    def render(self):
        signature = self.sources_signature()
        if signature != self.signature:
            # Preserve committed preview preferences; coordinates from older layouts cannot be replayed safely.
            # 保留已保存的预览设置；旧版排版坐标不在新版源码上重放。
            self.state.update(events=[],root_view=None)
            self.replay_origin=None
            subprocess.run(["bash", str(HERE / "build.sh")], cwd=ROOT, check=True,
                           capture_output=True, timeout=60)
            subprocess.run(["bash",str(ROOT/"tools/build_cloud_host.sh")],cwd=ROOT,check=True,capture_output=True,timeout=60)
            self.signature = signature
        cloud = self.state["screen"] == "cloud"
        view = VIEWS.get(self.state.get('root_view') or self.state['screen'], {})
        native = view.get('native', self.state['screen'])
        docs = self.documents(cloud)
        listing = BUILD / "titles.txt"
        listing.write_text("\n".join(d["title"].replace("\n", " ").replace("\r", " ") for d in docs[:8]), encoding="utf-8")
        path = BOOKS / self.state["active"] if self.state["active"] else FIXTURES / "阅读示例.md"
        if path.parent not in (BOOKS, FIXTURES):
            raise ValueError("invalid book path")
        if native=='cloud-0' or native.startswith('settings-'):
            path=CLOUD_PRIVATE/'ui-snapshot.json';path.write_text(json.dumps(self.cloud.snapshot(),ensure_ascii=False))
            if native=='cloud-0':aux=self.state.get('cloud_page',0)
        if native.startswith('book-'):
            path = self.state.get('root_path') if self.state['root_view'] else self.state['firmware_path']
            if native in ('book-1','book-2'):
                physical=CARD/path[8:] if isinstance(path,str) and path.startswith('/sdcard/') else BUILD/'vfs/flash'/path[7:] if isinstance(path,str) and path.startswith('/flash/') else Path(path or '')
                if not physical.is_file():
                    # 无有效阅读对象时仅在模拟器存储中准备示例，不修改用户TF卡。/ Keep fallback reading samples inside preview storage, never on the user's card.
                    sample=BUILD/'vfs/flash/books/阅读操作示例.txt'
                    sample.parent.mkdir(parents=True,exist_ok=True)
                    if not sample.exists():sample.write_text(('阅读操作示例。单击左侧或右侧翻页，单击中间打开控制面板。\n\n在“更多”的操作设置中，可以为滑动、长按和双指设置快捷功能；默认不启用。\n\n')*20,encoding='utf-8')
                    path='/flash/books/阅读操作示例.txt'
                    if self.state['root_view']:self.state['root_path']=path
                    else:self.state['firmware_path']=path

        mask = sum(1 << i for i, d in enumerate(docs[:8]) if d["id"] in self.state["selected"])
        if not cloud:
            mask = view.get('mask', 0)
        aux = self.state.get('root_page', 0) if self.state['root_view'] else self.state['page']
        px = self.state.get('root_px', self.state['px']) if self.state['root_view'] else self.state['px']
        if native.startswith('settings'):
            aux = self.state.get('root_scroll', 0) if self.state['root_view'] else self.state['scroll']
        elif native=='cloud-0':
            aux=self.state.get('cloud_page',0)
        elif native.startswith(('files-', 'transfer-')):
            aux = view.get('aux', 0)
        directory = self.state.get('root_dir', '') if self.state['root_view'] else ''
        frame_tmp = BUILD / "frame.next"
        seconds = ",".join(str(max(0, min(int(n), 1440)) * 60) for n in self.state["days"])
        if self.state['events'] and native!='cloud-0':
            self.restore_origin()
        backup_input=None
        process_env=None
        if native.startswith('settings-') and self.state['screen']=='settings-11' and self.state['events']:
            import os
            last_event=self.state['events'][-1].split(',')
            if last_event[0]=='t' and (561<=int(last_event[2])<644 or 681<=int(last_event[2])<764):
                if 561<=int(last_event[2])<644:
                    credentials=self.cloud.backup_export()
                else:
                    snapshot=self.cloud.snapshot()
                    credentials={'folder':snapshot['folder'],'account':snapshot['account'],'password':''}
                backup_input=json.dumps(credentials,ensure_ascii=False)
                process_env={**os.environ,'PICO_SIM_CLOUD_BACKUP_INPUT':'1'}
        if native.startswith('settings-'):
            import os
            process_env={**(process_env or os.environ),'PICO_SIM_BATTERY_DEMO':'1'}
        result = subprocess.run([str(BUILD / "render"), native, str(path), str(aux), str(px),
                                 str(frame_tmp), str(listing), str(mask), seconds, "builtin",
                                 ';'.join(self.state['events']), directory],
                                cwd=ROOT, capture_output=True, text=True, timeout=30,input=backup_input,env=process_env)
        if result.returncode:
            raise ValueError("界面渲染失败；测试图书可能已被删除，请改选其他图书")
        info = json.loads(result.stdout)
        restored_cloud=info.pop('cloud_restore',None)
        if restored_cloud is not None:
            self.cloud.backup_restore(restored_cloud)
            restored_cloud.clear()
        for field in ('font_path','font_builtin','system_font_path','reading_font_path','document_count','document_starred_only','document_menu','document_delete_confirm','document_first'):
            self.state[field]=info.get(field)
        if info.get('book_path'):
            self.state['firmware_path'] = info['book_path']
        route = info.get('route', -1)
        if route >= 0:
            target = ('baseline-home', 'book-0', 'files-home', 'settings-0', 'image-list', 'transfer-methods','cloud-sync')[route]
            if native=='cloud-0' and self.cloud.snapshot()['busy']:
                self.cloud.request_cancel();self.cloud_pending_view=target;self.state.update(events=[],root_view=None);return self.render()
            if route==3 and info.get('cloud_settings_requested'):
                target='settings-20';self.state['open_cloud_config']=True
            if route==3 and info.get('home_quote_requested'):target='home-quote'
            if route==6 and not self.cloud.snapshot()['busy']:self.cloud.start()
            if route == 1 and info.get('book_path'):
                target = 'reader'
            elif route == 5:
                target = {1:'transfer-networks', 2:'transfer-hotspot', 3:'transfer-usb', 4:'wifi-settings'}.get(info.get('transfer_entry'), target)
            # 跨页面指定文件沿用固件保存的阅读位置，其他页面仍从第一页开始。
            # File-open routes resume production progress; other pages still start on their first page.
            resume = route == 1 and bool(info.get('book_path'))
            self.state.update(screen=target, events=[], root_view=None, page=-1 if resume else 0)
            self.replay_origin=None
            return self.render()
        if native=='cloud-0':
            self.state['cloud_page']=info.get('cloud_page',0)
            command=info.get('cloud_command',0)
            self.state.update(events=[],root_view=None)
            if command:
                if command==1:self.cloud.start()
                elif command==2:self.cloud.toggle(info.get('cloud_index',-1))
                elif command==3:self.cloud.start(download=True)
                elif command==4:self.cloud.request_cancel()
                return self.render()
        if native.startswith('settings-') and info.get('cloud_command')==5:self.state['message']='请使用页面下方的坚果云连接配置。';self.state['open_cloud_config']=True
        if info.get('settings_page', -1) >= 0:
            self.state['screen'] = next((v["id"] for v in VIEWS.values() if v["native"]==f"settings-{info['settings_page']}"),"settings-0")
            if info['settings_page']==20:self.state['open_cloud_config']=True
        if info.get('cloud_backup_used') or info.get('quote_settings_committed') or info.get('markdown_committed'):
            self.state.update(events=[],root_view=None)
            self.replay_origin=None
        self.state['document_zoom']=info.get('document_zoom',False)
        self.state['image_top']=info.get('image_top',0)
        self.state['toc_count']=info.get('toc_count',0)
        self.state['document_image_ready']=info.get('document_image_ready',False)
        self.state['reader_progress']=info.get('reader_progress',0)
        if info.get('book_view', -1) >= 0:
            n = info['book_view']
            panel = info['reader_panel'] if n == 1 else 0
            # 全屏偏好不应盖过正在编辑的面板。/ A fullscreen preference must not hide the active settings panel.
            mask = panel if panel else 100 if info['reader_fullscreen'] else 0
            if n == 0:
                self.state['screen']='book-0'
            elif n == 1:
                self.state['screen'] = next((v['id'] for v in VIEWS.values() if v['native']=='book-1' and v['mask']==mask), 'reader')
            elif n == 2:
                self.state['screen'] = 'reader-toc-jump' if info.get('toc_jump') else 'reader-toc'
            else:
                self.state['screen'] = next((v['id'] for v in VIEWS.values() if v['native']==f'book-{n}'), 'book-0')
            self.state['page'] = info['reader_page']
            self.state['px'] = info['reader_px']
        if info.get('files_view', -1) >= 0:
            n, folder = info['files_view'], info['files_folder']
            self.state['files_dir'] = info.get('files_dir', '')
            self.state['screen'] = next((v['id'] for v in VIEWS.values() if v['native']==f'files-{n}' and (n != 0 or v['aux']==folder)), 'files-home')
        if info.get('transfer_view', -1) >= 0:
            n, mode = info['transfer_view'], info.get('transfer_mode', 0)
            self.state['screen'] = next((v['id'] for v in VIEWS.values() if v['native']==f'transfer-{n}' and ((mode==3 and v['aux']==3) or (mode!=3 and v['aux']!=3 and (n!=1 or v['aux']==mode)))), 'transfer-methods')
        if info.get('image_view', -1) >= 0:
            self.state['screen'] = f"image-{'view' if info['image_view'] else 'list'}"
        if native=='power' and info.get('system_view') in (0,2,3):
            target={0:self.state.get('power_return','baseline-home'),2:'shutdown',3:'restart'}[info['system_view']]
            self.state.update(screen=target,events=[],root_view=None)
            return self.render()
        self.state['pages'] = info['pages']
        self.state['page'] = max(0, min(self.state['page'], info['pages'] - 1))
        frame_tmp.replace(BUILD / 'frame.bin')
        self.storage_signature=self.storage_digest()
        self.state['version'] += 1
        self.state_path.write_text(json.dumps(self.state, ensure_ascii=False), encoding='utf-8')

    def snapshot(self):
        # 页面轮询时检查源文件与预览资源，代码更新后自动重新生成帧。
        # Polls rebuild changed native sources and refresh the frame.
        current=None
        try:
            cloud=self.cloud.snapshot()
            if cloud['revision']!=self.cloud_revision:
                self.cloud_revision=cloud['revision']
                if not cloud['busy'] and self.cloud_pending_view:
                    self.state.update(screen=self.cloud_pending_view,events=[],root_view=None);self.cloud_pending_view=None
                if self.state['screen']=='cloud-sync' or not cloud['busy']:
                    self.state.update(events=[],root_view=None);self.replay_origin=None;self.render()
            current=self.sources_signature()
            if current != self.signature and current!=self.failed_signature:
                self.render()
        except (subprocess.SubprocessError, ValueError, OSError) as exc:
            self.state["message"] = "预览编译或渲染暂时失败；保留上一帧"
            self.failed_signature=current
            print(f"preview refresh failed: {exc}", flush=True)
        return {**self.state, 'cloud_state':self.cloud.snapshot(), "books": self.documents(), "cloud": self.documents(True),
                'views':list(VIEWS.values()),'view_label':VIEWS.get(self.state['screen'],{}).get('label','Markdown阅读'),
                'web_version':hashlib.sha256((HERE/'index.html').read_bytes()).hexdigest()[:12],
                "baseline": "0.3.3-rc58", "width": 684, "height": 1216,
                "cloud_mode": "readonly-webdav"}

    def open_book(self, name):
        allowed = {d["id"] for d in self.documents()}
        if name not in allowed:
            raise ValueError("文档不存在")
        self.state.update(screen="reading", active=name, page=0)

    def action(self, data):
        action = data.get("action")
        self.state["message"] = ""
        if self.cloud.snapshot()['busy'] and action not in ('gesture','key','view','cloud-cancel','home','cloud','shelf','settings','files','contents'):
            raise CloudError('同步进行中，请先取消同步。')
        if action=='cloud-cancel':self.cloud.request_cancel();self.render();return self.snapshot()
        if action in ("home", "cloud", "shelf", "settings",'files','contents'):
            if self.cloud.snapshot()["busy"] and action!="cloud":
                self.cloud.request_cancel();self.cloud_pending_view={"home":"baseline-home","shelf":"shelf","settings":"settings-0","files":"files-home","contents":"book-0"}[action];self.render();return self.snapshot()
            self.state["screen"] = {'home':'baseline-home','settings':'settings-0','cloud':'cloud-sync','files':'files-home','contents':'book-0'}.get(action,action)
            self.state["page"] = 0
            self.state['scroll']=0
            self.state.update(events=[],root_view=None)
            if action=='cloud' and not self.cloud.snapshot()['busy']:self.cloud.start()
        elif action=='reload-card':
            self.state.update(screen="baseline-home",events=[],root_view=None,page=0,files_dir="",message="已重新读取项目 TF卡目录")
        elif action=='view':
            key=data.get('view')
            if not isinstance(key,str) or key not in VIEWS: raise ValueError('界面不存在')
            if key=='power':
                self.state['power_return']=self.state['screen']
                shutil.copyfile(BUILD/'frame.bin',BUILD/'power-background.bin')
            if self.cloud.snapshot()['busy'] and key!='cloud-sync':
                self.cloud.request_cancel();self.cloud_pending_view=key;self.render();return self.snapshot()
            self.state.update(screen=key,page=0,scroll=0,events=[],root_view=None)
            if key=='cloud-sync' and not self.cloud.snapshot()['busy']:self.cloud.start()
        elif action in ('gesture','key'):
            screen=self.state['screen']
            if action=='key':
                key=data.get('key')
                if type(key) is not int or not 0<=key<=2:raise ValueError('按键无效')
                event=f'k,{key},0,0,0'
            else:
                kind=data.get('type')
                coords=[data.get(k) for k in ('x','y','xx','yy')]
                if kind not in ('p','m','c','t','l','u','d','r','a','q') or any(type(n) is not int or not 0<=n<(684 if i%2==0 else 1216) for i,n in enumerate(coords)):raise ValueError('手势无效')
                event=kind+','+','.join(map(str,coords))
            native = VIEWS.get(screen, {}).get('native', '')
            if native=='cloud-0':
                self.state.update(events=[event],root_view=None)
            elif self.cloud.snapshot()['busy']:
                raise CloudError('同步进行中，请先取消同步。')
            elif native.startswith(('settings-', 'book-', 'files-', 'transfer-', 'image-')) or native in ('baseline-home','power','menu'):
                if not self.state['root_view']:
                    self.state.update(root_view=screen, root_page=self.state['page'], root_px=self.state['px'],
                                      root_path=self.state['firmware_path'], root_dir=self.state['files_dir'],
                                      root_scroll=self.state['scroll'])
                    self.capture_origin()
                if len(self.state['events'])>=500:
                    raise ValueError('当前模拟会话过长，请重新进入该界面')
                self.state['events'].append(event)
            elif action=='key':
                if screen.startswith('reader') and key==1:self.state['screen']='reader' if screen=='reader-tools' else 'reader-tools'
                elif key==1:self.state['screen']='baseline-home'
                else:return self.action({'action':'previous' if key==0 else 'next'})
            elif kind in ('t','l'):return self.action({'action':'tap','x':coords[0],'y':coords[1]})
            elif screen.startswith('reader'):return self.action({'action':'next' if kind in ('u','a') else 'previous'})
            else:self.state['scroll']=max(0,min(200,self.state['scroll']+(100 if kind=='u' else -100)))
        elif action=='scroll':
            self.state['scroll']=max(0,min(200,self.state['scroll']+int(data.get('delta',0))))
        elif action in ("next", "previous"):
            if self.state["screen"].startswith("reader"):
                return self.action({"action":"key","key":2 if action=="next" else 0})
            self.state["page"] = max(0, min(self.state["pages"] - 1, self.state["page"] + (1 if action == "next" else -1)))
        elif action == "font":
            px = data.get("px")
            if px not in (32,36,38,40,44,48,50,56,64,68,72):
                raise ValueError("字号无效")
            self.state.update(px=px, page=0, events=[], root_view=None)
        elif action == "open":
            self.open_book(data.get("id"))
        elif action == "days":
            values = data.get("days")
            if not isinstance(values, list) or len(values) not in (7, 30) or any(type(n) is not int or not 0 <= n <= 1440 for n in values):
                raise ValueError("阅读时长需要最近7天或30天的分钟数（0至1440）")
            self.state["days"] = self.state["days"][:23] + values if len(values) == 7 else values
        elif action == "download":
            selected = set(self.state["selected"])
            count = 0
            for doc in self.documents(True):
                if doc["id"] in selected:
                    tmp = BOOKS / (doc["id"] + ".part")
                    shutil.copyfile(FIXTURES / doc["id"], tmp)
                    tmp.replace(BOOKS / doc["id"])
                    count += 1
            self.state.update(screen="home", selected=[], page=0,
                              message=f"已把 {count} 篇示例文档复制到模拟书库；没有访问坚果云")
        elif action == "tap":
            x, y = data.get("x"), data.get("y")
            if type(x) is not int or type(y) is not int or not (0 <= x < 684 and 0 <= y < 1216):
                raise ValueError("坐标无效")
            screen = self.state["screen"]
            if y >= 1136 and not screen.startswith('reader') and screen != "reading" and screen not in ('power','shutdown','restart'):
                self.state["screen"] = ('baseline-home','book-0','files-home','settings-0')[min(x // 171, 3)]
            elif screen in VIEWS and screen not in ('home','cloud','shelf'):
                self.baseline_tap(screen,x,y)
            elif screen == "home":
                docs = self.documents()
                if 176 <= y < 540:
                    if docs:
                        self.open_book(self.state["active"] if self.state["active"] in {d["id"] for d in docs} else docs[0]["id"])
                    else:
                        self.state.update(screen="reading", active="", page=0)
                elif 953 <= y < 1094:
                    index = (y - 953) // 43
                    if index < len(docs) and index < 3:
                        self.open_book(docs[index]["id"])
            elif screen in ("cloud", "shelf"):
                docs = self.documents(screen == "cloud")
                index = (y - 260) // 88
                if screen == "cloud" and 982 <= y <= 1068:
                    return self.action({"action": "download"})
                if y >= 260 and 0 <= index < min(len(docs), 8):
                    if screen == "cloud":
                        selected = set(self.state["selected"])
                        if docs[index]["id"] in selected:
                            selected.remove(docs[index]["id"])
                        else:
                            selected.add(docs[index]["id"])
                        self.state["selected"] = sorted(selected)
                    else:
                        self.open_book(docs[index]["id"])
            elif screen == "reading":
                return self.action({"action": "previous" if x < 342 else "next"})
        else:
            raise ValueError("不支持的操作")
        self.render()
        return self.snapshot()

    def baseline_tap(self,screen,x,y):
        native = VIEWS.get(screen, {}).get('native', '')
        if native.startswith(('settings-', 'book-', 'files-', 'transfer-', 'image-')) or native in ('baseline-home', 'power', 'menu'):
            # Legacy tap callers use the same production hit testing as pointer gestures.
            self.action({'action':'gesture','type':'t','x':x,'y':y,'xx':x,'yy':y})
            return
        def go(key): self.state.update(screen=key,page=0,scroll=0)
        if screen=='baseline-home':
            if 230<=y<555: go('reader')
            elif 176<=y<225 and x>480: go('book-details')
        elif screen=='book-0':
            if 80<=y<190 and x>480: go('book-bulk' if x<575 else 'book-import')
            elif 200<=y<1040: go('reader')
        elif screen.startswith('reader'):
            if screen=='reader':
                if 230<x<454: go('reader-fullscreen')
                else: self.state['page']=max(0,min(self.state['pages']-1,self.state['page']+(1 if x>=454 else -1)))
            elif screen=='reader-fullscreen': go('reader')
            elif y<190: go('reader')
            elif screen=='reader-tools':
                if y>=1096:go(('reader-toc','reader-bookmarks','reader-stats','reader-refresh','reader-font')[min(4,max(0,x//137))])
                else:go('reader')
            elif screen=='reader-toc' and 250<y<950: go('reader')
        elif screen=='files-home':
            if 266<y<366: go(('transfer-wifi','transfer-hotspot','transfer-methods')[min(2,x//228)])
            elif 550<y<1050: go(('files-books','files-pictures','files-fonts','files-card')[min(3,(y-550)//120)])
        elif screen.startswith('files-'):
            if y<190: go('files-home')
            elif screen=='files-actions': go('files-rename' if x<342 else 'files-move')
            elif 210<y<850: go('files-actions')
        elif screen=='settings-0':
            row_y=y+self.state['scroll']
            if 160<row_y<270: go('settings-14')
            elif 315<=row_y<383: go('settings-1')
            elif 505<=row_y<845: go(('settings-5','settings-6','settings-7','settings-19','settings-0')[(row_y-505)//68])
            elif 876<=row_y<1216: go(('settings-8','settings-13','settings-11','settings-2','settings-12')[min(4,(row_y-876)//68)])
        elif screen.startswith('settings-'):
            if y<190: go('settings-0')
            elif screen=='settings-1' and 408<=y<478: go('transfer-networks')
            elif screen=='settings-2' and 518<=y<600: go('settings-3')
            elif screen=='settings-8' and y>600: go('settings-9')
            elif screen=='settings-9' and y>290: go('settings-10')
            elif screen=='settings-14': go('settings-15' if y>366 else 'settings-16')
        elif screen=='transfer-methods': go(('transfer-networks','transfer-hotspot','transfer-wifi')[min(2,max(0,(y-230)//170))])
        elif screen.startswith('transfer-'):
            if y<190: go('files-home')
            elif screen=='transfer-networks' and 260<y<850: go('transfer-password')
        elif screen=='image-list': go('image-view' if y>210 else 'files-home')
        elif screen=='image-view': go('image-list')
        elif screen=='power':
            if 494<=y<650 and 146<=x<302:go('restart')
            elif 494<=y<650 and 382<=x<538:go('shutdown')
            elif not(92<=x<592 and 378<=y<748):go('baseline-home')
        elif screen=='menu': go(('baseline-home','book-0','files-home','settings-0','image-list')[min(4,max(0,(y-176)//150))])
        elif screen.startswith('book-') and y<190: go('book-0')

    def upload(self, data):
        name = data.get("name", "")
        content = data.get("content")
        if not isinstance(name, str) or not name.lower().endswith(".md") or not name or Path(name).name != name or "\\" in name or any(ord(c) < 32 for c in name):
            raise ValueError("请选择普通 Markdown 文件")
        if len(name.encode("utf-8")) > 200 or not isinstance(content, str):
            raise ValueError("文件名或内容无效")
        encoded = content.encode("utf-8")
        if len(encoded) > MAX_INPUT or b"\x00" in encoded:
            raise ValueError("首版限制 UTF-8 正文 256 KiB，不能包含零字节")
        # 先解析临时文件，失败不覆盖旧模拟书籍。
        # Validate a staging file before replacing an existing simulated book.
        target_dir=CARD/"markdown"
        target_dir.mkdir(parents=True,exist_ok=True)
        tmp = target_dir / (name + ".part")
        tmp.write_bytes(encoded)
        probe = subprocess.run([str(BUILD / "render"), "markdown-probe", "/sdcard/markdown/"+name+".part", "0", str(self.state["px"]),
                                str(BUILD / "probe.bin"), str(BUILD / "titles.txt"), "0", "0,0,0,0,0,0,0", "builtin"],
                               cwd=ROOT, capture_output=True, timeout=30)
        if probe.returncode:
            tmp.unlink(missing_ok=True)
            raise ValueError("Markdown 或关联插图校验失败，原文档已保留；请先把插图放入 TF卡/markdown/attachments")
        tmp.replace(target_dir / name)
        self.state.update(screen='reader',root_view=None,events=[],page=0,firmware_path='/sdcard/markdown/'+name)
        self.render()
        return self.snapshot()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def reply(self, code, body, kind="application/json; charset=utf-8"):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def origin_ok(self):
        host = self.headers.get("Host", "")
        origin = self.headers.get("Origin")
        return host in (f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}") and (origin is None or origin in (f"http://{host}",))

    def do_GET(self):
        if not self.origin_ok():
            return self.reply(403, {"error": "origin denied"})
        path = urlsplit(self.path).path
        with LOCK:
            if path == "/api/state":
                return self.reply(200, PREVIEW.snapshot())
            if path == "/api/frame":
                return self.reply(200, (BUILD / "frame.bin").read_bytes(), "application/octet-stream")
            if path in ("/", "/index.html"):
                return self.reply(200, (HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
        self.reply(404, {"error": "not found"})

    def do_POST(self):
        if not self.origin_ok() or not self.headers.get("Content-Type", "").startswith("application/json"):
            return self.reply(403, {"error": "origin or content type denied"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_INPUT * 6 + 4096:
                return self.reply(413, {"error": "请求过大"})
            data = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(data, dict):
                raise ValueError("请求格式无效")
            with LOCK:
                path = urlsplit(self.path).path
                saved=copy.deepcopy(PREVIEW.state)
                try:
                    if path == "/api/input":
                        result = PREVIEW.action(data)
                    elif path=="/api/cloud/config":
                        PREVIEW.cloud.configure(data);PREVIEW.state.update(events=[],root_view=None,screen='cloud-sync');PREVIEW.replay_origin=None;PREVIEW.cloud.start();PREVIEW.render();result=PREVIEW.snapshot()
                    elif path=="/api/cloud/forget":
                        PREVIEW.cloud.forget();PREVIEW.state.update(events=[],root_view=None);PREVIEW.replay_origin=None;PREVIEW.render();result=PREVIEW.snapshot()
                    elif path == "/api/upload":
                        result = PREVIEW.upload(data)
                    else:
                        return self.reply(404, {"error": "not found"})
                except (ValueError,OSError,subprocess.SubprocessError,TypeError,CloudError):
                    PREVIEW.state=saved
                    raise
            self.reply(200, result)
        except (ValueError, OSError, subprocess.SubprocessError,TypeError,CloudError) as exc:
            self.reply(400, {"error": str(exc)})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    PREVIEW = Preview()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Pico preview: http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
