# SPDX-License-Identifier: Apache-2.0
"""真实更新页面的确认与返回，模拟器禁止安装。/ Production page confirmation/navigation; the simulator never installs."""
from pathlib import Path
import json,os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=root/'build',prefix='online-ui-') as tmp:
    work=Path(tmp);out=work/'build/simulator';out.mkdir(parents=True);card=work/'card';card.mkdir();(out/'titles.txt').write_text('')
    def render(page,events='',available=False,mode=''):
        env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_SIM_OTA_STATE':mode or ('available' if available else '')}
        p=subprocess.run([str(root/'build/simulator/render'),f'settings-{page}','/sdcard/books/test.txt','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0','','builtin',events,''],cwd=work,env=env,capture_output=True,text=True,timeout=20)
        assert p.returncode==0,p.stderr
        return json.loads(p.stdout)
    stable='t,500,365,500,365'
    beta='t,72,459,72,459'
    confirm='t,500,1034,500,1034'
    cancel='t,160,1034,160,1034'
    entered=render(27,'t,180,1052,180,1052',True)
    assert entered['settings_page']==28 and entered['update_offers']==[2,2]
    assert render(28)['update_offers']==[3,3]
    assert not render(28,beta)['update_confirmation']
    assert render(28,'t,342,1034,342,1034',True)['update_offers']==[2,2]
    for channel,pick in ((0,stable),(1,beta)):
        state=render(28,pick,True)
        assert state['update_channel']==channel and state['update_confirmation']
        assert state['update_offers']==[2,2]
        state=render(28,f'{pick};{cancel}',True)
        assert state['update_state']==2 and not state['update_confirmation']
        state=render(28,f'{pick};t,342,1034,342,1034',True)
        assert state['update_confirmation']
        state=render(28,f'{pick};{confirm}',True)
        assert state['update_channel']==channel and state['update_state']==7 and not state['update_confirmation']
    state=render(28,f'{stable};{beta};{confirm}',True)
    assert state['update_channel']==1 and state['update_state']==7
    mixed=render(28,stable,mode='mixed')
    assert mixed['update_offers']==[7,2] and not mixed['update_confirmation']
    assert render(28,beta,mode='mixed')['update_confirmation']
    assert render(28,'',mode='checking')['update_offers']==[1,1]
    assert not render(28,beta,mode='checking')['update_confirmation']
    waiting=render(28,f'{stable};{confirm}',mode='slow-beta')
    assert waiting['update_offers']==[2,1] and waiting['update_confirmation'] and waiting['update_channel']==0
    assert not render(28,f'{stable};{cancel}',mode='slow-beta')['update_confirmation']
    assert render(28,'t,342,1034,342,1034',mode='checking')['update_offers']==[8,8]
    state=render(28,beta,mode='busy')
    assert state['update_channel']==0 and state['update_state']==4
    assert render(28,'t,60,105,60,105')['settings_page']==27
    assert render(28,'k,1,0,0,0')['settings_page']==27
    for key in (0,2):
        assert render(28,f'k,{key},0,0,0')['settings_page']==28
    for events in ('k,1,0,0,0','t,60,105,60,105'):
        state=render(28,events,mode='busy')
        assert state['settings_page']==28 and state['update_state']==4
    state=render(28,f'{beta};k,1,0,0,0',True)
    assert state['settings_page']==28 and not state['update_confirmation']
print('PASS: both offers, independent failures, exact channel choice, explicit confirmation, cancellation, and simulator no-install guard')
