#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""主导航点击与可选阅读基线逐像素比较；不模拟墨水屏光学残影。
Root navigation routing and optional pixel-for-pixel reader baseline comparison, not optical ghosting.
"""
from pathlib import Path
import argparse,json,os,subprocess,tempfile,zipfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path);args=parser.parse_args()
current=ROOT/'build/simulator/render'
with tempfile.TemporaryDirectory(prefix='nav-reader-',dir=ROOT/'build') as temp:
    root=Path(temp);card=root/'card'
    for name in ('books','markdown','fonts','pictures'):(card/name).mkdir(parents=True)
    prose='阅读测试。翻页后继续阅读，保持原有字形和段落。\n\n'*600
    (card/'books/Test.txt').write_text(prose)
    (card/'markdown/Test.md').write_text('# 标题\n\n'+prose)
    with zipfile.ZipFile(card/'books/Test.epub','w') as book:
        book.writestr('mimetype','application/epub+zip')
        book.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        book.writestr('content.opf','<package xmlns="http://www.idpf.org/2007/opf" version="2.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Test</dc:title></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        book.writestr('body.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+''.join('<p>'+p+'</p>' for p in prose.split('\n\n'))+'</body></html>')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(binary,work,view,path='/sdcard/books/Test.txt',events='',panel=0):
        (work/'build/simulator').mkdir(parents=True,exist_ok=True);(work/'titles.txt').write_text('')
        result=subprocess.run([str(binary.resolve()),view,path,'1' if view=='book-1' else '0','48','build/simulator/frame.bin','titles.txt',str(panel),','.join(['0']*30),'builtin',events,''],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert result.returncode==0,(view,result.stderr[-800:])
        return json.loads(result.stdout),(work/'build/simulator/frame.bin').read_bytes()
    views=['baseline-home','book-0','files-0','settings-0']
    work=root/'current'
    for view in views:
        for tab in range(4):
            x=tab*171+85
            state,_=frame(current,work,view,events=f't,{x},1176,{x},1176')
            assert state['route']==tab,(view,tab,state['route'])
    if args.baseline:
        for path in ('/sdcard/books/Test.txt','/sdcard/books/Test.epub','/sdcard/markdown/Test.md'):
            for panel,events in ((0,''),(0,'t,550,500,550,500'),(0,'t,100,500,100,500'),(0,'t,342,500,342,500'),(1,'')):
                old,pixels=frame(args.baseline,root/'before','book-1',path,events,panel)
                new,actual=frame(current,root/'after','book-1',path,events,panel)
                assert actual[56*342:]==pixels[56*342:],(path,panel,events,'reader pixels changed')
                for key in ('reader_page','reader_panel','reader_px','reader_offset','reader_fullscreen'):
                    assert old[key]==new[key],(path,key,old[key],new[key])
print('PASS: all16 root navigation targets; optional TXT/EPUB/Markdown reader body, turns and panel pixel baselines unchanged. Optical behavior needs hardware.')
