# SPDX-License-Identifier: Apache-2.0
"""主页/内容页走真实Python路由及C阅读器续读，仅使用私有TF/NVS。 / Home/content resume through production Python routing and C reader, over private storage."""
from pathlib import Path
import json, os, shutil, sys, tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/simulator'))
import server
class OfflineCloud:
    def snapshot(self):return {'busy':False,'revision':0}
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='home-resume-') as temp:
    work=Path(temp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
    for folder in ('books','markdown/attachments','fonts'):(card/folder).mkdir(parents=True)
    (card/'markdown/Test.md').write_text('# 测试文档\n\n'+('这是用于验证续读位置的正文。'*1200))
    (card/'books/Test.txt').write_text('阅读正文。 '*3000)
    shutil.copyfile(ROOT/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf')
    (out/'render').symlink_to(ROOT/'build/simulator/render')
    # 不调用准备样例或真实云账号构造；快照与回放只包含临时目录。
    # Skip fixture and credential constructors; replay snapshots contain only the temporary folders.
    server.ROOT=work;server.BUILD=out;server.BOOKS=work/'prototype';server.BOOKS.mkdir()
    server.CARD=card;server.FIXTURES=work/'fixtures';server.FIXTURES.mkdir()
    server.STORES={'nvs':out/'nvs','vfs':out/'vfs','card':card}
    server.CLOUD_PRIVATE=work/'cloud';server.CLOUD_PRIVATE.mkdir()
    os.environ['PICO_SIM_CARD_ROOT']=str(card)
    p=server.Preview.__new__(server.Preview)
    p.state={'screen':'documents','page':0,'pages':1,'px':48,'scroll':0,'events':[],'root_view':None,
             'firmware_path':'/sdcard/markdown/Test.md','files_dir':'','selected':[],'active':'','version':0,'days':[0]*30,'message':'','cloud_page':0}
    p.state_path=out/'state.json';p.signature='test';p.sources_signature=lambda:'test'
    p.cloud=OfflineCloud();p.cloud_revision=0;p.cloud_pending_view=None;p.failed_signature=''
    p.replay_origin=None;p.storage_signature=None
    def tap(x,y):return p.action({'action':'gesture','type':'t','x':x,'y':y,'xx':x,'yy':y})
    def dismiss_guide():
        if p.state['screen']=='reader-guide':p.action({'action':'key','key':1})
    p.render();tap(400,190);dismiss_guide()
    p.action({'action':'next'});p.action({'action':'next'})
    expected=p.state['page'];progress=p.state['reader_progress'];assert expected==2 and progress>0
    p.action({'action':'home'});tap(400,830)
    assert p.state['screen']=='reader' and p.state['firmware_path']=='/sdcard/markdown/Test.md'
    assert p.state['page']==expected and p.state['reader_progress']==progress,(p.state['page'],expected)
    # 连续两次从首页/内容进入，不能把保存记录重置为开头。
    # Repeated home/content entry must not overwrite the saved position with the start.
    p.action({'action':'home'});tap(400,830);assert p.state['page']==expected
    p.action({'action':'view','view':'documents'});tap(400,190);dismiss_guide();assert p.state['page']==expected
    # 图书也使用相同的跨页面续读路由。
    # Books share the same cross-app resume route.
    p.state.update(screen='reader',firmware_path='/sdcard/books/Test.txt',page=0,events=[],root_view=None)
    p.render();dismiss_guide();p.action({'action':'next'});p.action({'action':'next'})
    expected=p.state['page'];progress=p.state['reader_progress'];assert expected>0 and progress>0
    p.action({'action':'home'});tap(100,900)
    assert p.state['firmware_path']=='/sdcard/books/Test.txt' and p.state['page']==expected and p.state['reader_progress']==progress
    # 新文档没有记录时正常从第一页开始。
    # New documents without a record still start at page zero.
    (card/'markdown/New.md').write_text('# 新文章\n\n'+('新文章正文。'*300))
    p.state.update(screen='reader',firmware_path='/sdcard/markdown/New.md',page=-1,events=[],root_view=None)
    p.render();assert p.state['page']==0
    print('PASS: real home routing resumes MD and TXT at the saved page/progress; repeated home/content entry preserves progress; unread files start at zero; private TF/NVS only.')
