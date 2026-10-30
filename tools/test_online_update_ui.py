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
    assert render(27,'t,180,1052,180,1052')['settings_page']==28
    assert render(28)['update_state']==3
    assert render(28)['update_channel']==0
    assert render(28,'t,500,460,500,460')['update_channel']==1
    assert render(28,'t,500,460,500,460;t,500,460,500,460')['update_channel']==0
    state=render(28,'t,342,1034,342,1034;t,500,460,500,460',True)
    assert state['update_channel']==1 and not state['update_confirmation']
    state=render(28,'t,500,460,500,460;t,342,1034,342,1034',True)
    assert state['update_channel']==1 and state['update_confirmation']
    state=render(28,'t,500,460,500,460',mode='busy')
    assert state['update_channel']==0 and state['update_state']==4
    assert render(28,'t,342,1034,342,1034',True)['update_confirmation']
    assert render(28,'t,342,1034,342,1034;t,160,1034,160,1034',True)['update_state']==2
    state=render(28,'t,342,1034,342,1034;t,500,1034,500,1034',True)
    assert state['update_state']==7 and not state['update_confirmation']
    assert render(28,'t,60,105,60,105')['settings_page']==27
    assert render(28,'k,0,0,0,0')['settings_page']==27
print('PASS: online entry, explicit two-step installation/cancel, return routing and simulator no-install guard')
