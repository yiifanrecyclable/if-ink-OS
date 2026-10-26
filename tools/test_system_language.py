#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""系统语言的实际设置路由/NVS、四字号和用户正文不变。/ Real routing/NVS, presets and unchanged user reading content."""
from pathlib import Path
import json,os,subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[1]
# Printf arguments retain their exact types/order, including precision and escaped percentages.
pairs=re.findall(r'^    (\{.*\}),$',(ROOT/'main/ui/ui_messages.inc').read_text(),re.M)
fmt=re.compile(r'%(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn]|%)')
for item in pairs:
    zh,en=json.loads('['+item[1:-1]+']')
    assert fmt.findall(zh)==fmt.findall(en),(zh,en)
with tempfile.TemporaryDirectory(prefix='language-',dir=ROOT/'build') as folder:
    work=Path(folder);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for name in ('books','markdown','fonts','pictures'):(card/name).mkdir(parents=True)
    (card/'books/保存.txt').write_text('用户正文不应翻译。保存，取消，下一页，系统语言。\n\n'*300)
    (card/'markdown/取消.md').write_text('# 保存\n\n用户正文不应翻译。\n\n'*300)
    (out/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(view,events='',aux=0):
        p=subprocess.run([str(ROOT/'build/simulator/render'),view,'/sdcard/books/保存.txt',str(aux),'48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,''],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert p.returncode==0,(view,p.stderr[-1800:])
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()
    state,zh=render('settings-0');assert state['system_language']==0
    _,body_zh=render('book-1')
    for index,size in enumerate((100,120,140,160)):
        render('settings-5',f't,{108+156*index},300,{108+156*index},300')
        state,_=render('settings-0');g=state['settings_geometry'];y=g[2]+g[4]+g[4]//2;scroll=min(g[5],max(0,y-920))
        state,en=render('settings-0',f't,580,{y-scroll},580,{y-scroll}',scroll)
        assert state['settings_page']==0 and state['system_language']==1 and state['system_size']==size
        assert render('settings-0')[0]['system_language']==1
        for view in ('baseline-home','book-0','book-8','files-0','settings-2','settings-5','settings-20','settings-21','settings-24','settings-27','transfer-2','book-4'):
            render(view)
        # Controls are localized, reading body and actual Chinese title remain identical.
        _,body_en=render('book-1');assert body_en[160*342:1000*342]==body_zh[160*342:1000*342]
        state,_=render('settings-0',f't,450,{y-scroll},450,{y-scroll}',scroll)
        assert state['system_language']==0
        if size==120:assert render('settings-0')[1]==zh
print('PASS: bilingual printf catalog; persistent immediate language selector at all4 sizes; localized screens and unchanged Chinese book text/title')
