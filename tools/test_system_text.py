# SPDX-License-Identifier: Apache-2.0
"""Four type presets and real production settings routing over private TF/NVS. / 四档文字及实际设置路由，隔离TF/NVS。"""
from pathlib import Path
import json, os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='system-text-') as tmp:
    work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('books','markdown','fonts','font','pictures'):(card/folder).mkdir(parents=True)
    (card/'markdown/较长的中文文档标题与English.md').write_text('# 标题\n\n这是阅读正文。\n'*200)
    (card/'books/Test.txt').write_text('Chapter\n\n'+('reading text '*1000))
    font=ROOT/'assets/fonts/PicoSans-Medium.ttf';shutil.copy2(font,card/'fonts/PicoSans-Medium.ttf');shutil.copy2(font,card/'font/Custom.ttf')
    (out/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(native,events='',aux=0):
        result=subprocess.run([str(ROOT/'build/simulator/render'),native,'/sdcard/books/Test.txt',str(aux),'48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,''],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert result.returncode==0,(native,result.stderr)
        return json.loads(result.stdout),(out/'frame.bin').read_bytes()
    info,_=render('settings-0');g=info['settings_geometry'];y=g[1]+g[4]//2
    assert render('settings-0',f't,300,{y},300,{y}')[0]['settings_page']==5
    reader=None; title_pixels=[]
    for i,size in enumerate((100,120,140,160)):
        x=108+156*i
        state,frame=render('settings-5',f't,{x},300,{x},300')
        assert state['settings_page']==5 and state['system_size']==size
        assert render('settings-5')[0]['system_size']==size
        state,frame=render('book-8');assert state['document_count']==1
        def ink(x,y): return ((frame[y*342+x//2]>>(4 if x%2 else 0))&15)<10
        title_pixels.append(sum(ink(x,y) for y in range(148,236) for x in range(52,420)))
        state,frame=render('book-1')
        if reader is None:reader=frame
        else:assert frame[80*342:]==reader[80*342:],'UI text preset must not change reading body or controls'
        # 三分区绘制/点击在四档字号下保持一致。/ Three groups share paint/hit geometry at every preset.
        info,_=render('settings-0');g=info['settings_geometry']
        for group,pages in ((1,(5,19,None,9,21)),(2,(2,None,14,24,11,27,13))):
            for row,page in enumerate(pages):
                if page is None:continue  # 首页强刷为开关，不离开本页。 / Home refresh toggles in place.
                y=g[group]+row*g[4]+g[4]//2;scroll=min(g[5],max(0,y-970))
                assert render('settings-0',f't,300,{y-scroll},300,{y-scroll}',scroll)[0]['settings_page']==page
        assert render('settings-27','t,180,1052,180,1052')[0]['settings_page']==28
        assert render('settings-27','t,500,1052,500,1052')[0]['settings_page']==12
        assert render('settings-27','t,500,1052,500,1052;t,60,105,60,105')[0]['settings_page']==27
        assert render('settings-27','t,60,105,60,105')[0]['settings_page']==0
        for native in ('settings-0','settings-20','files-0','book-4','settings-9','settings-24','transfer-2'):
            render(native)
    assert title_pixels[3]>title_pixels[0],title_pixels
    # 配网来源决定返回设置或传书选择，密码取消先回无线网络。/ Settings context survives password cancellation and returns to Settings.
    assert render('transfer-2','t,60,110,60,110',3)[0]['route']==3
    assert render('transfer-2','k,0,0,0,0',3)[0]['route']==3
    assert render('transfer-3','t,60,110,60,110',3)[0]['transfer_view']==2
    assert render('transfer-2','t,60,110,60,110',1)[0]['transfer_view']==0
    assert render('settings-7')[0]['settings_page']==0
    assert render('settings-6')[0]['settings_page']==5
    assert render('settings-8')[0]['settings_page']==9
    info,_=render('settings-5');top,step=info['settings_geometry'][6:8]
    y=top+(step-12)//2
    assert render('settings-5',f't,300,{y},300,{y}')[0]['system_font_path']==''
    assert render('settings-5',f't,300,{y+step},300,{y+step}')[0]['system_font_path']=='/sdcard/fonts/PicoSans-Medium.ttf'
    assert render('settings-5')[0]['system_font_path']=='/sdcard/fonts/PicoSans-Medium.ttf'
    assert render('settings-24','t,510,380,510,380')[0]['settings_page']==24
    assert render('settings-9')[0]['lock_style']==1
print('PASS: four presets/persistence, bigger document titles, unchanged reading pixels, fixed-font-folder selection, three settings groups, firmware info/local update navigation, retired-page migration')
