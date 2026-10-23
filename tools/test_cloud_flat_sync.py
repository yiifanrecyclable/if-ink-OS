# SPDX-License-Identifier: Apache-2.0
"""扁平云目录的真实客户端/原生事务，网络使用内存替身。 / Flat cloud client/native transactions over an in-memory transport."""
from pathlib import Path
from unittest.mock import patch
from xml.sax.saxutils import escape
import base64, hashlib, io, json, os, subprocess, sys, tempfile, unittest, urllib.parse, zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent / 'simulator'))
import cloud_client as cloud
REPO = Path(__file__).resolve().parents[1]
JPEG=base64.b64decode('/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0oMCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/wAARCAABAAEDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD6pooooA//2Q==')

class Response(io.BytesIO):
    def __init__(self, data, status, etag):
        super().__init__(data); self.status = status
        self.headers = {'Content-Length': str(len(data)), 'ETag': etag}

class MemoryCloud:
    def __init__(self):
        self.files = {}; self.calls = []; self.tag = 'bare-token'; self.get_tag = None
        self.change_on_verification = False
    def open(self, request, timeout):
        method = request.get_method(); path = urllib.parse.unquote(urllib.parse.urlsplit(request.full_url).path)
        relative = path.removeprefix('/dav/PicoBox/')
        self.calls.append((method, relative, request.get_header('If-match')))
        if method == 'PROPFIND':
            entries = dict(self.files)
            entries['attachments/'] = b''; entries['_pico/'] = b''
            entries['a'*64+'.pico.json'] = b'{}'; entries['_pico/old/body.md'] = b'old'
            xml = '<d:multistatus xmlns:d="DAV:">'
            for name, body in entries.items():
                xml += '<d:response><d:href>/dav/PicoBox/'+escape(urllib.parse.quote(name,safe='/'))+'</d:href><d:propstat><d:prop>'
                xml += '<d:resourcetype>'+('<d:collection/>' if name.endswith('/') else '')+'</d:resourcetype><d:getetag>'+escape(self.tag)+'</d:getetag><d:getcontentlength>'+str(len(body))+'</d:getcontentlength>'
                xml += '<d:creationdate>2026-10-09T00:00:00Z</d:creationdate></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>'
            return Response((xml+'</d:multistatus>').encode(),207,'')
        assert method == 'GET' and relative in self.files, (method,relative)
        body = self.files[relative]
        previous = [c for c in self.calls if c[:2] == ('GET',relative)]
        if self.change_on_verification and len(previous) % 2 == 0:
            body = b'X' * len(body)
        return Response(body,200,self.tag if self.get_tag is None else self.get_tag)

class FlatSyncTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=REPO/'build',prefix='flat-cloud-')
        self.work = Path(self.temp.name); self.card = self.work/'card'
        (self.work/'build/simulator').mkdir(parents=True); self.card.mkdir()
        self.patches = [patch.object(cloud,'ROOT',self.work),patch.object(cloud,'CARD',self.card),
                        patch.object(cloud,'STAGE',self.card/'.pico-sync/current'),patch.object(cloud,'PRIVATE',self.work/'private'),
                        patch.dict(os.environ,{'PICO_SIM_CARD_ROOT':str(self.card),'PICO_TEST_FATFS_RENAME':'1'})]
        for p in self.patches: p.start()
        self.remote = MemoryCloud(); self.bridge = cloud.NativeBridge()
        self.client = cloud.DavClient({'folder':'PicoBox','account':'fiction@example.invalid'},'fictional-password',bridge=self.bridge)
        self.client.opener = self.remote
    def tearDown(self):
        self.client.close()
        for p in reversed(self.patches): p.stop()
        self.temp.cleanup()
    def item(self,name):
        return next(item for item in self.client.listing() if item['filename']==name)
    def test_supported_direct_types_and_no_manifest_gets(self):
        self.remote.files={'手动.md':b'# A\n','Book.EPUB':b'PK\3\4x','note.TXT':b'text',
                           '壁纸.PNG':b'png','照片.jpg':b'jpg','图.JPEG':b'jpeg','字.TTF':b'ttf',
                           'ignore.pdf':b'pdf','ignore.webp':b'webp','ignore.otf':b'otf','nested/a.ttf':b'ttf'}
        items=self.client.listing()
        self.assertEqual({item['filename'] for item in items},{'手动.md','Book.EPUB','note.TXT','壁纸.PNG','照片.jpg','图.JPEG','字.TTF'})
        self.assertTrue(all(item['selectable'] and not item['manifest'] for item in items))
        self.assertEqual(self.remote.calls,[('PROPFIND','',None)])
    def test_images_and_fonts_use_standard_folders_and_reject_bad_replacements(self):
        picture=(REPO/'assets/banner/default-banner.png').read_bytes()
        font=(REPO/'main/assets/builtin.ttf').read_bytes()
        self.remote.files={'壁纸.PNG':picture,'思源.TTF':font,'相片.jpg':JPEG,'相片.JPEG':JPEG}
        for item in self.client.listing():self.client.download_one(item)
        for name,folder in [('壁纸.PNG','pictures'),('思源.TTF','fonts'),('相片.jpg','pictures'),('相片.JPEG','pictures')]:
            target=self.card/folder/name
            self.assertEqual(target.read_bytes(),self.remote.files[name])
            self.assertFalse((self.card/'books'/name).exists())
            self.assertFalse((self.card/'markdown/.index'/(name+'.pico')).exists())
            original=target.read_bytes();self.remote.files[name]=b'not an image or font'*8
            with self.assertRaises(cloud.CloudError):self.client.download_one(self.item(name))
            self.assertEqual(target.read_bytes(),original)
        # 图片上限与设备图片选择器一致；字体保留原单文件64MiB上限。
        # Match the image picker limit and retain the existing 64 MiB file limit for fonts.
        self.remote.files={'too-big.png':b'x'*(cloud.MAX_PICTURE+1)}
        item=self.item('too-big.png');self.assertFalse(item['selectable'])
        self.remote.calls=[]
        with self.assertRaises(cloud.CloudError):self.client.download_one(item)
        self.assertFalse(any(c[0]=='GET' for c in self.remote.calls))
    def test_plain_md_update_and_noop_preserve_date_progress(self):
        name='周记1.md';self.remote.files[name]='# 标题\n\n正文 **重点**。\n'.encode()
        self.client.download_one(self.item(name));target=self.card/'markdown'/name
        self.assertEqual(target.read_bytes(),self.remote.files[name])
        metadata=self.card/'markdown/.index'/('周记1.md.pico');first=metadata.read_text().splitlines()[1]
        self.bridge.require('progress-save','/sdcard/markdown/'+name)
        self.client.download_one(self.item(name))
        self.assertTrue(self.bridge.require('progress-load','/sdcard/markdown/'+name)['present'])
        self.remote.files[name]='# 更新\n\n新的正文。\n'.encode();self.client.download_one(self.item(name))
        self.assertEqual(target.read_bytes(),self.remote.files[name]);self.assertEqual(metadata.read_text().splitlines()[1],first)
        self.assertFalse(self.bridge.require('progress-load','/sdcard/markdown/'+name)['present'])
    def test_txt_and_epub_route_to_configured_book_folder(self):
        config=self.work/'build/simulator/nvs/read_pico';config.mkdir(parents=True)
        (config/'books_dir').write_bytes(b'/sdcard/library\0');(self.card/'library').mkdir()
        epub=io.BytesIO()
        with zipfile.ZipFile(epub,'w') as z: z.writestr('mimetype','application/epub+zip')
        self.remote.files={'Book.epub':epub.getvalue(),'随笔.txt':'中文文本。'.encode()};self.remote.tag='"strong"'
        for item in self.client.listing(): self.client.download_one(item)
        for name,body in self.remote.files.items(): self.assertEqual((self.card/'library'/name).read_bytes(),body)
        self.assertTrue(all(c[2]=='"strong"' for c in self.remote.calls if c[0]=='GET'))
    def test_without_get_etag_hashes_twice(self):
        self.remote.files={'Test.md':b'# text\n'};self.remote.get_tag=''
        self.client.download_one(self.item('Test.md'))
        self.assertEqual(len([c for c in self.remote.calls if c[0]=='GET']),2)
        self.assertEqual((self.card/'markdown/Test.md').read_bytes(),b'# text\n')
    def test_changed_verification_preserves_old_body(self):
        self.remote.files={'Test.md':b'# old\n'};self.client.download_one(self.item('Test.md'))
        self.remote.calls=[];self.remote.files['Test.md']=b'# new\n';self.remote.change_on_verification=True
        with self.assertRaises(cloud.CloudError): self.client.download_one(self.item('Test.md'))
        self.assertEqual((self.card/'markdown/Test.md').read_bytes(),b'# old\n')
    def test_weak_missing_tags_are_selectable_but_checked_twice(self):
        self.remote.files={'Test.md':b'# text\n'}
        for tag in ('','W/"weak"'):
            self.remote.tag=tag;self.remote.calls=[];item=self.item('Test.md');self.assertTrue(item['selectable'])
            self.client.download_one(item);self.assertEqual(len([c for c in self.remote.calls if c[0]=='GET']),2)
    def test_existing_manual_image_refs_download_only_used_attachment(self):
        image=(REPO/'plugins/obsidian-pico/tests/fixtures/sample-image.png').read_bytes()
        self.remote.files={'Article.md':b'# text\n\n![](attachments/Article_a8.png)\n','attachments/Article_a8.png':image,'attachments/Article_old.png':image}
        self.client.download_one(self.item('Article.md'))
        self.assertTrue((self.card/'markdown/attachments/Article_a8.png').exists())
        self.assertFalse((self.card/'markdown/attachments/Article_old.png').exists())
    def test_plugin_text_export_is_downloadable_without_assets(self):
        plugin=REPO/'plugins/obsidian-pico'
        code="import {exportText} from './src/export-text.ts'; process.stdout.write(JSON.stringify(exportText('# 文章\\n\\n正文 **重点** ==高亮==\\n\\n![插图](picture.png)\\n','文章')));"
        result=subprocess.run(['/usr/local/bin/node','--import','tsx','--input-type=module','-e',code],cwd=plugin,capture_output=True,text=True,check=True)
        exported=json.loads(result.stdout);self.assertTrue(exported['warnings']);self.remote.files={'文章.md':exported['body'].encode()}
        self.client.download_one(self.item('文章.md'))
        self.assertEqual((self.card/'markdown/文章.md').read_bytes(),self.remote.files['文章.md'])
        self.assertFalse(any(c[1].startswith('attachments/') for c in self.remote.calls))
    def test_plugin_flat_images_download_as_one_document_and_preserve_previous_on_bad_update(self):
        # 生产插件导出器→真实Python客户端→生产C事务，不访问账号或真实TF。
        # Production exporter -> Python client -> production C transaction, with isolated storage/network.
        code = """import {exportImages} from './src/export-images.ts';
import {encodeGrayPng} from './src/image.ts';
const result=await exportImages('# 图文文章\\n\\n正文 **重点**\\n\\n![插图](picture.png)\\n','文章',{
resolve:()=>({key:'picture.png',mime:'image/png',read:async()=>new Uint8Array([1,2,3]).buffer}),
convert:async()=>encodeGrayPng(new Uint8ClampedArray([0,0,0,255,127,127,127,255,255,255,255,255]),3,1)});
console.log(JSON.stringify({body:result.body,assets:result.assets.map(a=>({name:a.name,data:Buffer.from(a.data).toString('base64')}))}));"""
        result=subprocess.run(['/usr/local/bin/node','--import','tsx','--input-type=module','-e',code],cwd=REPO/'plugins/obsidian-pico',capture_output=True,text=True,check=True)
        exported=json.loads(result.stdout);body=exported['body'].encode();asset=exported['assets'][0]
        name='attachments/'+asset['name'];image=base64.b64decode(asset['data'])
        self.remote.files={'文章.md':body,name:image};self.client.download_one(self.item('文章.md'))
        self.assertEqual((self.card/'markdown/文章.md').read_bytes(),body)
        self.assertEqual((self.card/'markdown'/name).read_bytes(),image)
        self.remote.files['文章.md']=body+b'\nchanged';self.remote.files[name]=b'broken'
        with self.assertRaises(cloud.CloudError):self.client.download_one(self.item('文章.md'))
        self.assertEqual((self.card/'markdown/文章.md').read_bytes(),body)
        self.assertEqual((self.card/'markdown'/name).read_bytes(),image)

if __name__=='__main__': unittest.main()
