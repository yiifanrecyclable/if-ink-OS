# SPDX-License-Identifier: Apache-2.0
"""rc83 IF 原生界面与隔离存储回归。 / Native UI regressions over isolated storage."""
from pathlib import Path
import json,os,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools/simulator'))
from views import VIEWS
with tempfile.TemporaryDirectory(dir=root/'build',prefix='if-ui-isolated-') as tmp:
    work=Path(tmp);card=work/'card';build=work/'build/simulator';build.mkdir(parents=True)
    for name in ('books','fonts','markdown/attachments','pictures','notes'):(card/name).mkdir(parents=True,exist_ok=True)
    note=card/'markdown/生僻字龘与标点.md';note.write_text('# 主标题\n\n正文中文及 English?!\n\n## 子标题\n\n- 列表\n  - 次级列表\n\n> 引用\n\n```\n  缩进代码\n```\n')
    book=card/'books/示例.txt';book.write_text('第一章\n\n'+'阅读测试正文。\n'*100)
    shutil.copyfile(root/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
    listing=build/'titles.txt';listing.write_text('生僻字龘与标点\n')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(native,path='/sdcard/markdown/生僻字龘与标点.md',aux=0,mask=0,events=''):
        args=[str(root/'build/simulator/render'),native,path,str(aux),'48','build/simulator/frame.bin','build/simulator/titles.txt',str(mask),','.join(['60']*30),'builtin',events,'']
        p=subprocess.run(args,cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert p.returncode==0,(native,p.stderr)
        info=json.loads(p.stdout);assert (build/'frame.bin').stat().st_size==684*1216//2
        return info
    assert 'settings-4' not in VIEWS and not any(k.startswith('shelf-') for k in VIEWS)
    count=0
    for v in VIEWS.values():
        if v['native'] in ('home','cloud','shelf','reading'):continue
        render(v['native'],aux=v['aux'],mask=v['mask']);count+=1
    assert render('book-8')['document_count']==1
    assert render('book-1')['pages']>=1
    assert render('book-1',path='/sdcard/books/示例.txt')['pages']>1
    probe=render('font-probe',path='龘中文，。「」English!?');assert probe['font_text_covered']
    geometry=render('settings-0')['settings_geometry'];top,tab_y,_,_,row_h=geometry[:5]
    for index,page in ((0,5),(2,19),(3,9),(6,20)):
        y=top+row_h*index+row_h//2
        assert render('settings-0',events=f't,340,{y},340,{y}')['settings_page']==page
    y=top+row_h*5+row_h//2
    assert render('settings-0',events=f't,500,{tab_y},500,{tab_y};t,340,{y},340,{y}')['settings_page']==11
    assert render('settings-4')['settings_page']==0
    assert not any(f'settings-{i}' in VIEWS for i in (15,16,17,18))
    for retired in (15,16,17,18):assert render(f'settings-{retired}')['settings_page']==0
    for x in (98,342,610):assert render('settings-0',events=f't,{x},135,{x},135')['settings_page']==0
    render('settings-0');settings_frame=(build/'frame.bin').read_bytes()
    # The root title has a full-width divider and no close action in the brand area.
    assert settings_frame[127*342:129*342]==bytes([0x55])*684
    assert render('settings-0',events='t,58,90,58,90')['route']==-1
    # 底栏选中的页面使用白底黑图标；四角仍可点击。 / The selected tab uses white with a black icon, and its corners remain tappable.
    def tone(frame,x,y):return (frame[y*342+x//2]>>(4 if x%2 else 0))&15
    for active,native in enumerate(('baseline-home','book-0','files-0','settings-0')):
        render(native,aux=-1 if active==2 else 0);frame=(build/'frame.bin').read_bytes()
        left=active*171;right=left+170
        for y in (1136,1137,1215):
            assert all(tone(frame,x,y)==15 for x in range(left,right+1)),(active,y)
        for x in (left,right):
            assert all(tone(frame,x,y)==15 for y in range(1136,1216)),(active,x)
        assert any(tone(frame,x,y)==0 for y in range(1154,1199) for x in range(left+60,left+111))
        for x,y in ((left,1136),(right,1215)):
            assert render('baseline-home',events=f't,{x},{y},{x},{y}')['route']==active
    assert render('baseline-home',events='t,426,1176,426,1176')['route']==2
    assert render('baseline-home',events='t,255,1176,255,1176')['route']==1
    assert render('files-0',aux=-1,events='t,130,236,130,236')['files_folder']==3
    for x,y,route in ((342,236,5),(570,236,6),(114,549,7)):
        assert render('files-0',aux=-1,events=f't,{x},{y},{x},{y}')['route']==route
    assert render('cloud-0',events='t,64,112,64,112')['route']==2
    assert render('baseline-home',events='t,342,930,342,930')['route']!=6
    shutil.rmtree(card/'fonts');(card/'fonts').mkdir()
    render('baseline-home');render('settings-0');render('settings-12')
    print(f'PASS {count} native screens, MD/TXT readers, TF glyph coverage, cloud route, 80px navigation and no-font fallback')
