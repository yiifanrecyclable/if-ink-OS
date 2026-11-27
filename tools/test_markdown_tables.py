# SPDX-License-Identifier: Apache-2.0
"""中文：真实字体、阅读页与续读的隔离表格回归。 / English: Isolated native-font table pages and resume regression."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
ARTIFACTS=ROOT/'build/markdown-table-preview'
ARTIFACTS.mkdir(exist_ok=True)
SAMPLE='''# 表格阅读示例

普通表格支持表头、自动换行和文字样式。

| 项目 | 说明 | 状态 |
| :--- | :---: | ---: |
| Markdown | **粗体**、*斜体*与==高亮== | 已支持 |
| 坚果云 | 从指定文件夹拉取文档 | 已支持 |
| 阅读位置 | 调整字号后保持阅读位置 | 已支持 |
| 长文字 | 单元格中的长段落自动换行，不挤压旁边的单元格。 | 测试中 |

## 长表格

| 编号 | 内容 | 备注 |
| :--- | --- | ---: |
'''+''.join(f'| {i:02} | 第{i}行：连续翻页检查，每页保留表头。 | 阅读记录 |\n' for i in range(1,26))+'''
## 宽表格

列太多时按列名与内容展开，保持正文的字号。

| 日期 | 项目 | 分类 | 状态 | 负责人 | 备注 | 优先级 |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-10-10 | 表格阅读 | Markdown | 测试 | Pico | 长文字自动换行 | 高 |

表格后面的正文也应正常显示。
'''
(ARTIFACTS/'表格阅读示例.md').write_text(SAMPLE)
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='md-tables-') as temp:
    work=Path(temp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('books','markdown/attachments','fonts'):(card/folder).mkdir(parents=True)
    md=card/'markdown/Tables.md';md.write_text(SAMPLE)
    (out/'titles.txt').write_text('')
    prefs=out/'nvs/read_pico';prefs.mkdir(parents=True)
    sizes=out/'nvs/if_content';sizes.mkdir(parents=True)
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'ASAN_OPTIONS':'detect_leaks=0'}
    def render(page=0):
        result=subprocess.run([str(ROOT/'build/simulator/render'),'book-1','/sdcard/markdown/Tables.md',str(page),'48','build/simulator/frame.bin','build/simulator/titles.txt','0',','.join(['0']*30),'builtin','',''],cwd=work,env=env,text=True,capture_output=True,timeout=60)
        assert result.returncode==0,result.stderr
        assert 'runtime error:' not in result.stderr and 'AddressSanitizer' not in result.stderr,result.stderr
        info=json.loads(result.stdout);assert info['book_view']==1,info
        return info,(out/'frame.bin').read_bytes()
    def save(frame,name):
        from PIL import Image
        pixels=bytearray(684*1216)
        for i,b in enumerate(frame):pixels[2*i]=(b&15)*17;pixels[2*i+1]=(b>>4)*17
        Image.frombytes('L',(684,1216),bytes(pixels)).save(ARTIFACTS/name)
    for font in ('PicoSans-Medium.ttf','MoonStarsKai-Regular.ttf'):
        if font=='PicoSans-Medium.ttf':
            source=ROOT/'assets/fonts'/font
        else:
            source=ROOT.parent/'TF卡/fonts'/font
            if not source.exists():continue
        shutil.copyfile(source,card/'fonts'/font)
        (prefs/'font').write_bytes(('/sdcard/fonts/'+font+'\0').encode())
        (sizes/'md_sizes').write_bytes(bytes([48,72,64,56]))
        first,frame=render();assert first['reading_font_path'].endswith(font),first
        assert first['pages']>5,first
        assert frame.count(0xdd)>1000 and frame.count(0x88)>30
        if font=='PicoSans-Medium.ttf':save(frame,'grid.png')
        for page in range(first['pages']):
            info,frame=render(page);assert info['reader_page']==page and info['pages']==first['pages'],info
            if font=='PicoSans-Medium.ttf' and page==2:save(frame,'continued.png')
            if font=='PicoSans-Medium.ttf' and page==first['pages']-2:save(frame,'wide.png')
        saved,_=render(2);resumed,_=render(-1);assert resumed['reader_page']==saved['reader_page']
        (sizes/'md_sizes').write_bytes(bytes([64,72,64,56]))
        resized,_=render(-1);assert resized['reader_page']>0 and resized['reader_progress']>0
        print('PASS:',font,'all',first['pages'],'pages, grid pixels, resume and font-size reflow')
    # 图片描述留在单元格，含空单元格和转义竖线。/ Keep image descriptions inline, with empty cells and escaped pipes.
    md.write_text('| A | B | C |\n| --- | --- | --- |\n| | a\\|b | ![说明](attachments/not-downloaded.png) |\n\n结束。')
    (sizes/'md_sizes').write_bytes(bytes([48,72,64,56]));info,_=render();assert info['pages']>=1
print('PASS: native table rendering over private card/NVS; screenshots and sample in',ARTIFACTS)
