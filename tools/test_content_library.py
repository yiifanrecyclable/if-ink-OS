# SPDX-License-Identifier: Apache-2.0
"""真实内容页手势与批量操作，仅使用临时TF卡。 / Native gestures and batch actions over a temporary card."""
from pathlib import Path
import json,os,shutil,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
def tap(x,y):return f't,{x},{y},{x},{y}'
def events(*parts):return ';'.join(parts)
def letters(text):
    keys="1234567890qwertyuiopasdfghjkl'zxcvbnm,./"
    return events(*(tap(67+61*(keys.index(c)%10),803+84*(keys.index(c)//10)) for c in text))
def tone(frame,x,y):
    return (frame[y*342+x//2]>>(4 if x%2 else 0))&15
def check_sheet(before,after,geometry):
    top,nav=geometry[:2]
    ax,ay,aw,ah=geometry[2:6]
    assert (ax,aw)==(0,171)
    assert before[:56*342]==after[:56*342] and before[nav*342:]==after[nav*342:]
    gray=lambda v:9+(6*v+7)//15
    lut=bytes(gray(i&15)+(gray(i>>4)<<4) for i in range(256))
    faded=before[56*342:(top-7)*342].translate(lut)
    assert after[56*342:(top-7)*342]==faded and faded!=before[56*342:(top-7)*342]
    for i in range(7):
        shade=(255-(i+1)*102//7)>>4
        assert after[(top-7+i)*342:(top-6+i)*342]==bytes([shade*17])*342
    for x in (171,342,513):
        assert tone(after,x,ay+24)<15 and tone(after,x-1,ay+24)==15
        assert tone(after,x,ay+22)==15 and tone(after,x,ay+ah-22)==15
def path_hash(path):
    h=2166136261
    for c in path.encode():h=((h^c)*16777619)&0xffffffff
    return h
def fail_progress_write_once(path):
    return {'PICO_TEST_FOPEN_WRITE_FAIL_ONCE':f'/sdcard/.readpico/progress/b_{path_hash(path):08x}.a'}
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='content-library-test-') as tmp:
    work=Path(tmp);card=work/'card';build=work/'build/simulator';build.mkdir(parents=True)
    for name in ('books','markdown/attachments','fonts'):(card/name).mkdir(parents=True,exist_ok=True)
    (build/'titles.txt').write_text('')
    image=ROOT/'plugins/obsidian-pico/tests/fixtures/sample-image.png'
    for name in ('shared.png','B-only.png'):shutil.copyfile(image,card/'markdown/attachments'/name)
    for name,text in {'A.md':'# A\n\n![shared](attachments/shared.png)\n',
        'B.md':'# B\n\n![shared](attachments/shared.png)\n\n![own](attachments/B-only.png)\n','C.md':'# C\n\nText\n'}.items():
        (card/'markdown'/name).write_text(text)
    for name in ('Book2.txt','Book10.txt','Book20.txt'):(card/'books'/name).write_text('Test chapter\n\n'+'Reading text.\n'*30)
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(view=8,sequence='',faults=None):
        p=subprocess.run([str(ROOT/'build/simulator/render'),f'book-{view}','/sdcard/markdown/A.md','0','48',
            str(build/'frame.bin'),str(build/'titles.txt'),'0','0,0,0,0,0,0,0','builtin',sequence,''],
            cwd=work,env={**env,**(faults or {})},capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout)
    def command(*args):
        p=subprocess.run([str(ROOT/'build/simulator/cloud-host'),*args],cwd=work,env=env,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr;result=json.loads(p.stdout);assert not result.get('error');return result
    def names(result,field):return [Path(p).name for p in result[field] if p]
    sort_open=tap(600,92);dismiss_sort=tap(630,902);name=tap(85,989);progress=tap(256,989)
    dates=tap(427,989);recent=tap(598,989);direction=tap(342,1090)
    assert render(0,sort_open)['content_sort_open']
    assert render(0)['book_view']==0
    # 底部卡片共用布局，保留背景、阴影和底栏。/ Both sort cards preserve and fade their own content.
    for view,selected in ((0,85),(8,598)):
        render(view);background=(build/'frame.bin').read_bytes()
        render(view,sort_open);card_frame=(build/'frame.bin').read_bytes()
        top,nav=852,1136
        assert card_frame[nav*342:]==background[nav*342:]
        gray=lambda v:9+(6*v+7)//15
        lut=bytes(gray(i&15)+(gray(i>>4)<<4) for i in range(256))
        assert card_frame[56*342:(top-7)*342]==background[56*342:(top-7)*342].translate(lut)
        assert card_frame[(top-1)*342:top*342]!=background[(top-1)*342:top*342]
        assert tone(card_frame,selected,1044)==0
        assert all(tone(card_frame,x,980)<15 and tone(card_frame,x,951)==15 for x in (171,342,513))
        assert all(tone(background,x,66)==15 for x in (538,613))
        for outside in (tap(25,300),tap(85,1180),dismiss_sort):
            closed=render(view,events(sort_open,outside))
            assert not closed['content_sort_open'] and closed['route']==-1
    b=render(0,events(sort_open,name,dismiss_sort));assert names(b,'book_items')==['Book2.txt','Book10.txt','Book20.txt']
    rejected=render(0,events(sort_open,progress),{'PICO_TEST_NVS_SET_FAIL':'books'})
    assert rejected['content_sort_open'] and rejected['book_sort_mode']==0
    # 旧收藏与仅看收藏位不能改标记、排序或隐藏任何图书；原记录保留。
    # Legacy favorites and filter bits never decorate/reorder/hide books; preserve their records.
    render(0);plain_book_frame=(build/'frame.bin').read_bytes()
    favorite_dir=build/'nvs/rp_favs';favorite_dir.mkdir(exist_ok=True)
    favorite_file=favorite_dir/f"f_{path_hash('/sdcard/books/Book20.txt'):08x}"
    favorite_file.write_bytes(b'\1')
    (build/'nvs/if_content/books').write_bytes(bytes([28]))
    old=render(0);assert names(old,'book_items')==['Book2.txt','Book10.txt','Book20.txt']
    assert (build/'frame.bin').read_bytes()==plain_book_frame
    render(0,events(sort_open,progress,name,dismiss_sort))
    assert (build/'nvs/if_content/books').read_bytes()==bytes([4])
    assert favorite_file.read_bytes()==b'\1'
    # 与文件操作卡一致的边界、背景淡显、阴影和四等分短竖线。
    # Match the Files card's bounds, fade, shadow and quarter dividers.
    render(0);shelf_frame=(build/'frame.bin').read_bytes()
    details='l,550,350,550,350'
    popup=render(0,details);assert popup['book_view']==3
    geometry=popup['content_card_geometry']
    check_sheet(shelf_frame,(build/'frame.bin').read_bytes(),geometry)
    ax,ay,aw,ah=geometry[2:6]
    cx,cy,cw,ch=geometry[6:10]
    close=tap(cx+cw//2,cy+ch//2)
    above=tap(20,300)
    action_y=ay+ah//2
    rename,pin,delete_item,clear_item=(tap((2*i+1)*aw//2,action_y) for i in range(4))
    cancel_action=tap(180,action_y)
    confirm_action=tap(500,action_y)
    assert render(0,events(details,above))['book_view']==0
    assert render(0,events(details,f't,630,{cy+ch//2},558,{cy+ch//2}'))['book_view']==3
    assert render(0,events(details,delete_item,above))['book_view']==0
    across=f't,500,{action_y},220,{action_y}'
    assert render(0,events(details,delete_item,across))['book_view']==3
    assert (card/'books/Book20.txt').exists()
    assert render(0,events(details,close))['book_view']==0
    assert render(0,events(details,tap(430,1176)))['route']!=2
    pinned=render(0,events(details,pin,close));assert names(pinned,'book_items')[0]=='Book20.txt'
    assert names(render(0),'book_items')[0]=='Book20.txt'
    unpinned=render(0,events('l,130,350,130,350',pin,close))
    assert names(unpinned,'book_items')==['Book2.txt','Book10.txt','Book20.txt']
    assert render(8,events(sort_open,name,dismiss_sort))['document_sort_mode']==0
    command('progress-save','/sdcard/books/Book10.txt');command('progress-save','/sdcard/markdown/B.md')
    b=render(0,events(sort_open,progress));assert b['book_sort_mode']==1 and b['content_sort_open']
    assert names(b,'book_items')==['Book2.txt','Book20.txt','Book10.txt'], (b['book_sort_mode'],names(b,'book_items'),command('progress-load','/sdcard/books/Book10.txt'))
    # 九宫格标题与无数字横条直接验证原生像素；不使用真实TF卡。
    # Check native grid captions and number-free bars using only the private test card.
    b=render(0)
    grid=(build/'frame.bin').read_bytes()
    geometries=b['shelf_geometry']
    for index,progress_value in ((0,0),(2,33)):
        cell=geometries[index];x,y,w,h=cell[10:14]
        shades=[tone(grid,xx,y+1) for xx in range(x,x+w)]
        filled=(w+2)//3 if progress_value else 0
        assert shades==[0]*filled+[12]*(w-filled),(index,shades)
    cell=geometries[0];x,y,w,h=cell[10:14]
    assert any(tone(grid,xx,yy)==0 for yy in range(y+h+8,min(y+h+72,1048)) for xx in range(cell[0],cell[0]+cell[2]))
    assert render(8)['document_sort_mode']==0
    d=render(8,events(sort_open,progress,direction,dismiss_sort));assert names(d,'document_items')[0]=='B.md'
    d=render(8,events(sort_open,progress));assert names(d,'document_items')[0]=='B.md' and d['content_sort_open']
    assert render(0)['book_sort_mode']==1
    added_records=list((card/'.readpico/records/if_content').glob('a_*.[ab]'))
    assert len(added_records)==3
    before={p.name:p.read_bytes() for p in added_records}
    os.utime(card/'books/Book2.txt',(1,1));render(0)
    assert before=={p.name:p.read_bytes() for p in added_records}
    stamps={'Book2.txt':1704067400,'Book10.txt':1704067500,'Book20.txt':1704067600}
    for p in added_records:
        raw=bytearray(p.read_bytes())
        assert raw[:4]==b'IFR1' and raw[40:44]==b'IFA1'
        filename=Path(raw[48:].split(b'\0')[0].decode()).name
        struct.pack_into('<I',raw,44,stamps[filename])
        struct.pack_into('<I',raw,16,0)
        struct.pack_into('<I',raw,16,zlib.crc32(raw))
        p.write_bytes(raw)
    b=render(0,events(sort_open,dates,dismiss_sort));assert b['book_sort_mode']==2 and names(b,'book_items')==['Book20.txt','Book10.txt','Book2.txt']
    for filename,date in [('A.md','2026-01-01'),('B.md','2026-02-01'),('C.md','2026-03-01')]:
        meta=card/'markdown/.index'/f'{filename}.pico';lines=meta.read_text().splitlines();lines[1]=date;meta.write_text('\n'.join(lines)+'\n')
    assert names(render(8,events(sort_open,dates,dismiss_sort)),'document_items')[0]=='C.md'
    command('progress-save','/sdcard/books/Book2.txt')
    b=render(0,events(sort_open,recent,dismiss_sort));assert names(b,'book_items')[0]=='Book2.txt'
    render(8,events(sort_open,name,dismiss_sort))
    assert render(0,tap(539,92))['book_view']==6
    assert render(8,tap(539,92))['book_view']==6
    assert render(0,'t,539,92,614,92')['book_view']==0
    assert render(8,'t,539,92,614,92')['book_view']==8
    # 旧星标数据既不显示，也不影响排序或隐藏正文。/ Legacy stars neither render nor filter documents.
    render(8);plain_docs=(build/'frame.bin').read_bytes()
    old_stars=card/'markdown/.index/B.md.prefs';old_stars.write_text('pico-md-prefs-1\n2\n')
    (build/'nvs/if_content/docs').write_bytes(bytes([28]))
    migrated=render(8);assert migrated['document_count']==3 and not migrated['document_starred_only']
    assert (build/'frame.bin').read_bytes()[60*342:]==plain_docs[60*342:]
    assert old_stars.read_text()=='pico-md-prefs-1\n2\n'
    render(8);doc_frame=(build/'frame.bin').read_bytes()
    doc_details='l,340,350,340,350'
    doc_popup=render(8,doc_details);assert doc_popup['document_menu']
    check_sheet(doc_frame,(build/'frame.bin').read_bytes(),doc_popup['content_card_geometry'])
    assert not render(8,events(doc_details,above))['document_menu']
    assert not render(8,events(doc_details,close))['document_menu']
    assert not render(8,events(doc_details,delete_item,above))['document_menu']
    doc_pin=pin
    d=render(8,events(doc_details,doc_pin,close));assert names(d,'document_items')==['C.md','A.md','B.md']
    assert not d['document_menu']
    assert render(8,events(doc_details,tap(430,1176)))['route']!=2
    # 重排后继续操作仍绑定同篇文档；清进度有确认，保留正文和图片。
    # Sorting does not change the modal target; clearing progress needs confirmation and preserves content.
    render(8,events('l,340,190,340,190',doc_pin,doc_pin,close))
    assert (card/'markdown/.index/C.md.prefs').read_text()=='pico-md-prefs-1\n1\n'
    single_clear=events('l,340,350,340,350',clear_item)
    render(8,events(single_clear,cancel_action,close));assert command('progress-load','/sdcard/markdown/B.md')['present']
    render(8,events(single_clear,confirm_action,above));assert not command('progress-load','/sdcard/markdown/B.md')['present']
    assert (card/'markdown/B.md').exists() and (card/'markdown/attachments/B-only.png').exists()
    command('progress-save','/sdcard/markdown/B.md')
    # 管理页同样提供来源、排序、独立搜索；筛选/重扫清除勾选。/ Shared source/sort/search controls clear selections on filtering or rescan.
    assert render(9,tap(342,251))['content_sort_open']
    assert render(9,events(tap(340,350),tap(130,251)))['document_selected_count']==0
    assert render(9,events(tap(340,350),tap(550,981)))['document_selected_count']==0
    # 旧批量星标/置顶区域现在不会修改标记；空的第六操作格不触发批量操作。
    # Old batch star/pin locations and the empty sixth action cell cannot modify document flags.
    prefs={p.name:p.read_bytes() for p in (card/'markdown/.index').glob('*.prefs')}
    d=render(9,events(tap(340,350),tap(180,898),tap(550,1051)))
    assert d['document_selected_count']==1 and not d['document_batch_confirm']
    assert prefs=={p.name:p.read_bytes() for p in (card/'markdown/.index').glob('*.prefs')}
    search=tap(550,251);apply=tap(590,1152);letter_b=tap(311,1055)
    assert render(9,search)['book_view']==6
    found=render(9,events(search,letter_b,apply));assert found['book_view']==9 and names(found,'document_items')==['B.md']
    assert found['document_selected_count']==0
    b=render(9,events(search,letter_b,apply,tap(342,1144),tap(110,121)))
    assert b['book_view']==0 and len(names(b,'book_items'))==3
    assert render(9,tap(342,1144))['book_view']==8
    assert render(4,tap(342,1144))['book_view']==0
    d=render(8,events(sort_open,tap(342,649),tap(342,735)));assert names(d,'document_items')==['C.md','A.md','B.md']
    assert d['document_count']==3 and not d['document_starred_only'] and not d['content_sort_open']
    clear=events(tap(340,542),tap(342,1051))
    assert render(9,clear)['document_batch_confirm']
    assert command('progress-load','/sdcard/markdown/B.md')['present']
    assert not render(9,events(clear,tap(190,658)))['document_batch_confirm']
    assert command('progress-load','/sdcard/markdown/B.md')['present']
    render(9,events(clear,tap(490,658)))
    assert not command('progress-load','/sdcard/markdown/B.md')['present']
    assert command('progress-load','/sdcard/books/Book10.txt')['present']
    # 删除失败后保留勾选及缺失正文条目，随后同次开机可重试清理。/ Preserve failed cleanup selections and retry during the same boot.
    assert (card/'markdown/attachments/B-only.png').exists(), 'owned image disappeared before delete'
    checkpoint=work/'checkpoint';shutil.copytree(card,checkpoint/'card');shutil.copytree(build/'nvs',checkpoint/'nvs')
    delete=events(tap(340,542),tap(130,1051),tap(490,658))
    d=render(9,delete,fail_progress_write_once('/sdcard/markdown/B.md'))
    assert d['document_selected_count']==1 and d['document_count']==3
    assert not (card/'markdown/B.md').exists()
    shutil.rmtree(card);shutil.copytree(checkpoint/'card',card)
    shutil.rmtree(build/'nvs');shutil.copytree(checkpoint/'nvs',build/'nvs')
    d=render(9,events(delete,tap(130,1051),tap(490,658)),fail_progress_write_once('/sdcard/markdown/B.md'))
    assert d['document_count']==2 and not (card/'markdown/B.md').exists()
    assert not (card/'markdown/attachments/B-only.png').exists() and (card/'markdown/attachments/shared.png').exists()
    render(9,events(tap(340,446),tap(130,1051),tap(490,658)))
    assert not (card/'markdown/A.md').exists() and not (card/'markdown/attachments/shared.png').exists()
    assert (card/'markdown/C.md').exists() and all((card/'books'/n).exists() for n in ('Book2.txt','Book10.txt','Book20.txt'))
    for i in range(7):(card/'markdown'/f'D{i}.md').write_text(f'# D{i}\n\nText\n')
    d=render(9,events(tap(340,350),'u,340,900,340,350',tap(340,350),tap(342,251),name,direction,dismiss_sort))
    assert d['document_selected_count']==2 and d['book_view']==9
    assert render(8)['document_sort_mode']==0 and render(0)['book_sort_mode']==3
    assert render(9,tap(550,1144))['reader_page']==1
    assert render(9,events(tap(550,1144),tap(130,1144)))['reader_page']==0
    for i in range(7):(card/'books'/f'Extra{i}.txt').write_text('Extra book\n')
    assert render(0)['pages']==2
    assert render(4,tap(550,1144))['reader_page']==1
    assert render(4,events(tap(550,1144),tap(130,1144)))['reader_page']==0
    paged='t,620,1090,620,1090;l,130,350,130,350'
    assert render(0,paged)['reader_page']==1
    closed=render(0,events(paged,close));assert closed['book_view']==0 and closed['reader_page']==1
    # 单篇删除取消、失败和重试仍保护其他正文及插图。/ Single-item delete cancellation and retry preserve unrelated content.
    shutil.copyfile(image,card/'markdown/attachments/single-only.png')
    single_path=card/'markdown/DeleteTest.md';single_path.write_text('# Delete test\n\n![own](attachments/single-only.png)\n')
    render(8,events(sort_open,dates,name,dismiss_sort))
    single_open='l,340,866,340,866';single_delete=events(single_open,delete_item,confirm_action)
    cancelled=render(8,events(sort_open,name,dismiss_sort,single_open,delete_item,cancel_action,close))
    assert single_path.exists() and not cancelled['document_menu']
    single_checkpoint=work/'single-checkpoint';shutil.copytree(card,single_checkpoint/'card');shutil.copytree(build/'nvs',single_checkpoint/'nvs')
    failed=render(8,single_delete,fail_progress_write_once('/sdcard/markdown/DeleteTest.md'))
    assert not single_path.exists() and failed['document_menu'] and failed['document_count']==9
    shutil.rmtree(card);shutil.copytree(single_checkpoint/'card',card)
    shutil.rmtree(build/'nvs');shutil.copytree(single_checkpoint/'nvs',build/'nvs')
    retried=render(8,events(single_delete,confirm_action),fail_progress_write_once('/sdcard/markdown/DeleteTest.md'))
    assert retried['document_count']==8 and not retried['document_menu']
    assert not single_path.exists() and not (card/'markdown/attachments/single-only.png').exists() and (card/'markdown/C.md').exists()
    # 真实详情卡进入改名编辑器，写入显示名并保留原文件、进度和附件。
    # Enter rename through each production card; persist the display name without replacing the source.
    for view,opened,field in ((0,'l,130,350,130,350','book_items'),
                              (8,'l,340,190,340,190','document_items')):
        original_path=render(view)[field][0]
        original=card/original_path.removeprefix('/sdcard/')
        body=original.read_bytes()
        progress_before=command('progress-load',original_path)
        assert render(view,events(opened,rename))['book_view']==7
        cleared=events(opened,rename,tap(573,400),tap(89,1152),letters('newname'))
        cancelled=render(view,events(cleared,tap(60,110)))
        assert cancelled['book_view']==(3 if view==0 else 8)
        assert not any(render(view,events(tap(539,92),letters('newname'),apply))[field])
        assert render(view,events(cleared,apply))['book_view']==view
        result=render(view,events(tap(539,92),letters('newname'),apply))
        assert [p for p in result[field] if p]==[original_path]
        assert original.read_bytes()==body and command('progress-load',original_path)==progress_before
    # 按字体档位和语言对比两种详情卡与文件操作卡的尺寸与触区。
    # Compare both details cards with the Files card across text sizes and languages.
    ns=build/'nvs/read_pico';ns.mkdir(exist_ok=True)
    for size in (100,120,140,160):
        for english in (0,1):
            (ns/'sys_size').write_bytes(bytes([size]))
            (ns/'language').write_bytes(bytes([english]))
            p=subprocess.run([str(ROOT/'build/simulator/render'),'files-1','','0','48',
                str(build/'frame.bin'),str(build/'titles.txt'),'0','0','builtin','','/sdcard/books'],
                cwd=work,env=env,capture_output=True,text=True,timeout=30,check=True)
            file_card=json.loads(p.stdout)['files_card_geometry']
            expected=file_card[:2]+file_card[7:15]
            for view,opened in ((0,'l,130,350,130,350'),(8,'l,340,190,340,190')):
                render(view);background=(build/'frame.bin').read_bytes()
                state=render(view,opened);geometry=state['content_card_geometry']
                assert geometry==expected,(size,english,view,geometry,expected)
                check_sheet(background,(build/'frame.bin').read_bytes(),geometry)
                cx,cy,cw,ch=geometry[6:10]
                state=render(view,events(opened,tap(cx+cw//2,cy+ch//2)))
                assert state['book_view']==view and not state['document_menu']
print('PASS content sorting, search, persistent pins, shared action card in four sizes and two languages, display-name edits, safe dismissal, progress clearing and recoverable deletion')
