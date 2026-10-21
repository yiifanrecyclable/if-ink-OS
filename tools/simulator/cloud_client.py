# SPDX-License-Identifier: Apache-2.0
"""Read-only Nutstore transport. Production C lists reading/image/TTF files and commits TF transactions.
Passwords live in macOS Keychain; only explicit settings backups contain plaintext credentials.
日常密码保存在钥匙串；仅用户主动保存的设置备份包含明文凭据。
"""
from __future__ import annotations
from pathlib import Path
import base64,ctypes,shutil,datetime,email.utils,hashlib,http.client,json,os,ssl,subprocess,threading,time,unicodedata,urllib.error,urllib.parse,urllib.request
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/simulator'
CARD=ROOT.parent/'TF卡'
PRIVATE=BUILD/'cloud'
STAGE=CARD/'.pico-sync/current'
MAX_MD=256*1024
MAX_FILE=64*1024*1024
MAX_PICTURE=2*1024*1024
MAX_IMAGE=17*1024*1024
MAX_ASSETS=32*1024*1024
PROPFIND=b'<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:resourcetype/><d:getcontentlength/><d:getetag/><d:creationdate/><d:getlastmodified/></d:prop></d:propfind>'
class CloudError(Exception):
    def __init__(self,message,*,fatal=False,indicator=None):
        super().__init__(message);self.fatal=fatal;self.indicator=indicator
class Cancelled(CloudError):pass

def relative(path:str)->str:
    if not isinstance(path,str) or not path or path.startswith('/') or '\\' in path or any(ord(c)<32 or ord(c)==127 for c in path) or any(part in ('','.','..') for part in path.split('/')) or len(path.encode())>240:
        raise CloudError('同步文件夹或文件路径无效。')
    return unicodedata.normalize('NFC',path)

def config_valid(account:str,folder:str)->dict:
    if not isinstance(account,str) or not account.strip() or len(account.encode())>128 or any(c in account for c in ':\r\n\0'):
        raise CloudError('请填写有效的坚果云账号。')
    if not isinstance(folder,str):raise CloudError('请填写同步文件夹。')
    return {'account':account.strip(),'folder':relative(folder.strip('/'))}

class Keychain:
    """Security.framework calls avoid putting an application password in a process argument."""
    SERVICE='com.pico-reader.cloud-preview'
    def __init__(self):
        self.cf=ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
        self.sec=ctypes.CDLL('/System/Library/Frameworks/Security.framework/Security')
        self.cf.CFStringCreateWithCString.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint32];self.cf.CFStringCreateWithCString.restype=ctypes.c_void_p
        self.cf.CFDataCreate.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_long];self.cf.CFDataCreate.restype=ctypes.c_void_p
        self.cf.CFDictionaryCreate.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_void_p),ctypes.c_long,ctypes.c_void_p,ctypes.c_void_p];self.cf.CFDictionaryCreate.restype=ctypes.c_void_p
        self.cf.CFRelease.argtypes=[ctypes.c_void_p]
        self.cf.CFDataGetLength.argtypes=[ctypes.c_void_p];self.cf.CFDataGetLength.restype=ctypes.c_long
        self.cf.CFDataGetBytePtr.argtypes=[ctypes.c_void_p];self.cf.CFDataGetBytePtr.restype=ctypes.c_void_p
        self.sec.SecItemCopyMatching.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p)];self.sec.SecItemCopyMatching.restype=ctypes.c_int32
        self.sec.SecItemAdd.argtypes=[ctypes.c_void_p,ctypes.c_void_p];self.sec.SecItemAdd.restype=ctypes.c_int32
        self.sec.SecItemUpdate.argtypes=[ctypes.c_void_p,ctypes.c_void_p];self.sec.SecItemUpdate.restype=ctypes.c_int32
        self.sec.SecItemDelete.argtypes=[ctypes.c_void_p];self.sec.SecItemDelete.restype=ctypes.c_int32
    def constant(self,name):return ctypes.c_void_p.in_dll(self.sec,name).value
    def dictionary(self,account,*,secret=None,read=False,attributes=False):
        owned=[];pairs=[]
        def string(value):
            ref=self.cf.CFStringCreateWithCString(None,value.encode(),0x08000100);owned.append(ref);return ref
        if not attributes:
            pairs=[(self.constant('kSecClass'),self.constant('kSecClassGenericPassword')),(self.constant('kSecAttrService'),string(self.SERVICE)),(self.constant('kSecAttrAccount'),string(account))]
        if secret is not None:
            data=secret.encode();ref=self.cf.CFDataCreate(None,data,len(data));owned.append(ref);pairs.append((self.constant('kSecValueData'),ref))
        if read:pairs.extend([(self.constant('kSecReturnData'),ctypes.c_void_p.in_dll(self.cf,'kCFBooleanTrue').value),(self.constant('kSecMatchLimit'),self.constant('kSecMatchLimitOne'))])
        keys=(ctypes.c_void_p*len(pairs))(*(key for key,_ in pairs));values=(ctypes.c_void_p*len(pairs))(*(value for _,value in pairs))
        result=self.cf.CFDictionaryCreate(None,keys,values,len(pairs),None,None)
        if not result:raise CloudError('无法使用Mac钥匙串。')
        return result,owned
    def release(self,pair):
        ref,owned=pair;self.cf.CFRelease(ref)
        for value in owned:self.cf.CFRelease(value)
    def get(self,account):
        query=self.dictionary(account,read=True);data=ctypes.c_void_p()
        try:code=self.sec.SecItemCopyMatching(query[0],ctypes.byref(data))
        finally:self.release(query)
        if code==-25300:return None
        if code or not data.value:raise CloudError('无法读取Mac钥匙串，请检查系统授权。')
        try:return ctypes.string_at(self.cf.CFDataGetBytePtr(data),self.cf.CFDataGetLength(data)).decode()
        finally:self.cf.CFRelease(data)
    def save(self,account,secret):
        query=self.dictionary(account,secret=secret)
        try:code=self.sec.SecItemAdd(query[0],None)
        finally:self.release(query)
        if code==-25299:
            match=self.dictionary(account);attributes=self.dictionary(account,secret=secret,attributes=True)
            try:code=self.sec.SecItemUpdate(match[0],attributes[0])
            finally:self.release(match);self.release(attributes)
        if code:raise CloudError('应用密码未能保存到Mac钥匙串。')
    def forget(self,account):
        query=self.dictionary(account)
        try:code=self.sec.SecItemDelete(query[0])
        finally:self.release(query)
        if code not in (0,-25300):raise CloudError('无法删除本工具的钥匙串记录。')

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None

class NativeBridge:
    def __init__(self,binary=BUILD/'cloud-host'):self.binary=binary
    def call(self,*args):
        result=subprocess.run([str(self.binary),*map(str,args)],cwd=ROOT,capture_output=True,timeout=60,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
        if result.returncode:raise CloudError('本地校验或恢复未能完成，原文件和备份已保留。')
        try:return json.loads(result.stdout)
        except (ValueError,UnicodeDecodeError):raise CloudError('本地校验返回无效结果。') from None
    def require(self,*args):
        result=self.call(*args)
        if result.get('error'):raise CloudError('本地校验或恢复失败，原文件和备份已保留。')
        return result

def stamp(value):
    if not value:return 0
    try:return int(datetime.datetime.fromisoformat(value.replace('Z','+00:00')).timestamp()*1000)
    except (ValueError,OverflowError):
        try:return int(email.utils.parsedate_to_datetime(value).timestamp()*1000)
        except (ValueError,TypeError,OverflowError):return 0

def etag_version(etag):
    # Nutstore DAV and GET expose opaque tokens without HTTP's surrounding quotes.
    # 裸串可比较版本；弱或非法串不作为条件标记，下载仍可二次核对正文。
    if not isinstance(etag,str) or not etag or len(etag)>128 or etag.startswith('W/'):return None
    value=etag[1:-1] if len(etag)>=2 and etag.startswith('"') and etag.endswith('"') else etag
    return value if value and all(33<=ord(c)<=126 and c!='"' for c in value) else None

def strong(etag):return etag_version(etag) is not None and etag.startswith('"') and etag.endswith('"')
def same_version(expected,actual):return etag_version(expected) is not None and etag_version(expected)==etag_version(actual)

def file_policy(filename):
    suffix=Path(filename).suffix.lower()
    if suffix=='.md':return '/sdcard/markdown',MAX_MD
    if suffix in ('.epub','.txt'):return '/sdcard/books',MAX_FILE
    if suffix in ('.png','.jpg','.jpeg'):return '/sdcard/pictures',MAX_PICTURE
    if suffix=='.ttf':return '/sdcard/fonts',MAX_FILE
    raise CloudError('不支持此文件类型。')

class DavClient:
    def __init__(self,config,secret,cancel=None,progress=None,bridge=None,*,test_base=None,storage_lock=None):
        self.storage_lock=storage_lock or threading.RLock();
        self.config=config_valid(config['account'],config['folder']);self.cancel=cancel or threading.Event();self.progress=progress or (lambda **fields:None);self.bridge=bridge or NativeBridge()
        self.base='https://dav.jianguoyun.com/dav/'+urllib.parse.quote(self.config['folder'],safe='/')+'/'
        # test_base is injection-only; it is never accepted from the settings API.
        if test_base:self.base=test_base
        self.decoded_folder=urllib.parse.unquote(urllib.parse.urlsplit(self.base).path,errors='strict')
        self.auth='Basic '+base64.b64encode((self.config['account']+':'+secret).encode()).decode()
        self.opener=urllib.request.build_opener(NoRedirect(),urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        scope=hashlib.sha256((self.base+'\0'+self.config['account']).encode()).hexdigest()
        self.cache=PRIVATE/'cache'/scope;self.cache.mkdir(parents=True,exist_ok=True)
        self.budget_file=self.cache/'request-budget.json';self.requests=[]
        try:self.requests=[t for t in json.loads(self.budget_file.read_text()) if isinstance(t,(int,float)) and t>time.time()-1800]
        except (OSError,ValueError,TypeError):pass
    def check(self):
        if self.cancel.is_set():raise Cancelled('已取消；已经入库的文件会保留。')
    def budget(self):
        self.check();now=time.time();self.requests=[t for t in self.requests if t>now-1800]
        if len(self.requests)>=450:raise CloudError('本工具已达到本轮访问预算，请稍后继续。',fatal=True,indicator='访问受限，稍后重试')
        self.requests.append(now);tmp=self.budget_file.with_suffix('.part');tmp.write_text(json.dumps(self.requests));os.chmod(tmp,0o600);tmp.replace(self.budget_file)
    def request(self,method,path,destination,maximum,*,expected=None,sha=None,headers=None):
        if method not in ('GET','PROPFIND'):raise CloudError('下载客户端仅允许读取。')
        if path:relative(path)
        url=self.base+urllib.parse.quote(path,safe='/');headers={'Authorization':self.auth,'Accept-Encoding':'identity',**(headers or {})}
        if method=='PROPFIND':headers.update({'Depth':'1','Content-Type':'application/xml; charset=utf-8'})
        for retry in range(3):
            self.budget();request=urllib.request.Request(url,data=PROPFIND if method=='PROPFIND' else None,headers=headers,method=method)
            try:response=self.opener.open(request,timeout=5)
            except urllib.error.HTTPError as error:
                code=error.code;wait=error.headers.get('Retry-After','1');error.close()
                if code in (429,503) and retry<2:
                    try:delay=float(wait)
                    except ValueError:raise CloudError('坚果云要求稍后重试，请稍后手动刷新。',fatal=True,indicator='访问受限，稍后重试') from None
                    if delay<0 or delay>10:raise CloudError('坚果云暂时限制访问，请稍后重试。',fatal=True,indicator='访问受限，稍后重试') from None
                    self.progress(message='坚果云繁忙，稍后重试…')
                    if self.cancel.wait(delay):self.check()
                    continue
                if code in (401,403):raise CloudError(f'账号或应用密码不可用（HTTP {code}）。',fatal=True,indicator='账号或应用密码错误') from None
                if code==404:raise CloudError('同步文件夹不存在，请检查设置。' if method=='PROPFIND' else '云端内容不存在，请刷新目录。',indicator='同步文件夹不存在' if method=='PROPFIND' else '云端内容已删除') from None
                if code==412:raise CloudError('云端内容已更新，请刷新目录后重新选择。') from None
                if 300<=code<400:raise CloudError('目录地址发生跳转，请检查同步文件夹设置。') from None
                raise CloudError(f'云端读取失败（HTTP {code}）。',fatal=code in (429,503),indicator='访问受限，稍后重试' if code in (429,503) else '云端读取失败') from None
            except (urllib.error.URLError,TimeoutError,OSError):raise CloudError('连接失败，请检查电脑网络、系统时间与证书。',indicator='网络连接失败') from None
            with response:
                if response.status != (207 if method=='PROPFIND' else 200):raise CloudError('云端返回了不支持的读取结果。')
                if response.headers.get('Content-Encoding','identity').lower() not in ('','identity'):raise CloudError('云端返回了不支持的压缩编码。')
                length=response.headers.get('Content-Length');declared=None
                if length is not None:
                    try:declared=int(length)
                    except ValueError:raise CloudError('云端文件长度无效。') from None
                    if declared<0 or declared>maximum or (expected is not None and declared!=expected):raise CloudError('云端文件长度不符合清单，原内容已保留。')
                count=0;digest=hashlib.sha256();destination.parent.mkdir(parents=True,exist_ok=True)
                try:
                    with destination.open('wb') as output:
                        while True:
                            self.check();block=response.read(4096)
                            if not block:break
                            count+=len(block)
                            if count>maximum or (expected is not None and count>expected):raise CloudError('下载内容超过清单上限，原内容已保留。')
                            output.write(block);digest.update(block);self.progress(bytes_done=count,bytes_total=expected or declared or 0)
                        output.flush();os.fsync(output.fileno())
                except (http.client.HTTPException,TimeoutError,OSError):raise CloudError('下载中断，原内容已保留，请检查网络。',indicator='下载连接中断') from None
                self.check()
                if (declared is not None and count!=declared) or (expected is not None and count!=expected) or (sha is not None and digest.hexdigest()!=sha):raise CloudError('下载长度或SHA256校验失败，原内容已保留。')
                return {'bytes':count,'sha256':digest.hexdigest(),'etag':response.headers.get('ETag','')}
        raise CloudError('云端读取未完成。')
    def listing(self):
        self.progress(message=f'正在读取{self.config["folder"]}目录…');file=self.cache/'listing.xml'
        self.request('PROPFIND','',file,512*1024)
        parsed=self.bridge.require('listing',file,self.decoded_folder)
        if parsed.get('truncated'):raise CloudError('目录条目过多或可能未完整，请拆分同步文件夹（最多96项可读内容）。')
        items=[];rows=parsed['entries']
        for number,row in enumerate(rows,1):
            self.check();self.progress(message=f'正在检查内容 {number}/{len(rows)}')
            item={'href':row['href'],'etag':row['etag'],'manifest':row['manifest'],'selected':False,'selectable':True,'error':'','bytes':row['bytes']}
            date=row['created_at'] or row['modified'];item.update(name=row['href'],filename=row['href'],date=date,date_ms=stamp(date),date_fallback=not bool(row['created_at']))
            _,limit=file_policy(row['href'])
            if row['bytes']>limit:item.update(selectable=False,error='超过本阶段单文件大小限制。')
            items.append(item)
        items.sort(key=lambda item:(-item['date_ms'],item['name']))
        return items
    def download_one(self,item):
        self.check();assets=[]
        if item.get('manifest') or item['href']!=item['filename']:raise CloudError('旧版推送请使用新版插件重新发送。')
        folder,limit=file_policy(item['filename'])
        if item['bytes']>limit:raise CloudError('文件超过大小上限。')
        if shutil.disk_usage(CARD).free<item['bytes']*2+17*1024*1024:raise CloudError('TF卡剩余空间不足，请先清理空间。')
        with self.storage_lock:self.bridge.require('prepare')
        try:
            body_result=self.request('GET',item['href'],STAGE/'body',limit,expected=item['bytes'],headers={'If-Match':item['etag']} if strong(item['etag']) else {})
            if etag_version(item['etag']) and etag_version(body_result['etag']) and not same_version(item['etag'],body_result['etag']):raise CloudError('云端文件版本已变化，请刷新目录。')
            if item['filename'].lower().endswith('.md'):
                links=self.bridge.require('links')['names'];total=0
                for name in links:
                    result=self.request('GET','attachments/'+name,STAGE/'attachments'/name,MAX_IMAGE);total+=result['bytes']
                    if total>MAX_ASSETS:raise CloudError('图片附件总量超过32MiB。')
                    assets.append({'name':name,'bytes':result['bytes'],'sha256':result['sha256']})
            if not strong(item['etag']):
                self.progress(message='正在复核文件版本…');verification=self.cache/'verify-body.part'
                try:
                    checked=self.request('GET',item['href'],verification,limit,expected=body_result['bytes'],sha=body_result['sha256'])
                    if etag_version(item['etag']) and etag_version(checked['etag']) and not same_version(item['etag'],checked['etag']):raise CloudError('文章在下载期间更新，请刷新后重新选择。')
                finally:verification.unlink(missing_ok=True)
            self.check();plan={'body':{'bytes':body_result['bytes'],'sha256':body_result['sha256']},'assets':assets}
            plan_file=self.cache/'commit-plan.json';plan_file.write_text(json.dumps(plan))
            if folder=='/sdcard/books':folder=self.bridge.require('bookdir')['path']
            if not folder.startswith('/sdcard/'):raise CloudError('保存目录必须位于TF卡。')
            target=folder+'/'+item['filename'];self.progress(message='正在校验并入库…')
            with self.storage_lock:return self.bridge.require('commit',target,plan_file)
        except BaseException:
            with self.storage_lock:self.bridge.call('abort')
            raise
    def close(self):self.auth=''

class CloudController:
    def __init__(self,storage_lock=None,*,keychain_factory=Keychain,client_factory=DavClient,private_dir=PRIVATE):
        self.lock=threading.RLock();self.storage_lock=storage_lock or threading.RLock();self.keychain_factory=keychain_factory;self.client_factory=client_factory
        private_dir.mkdir(parents=True,exist_ok=True);os.chmod(private_dir,0o700);self.config_file=private_dir/'config.json';self.config={'folder':'PicoBox','account':'','configured':False}
        try:
            value=json.loads(self.config_file.read_text())
            if value.get('account')=='':self.config={'account':'','folder':relative(value['folder']),'configured':False}
            else:self.config={**config_valid(value['account'],value['folder']),'configured':bool(value.get('configured'))}
        except (OSError,ValueError,KeyError,TypeError,CloudError):pass
        self.entries=[];self.cancel=threading.Event();self.thread=None;self.revision=0;self.status={'busy':False,'indicator':'待读取目录' if self.config['configured'] else '未配置账号','message':'请点刷新读取目录。' if self.config['configured'] else '请先配置坚果云账号和应用密码。','done':0,'total':0,'failed':0,'bytes_done':0,'bytes_total':0}
    def snapshot(self):
        with self.lock:return {**self.status,'revision':self.revision,'configured':self.config['configured'],'folder':self.config['folder'],'account':self.config['account'],'count':len(self.entries),'cancel_requested':self.cancel.is_set(),'entries':[{k:v for k,v in item.items() if not k.startswith('_')} for item in self.entries]}
    def update(self,**fields):
        with self.lock:self.status.update(fields);self.revision+=1
    def configure(self,data):
        if self.status['busy']:raise CloudError('请先取消正在进行的同步。')
        config=config_valid(data.get('account'),data.get('folder','PicoBox'));secret=data.get('password','')
        if not isinstance(secret,str) or len(secret.encode())>256 or any(c in secret for c in '\r\n\0'):raise CloudError('应用密码无效。')
        keychain=self.keychain_factory()
        if secret:keychain.save(config['account'],secret)
        elif not keychain.get(config['account']):raise CloudError('首次配置需要填写坚果云应用密码。')
        self.config={**config,'configured':True};temporary=self.config_file.with_suffix('.part');temporary.write_text(json.dumps(self.config,ensure_ascii=False));os.chmod(temporary,0o600);temporary.replace(self.config_file)
        self.entries=[];self.update(indicator='待读取目录',message='已保存到Mac钥匙串，可开始读取目录。');return self.snapshot()
    def forget(self):
        if self.status['busy']:raise CloudError('请先取消正在进行的同步。')
        if self.config['account']:self.keychain_factory().forget(self.config['account'])
        self.config_file.unlink(missing_ok=True);self.config={'folder':'PicoBox','account':'','configured':False};self.entries=[];self.update(indicator='未配置账号',message='已移除本工具的坚果云配置。');return self.snapshot()
    def backup_export(self):
        # 用户保存配置时读取，交给原生备份编码器；不加入公开快照。/ Read for explicit backup only, never public snapshots.
        with self.lock:
            if self.status['busy']:raise CloudError('请先取消正在进行的同步，再保存或恢复配置。')
            secret=self.keychain_factory().get(self.config['account']) if self.config['account'] else None
            if self.config['configured'] and not secret:raise CloudError('无法读取应用密码，设置备份未保存；请重新填写密码。')
            return {'folder':self.config['folder'],'account':self.config['account'],'password':secret or ''}
    def backup_restore(self,data):
        # 此字典来自已校验的原生备份，只通过进程管道传递。/ Input comes from validated native backup through a private process pipe.
        with self.lock:
            if self.status['busy']:raise CloudError('请先取消正在进行的同步，再恢复配置。')
            account=data.get('account');folder=data.get('folder');secret=data.get('password')
            if not isinstance(account,str) or len(account.encode())>128 or any(c in account for c in ':\r\n\0'):
                raise CloudError('备份中的坚果云账号无效。')
            if not isinstance(secret,str) or len(secret.encode())>256 or any(c in secret for c in '\r\n\0'):
                raise CloudError('备份中的坚果云应用密码无效。')
            folder=relative(folder)
            if secret and not account:raise CloudError('此备份的坚果云配置未填完整，请在Pico真机恢复。')
            keychain=self.keychain_factory()
            current_secret=keychain.get(account) if account else ''
            if self.config=={'folder':folder,'account':account,'configured':bool(account and secret)} and (current_secret or '')==secret:
                return
            if account:
                if secret:keychain.save(account,secret)
                else:keychain.forget(account)
            elif self.config['account']:keychain.forget(self.config['account'])
            self.config={'folder':folder,'account':account,'configured':bool(account and secret)}
            temporary=self.config_file.with_suffix('.part');temporary.write_text(json.dumps(self.config,ensure_ascii=False));os.chmod(temporary,0o600);temporary.replace(self.config_file)
            self.entries=[];self.update(indicator='待读取目录' if self.config['configured'] else '未配置账号',message='坚果云配置已从设置备份恢复。')
    def toggle(self,index):
        with self.lock:
            if self.status['busy']:return
            if type(index) is not int or not 0<=index<len(self.entries):raise CloudError('条目无效。')
            item=self.entries[index]
            if item['selectable']:item['selected']=not item['selected'];self.revision+=1
    def start(self,download=False):
        with self.lock:
            if self.status['busy']:raise CloudError('同步任务正在进行。')
            if not self.config['configured']:self.update(indicator='未配置账号',message='请展开页面下方的坚果云连接配置，填写应用密码。');return
            selected=[dict(item) for item in self.entries if item['selected'] and item['selectable']]
            if download and not selected:raise CloudError('请先勾选需要下载的内容。')
            names=[item['filename'].casefold() for item in selected]
            if len(set(names))!=len(names):raise CloudError('所选内容有同名文件，请一次只勾选其中一篇。')
            self.cancel=threading.Event();self.update(busy=True,indicator='正在同步…' if download else '正在读取目录…',message='准备连接坚果云…',done=0,total=len(selected),failed=0,bytes_done=0,bytes_total=0)
            self.thread=threading.Thread(target=self.worker,args=(download,selected),daemon=True);self.thread.start()
    def worker(self,download,selected):
        client=None
        try:
            secret=self.keychain_factory().get(self.config['account'])
            if not secret:raise CloudError('钥匙串中没有应用密码，请重新保存配置。',indicator='请重新配置应用密码')
            client=self.client_factory(self.config,secret,self.cancel,self.update,storage_lock=self.storage_lock);secret=None
            if not download:
                entries=client.listing()
                with self.lock:self.entries=entries
                self.update(indicator=f'目录已读取，共{len(entries)}项内容',message='目录已读取，请勾选需要下载的内容。' if entries else '同步文件夹中还没有可读内容。')
            else:
                for number,item in enumerate(selected,1):
                    client.check();self.update(message=f'正在下载 {number}/{len(selected)}：{item["filename"]}',bytes_done=0,bytes_total=item['bytes'])
                    try:
                        # Only TF commits take the render lock; network waits remain cancellable.
                        client.download_one(item)
                        self.update(done=self.status['done']+1)
                    except Cancelled:raise
                    except CloudError as error:
                        with self.lock:
                            for listed in self.entries:
                                if listed['href']==item['href']:listed['error']=str(error)
                        self.update(failed=self.status['failed']+1,message=str(error))
                        # A pending recovery journal gates all later commits.
                        if error.fatal or (CARD/'.pico-sync/transaction').exists():raise
                self.update(indicator='同步完成' if not self.status['failed'] else '部分内容同步失败',message=f'同步完成：成功{self.status["done"]}项，失败{self.status["failed"]}项。')
        except CloudError as error:self.update(indicator='已取消' if isinstance(error,Cancelled) else error.indicator or ('同步失败，请重试' if download else '读取失败，请重试'),message=str(error))
        except (OSError,subprocess.SubprocessError):self.update(indicator='存储或校验失败',message='本地存储或校验异常，原文件和备份已保留。')
        except Exception:self.update(indicator='同步失败，请重试' if download else '读取失败，请重试',message='同步未完成，请检查配置后重试。')
        finally:
            if client:client.close()
            self.update(busy=False)
    def request_cancel(self):self.cancel.set();self.update(indicator='正在停止…',message='正在停止下载…')
