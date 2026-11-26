#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise real Settings header exits and inert title/blank areas on isolated media."""
from pathlib import Path
import json,os,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='settings-header-',dir=ROOT/'build') as tmp:
    work=Path(tmp);card=work/'card';(card/'pictures').mkdir(parents=True)
    (work/'build/simulator').mkdir(parents=True);(work/'titles.txt').write_text('')
    (card/'books').mkdir()
    (card/'books/Title.txt').write_text('Title fixture\nA paragraph of text.\n')
    nvs=work/'build/simulator/nvs/read_pico';nvs.mkdir(parents=True)
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(view,events='',mode=0,path='/sdcard/books/none.txt'):
        args=[str(ROOT/'build/simulator/render'),view,path,str(mode),'48',
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
        state=frame(view,'k,1,0,0,0')
        assert state['settings_page']==destinations.get(page,0),(page,state['settings_page'])
        for key in (0,2):
            state=frame(view,f'k,{key},0,0,0')
            assert state['settings_page']==page and state['route']==-1,(page,key,state['route'])
    assert frame('settings-0','k,1,0,0,0')['route']==0
    assert frame('settings-19','k,1,0,0,0')['settings_page']==0
    for x,y in ((342,83),(115,83),(30,30)):
        state=frame('settings-19',tap(x,y))
        assert state['settings_page']==19,state['settings_page']
    for x,y in ((342,83),(115,83),(30,30)):
        state=frame('transfer-2',tap(x,y),3)
        assert state['route']<0 and state['transfer_view']==2,state
    assert frame('transfer-2',tap(58,83),3)['route']==3
    env['PICO_SIM_SAVED_WIFI']='1'
    open_forget=tap(562,315)
    raw=work/'build/simulator/frame.bin'
    state=frame('transfer-2',open_forget,3)
    assert state['transfer_view']==2 and state['route']==-1
    assert state['transfer_saved_wifi'] and state['transfer_forget_open']
    top=state['transfer_forget_card_top']
    pixels=raw.read_bytes()
    assert 800<top<1000 and pixels[top*342:(top+1)*342]==bytes([0x55])*342
    assert pixels[127*342]>0x55  # The original header is still visible through the faded backdrop.
    for event in (tap(502,1080),tap(630,top+50),tap(58,90),'k,1,0,0,0',tap(170,1138)):
        dismissed=frame('transfer-2',open_forget+';'+event,3)
        assert dismissed['transfer_view']==2 and dismissed['route']==-1
        assert dismissed['transfer_saved_wifi'] and not dismissed['transfer_forget_open'],event
    dragged=frame('transfer-2',open_forget+';t,180,1080,500,1080',3)
    assert dragged['transfer_saved_wifi'] and dragged['transfer_forget_open']
    confirmed=frame('transfer-2',open_forget+';'+tap(180,1080),3)
    assert not confirmed['transfer_saved_wifi'] and not confirmed['transfer_forget_open']
    assert confirmed['transfer_view']==2 and confirmed['route']==-1
    del env['PICO_SIM_SAVED_WIFI']
    for event in (tap(120,90),tap(58,40),tap(58,130),'t,58,90,115,90'):
        state=frame('cloud-0',event,path='titles.txt')
        assert state['route']==-1 and state['cloud_command']==0,state
    for event in (tap(58,90),'k,1,0,0,0'):
        state=frame('cloud-0',event,path='titles.txt')
        assert (state['route'],state['cloud_command'])==(2,4),state
    for view,mode in (('transfer-0',0),('transfer-1',0),('transfer-1',1),('transfer-1',2),('transfer-2',3),('transfer-3',3)):
        def outcome(state):
            return state['route'],state['transfer_view'],state['transfer_mode']
        before=outcome(frame(view,mode=mode))
        for event in (tap(115,90),tap(58,40),tap(58,130),'t,58,90,115,90'):
            assert outcome(frame(view,event,mode))==before,(view,mode,event)
        by_key=outcome(frame(view,'k,1,0,0,0',mode))
        assert by_key!=before and outcome(frame(view,tap(58,90),mode))==by_key,(view,mode,by_key,before)
    # 检查最大字号下的实际墨迹与阅读页同款通长线。/ Check real glyph ink and the same edge-to-edge rule at XL.
    (nvs/'sys_size').write_bytes(b'\xa0')
    assert not frame('book-1',path='/sdcard/books/Title.txt')['reader_fullscreen']
    reader=raw.read_bytes()
    def icon(data):
        return b''.join(data[y*342+12:y*342+46] for y in range(56,124))
    for language in (0,1):
        (nvs/'language').write_bytes(bytes([language]))
        for view,mode in [('cloud-0',0),('transfer-0',0),('transfer-2',3),('transfer-3',3)]+[
            (f'settings-{page}',0) for page in pages]+[('settings-19',0)]:
            state=frame(view,mode=mode,path='titles.txt')
            assert state['system_size']==160
            data=raw.read_bytes()
            assert icon(data)==icon(reader),(view,language)
            assert data[127*342:129*342]==reader[127*342:129*342]==bytes([0x55])*684,(view,language)
            assert all(data[y*342+50:y*342+292]==b'\xff'*242 for y in range(117,127)),(view,language)
            if view=='settings-2':
                assert all(data[y*342+18:y*342+324]==bytes([0x55])*306 for y in (600,601))
    (nvs/'sys_size').unlink()
    (nvs/'language').unlink()
    # The retired manual-log page is no longer exposed; the last device row still opens BOOT.
    assert frame('settings-33')['settings_page']==0
    geometry=frame('settings-0')['settings_geometry'];device,step=geometry[2],geometry[4]
    for index,page in ((0,2),(2,14),(3,24),(4,11),(5,27),(6,13)):
        y=device+step*index+step//2;scroll=max(0,y-1000)
        assert frame('settings-0',tap(340,y-scroll),scroll)['settings_page']==page,(index,page)
print('PASS Settings header exits: bounded press/release, inert titles/blanks, nested destinations, input and WiFi return')
