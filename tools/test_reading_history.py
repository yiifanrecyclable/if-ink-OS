#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test real history storage and paged UI on an isolated TF card, without GUI automation."""
from pathlib import Path
from datetime import date,timedelta
import json,os,struct,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
EPOCH=date(1970,1,1)
def day(d):return (d-EPOCH).days
with tempfile.TemporaryDirectory(prefix='history-',dir=R/'build') as tmp:
    base=Path(tmp);card=base/'card';card.mkdir();out=base/'build/simulator';out.mkdir(parents=True)
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    binary=base/'storage-test'
    flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-Dtime=history_test_time','-DPICO_SIM_CARD_PATH="unused"','-include','tools/simulator/fs.h']
    for inc in ('tools/simulator/include','main','main/app','main/book','main/font','main/ui','components/sc7a20h/include','components/read_pico_pmu/include','components/read_pico_transfer/include'):flags+=['-I'+inc]
    subprocess.run(flags+['tools/reading_history_host_test.c','main/book/reading_history_store.c','main/book/book_ticket.c','tools/simulator/fs.c','tools/simulator/nvs.c','-Wl,-dead_strip','-o',str(binary)],cwd=R,check=True)
    subprocess.run([str(binary)],cwd=base,env=env,check=True)
    # Independent fixture with more than two years; daily rows cross month and year boundaries.
    store=card/'.readpico/reading'
    for p in store.glob('*.bin'):p.unlink()
    first=date(2024,2,28);today=date(2026,1,4);first_day=day(first);today_day=day(today)
    values={d:(0 if d%13==0 else (d%17+1)*65) for d in range(first_day,today_day+1)}
    values.update({today_day:5400,today_day-1:3600,today_day-2:10800,today_day-3:0})
    for d,seconds in values.items():
        if not seconds:continue
        actual=EPOCH+timedelta(days=d);path=store/f'{actual.year}.bin'
        fields=[0x49485231,d,seconds];checksum=2166136261
        for value in fields:
            for b in range(4):checksum=((checksum^((value>>(b*8))&255))*16777619)&0xffffffff
        with path.open('r+b' if path.exists() else 'w+b') as f:
            f.seek((actual-date(actual.year,1,1)).days*32);f.write(struct.pack('<4I',*fields,checksum))
    # Remove prior test NVS: migration has already been tested above.
    import shutil
    shutil.rmtree(out/'nvs',ignore_errors=True)
    (out/'titles.txt').write_text('')
    env['PICO_SIM_HOME_DAY']=str(today_day)
    seconds=[values.get(d,0) for d in range(today_day-29,today_day+1)]
    def frame(events='',scale=120):
        nvs=out/'nvs/read_pico';nvs.mkdir(parents=True,exist_ok=True);(nvs/'sys_size').write_bytes(bytes([scale]))
        result=subprocess.run([str(R/'build/simulator/render'),'baseline-home','','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0',','.join(map(str,seconds)),'builtin',events],cwd=base,env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stderr
        return json.loads(result.stdout)
    opened='l,342,500,342,500'
    more='t,620,386,620,386'
    assert frame(more)['history_state'][0]==1
    assert frame()['reading_target']==90
    start=frame(opened);s=start['history_state'];assert s[0]==1 and s[1:3]==[0,0] and s[5:7]==[first_day,today_day],s
    rows=s[7];assert start['history_seconds'][:rows]==[values.get(today_day-i,0) for i in range(rows)]
    next_page=frame(opened+';k,2,0,0,0');assert next_page['history_state'][2]==1
    assert next_page['history_seconds'][0]==values[today_day-rows]
    assert frame(opened+';k,0,0,0,0')['history_state'][2]==0
    assert frame(opened+';k,1,0,0,0')['history_state'][0]==0
    assert frame(opened+';t,50,94,50,94')['history_state'][0]==0
    assert start['reading_target']==90 and start['history_bar_widths'][:4]==[612,408,612,0]
    # Six real selections persist across renderer launches; bars use the target rather than the page maximum.
    for minutes in (30,60,90,120,150,180):
        picker=frame(opened+';t,590,94,590,94');state=picker['history_state'];assert state[1]==1
        index=minutes//30-1;y=state[10]+index*state[9]+state[9]//2
        selected=frame(opened+f';t,590,94,590,94;t,300,{y},300,{y}')
        assert selected['history_state'][1]==0 and selected['reading_target']==minutes
        assert frame()['reading_target']==minutes
        for elapsed,width in zip(selected['history_seconds'],selected['history_bar_widths']):
            target=minutes*60;expected=612 if elapsed>=target else (elapsed*612+target-1)//target
            assert width==expected,(minutes,elapsed,width,expected)
    # Target choices fit all six rows; cancel/back keeps the prior value and list page.
    picker=frame(opened+';t,590,94,590,94');assert picker['history_state'][1]==1
    assert frame(opened+';t,590,94,590,94;k,1,0,0,0')['history_state'][:3]==[1,0,0]
    assert frame()['reading_target']==180
    # Home's current-day cell and list bar both reach full/dark only at the same daily target.
    def gray_at(x,y):
        pixels=(out/'frame.bin').read_bytes();byte=pixels[y*342+x//2]
        return byte>>4 if x%2==0 else byte&15
    frame();assert gray_at(528,632)>3
    state=frame(opened+';t,590,94,590,94');y=state['history_state'][10]+2*state['history_state'][9]+state['history_state'][9]//2
    frame(opened+f';t,590,94,590,94;t,300,{y},300,{y}')
    frame();assert gray_at(528,632)==3
    for scale in (100,120,140,160):
        state=frame(opened,scale)['history_state'];assert state[4]<=state[7]<=10 and state[10]+state[7]*state[8]<=1048,state
        assert state[10]+6*state[9]<1136,state
    # Empty and unavailable cards have no invented historical totals; today's zero row remains usable.
    shutil.rmtree(store)
    seconds[:]=[0]*30
    empty=frame(opened);assert empty['history_state'][3:5]==[1,1] and not any(empty['history_seconds'])
    assert not list(card.rglob('*.bin'))
print('reading history UI: daily totals, target persistence and saturation, year boundary, >2-year archive, paging, return, S/M/L/XL and empty data passed')
