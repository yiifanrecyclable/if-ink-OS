#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""统一壁纸选择、递归轮播、触区、真实解码与配置恢复；只用临时TF。
Unified wallpaper picking, subtree rotation, hit zones, decoding and restore on private media.
"""
from pathlib import Path
import os,json,shutil,subprocess,tempfile,struct,zlib
R=Path(__file__).resolve().parents[1]
def png(w,h):
 def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([170])*w)*h))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory(prefix='wallpaper-',dir=R/'build') as tmp:
 base=Path(tmp);card=base/'card';card.mkdir()
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Imain/ui',f'-DWALLPAPER_CARD_ROOT="{card}"','tools/wallpaper_rotation_host_test.c','main/ui/ui_wallpaper_rotation.c','-o',str(base/'policy')],cwd=R,check=True)
 subprocess.run([str(base/'policy')],cwd=R,check=True)
 shutil.rmtree(card);(card/'pictures/Albums/Child').mkdir(parents=True);(card/'pictures/Empty').mkdir();(card/'Other').mkdir()
 (base/'titles.txt').write_text('');(base/'build/simulator').mkdir(parents=True)
 (card/'pictures/a.png').write_bytes(png(342,608));(card/'pictures/b.png').write_bytes(png(684,1216))
 (card/'pictures/Albums/Child/c.png').write_bytes(png(342,608));(card/'pictures/Albums/wide.png').write_bytes(png(684,300));(card/'pictures/Empty/broken.png').write_text('broken')
 env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
 def frame(view,events='',extra=None):
  p=subprocess.run([str(R/'build/simulator/render'),view,'/sdcard/books/none.txt','0','48','build/simulator/frame.bin','titles.txt','0',','.join(['0']*30),'builtin',events],cwd=base,env={**env,**(extra or {})},capture_output=True,text=True,timeout=30)
  assert p.returncode==0,p.stderr[-2000:]
  return json.loads(p.stdout)
 def tap(x,y):return f't,{x},{y},{x},{y}'
 def item(state,path):
  index=state['wallpaper_items'].index(path);return tap(300,190+index*state['files_row_height']+state['files_row_height']//2)
 # 标题与空白不返回，按钮独立；上一级能浏览根目录，但根目录不能保存轮播。
 # Titles/whitespace never navigate; parent can reach the root, but root cannot be saved for rotation.
 s=frame('settings-30');assert s['wallpaper_directory']=='/sdcard/pictures' and s['settings_page']==30
 s=frame('settings-30',tap(350,83)+';'+tap(500,130));assert s['wallpaper_directory']=='/sdcard/pictures' and s['settings_page']==30
 s=frame('settings-30',tap(610,83));assert s['wallpaper_picker'][2]==1 and s['settings_page']==30 and s['files_view']==-1
 folders={i['path']:i['image_folder'] for i in s['files_items']}
 assert folders['/sdcard/pictures/Albums'] and not folders['/sdcard/pictures/Empty'],folders
 assert frame('settings-30',tap(58,83))['settings_page']==9
 s=frame('settings-29',tap(142,83)+';'+tap(342,1090));assert s['settings_page']==29 and s['wallpaper_directory']=='/sdcard' and not s['wallpaper_rotating']
 # 空/坏图目录不修改原选择，固定图片先预览再保存。
 # Empty/corrupt folders preserve settings; fixed images preview before save.
 s=frame('settings-29');empty=item(s,'/sdcard/pictures/Empty')
 s=frame('settings-29',empty+';'+tap(342,1090));assert s['settings_page']==29 and not s['wallpaper_rotating']
 s=frame('settings-30');choose=item(s,'/sdcard/pictures/a.png')
 s=frame('settings-30',choose);assert s['wallpaper_picker'][3:5]==[1,1] and s['settings_page']==30
 s=frame('settings-30',choose+';'+tap(342,1090));assert s['settings_page']==9 and s['wallpaper_path']=='/sdcard/pictures/a.png'
 assert frame('lock-wallpaper')['wallpaper_last']=='/sdcard/pictures/a.png'
 # 本层只有宽图，子目录有合格竖图，应可启用且锁屏确实选择子目录图片。
 # A wide-only parent with a usable child image must enable rotation and select the child on lock.
 s=frame('settings-29');album=item(s,'/sdcard/pictures/Albums')
 s=frame('settings-29',album+';'+tap(342,1090));assert s['settings_page']==9 and s['wallpaper_path']=='/sdcard/pictures/Albums/' and s['wallpaper_rotating']
 assert frame('lock-wallpaper')['wallpaper_last']=='/sdcard/pictures/Albums/Child/c.png'
 (card/'pictures/Albums/Child/d.png').write_bytes(png(342,608))
 previous=''
 for _ in range(4):
  s=frame('lock-wallpaper');assert s['wallpaper_last']!=previous and '/Child/' in s['wallpaper_last'];previous=s['wallpaper_last']
 # 无卡/保存失败保留旧选择；设置备份恢复仍保留目录模式。
 # Failed saves preserve the old choice; settings backup/restore retains directory mode.
 frame('settings-11',tap(300,602));assert (card/'Pico-settings.backup').is_file()
 assert not frame('settings-30',tap(342,1090))['wallpaper_rotating']
 frame('settings-11',tap(300,720)+';'+tap(300,720));assert frame('settings-9')['wallpaper_path']=='/sdcard/pictures/Albums/'
 before=frame('settings-9')['wallpaper_path']
 slots={p.suffix:struct.unpack_from('<I',p.read_bytes(),8)[0] for p in (card/'.readpico/settings').glob('wallpaper.[ab]')}
 inactive='b' if slots.get('.a',0)>=slots.get('.b',0) else 'a'
 s=frame('settings-30',choose+';'+tap(342,1090),{'PICO_TEST_FOPEN_WRITE_FAIL':'/sdcard/.readpico/settings/wallpaper.'+inactive})
 assert s['settings_page']==30 and s['wallpaper_path']==before
 shutil.rmtree(card/'pictures/Albums')
 assert frame('lock-wallpaper')['wallpaper_path']==before
print('PASS unified fixed/rotation browser, precise header hit zones, thumbnails, preview/save, root rejection, child images, no repeats, backup and safe fallback')
