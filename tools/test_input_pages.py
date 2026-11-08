# SPDX-License-Identifier: Apache-2.0
"""Native entry/commit/cancel and keyboard pixels using an isolated card/NVS only."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
def tap(x,y): return f't,{x},{y},{x},{y}'
def events(*steps): return ';'.join(steps)
clear=tap(573,400);banner_clear=tap(464,462);language=tap(89,1152);done=tap(595,1152);cancel=tap(60,110)
# Explicit row/column calculation avoids ambiguous precedence.
def letters(text):
    keys="1234567890qwertyuiopasdfghjkl'zxcvbnm,./"
    return events(*(tap(67+61*(keys.index(c)%10),803+84*(keys.index(c)//10)) for c in text))
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='input-pages-') as tmp:
    work=Path(tmp);card=work/'card';build=work/'build/simulator';build.mkdir(parents=True)
    for folder in ('books','markdown/attachments','fonts','system/ime'):(card/folder).mkdir(parents=True)
    (card/'books/Test.txt').write_text('Chapter\n\nText body.\n')
    (card/'markdown/Test.md').write_text('# 标题\n\n正文\n')
    shutil.copyfile(ROOT/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
    shutil.copyfile(ROOT/'assets/ime/pinyin.ifdict',card/'system/ime/pinyin.ifdict')
    (build/'titles.txt').write_text('');env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def render(native,steps='',aux=0,path='/sdcard/books/Test.txt'):
        args=[str(ROOT/'build/simulator/render'),native,path,str(aux),'48','build/simulator/frame.bin','build/simulator/titles.txt','0',','.join(['60']*30),'builtin',steps,'']
        result=subprocess.run(args,cwd=work,env=env,text=True,capture_output=True,timeout=30)
        assert result.returncode==0,(native,result.stderr)
        return json.loads(result.stdout),(build/'frame.bin').read_bytes()
    # Every page uses byte-identical key rows; captions/candidates and commit labels may vary.
    layout=render('settings-0')[0]['settings_geometry'];signature_y=layout[1]+layout[4]+layout[4]//2
    cloud=render('settings-20')[0]['settings_geometry'];folder_y,account_y,password_y=[y+cloud[4]//2 for y in cloud[11:14]]
    entries=[('settings-19','',0),('settings-23','',0),('settings-0',tap(340,signature_y),0),('settings-20',tap(340,folder_y),0),('settings-20',tap(340,account_y),0),('settings-20',tap(340,password_y),0),('files-2','',0),('book-7','',0),('book-6','',0),('transfer-3','',0)]
    crop=None
    for native,steps,aux in entries:
        info,frame=render(native,steps,aux)
        keys=frame[766*342:1092*342]
        if crop is None:crop=keys
        assert keys==crop,('keyboard rows moved or changed',native)
        # Old app navigation is replaced by five keyboard controls, extending to y1200.
        assert frame[1152*342:1200*342]!=bytes([255])*(48*342)
    # Cancel edits leave device settings untouched; completed edits survive a new process.
    before={p.relative_to(build).as_posix():p.read_bytes() for p in (build/'nvs').rglob('*') if p.is_file()}
    info,_=render('settings-19',events(clear,language,letters('abc'),cancel));assert info['settings_page']==0
    after={p.relative_to(build).as_posix():p.read_bytes() for p in (build/'nvs').rglob('*') if p.is_file()}
    assert before==after
    info,_=render('settings-19',events(clear,language,letters('abc'),done));assert info['settings_page']==0
    values=[p for p in (build/'nvs').rglob('status_sig') if not p.parent.name.startswith('.')];assert len(values)==1 and values[0].read_bytes()==b'abc\0',[(p.relative_to(build).as_posix(),p.read_bytes()) for p in values]
    render('settings-19');assert values[0].read_bytes()==b'abc\0'
    # Exact full-pinyin phrase selection is available through the native signature page.
    info,_=render('settings-0',events(tap(340,signature_y),clear,letters('yuedu'),tap(136,718),done));assert info['settings_page']==0
    values=[p for p in (build/'nvs').rglob('status_sig') if not p.parent.name.startswith('.')];assert len(values)==1 and values[0].read_bytes()=='阅读\0'.encode()
    # Rename preserves the locked extension; invalid names remain editable and retry succeeds.
    info,_=render('files-2',events(clear,language,letters('abc'),tap(188,1152),tap(250,1055),done));assert info['files_view']==2 and (card/'books/Test.txt').exists()
    info,_=render('files-2',events(clear,language,letters('abc'),done));assert info['files_view']==1
    assert (card/'books/abc.txt').exists() and not (card/'books/Test.txt').exists()
    info,_=render('files-2',events(clear,language,letters('cancelled'),cancel));assert info['files_view']==1
    assert (card/'books/abc.txt').exists() and not (card/'books/cancelled.txt').exists()
    # 内容搜索通过统一键盘；用同一会话覆盖清除、取消、打开、返回及切换标签。
    # Exercise clearing, canceling, opening, returning and tab changes through the shared keyboard.
    (card/'books/Other.txt').write_text('Chapter\n\nAnother book.\n')
    (card/'markdown/Other.md').write_text('# Another note\n\nText.\n')
    search=tap(539,92);clear_search=tap(476,92)
    docs=tap(260,92);books=tap(110,92)
    for native,field,query,selected,tab_roundtrip in (
        ('book-0','book_items','abc','abc.txt',events(docs,books)),
        ('book-8','document_items','test','Test.md',events(books,docs)),
    ):
        full,_=render(native)
        full_items=[p for p in full[field] if p]
        assert len(full_items)==2
        matched=events(search,letters(query),done)
        info,matched_frame=render(native,matched)
        items=[p for p in info[field] if p]
        assert len(items)==1 and Path(items[0]).name==selected
        restored,_=render(native,events(matched,clear_search))
        assert [p for p in restored[field] if p]==full_items
        assert render(native,events(matched,search,clear,letters('zzzz'),cancel))[1]==matched_frame
        assert render(native,events(matched,search,'k,1,0,0,0'))[1]==matched_frame
        assert render(native,events(matched,search,clear,done))[0][field]==full[field]
        assert render(native,events(matched,tab_roundtrip))[0][field]==full[field]
        empty=events(search,letters('zzzz'),done)
        assert not any(render(native,empty)[0][field])
        assert render(native,events(empty,clear_search))[0][field]==full[field]
    matched=events(search,letters('test'),done)
    opened,_=render('book-8',events(matched,tap(340,180)))
    assert opened['book_view']==1 and opened['book_path']=='/sdcard/markdown/Test.md',opened['book_path']
    returned,_=render('book-8',events(matched,tap(340,180),tap(600,100),tap(60,110)))
    assert returned['book_view']==8 and [p for p in returned['document_items'] if p]==[opened['book_path']], (returned['book_view'],returned['reader_panel'],returned['document_items'])
    info,_=render('transfer-3',events(letters('short'),done));assert info['transfer_view']==3
    info,_=render('transfer-3',events(letters('dummy'),cancel));assert info['transfer_view']==2
    info,_=render('settings-20',events(tap(340,password_y),letters('dummy'),cancel));assert info['settings_page']==20
    # Banner编辑器只改草稿；最终保存统一写入快照。/ Banner input changes only its draft; final Save persists one snapshot.
    assert render('settings-21',tap(340,800))[0]['settings_page']==23
    assert render('settings-21',tap(340,864))[0]['settings_page']==22
    config=build/'nvs/if_content/banner_v1'
    info,_=render('settings-22',tap(340,550));assert info['settings_page']==21 and info['banner_font']=='/sdcard/fonts/PicoSans-Medium.ttf' and not config.exists()
    assert render('settings-22',tap(340,460))[0]['banner_font']==''
    info,_=render('settings-23',events(banner_clear,language,letters('abc'),cancel));assert info['settings_page']==21 and not config.exists()
    info,_=render('settings-23',events(banner_clear,language,letters('abc'),done));assert info['settings_page']==21 and info['banner_text']=='abc' and not config.exists()
    info,_=render('settings-23',events(banner_clear,language,letters('abc'),done,tap(500,1098)));assert info['quote_settings_committed'] and config.exists();saved=config.read_bytes()
    info,_=render('settings-23',events(banner_clear,done));assert info['settings_page']==21 and info['banner_text']=='' and config.read_bytes()==saved
    info,_=render('settings-23',events(banner_clear,language,letters('w'*50),done));assert info['route']==-1 and info['settings_page']==23 and config.read_bytes()==saved
    info,_=render('settings-23',events(banner_clear,language,letters('ok'),done));assert info['settings_page']==21 and info['banner_text']=='ok' and config.read_bytes()==saved
    # 四行输入保留显式换行，界面返回草稿后仍需最终保存。/ Four-line input retains explicit breaks and remains a draft until final Save.
    smaller=bytearray(config.read_bytes());smaller[8]=48;smaller[652:654]=(2).to_bytes(2,"little");config.write_bytes(smaller)
    newline=tap(588,462)
    steps=events(banner_clear,language,letters('one'),newline,letters('two'),newline,letters('three'),newline,letters('four'),done)
    info,_=render('settings-23',steps);assert info['settings_page']==21 and info['banner_text']=='one\ntwo\nthree\nfour'
    info,_=render('settings-23',events(banner_clear,language,letters('a'),newline,newline,newline,newline,done));assert info['banner_text']=='a\n\n\n'
    print('PASS: 10 native input entries have identical bottom key rows; UTF-8 phrases, settings persistence, cancellation, rename validation, search and WiFi validation verified on a private card.')
