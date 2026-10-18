# SPDX-License-Identifier: Apache-2.0
"""文档字号/样式原生回归，所有文件与设置均使用私有TF。 / Native Markdown typography over a private card/NVS."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
def tap(x,y):return f't,{x},{y},{x},{y}'
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='md-styles-') as temp:
    work=Path(temp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('books','markdown/attachments','fonts'):(card/folder).mkdir(parents=True)
    shutil.copyfile(ROOT/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
    md=card/'markdown/Test.md';md.write_text('# 一级标题\n\n## 二级标题\n\n### 三级标题\n\n#### 小标题\n\n正文 **粗体** *斜体* ==高亮 **重点**==\n\n`==代码保持原样==`\n\n'+('长段落测试，保持阅读位置。'*100))
    (card/'books/Test.txt').write_text('Normal book\n\n'+('Normal body text. '*500))
    (out/'titles.txt').write_text('');env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(panel=0,events='',path='/sdcard/markdown/Test.md',page=0):
        p=subprocess.run([str(ROOT/'build/simulator/render'),'book-1',path,str(page),'48','build/simulator/frame.bin','build/simulator/titles.txt',str(panel),','.join(['60']*30),'builtin',events,''],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout),(out/'frame.bin').read_bytes()
    info,body=render();assert info['markdown_sizes']==[48,72,64,56] and info['reader_px']==48
    assert info['pages']>3 and info['toc_count']==1
    info,preview=render(2);assert info['reader_panel']==2
    # 拖动时只改预览；取消不保存；松手才保存一次并更新分页。/ Draft-only drag, cancel and release commit.
    prefs=out/'nvs/if_content/md_sizes'
    y=info['markdown_slider_y'][0]
    press=f'p,360,{y},360,{y}';move=f'm,360,{y},584,{y}';release=f'r,360,{y},584,{y}'
    info,draft=render(11,press+';'+move);assert info['markdown_sizes'][0]==72 and info['reader_px']==48 and not prefs.exists()
    assert draft[936*342:1036*342]!=preview[936*342:1036*342]
    info,_=render(11,press+';'+move+';c,360,720,584,720');assert info['reader_px']==48 and info['markdown_sizes'][0]==48 and not prefs.exists()
    info,_=render(11,press+';'+move+';'+release);assert info['reader_px']==72 and info['markdown_committed'] and prefs.read_bytes()==bytes([72,72,64,56])
    info,_=render();assert info['reader_px']==72 and not info['markdown_committed']
    # H1/H2/H3 separately persist without overwriting body or book sizes.
    for i,y in enumerate(info['markdown_slider_y'][1:],1):
        info,_=render(11,tap(40,y));assert info['markdown_sizes'][i]==[72,71,63,55][i] and info['markdown_committed']
    assert prefs.read_bytes()==bytes([72,71,63,55])
    info,_=render(path='/sdcard/books/Test.txt');assert info['reader_px']==48
    assert render(11,tap(340,1183))[0]['reader_panel']==2
    # Reading offsets survive size changes, even though the page count changes.
    prefs.write_bytes(bytes([48,72,64,56]));info,_=render(11,press+';'+move+';'+release,page=2)
    assert info['reader_page']>0 and info['reader_progress']>0
    # Actual pixels distinguish all three styles, using the real font and production draw path.
    prefs.write_bytes(bytes([48,72,64,56]))
    def sample(text):
        md.write_text(text+'\n');info,frame=render();return frame[175*342:950*342]
    normal=sample('中文 Text');bold=sample('**中文 Text**');italic=sample('*中文 Text*');highlight=sample('==中文 Text==')
    assert len({normal,bold,italic,highlight})==4
    def dark(frame):return sum((b&15)<8 for b in frame)+sum((b>>4)<8 for b in frame)
    assert dark(bold)>dark(normal), (dark(bold),dark(normal))
    assert highlight.count(0xcc)>100
    assert sample('\\==中文 Text==')==sample('`==中文 Text==`')
    # Opaque / corrupt preferences fall back safely.
    prefs.write_bytes(bytes([0,250,64,56]));info,_=render();assert info['markdown_sizes']==[48,72,64,56]
    print('PASS: Markdown tiers, real-font live preview, drag/commit/cancel, persistence, separate book size, repagination and bold/italic/highlight pixels on private TF/NVS.')
