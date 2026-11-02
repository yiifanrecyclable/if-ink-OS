#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Differentiate RAM-only changes, durable NVS values and restart boundaries without hardware assumptions.
中文：区分仅RAM变化、NVS已保存值和重新启动边界，不以浅睡保留冒充持久化证明。
"""
from pathlib import Path
import subprocess,tempfile,os,json,csv,shutil
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='settings-trace-',dir=R/'build') as tmp:
 work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
 for n in ('fonts','books','markdown','pictures'):(card/n).mkdir(parents=True)
 shutil.copy2(R/'assets/fonts/PicoSans-Medium.ttf',card/'fonts/PicoSans-Medium.ttf');(out/'titles.txt').touch()
 def frame(events='',fail=None,phase=None,page=5):
  env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
  if fail:env['PICO_TEST_NVS_SET_FAIL']=fail
  if phase:env['PICO_SIM_SETTINGS_TRACE_PHASE']=phase
  p=subprocess.run([str(R/'build/simulator/render'),f'settings-{page}','','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events],cwd=work,env=env,capture_output=True,text=True,timeout=30)
  assert p.returncode==0,p.stderr[-1600:]
  return json.loads(p.stdout)
 def records():
  with (card/'diagnostics/ifink-settings.csv').open() as f:return list(csv.DictReader(f))
 info=frame();assert not (card/'diagnostics').exists() # No diagnostic files on an ordinary unarmed boot.
 top,step=info['settings_geometry'][6:8];font=f't,180,{top+step+step//2},180,{top+step+step//2}'
 saved=frame(font+';t,420,276,420,276',phase='pre_restart')
 assert saved['system_font_path']=='/sdcard/fonts/PicoSans-Medium.ttf' and saved['system_size']==140,saved
 rows=records();success=next(r for r in reversed(rows) if r['stage']=='pre_restart')
 assert success['operation']=='0' and success['font_read']=='0' and success['size_read']=='0'
 assert success['ram_size']==success['nvs_size']=='140' and success['ram_font_hash']==success['nvs_font_hash']
 boot=frame();assert boot['system_size']==140 and boot['system_font_path']==saved['system_font_path']
 assert records()[-1]['stage']=='boot_loaded' and records()[-1]['ram_size']=='140'
 # A failed write now keeps the last confirmed RAM value; shallow sleep cannot acknowledge an unsaved change.
 builtin=f't,180,{top+step//2},180,{top+step//2}'
 volatile=frame(builtin,fail='sys_font',phase='menu_staged_sleep');assert volatile['system_font_path']==saved['system_font_path']
 row=next(r for r in reversed(records()) if r['stage']=='font_set')
 assert row['operation']!='0' and row['ram_font_hash']==row['nvs_font_hash']
 assert frame()['system_font_path']==saved['system_font_path']
 volatile=frame('t,576,276,576,276',fail='sys_size',phase='pre_restart');assert volatile['system_size']==140
 row=next(r for r in reversed(records()) if r['stage']=='size_set');assert row['operation']!='0' and row['ram_size']=='140' and row['nvs_size']=='140'
 assert frame()['system_size']==140
 # The one-byte shutdown setting can fail too: a cold process reloads the old staged policy, not a font-specific default.
 frame('t,180,480,180,480',page=14,phase='pre_restart')
 row=records()[-1];assert row['staged']==row['nvs_staged']=='1' and row['shutdown_read']=='0',row
 frame();row=records()[-1];assert row['stage']=='boot_loaded' and row['boot_shutdown_read']=='0' and row['staged']=='1',row
 frame('t,180,330,180,330',fail='shutdown',page=14,phase='pre_power_off')
 row=next(r for r in reversed(records()) if r['stage']=='shutdown_set')
 assert row['operation']!='0' and row['staged']=='1' and row['nvs_staged']=='1',row
 assert records()[-1]['stage']=='pre_power_off' and records()[-1]['nvs_staged']=='1'
 frame();row=records()[-1];assert row['staged']==row['nvs_staged']=='1' and row['boot_shutdown_read']=='0',row
 frame('t,180,330,180,330',page=14,phase='pre_power_off')
 row=next(r for r in reversed(records()) if r['stage']=='shutdown_saved');assert row['operation']=='0' and row['staged']==row['nvs_staged']=='0',row
 frame();assert records()[-1]['staged']==records()[-1]['nvs_staged']=='0'
 raw=(card/'diagnostics/ifink-settings.csv').read_text();assert '/sdcard/' not in raw and 'PicoSans' not in raw and 'password' not in raw
 # Log data remains bounded and fresh snapshots remain parseable after rotation.
 target=card/'diagnostics/ifink-settings.csv';target.write_text(raw+'\n'*66000)
 frame(phase='pre_power_off');assert target.stat().st_size<4096 and (target.parent/(target.name+'.previous')).exists()
 assert records()[-1]['stage']=='pre_power_off'
print('PASS: successful write/readback/reboot, injected nvs_set failures distinguish RAM from durable font/size/shutdown policy, boot-first reads, staged/restart snapshots, private hashes and bounded CSV rotation.')
