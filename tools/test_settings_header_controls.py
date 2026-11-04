#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise real Settings header exits and inert title/blank areas on isolated media."""
from pathlib import Path
import json,os,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='settings-header-',dir=ROOT/'build') as tmp:
    work=Path(tmp);card=work/'card';(card/'pictures').mkdir(parents=True)
    (work/'build/simulator').mkdir(parents=True);(work/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(view,events='',mode=0):
        args=[str(ROOT/'build/simulator/render'),view,'/sdcard/books/none.txt',str(mode),'48',
              'build/simulator/frame.bin','titles.txt','0','0','builtin',events]
        p=subprocess.run(args,cwd=work,env=env,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr[-1500:]
        return json.loads(p.stdout)
    tap=lambda x,y:f't,{x},{y},{x},{y}'
    # Legacy page 32 is normalized to the same Banner browser page 31 on entry.
    pages=(1,2,3,5,9,11,12,13,14,20,22,24,25,26,27,28,31)
    destinations={3:2,12:27,22:21,25:2,26:14,28:27,31:21,32:21}
    for page in pages:
        view=f'settings-{page}'
        for x,y in ((342,83),(115,83),(30,30),(580,135)):
            state=frame(view,tap(x,y))
            assert state['settings_page']==page,(page,x,y,state['settings_page'])
        state=frame(view,tap(58,83))
        assert state['settings_page']==destinations.get(page,0),(page,state['settings_page'])
        state=frame(view,'t,58,83,115,83')
        assert state['settings_page']==page,(page,'release outside exit',state['settings_page'])
    for x,y in ((342,83),(115,83),(30,30)):
        state=frame('settings-19',tap(x,y))
        assert state['settings_page']==19,state['settings_page']
    for x,y in ((342,83),(115,83),(30,30)):
        state=frame('transfer-2',tap(x,y),3)
        assert state['route']<0 and state['transfer_view']==2,state
    assert frame('transfer-2',tap(58,83),3)['route']==3
    # The retired manual-log page is no longer exposed; the last device row still opens BOOT.
    assert frame('settings-33')['settings_page']==0
    geometry=frame('settings-0')['settings_geometry'];device,step=geometry[2],geometry[4]
    for index,page in ((0,2),(2,14),(3,24),(4,11),(5,27),(6,13)):
        y=device+step*index+step//2;scroll=max(0,y-1000)
        assert frame('settings-0',tap(340,y-scroll),scroll)['settings_page']==page,(index,page)
print('PASS Settings header exits: bounded press/release, inert titles/blanks, nested destinations, input and WiFi return')
