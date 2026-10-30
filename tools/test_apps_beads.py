#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""实际原生应用入口与拼豆PNG隔离回归。/ Isolated native routing and bead PNG regressions."""
from pathlib import Path
import json,os,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='apps-beads-',dir=ROOT/'build') as tmp:
    work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for name in ('books','fonts','markdown','pictures'):(card/name).mkdir(parents=True)
    (out/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1'}
    def render(view,events='',folder=0):
        result=subprocess.run([str(ROOT/'build/simulator/render'),view,'/sdcard/books/demo.txt',str(folder),'48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,''],cwd=work,env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stderr[-2000:]
        return json.loads(result.stdout)
    def tap(x,y):return f't,{x},{y},{x},{y}'
    # 五个入口与返回：第3项只打开目录，第5项进真实C拼豆。/ Five routes; storage opens a directory and beads invokes production C.
    for i,route in ((1,5),(2,5),(3,6),(4,7)):
        state=render('files-0',tap(180,154+140*i),-1);assert state['route']==route,(i,state)
    state=render('files-0',tap(180,154),-1);assert state['files_folder']==3 and state['route']==-1
    state=render('files-0',tap(40,90),3);assert state['files_folder']==-1
    state=render('files-0',tap(426,1176),-1);assert state['route']==2
    assert render('cyber-beads',tap(36,1144))['route']==2
    # 两次保存相同画面只写一张，已有文件不覆盖。/ No repeated unchanged save and no overwriting existing files.
    folder=card/'pictures/pindou';folder.mkdir();(folder/'pindou-0001.png').write_bytes(b'original')
    events=';'.join((tap(22,24),tap(643,1144),tap(643,1144)))
    render('cyber-beads',events)
    assert (folder/'pindou-0001.png').read_bytes()==b'original'
    assert len(list(folder.glob('*.png')))==2
    def pixels(file):
        data=file.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';at=8;compressed=b'';types=[]
        while at<len(data):
            n=struct.unpack('>I',data[at:at+4])[0];kind=data[at+4:at+8];body=data[at+8:at+8+n]
            assert zlib.crc32(kind+body)==struct.unpack('>I',data[at+8+n:at+12+n])[0]
            if kind==b'IHDR':assert struct.unpack('>IIBBBBB',body)==(684,1216,4,0,0,0,0)
            if kind==b'IDAT':compressed+=body
            types.append(kind);at+=12+n
        assert types[-1]==b'IEND';raw=zlib.decompress(compressed);assert len(raw)==343*1216
        def pixel(x,y):
            assert raw[y*343]==0;b=raw[y*343+1+x//2];return b&15 if x%2 else b>>4
        return pixel
    render('cyber-beads')
    restored=(out/'frame.bin').read_bytes();assert restored[24*342+18]&15==13
    render('cyber-beads',tap(643,1144));assert len(list(folder.glob('*.png')))==2
    image=pixels(folder/'pindou-0002.png')
    assert image(22,24)==15 and image(36,24)==13
    assert image(42,24)==15
    assert all(image(x,y)==15 for y in range(1100,1130) for x in range(684))
    assert all(image(x,y)>=11 for y in range(1130,1190) for x in range(684))
    assert any(image(x,y)==11 for y in range(1130,1190) for x in range(200,484))
    # 退出保存不能把确认框写入PNG；保存失败保留未保存画布及退出提醒。
    # Save-and-exit excludes the dialog; failures preserve dirty state and its exit guard.
    events=';'.join((tap(22,24),tap(36,1144),tap(342,598)))
    assert render('cyber-beads',events)['route']==2
    image=pixels(folder/'pindou-0003.png')
    assert all(image(x,y)==15 or image(x,y)==13 for y in (424,560,852) for x in range(684))
    env['PICO_TEST_RENAME_FAIL_TO']='/sdcard/pictures/pindou/pindou-0004.png'
    assert render('cyber-beads',events)['route']==-1
    assert not (folder/'pindou-0004.png').exists() and not list(folder.glob('*.part'))
    # 未保存退出、清空确认和撤回恢复。/ Guard unsaved exit and restore a confirmed clear.
    assert render('cyber-beads',';'.join((tap(22,24),tap(36,1144))))['route']==-1
    render('cyber-beads',';'.join((tap(22,24),tap(576,1144),tap(342,598),tap(108,1144))))
    frame=(out/'frame.bin').read_bytes();assert ((frame[24*342+36//2]>>(4 if 36%2 else 0))&15)==13
    # 浏览器服务层也必须把拼豆触控和底栏操作交给C，而不是通用导航。
    # The browser service must forward bead taps and tools to C rather than generic navigation.
    for slot in folder.glob('.board-*'):slot.unlink()
    import sys
    sys.path.insert(0,str(ROOT/'tools/simulator'))
    import server
    server.ROOT=work;server.BUILD=out;server.CARD=card;server.BOOKS=work/'prototype';server.FIXTURES=work/'fixtures'
    server.FIXTURES.mkdir();server.CLOUD_PRIVATE=work/'cloud';server.CLOUD_PRIVATE.mkdir()
    server.STORES={'nvs':out/'nvs','vfs':out/'vfs','card':card}
    server.prepare=lambda _:None
    server.Preview.sources_signature=lambda self:'apps-beads-test'
    class Cloud:
        def snapshot(self):return {'busy':False,'revision':0}
        def start(self,**_):pass
    server.CloudController=lambda **_:Cloud()
    (out/'render').symlink_to(ROOT/'build/simulator/render')
    env.pop('PICO_TEST_RENAME_FAIL_TO')
    previous=os.environ.get('PICO_SIM_CARD_ROOT');os.environ['PICO_SIM_CARD_ROOT']=str(card)
    try:
        preview=server.Preview(build_native=False)
        preview.action({'action':'files'})
        def gesture(x,y):return preview.action({'action':'gesture','type':'t','x':x,'y':y,'xx':x,'yy':y})
        assert gesture(180,714)['screen']=='cyber-beads'
        assert gesture(22,24)['screen']=='cyber-beads'
        frame=(out/'frame.bin').read_bytes();assert frame[24*342+18]&15==13
        assert gesture(108,1144)['screen']=='cyber-beads'
        frame=(out/'frame.bin').read_bytes();assert frame[24*342+18]&15==15
        gesture(22,24);count=len(list(folder.glob('*.png')))
        assert gesture(643,1144)['screen']=='cyber-beads'
        assert len(list(folder.glob('*.png')))==count+1
        assert gesture(36,1144)['screen']=='files-home'
        assert gesture(180,714)['screen']=='cyber-beads'
        frame=(out/'frame.bin').read_bytes();assert frame[24*342+18]&15==13
        # 放弃修改后重新进入应恢复最近保存的画布。/ Discarded edits must not replace the saved board.
        gesture(22,24);gesture(36,1144)
        assert gesture(342,690)['screen']=='files-home'
        assert gesture(180,714)['screen']=='cyber-beads'
        frame=(out/'frame.bin').read_bytes();assert frame[24*342+18]&15==13
        # 应用入口返回路径：USB按钮/左上返回/实体左右键都回应用，Home回首页。
        # USB button/header/side keys return to Apps; the physical Home key retains Home.
        for event in ({'action':'gesture','type':'t','x':342,'y':990,'xx':342,'yy':990},
                      {'action':'gesture','type':'t','x':40,'y':90,'xx':40,'yy':90},
                      {'action':'key','key':0},{'action':'key','key':2},{'action':'key','key':1}):
            preview.action({'action':'files'})
            assert gesture(180,434)['screen']=='transfer-usb'
            state=preview.action(event)
            assert state['screen']==('baseline-home' if event.get('key')==1 else 'files-home'),state
        preview.action({'action':'files'})
        assert gesture(180,294)['screen']=='transfer-methods'
        assert gesture(40,90)['screen']=='files-home'
        for transfer in ('transfer-hotspot','transfer-wifi'):
            preview.action({'action':'view','view':transfer})
            assert gesture(342,990)['screen']=='transfer-methods'
            assert gesture(40,90)['screen']=='files-home'
        preview.action({'action':'files'})
        assert gesture(180,574)['screen']=='cloud-sync'
        assert gesture(426,1176)['screen']=='files-home'
        # 子目录图片通过文件页跨进程传入图片页，不能回退到内置测试图。
        # Carry nested image paths across renderer processes, never fall back to the built-in chart.
        images=card/'pictures/preview-test';images.mkdir()
        (images/'alpha.png').write_bytes((folder/'pindou-0002.png').read_bytes())
        (images/'beta.png').write_bytes((folder/'pindou-0003.png').read_bytes())
        preview.action({'action':'view','view':'files-card'})
        preview.state.update(root_view='files-card',root_dir='/sdcard/pictures/preview-test',events=[])
        preview.capture_origin()
        state=gesture(180,230)
        assert state['screen']=='image-view' and state['image_path']=='/sdcard/pictures/preview-test/alpha.png',state
        first_frame=(out/'frame.bin').read_bytes()
        state=gesture(640,500)
        assert state['screen']=='image-view' and state['image_path']=='/sdcard/pictures/preview-test/beta.png',state
        assert (out/'frame.bin').read_bytes()!=first_frame
        # 连续重放要从最初选中的路径开始，否则会重复跳图。/ Replays must start at the entry path, not advance twice from the current image.
        state=gesture(40,500)
        assert state['image_path']=='/sdcard/pictures/preview-test/alpha.png'
        assert (out/'frame.bin').read_bytes()==first_frame
    finally:
        if previous is None:os.environ.pop('PICO_SIM_CARD_ROOT',None)
        else:os.environ['PICO_SIM_CARD_ROOT']=previous
    # 保存空白再保存一颗豆，破坏最新槽应回退上一份；两槽损坏不读取垃圾。
    # A corrupt latest slot falls back to the prior blank; two corrupt slots safely start empty.
    render('cyber-beads',';'.join((tap(576,1144),tap(342,598),tap(643,1144))))
    render('cyber-beads',';'.join((tap(507,1144),tap(22,24),tap(643,1144))))
    render('cyber-beads',tap(62,24));frame=(out/'frame.bin').read_bytes();assert frame[24*342+38]&15==0
    slots=list(folder.glob('.board-*'));assert len(slots)==2
    latest=max(slots,key=lambda p:int.from_bytes(p.read_bytes()[8:12],'big'))
    assert latest.read_bytes()[12]==5
    latest.write_bytes(b'corrupt')
    render('cyber-beads');frame=(out/'frame.bin').read_bytes();assert frame[24*342+18]&15==15
    for slot in slots:slot.write_bytes(b'corrupt')
    render('cyber-beads');frame=(out/'frame.bin').read_bytes();assert frame[24*342+18]&15==15
print('PASS: restore saved board/palette, discard, no duplicate export, corrupt-slot fallback; five app routes, storage/back, native beads, PNG pixels/CRC/4-bit export, duplicate/collision/FatFs/failure, undo and exit guards')
