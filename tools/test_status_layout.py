#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""检查56px状态栏、新首页分区及紧靠状态栏的点击区域。/ Verify the 56px status band, home sections and newly raised hit targets."""
from pathlib import Path
import json,os,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='status56-',dir=R/'build') as tmp:
    base=Path(tmp);out=base/'build/simulator';out.mkdir(parents=True);card=base/'card'
    for n in ('books','markdown','fonts'):(card/n).mkdir(parents=True)
    (out/'titles.txt').write_text('');env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(view,events=''):
        p=subprocess.run([str(R/'build/simulator/render'),view,'','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0',','.join(['0']*30),'builtin',events],cwd=base,env=env,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()
    def tone(data,x,y):return (data[y*342+x//2]>>(4 if x%2 else 0))&15
    meta,home=frame('baseline-home');g=meta['home_geometry'];assert g[0:4]==[712,81,5,466],g
    assert g[8:]==[440,386,696,730],g
    for y in (356,696):assert all(tone(home,x,y)==6 for x in range(36,648)),y
    assert any(tone(home,x,y)==0 for x in range(606,654) for y in range(19,42))
    assert all(tone(home,x,y)==15 for x in range(606,654) for y in range(44,56))
    for view in ('book-0','files-0','settings-9'):
        _,data=frame(view);assert data[:56*342]==home[:56*342],view
    meta,_=frame('book-0','t,240,70,240,70');assert meta['book_view']==8,meta['book_view']
    meta,_=frame('settings-9','t,50,65,50,65');assert meta['settings_page']==0,meta['settings_page']
print('status layout: identical 56px status strips, 56/300/340/440/80 home geometry, battery raised and matching top hit targets passed')
