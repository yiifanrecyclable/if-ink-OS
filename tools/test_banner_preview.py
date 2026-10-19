# SPDX-License-Identifier: Apache-2.0
"""Independent banner layers, outline compositing, draft transactions and v1 migration."""
from pathlib import Path
import json,os,shutil,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
def tap(x,y):return f't,{x},{y},{x},{y}'
def png(width,height,tone):
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    pixels=b''.join(b'\0'+bytes([tone])*width for _ in range(height))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='banner-layers-') as temp:
    work=Path(temp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('books','markdown/attachments','fonts','banner pic/sub'):(card/folder).mkdir(parents=True)
    shutil.copyfile(ROOT/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
    shutil.copyfile(ROOT/'assets/banner/default-banner.png',card/'banner pic/默认背景.png')
    (card/'banner pic/dark.png').write_bytes(png(684,300,0));(card/'banner pic/broken.png').write_bytes(b'invalid png')
    (out/'titles.txt').write_text('');env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_HOME_DAY':'20733'}
    def render(native='settings-21',events=''):
        p=subprocess.run([str(ROOT/'build/simulator/render'),native,'','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0',','.join(['60']*30),'builtin',events,''],cwd=work,env=env,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()
    def banner(frame):return frame[56*342:356*342]
    def tone(frame,x,y):return (frame[y*342+x//2]>>(4 if x%2 else 0))&15
    config=out/'nvs/if_content/banner_v1'
    info,initial=render();assert info['banner_draft'][:5]==[0,1,96,2,0] and info['banner_draft'][11]==1
    assert banner(initial)==banner(render('baseline-home')[1]) and not config.exists()
    assert tone(initial,0,356)==15 and tone(initial,342,356)<15
    # Three parent controls: background enters a secondary choice; text/date independently hide.
    assert render(events=tap(342,476))[0]['settings_page']==31
    assert render(events=tap(342,800))[0]['settings_page']==23
    assert render(events=tap(342,864))[0]['settings_page']==22
    off=tap(590,736);info,text_off=render(events=off);assert info['banner_draft'][11]==0
    assert render(events=off+';'+tap(342,800))[0]['settings_page']==21
    info,blank=render(events=off+';'+tap(590,568));assert info['banner_draft'][1]==0 and set(banner(blank))=={255}
    # All four alignments are live draft controls; the size slider lives in the font subpage.
    for i in range(4):
        info,_=render(events=tap(400+72*i,928));assert info['banner_draft'][3]==i and not config.exists()
    font_entry=tap(342,864)
    info,_=render(events=font_entry+';'+tap(612,300));assert info['quote_size']==120 and not config.exists()
    info,_=render(events=font_entry+';p,432,300,432,300;m,432,300,612,300;c,432,300,612,300');assert info['quote_size']==96 and not config.exists()
    info,_=render(events=font_entry+';'+tap(340,550));assert info['settings_page']==21 and info['banner_font'].endswith('PicoSans-Medium.ttf') and not config.exists()
    # Folder browsing, broken image rejection, exact home/preview parity and opaque black halo fixture.
    picker=tap(342,476)
    info,_=render(events=picker);assert info['settings_page']==31
    paths=info['banner_items'];step=info['banner_draft'][8]
    index=next(i for i,p in enumerate(paths) if p.endswith('/sub'))
    assert render(events=picker+';'+tap(342,366+index*step+20))[0]['settings_page']==31
    assert render(events=picker+';'+tap(600,316))[0]['settings_page']==31
    broken=next(i for i,p in enumerate(paths) if p.endswith('/broken.png'))
    assert render(events=picker+';'+tap(342,366+broken*step+20))[0]['settings_page']==31 and not config.exists()
    dark=next(i for i,p in enumerate(paths) if p.endswith('/dark.png'))
    choose=picker+';'+tap(342,366+dark*step+20)
    info,all_on=render(events=choose);assert len(set(all_on[356*342:358*342]))==1 and all_on[356*342]!=255;assert info['banner_draft'][:2]==[1,1] and info['banner_draft'][11]==1
    info,date_only=render(events=choose+';'+off);assert info['banner_draft'][11]==0
    info,text_only=render(events=choose+';'+tap(590,568));assert info['banner_draft'][1]==0
    for frame in (all_on,date_only,text_only):assert any(tone(frame,x,y)==15 for y in range(56,356) for x in range(36,648))
    none=choose+';'+off+';'+tap(590,568);info,black=render(events=none);assert set(banner(black))=={0}
    # Date arrows change only the independent date position, and clamp at both boundaries.
    base=render(events=choose)[0]['banner_draft'][5]
    assert render(events=choose+';'+tap(604,640))[0]['banner_draft'][5]==min(260,base+4)
    assert render(events=choose+';'+tap(516,640))[0]['banner_draft'][5]==max(4,base-4)
    assert render(events=choose+';'+tap(342,476)+';'+tap(342,240))[0]['banner_draft'][0]==0
    assert not config.exists()
    # Cancel leaves storage untouched; final Save alone commits the complete layered state.
    render(events=none+';'+tap(180,1098));assert not config.exists()
    info,_=render(events=choose+';'+tap(500,1098));assert info['quote_settings_committed'] and info['route']==0
    assert config.exists() and banner(render('baseline-home')[1])==banner(all_on)
    saved=config.read_bytes();render(events=off+';'+tap(180,1098));assert config.read_bytes()==saved
    (card/'banner pic/dark.png').unlink();info,missing=render('baseline-home');assert info['route']==-1 and banner(missing)==banner(initial)
    # The quote can move across the full banner; actual ink remains within both bounds.
    current=list(struct.unpack('<IH6B192s160s288sH2x',saved));current[2]=0;current[3]=0
    for y in (0,296):
        current[-1]=y;config.write_bytes(struct.pack('<IH6B192s160s288sH2x',*current))
        info,frame=render();ys=[yy for yy in range(56,356) if any(tone(frame,x,yy)<8 for x in range(36,648))]
        assert ys and min(ys)>=58 and max(ys)<354
        assert min(ys)<=62 if y==0 else max(ys)>=349
        before=info['banner_draft'][12]
        info,_=render(events=tap(604 if y==0 else 516,1008));after=info['banner_draft'][12]
        assert after>before if y==0 else after<before
    # Explicit breaks are preserved in four actual lines at a fitting size, with exact Home/preview output.
    current=list(struct.unpack('<IH6B192s160s288sH2x',saved));current[2]=0;current[3]=0;current[4]=48;current[8]=b'one\ntwo\nthree\nfour';current[-1]=2
    config.write_bytes(struct.pack('<IH6B192s160s288sH2x',*current));info,four=render()
    assert info['banner_text']=='one\ntwo\nthree\nfour' and banner(four)==banner(render('baseline-home')[1])
    ys=[y for y in range(56,354) if any(tone(four,x,y)<8 for x in range(36,648))];runs=[]
    for y in ys:
        if not runs or y>runs[-1][-1]+1:runs.append([y])
        else:runs[-1].append(y)
    assert len(runs)==4,runs
    # Legacy image-only/text-only snapshots retain old visibility and migrate without writes.
    old=list(struct.unpack('<IH6B192s160s288s',saved[:652]));old[0]=1;old[7]=0
    for mode in (0,1):
        old[2]=mode;old[3]=0;old[-1]=b'/sdcard/banner pic/\xe5\xb1\xb1\xe6\xb0\xb4\xe7\xa4\xba\xe4\xbe\x8b.png'
        blob=struct.pack('<IH6B192s160s288s',*old);config.write_bytes(blob)
        info,_=render();assert info['banner_draft'][11]==(not mode) and info['banner_draft'][1]==(not mode) and config.read_bytes()==blob
    print('PASS: independent background/date/text, text/date halos, four alignment icons and full-height text movement, font/size drafts, date arrows, file browsing, Save/Cancel, missing-image fallback and v1 migration.')
