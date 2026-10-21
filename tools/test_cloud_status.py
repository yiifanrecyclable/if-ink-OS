# SPDX-License-Identifier: Apache-2.0
"""Isolated status transitions and production header placement, without account/TF mutations.
状态测试使用虚拟凭据和独立缓存，不修改用户TF卡；原生帧检查不进行交互重放。
"""
from pathlib import Path
from unittest.mock import patch
import io,json,re,subprocess,sys,tempfile,unittest,urllib.error
sys.path.insert(0,str(Path(__file__).resolve().parent/'simulator'))
import cloud_client as cloud
class Keychain:
    def get(self,account):return 'synthetic-password'
class FailureClient:
    def __init__(self,error):self.error=error
    def listing(self):raise self.error
    def close(self):pass
class StatusTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=cloud.BUILD,prefix='status-isolated-');self.root=Path(self.temp.name)
        self.private=patch.object(cloud,'PRIVATE',self.root/'cache');self.private.start()
    def tearDown(self):self.private.stop();self.temp.cleanup()
    def client(self):return cloud.DavClient({'account':'fixture@example.invalid','folder':'PicoBox'},'synthetic-password')
    def test_transport_failure_labels_are_distinct(self):
        for code,label in [(401,'账号或应用密码错误'),(403,'账号或应用密码错误'),(404,'同步文件夹不存在'),(500,'云端读取失败')]:
            with self.subTest(code=code):
                client=self.client()
                class Opener:
                    def open(self,*args,**kwargs):raise urllib.error.HTTPError('https://example.invalid',code,'fixture',{},io.BytesIO())
                client.opener=Opener()
                with self.assertRaises(cloud.CloudError) as caught:client.request('PROPFIND','',self.root/'out',4096)
                self.assertEqual(caught.exception.indicator,label);client.close()
        client=self.client()
        class Offline:
            def open(self,*args,**kwargs):raise urllib.error.URLError('synthetic offline')
        client.opener=Offline()
        with self.assertRaises(cloud.CloudError) as caught:client.request('PROPFIND','',self.root/'out',4096)
        self.assertEqual(caught.exception.indicator,'网络连接失败');client.close()
    def test_failed_refresh_keeps_catalog_and_sets_failure_indicator(self):
        controller=cloud.CloudController(keychain_factory=Keychain,private_dir=self.root/'config',client_factory=lambda *a,**kw:FailureClient(cloud.CloudError('fixture',indicator='网络连接失败')))
        controller.config={'account':'fixture@example.invalid','folder':'PicoBox','configured':True};controller.entries=[{'href':'fixture.md'}]
        controller.worker(False,[]);snapshot=controller.snapshot();self.assertFalse(snapshot['busy']);self.assertEqual(snapshot['indicator'],'网络连接失败');self.assertEqual(snapshot['count'],1)
    def test_success_empty_and_not_configured_have_accurate_states(self):
        class Empty:
            def listing(self):return []
            def close(self):pass
        controller=cloud.CloudController(keychain_factory=Keychain,private_dir=self.root/'config',client_factory=lambda *a,**kw:Empty())
        controller.start();self.assertEqual(controller.snapshot()['indicator'],'未配置账号')
        controller.config={'account':'fixture@example.invalid','folder':'PicoBox','configured':True};controller.worker(False,[])
        self.assertEqual(controller.snapshot()['indicator'],'目录已读取，共0项内容');self.assertIn('没有可读',controller.snapshot()['message'])
    def draw(self,indicator):
        snapshot={'folder':'PicoBox','configured':True,'count':1,'busy':False,'indicator':indicator,'message':'fixture details','entries':[{'name':'fixture.md','filename':'fixture.md','selectable':True,'selected':False,'bytes':12}]}
        source=self.root/'snapshot.json';source.write_text(json.dumps(snapshot));frame=self.root/'frame.bin'
        subprocess.run([str(cloud.BUILD/'render'),'cloud-0',str(source),'0','48',str(frame),str(source),'0','0,0,0,0,0,0,0','builtin'],cwd=cloud.ROOT,capture_output=True,check=True,timeout=30)
        return frame.read_bytes()
    def test_header_status_changes_only_left_of_refresh_not_under_article(self):
        before=self.draw('目录已读取，共1项内容');after=self.draw('未连接WiFi')
        header=(cloud.ROOT/'main/ui/ui_kit.h').read_text()
        shift=80-int(re.search(r'#define UI_STATUS_HEIGHT (\d+)',header)[1])
        changed=[]
        for y in range(170-shift,950):
            for x in range(684):
                i=y*684+x;a=before[i//2]>>(4*(i%2))&15;b=after[i//2]>>(4*(i%2))&15
                if a!=b:changed.append((x,y))
        self.assertTrue(changed);self.assertTrue(all(240<=x<540 and 175-shift<=y<230-shift for x,y in changed))
if __name__=='__main__':unittest.main()
