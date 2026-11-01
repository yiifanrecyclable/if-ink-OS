#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：实际文件操作卡片：保留导航、单行操作、确认删除和状态栏限时通知。
# English: Exercise the real file card: anchored navigation, one-row actions, deletion and timed status notices.
from pathlib import Path
import json,os,subprocess,tempfile,sys,time,hashlib
from PIL import Image
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='file-card-',dir=R/'build') as tmp:
    work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('pictures','banner pic','books','fonts','markdown','other'):(card/folder).mkdir(parents=True)
    Image.new('L',(684,1216),100).save(card/'pictures/portrait.png')
    Image.new('L',(684,300),140).save(card/'banner pic/wide.png')
    (card/'other/test.txt').write_text('keep me')
    (out/'titles.txt').touch();ns=out/'nvs/read_pico';ns.mkdir(parents=True)
    (ns/'stat_sig').write_bytes(b'IF TEST\0')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(directory='/sdcard/pictures',events='',size=120,english=False,promotion=False):
        (ns/'sys_size').write_bytes(bytes([size]));(ns/'language').write_bytes(bytes([english]))
        args=[str(R/'build/simulator/render'),'files-0','','3','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,directory]
        p=subprocess.run(args,cwd=work,env={**env,**({'PICO_TEST_FILES_NOTICE_FULL':'1'} if promotion else {})},capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr[-2000:]
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()
    def tap(x,y):return f't,{x},{y},{x},{y}'
    def opened(directory,size=120,english=False):
        state,_=frame(directory,size=size,english=english)
        y=190+state['files_row_height']//2;events=f'l,180,{y},180,{y}'
        state,data=frame(directory,events,size,english);return state,data,events
    def center(g,offset):x,y,w,h=g[offset:offset+4];return x+w//2,y+h//2
    def tone(data,x,y):return (data[y*342+x//2]>>(4 if x&1 else 0))&15
    for size in (100,120,140,160):
        for english in (False,True):
            base,background=frame(size=size,english=english);state,shown,events=opened('/sdcard/pictures',size,english);g=state['files_card_geometry']
            assert 56<g[0]<g[1]==1136 and g[2]
            assert shown[1136*342:]==background[1136*342:] # No white gutter or overwritten tabs.
            assert all(tone(shown,x,g[0]+1)==15 for x in range(684)) # Edge-to-edge white card, no rounded outer border.
            assert any(tone(shown,x,y)>tone(background,x,y) for x in range(40,500,4) for y in range(190,min(350,g[0]),3))
            assert g[8]+g[10]<1136 and g[4]+g[6]<=g[8]
            apply=tap(*center(g,3));result,notice=frame('/sdcard/pictures',events+';'+apply,size,english)
            assert result['wallpaper_path']=='/sdcard/pictures/portrait.png' and result['files_notice']
            before,_=frame('/sdcard/pictures',events+';'+apply+';n,2000,0,0,0',size,english)
            assert before['files_notice']
            after,restored=frame('/sdcard/pictures',events+';'+apply+';n,4000,0,0,0',size,english)
            assert not after['files_notice'] and not after['files_message']
            assert notice[56*342:]==restored[56*342:] # Expiration does not repaint the card/body/nav.
            assert restored[:56*342]==background[:56*342]
            closed,_=frame('/sdcard/pictures',events+';'+tap(*center(g,11)),size,english);assert closed['files_view']==0
            route,_=frame('/sdcard/pictures',events+';'+tap(85,1170),size,english);assert route['route']==0
    # A wake promotion must consume the expired notice's area before a subsequent keyboard update.
    state,_,events=opened('/sdcard/pictures');g=state['files_card_geometry']
    frame('/sdcard/pictures',events+';'+tap(*center(g,3)),promotion=True)
    # No wallpaper action for a banner; regular actions occupy one shared row.
    state,_,events=opened('/sdcard/banner pic');g=state['files_card_geometry'];assert not g[2]
    state,_,events=opened('/sdcard/other');g=state['files_card_geometry']
    x,y=center(g,7)
    rename,_=frame('/sdcard/other',events+';'+tap(x,y));assert rename['files_view']==2
    moved,_=frame('/sdcard/other',events+';'+tap(x+2*g[9],y));assert moved['files_view']==3
    copied,_=frame('/sdcard/other',events+';'+tap(x+g[9],y));assert copied['files_notice'] and len(list((card/'other').glob('*.txt')))==2
    delete,_=frame('/sdcard/other',events+';'+tap(x+3*g[9],y));assert delete['files_card_geometry'][15]
    cancelled,_=frame('/sdcard/other',events+';'+tap(x+3*g[9],y)+';'+tap(170,y));assert not cancelled['files_card_geometry'][15]
    done,_=frame('/sdcard/other',events+';'+tap(x+3*g[9],y)+';'+tap(500,y));assert done['files_view']==0 and done['files_notice']=='文件已删除'
    assert (card/'other/test.txt').exists() # Alphabetical copy was selected and deleted, not the original.
    # Browser polling expires via the same native tick, without replaying an extra copy.
    sys.path.insert(0,str(R/'tools/simulator'));import server
    class OfflineCloud:
        def snapshot(self):return {'busy':False,'revision':0}
    server.ROOT=work;server.BUILD=out;server.CARD=card;server.BOOKS=work/'prototype';server.BOOKS.mkdir();server.FIXTURES=work/'fixtures';server.FIXTURES.mkdir();server.CLOUD_PRIVATE=work/'cloud';server.CLOUD_PRIVATE.mkdir()
    server.STORES={'nvs':out/'nvs','vfs':out/'vfs','card':card};(out/'render').symlink_to(R/'build/simulator/render');os.environ['PICO_SIM_CARD_ROOT']=str(card)
    server.prepare=lambda root:None;server.CloudController=lambda **kwargs:OfflineCloud();server.Preview.sources_signature=lambda self:"file-card-test"
    p=server.Preview(build_native=False);p.state.update(screen='files-card',events=[],root_view=None,page=0,files_dir='/sdcard');p.replay_origin=None;p.render()
    def gesture(kind,x,y):return p.action({'action':'gesture','type':kind,'x':x,'y':y,'xx':x,'yy':y})
    # Use the native directory metadata to locate the picture folder in the root.
    root,_=frame('/sdcard');index=next(i for i,item in enumerate(root['files_items']) if item['path']=='/sdcard/pictures')
    p.action({'action':'view','view':'files-card'});gesture('t',180,190+index*root['files_row_height']+root['files_row_height']//2)
    y=190+root['files_row_height']//2;gesture('l',180,y)
    meta=json.loads(subprocess.run([str(R/'build/simulator/render'),'files-1','','3','48',str(out/'probe.bin'),str(out/'titles.txt'),'0','0','builtin','','/sdcard/pictures'],cwd=work,env=env,capture_output=True,text=True,check=True).stdout);g=meta['files_card_geometry']
    gesture('t',*center(g,3));assert p.state['files_notice']
    before={f:hashlib.sha256(f.read_bytes()).digest() for f in card.rglob('*') if f.is_file()}
    p.file_notice_due=time.monotonic()-1;p.snapshot();assert not p.state['files_notice']
    assert before=={f:hashlib.sha256(f.read_bytes()).digest() for f in card.rglob('*') if f.is_file()}
print('PASS: edge-to-edge file card, preserved/faded backdrop and navigation, four actions, confirmation/dismissal, eligibility and automatic status restore at all four sizes in Chinese/English; browser idle expiry does not duplicate writes.')
