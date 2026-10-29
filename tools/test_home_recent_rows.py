# SPDX-License-Identifier: Apache-2.0
"""首页右列整高排版、四档字号与新增行点击。 / Full-height Home list, four presets and extra-row hit tests."""
from pathlib import Path
import json,os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='home-recent-') as directory:
    work=Path(directory);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for name in ('books','markdown','fonts'):(card/name).mkdir(parents=True)
    shutil.copyfile(ROOT/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
    for i in range(8):(card/f'markdown/Article{i}.md').write_text('# Title\n\nReading text.\n'*20)
    (out/'titles.txt').write_text('');env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(view='baseline-home',events=''):
        result=subprocess.run([str(ROOT/'build/simulator/render'),view,'','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events,''],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert result.returncode==0,result.stderr
        return json.loads(result.stdout),(out/'frame.bin').read_bytes()
    def tap(x,y):return f't,{x},{y},{x},{y}'
    def check_separators(info,frame):
        top,step,rows=info['home_geometry'][:3]
        for boundary in range(1,rows):
            y=top+boundary*step
            assert all(((frame[y*342+x//2]>>(4 if x%2 else 0))&15)==8 for x in range(272,648))
        for y in (top,top+rows*step):
            assert all(((frame[y*342+x//2]>>(4 if x%2 else 0))&15)==15 for x in range(272,648))
    check_separators(*render())
    for i in range(8):
        result=subprocess.run([str(ROOT/'build/simulator/cloud-host'),'progress-save',f'/sdcard/markdown/Article{i}.md'],cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert result.returncode==0 and json.loads(result.stdout)['error']==0
        if i==1:check_separators(*render())
    counts=[]
    for i in range(4):
        render('settings-5',tap(108+156*i,300))
        info,frame=render();top,step,rows=info['home_geometry'][:3];counts.append(rows)
        assert top==712 and top<info['home_geometry'][11] and 4<=rows<=6
        assert top+step*rows<=1120
        paths=[]
        for row in range(rows):
            state,_=render(events=tap(400,top+row*step+step//2))
            assert state['route']==1;paths.append(state['book_path'])
        assert len(set(paths))==rows
        state,_=render(events=tap(400,top-1));assert state['route']==-1
        check_separators(info,frame)
    assert counts[0]>=counts[1]>=counts[2]>=counts[3] and counts[0]>4 and counts[3]>3,counts
    # 切换TF字体后仍使用实际行高，所有新增行继续可点击。/ TF font metrics retain valid layout and hit regions.
    info,_=render('settings-5');font_top,font_step=info['settings_geometry'][6:8]
    render('settings-5',tap(300,font_top+font_step+(font_step-12)//2))
    info,_=render();assert info['system_font_path']=='/sdcard/fonts/PicoSans-Medium.ttf'
    top,step,rows=info['home_geometry'][:3]
    for row in range(rows):assert render(events=tap(400,top+row*step+step//2))[0]['route']==1
    print('PASS: full-height recent list; preset row counts',counts,'; all rows open distinct read documents; TF font, separators and top hit bounds pass')
