#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""原生阅读回调回归；隔离TF/NVS，不做截图或鼠标自动化。
Exercise native reader callbacks with isolated TF/NVS, without visual or cursor tests.
"""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/simulator/render'
with tempfile.TemporaryDirectory(prefix='reader-controls-',dir=ROOT/'build') as folder:
    base=Path(folder)
    (base/'build/simulator/vfs/flash/books').mkdir(parents=True)
    (base/'build/simulator/nvs').mkdir(parents=True)
    (base/'build/simulator/vfs/flash/books/controls.txt').write_text('阅读测试。字体变化后继续这一段，避免阅读位置跳动。\n'*1800)
    (base/'titles.txt').write_text('')
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(base/'card')}
    def frame(events='',panel=0,page=1,path='/flash/books/controls.txt',view=1):
        args=[str(BINARY),f'book-{view}',path,str(page),'48','build/simulator/frame.bin','titles.txt',str(panel),','.join(['0']*30),'builtin',events,'']
        result=subprocess.run(args,cwd=base,env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stderr[-1000:]
        return json.loads(result.stdout)
    tap=lambda x,y:f't,{x},{y},{x},{y}'
    baseline=frame();offset=baseline['reader_offset']
    assert offset>0 and baseline['reader_panel']==0 and baseline['reader_shortcuts']==[0]*12
    state=frame(tap(342,500))
    assert state['reader_panel']==1 and state['reader_offset']==offset
    state=frame(tap(342,500)+';k,1,0,0,0')
    assert state['reader_panel']==0 and state['reader_offset']==offset
    state=frame('a,500,500,200,500;l,342,500,342,500;q,5,0,5,0')
    assert state['reader_offset']==offset and state['reader_panel']==0
    assert frame(tap(550,500))['reader_page']==2
    # 从真实面板绑定“右滑→目录”，而不是在测试中直接改偏好。/ Bind right-swipe to TOC through production controls.
    rowh=baseline['reader_controls_geometry'][1]
    shortcuts=frame(panel=12);rh=shortcuts['reader_controls_geometry'][1]
    bindings=frame(panel=13);bh=bindings['reader_controls_geometry'][1]
    assert shortcuts['reader_controls_geometry'][2]==8 and bindings['reader_controls_geometry'][2]==11
    events=';'.join([tap(342,500),tap(615,1183),tap(200,220+rowh+rowh//2),tap(200,220+rh+rh//2),tap(180,220+3*bh+bh//2),'k,1,0,0,0','q,1,0,1,0'])
    state=frame(events)
    assert state['reader_shortcuts'][1]==3 and state['book_view']==2,state
    assert frame()['reader_shortcuts'][1]==3
    # 最小边距真正到8px；滑块重排保持字节位置所在页。/ Minimum margin really reaches 8px; reflow retains the anchored text.
    state=frame(tap(100,220+rowh-40),panel=3)
    assert state['reader_margin']==8,state['reader_margin']
    assert abs(state['reader_offset']-offset)<3000
    # 边距逐像素加减，与字间距独立；段距支持非25倍数。/ Fine independent spacing through production controls.
    sy=220+rowh-40
    assert frame(tap(625,sy),panel=3)['reader_margin']==9
    assert frame(tap(625,sy),panel=3)['reader_margin']==10
    assert frame(tap(60,sy),panel=3)['reader_margin']==9
    frame(tap(625,sy+3*rowh),panel=3)
    assert frame()['reader_margin']==9
    assert frame(tap(625,sy),panel=3)['reader_margin']==10
    assert frame(tap(60,sy),panel=3)['reader_margin']==9
    frame(tap(60,sy+3*rowh),panel=3)
    # 真实短拖/长按后松手提交，按住阶段不写版式配置。/ Real short and held releases commit without premature persistence.
    margin_file=base/'build/simulator/nvs/read_pico/bk_margin'
    held=f'b,300,{sy},300,{sy};h,300,{sy},300,{sy};v,360,{sy},360,{sy}'
    previous=frame()['reader_margin'];frame(held,panel=3)
    assert int.from_bytes(margin_file.read_bytes(),'little')==previous
    state=frame(held+f';e,360,{sy},360,{sy}',panel=3)
    assert state['reader_margin']>previous
    before_short=state['reader_margin']
    short=f'b,240,{sy},240,{sy};v,300,{sy},300,{sy};e,300,{sy},300,{sy}'
    state=frame(short,panel=3)
    assert state['reader_margin']<before_short and state['reader_margin']==int.from_bytes(margin_file.read_bytes(),'little')
    state=frame(tap(625,sy+2*rowh),panel=3)
    assert (base/'build/simulator/nvs/read_pico/bk_para').read_bytes()==bytes([51])
    # 恢复基线边距，后续定位断言使用相同布局。/ Restore baseline layout for later anchor checks.
    frame(tap(100,sy),panel=3)
    # 中部第三按钮切换当前页书签；底部入口只展示列表。/ The third quick action toggles the current page; the footer only opens the list.
    ay,height=baseline['reader_progress_geometry'][1:]
    toggle=tap(425,ay+height//2)
    state=frame(toggle,panel=1)
    assert state['reader_bookmark_count']==1 and state['reader_panel']==1
    assert frame(toggle,panel=1)['reader_bookmark_count']==0
    state=frame(tap(205,1180),panel=1)
    assert state['reader_panel']==7 and state['reader_bookmark_count']==0
    for panel in (2,3,9):
        state=frame(tap(205,1180),panel=panel)
        assert state['reader_panel']==7 and state['reader_bookmark_count']==0
    state=frame(tap(205,1180),view=2)
    assert state['book_view']==1 and state['reader_panel']==7 and state['reader_bookmark_count']==0
    # 绑定冲突经生产页面拦截，原手势保持有效。/ Reject conflicting action selection in the real UI.
    state=frame(tap(180,220+rh//2)+';'+tap(180,220+3*bh+bh//2),panel=12)
    assert state['reader_panel']==13 and state['reader_shortcuts'][0]==0 and state['reader_shortcuts'][1]==3
    # 进度拖动不加载；取消保留原位置，松手跳转并可返回。/ Preview/cancel preserve the anchor; release seeks and can return.
    py=baseline['reader_progress_geometry'][0]
    drag=f'p,36,{py},36,{py};m,36,{py},495,{py}'
    state=frame(drag,panel=1)
    assert state['reader_panel']==1 and state['reader_offset']==offset
    state=frame(drag+f';c,36,{py},495,{py}',panel=1)
    assert state['reader_offset']==offset
    release=drag+f';r,36,{py},495,{py}'
    state=frame(release,panel=1)
    assert state['reader_panel']==0 and 73<=state['reader_progress']<=76,state
    state=frame(release+';'+tap(342,500)+';'+tap(610,py+35),panel=1)
    assert state['reader_panel']==0 and state['reader_offset']==offset,state
    assert frame(tap(36,py),panel=1)['reader_page']==0
    state=frame(tap(648,py),panel=1)
    assert state['reader_panel']==0 and state['reader_progress']>90
    # 真实触摸状态机：短拖CANCEL代表松手；长按不会提前跳转，后续仍能拖动；多点丢弃。/ Exercise actual recognizer release/hold/cancel semantics.
    raw=lambda kind,x,y:f'{kind},{x},{y},{x},{y}'
    short=';'.join([raw('b',342,py),raw('v',402,py),raw('e',402,py)])
    state=frame(short,panel=1)
    assert state['reader_panel']==0 and 58<=state['reader_progress']<=61,state
    held=';'.join([raw('b',342,py),raw('h',342,py),raw('v',495,py)])
    assert frame(held,panel=1)['reader_offset']==offset
    assert frame(held+';'+raw('e',495,py),panel=1)['reader_progress']>=73
    multi=';'.join([raw('b',342,py),raw('v',495,py),raw('n',495,py),raw('e',495,py)])
    assert frame(multi,panel=1)['reader_offset']==offset
    state=frame(';'.join([raw('b',425,ay+height//2),raw('e',425,ay+height//2)]),panel=1)
    assert state['reader_bookmark_count']==1 and state['reader_panel']==1
    frame(toggle,panel=1)
    # 主面板四个操作，更多页面的定期全刷无手动强刷入口。/ Exercise fullscreen, refresh and shifted More settings.
    state=frame(tap(255,ay+height//2),panel=1)
    assert state['reader_layout']==1 and state['reader_panel']==1
    (base/'build/simulator/vfs/flash/books/second.txt').write_text('第二本图书阅读测试。'*100)
    (base/'card/markdown').mkdir(parents=True,exist_ok=True)
    (base/'card/markdown/second.md').write_text('# 文档\n\n全屏跨文档保留。'*80)
    assert frame(path='/flash/books/second.txt')['reader_fullscreen']
    assert frame(path='/sdcard/markdown/second.md')['reader_fullscreen']
    state=frame(tap(255,ay+height//2),panel=1)
    assert state['reader_layout']==2 and state['reader_panel']==1
    assert frame(path='/sdcard/markdown/second.md')['reader_layout']==2
    state=frame(tap(255,ay+height//2),panel=1)
    assert state['reader_layout']==0 and state['reader_panel']==1
    assert not frame(path='/flash/books/second.txt')['reader_fullscreen']
    assert frame(tap(595,ay+height//2),panel=1)['reader_panel']==0
    state=frame(tap(160,220+3*rowh+rowh//2),panel=9)
    assert state['reader_panel']==9
    # 阅读线列表直接选择样式，使用真实设置持久化。/ Select preview rows through production persistence.
    rule=base/'build/simulator/nvs/read_pico/bk_rule'
    for style in (1,2,0):
        state=frame(tap(100,220+style*rowh+rowh//2),panel=4)
        assert state['reader_panel']==4 and rule.read_bytes()==bytes([style])
    # 读线偏移按钮分区明确，中间数值不承担加减操作。/ Separate offset hit targets; tapping the value does not change it.
    rule_y=220+3*rowh+rowh//2
    offset_file=base/'build/simulator/nvs/read_pico/bk_rule_y'
    frame(tap(380,rule_y),panel=4)
    assert offset_file.read_bytes()==bytes([3])
    frame(tap(484,rule_y),panel=4)
    assert offset_file.read_bytes()==bytes([3])
    frame(tap(580,rule_y),panel=4)
    assert offset_file.read_bytes()==bytes([4])
    # 绘图接口用8位灰值；检查说明区没有误填成黑色。/ Rectangles use 8-bit gray; reject accidental black help bands.
    frame(panel=14)
    pixels=(base/'build/simulator/frame.bin').read_bytes()
    # 底图正文应跨至右侧点击区，而非固定字数排在左半屏。/ Faded prose must extend into the right-hand tap zone.
    right_ink=sum(((pixels[y*342+x//2]>>(4*(x%2)))&15)<15 for y in range(250,450) for x in range(490,640))
    assert right_ink>300,right_ink
    for top,bottom in ((80,160),(1110,1216)):
        grays=[(pixels[y*342+x//2]>>(4*(x%2)))&15 for y in range(top,bottom,2) for x in range(0,684,2)]
        assert sum(gray==15 for gray in grays)/len(grays)>(.85 if top==80 else .65),(top,bottom)
    # 底部保留淡色原文和分割线，按键提示作为白描边浮层。/ Prose and zone dividers continue behind the physical-key legend.
    bottom_gray=[(pixels[y*342+x//2]>>(4*(x%2)))&15 for y in range(1080,1179) for x in range(36,648)]
    assert sum(11<=gray<15 for gray in bottom_gray)>300
    for x in (228,456):
        assert ((pixels[1160*342+x//2]>>(4*(x%2)))&15)==0
        assert ((pixels[1200*342+x//2]>>(4*(x%2)))&15)==15
    # 引导的画面点击只作说明，不翻页；顶部关闭及不再显示有效。/ Guide regions explain rather than turn; header controls work.
    state=frame(tap(550,500)+';'+tap(100,123)+';'+tap(610,123),panel=14)
    assert state['reader_panel']==0 and state['reader_page']==1
    prefs=(base/'build/simulator/nvs/rp_input/preferences').read_bytes()
    assert len(prefs)==14 and prefs[-1]==1
print('reader controls: fixed taps, unique native binding, 8px margin, bookmark toggle/list, drag/cancel/seek/return, panel actions and guide passed')
