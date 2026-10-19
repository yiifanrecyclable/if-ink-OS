#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""轮播算法、真实设置/解码、跨重启选择与配置恢复。/ Rotation policy, real settings/decoding, reboot persistence and backup restore."""
from pathlib import Path
import os,json,shutil,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wallpaper-',dir=R/'build') as tmp:
    base=Path(tmp);card=base/'card';card.mkdir();(card/'Photos').mkdir()
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Imain/ui',f'-DWALLPAPER_CARD_ROOT="{card}"','tools/wallpaper_rotation_host_test.c','main/ui/ui_wallpaper_rotation.c','-o',str(base/'policy')],cwd=R,check=True)
    subprocess.run([str(base/'policy')],cwd=R,check=True)
    shutil.rmtree(card);card.mkdir();(card/'Photos').mkdir()
    (base/'titles.txt').write_text('');(base/'build/simulator').mkdir(parents=True)
    env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)}
    def frame(view,events=''):
        p=subprocess.run([str(R/'build/simulator/render'),view,'/sdcard/books/none.txt','0','48','build/simulator/frame.bin','titles.txt','0',','.join(['0']*30),'builtin',events],cwd=base,env=env,capture_output=True,text=True,timeout=30)
        assert p.returncode==0,p.stderr[-1000:]
        return json.loads(p.stdout)
    # 空目录不能启用，坏图也不能冒充可用壁纸。/ Neither empty folders nor corrupt images enable rotation.
    s=frame('settings-9','t,300,480,300,480;t,300,307,300,307')
    assert s['settings_page']==29 and not s['wallpaper_rotating'],s
    (card/'Photos/broken.png').write_text('broken')
    s=frame('settings-29','t,300,450,300,450;t,300,307,300,307')
    assert s['settings_page']==29 and not s['wallpaper_rotating'],s
    for n in ('one.PNG','two.jpg'):
        # 解码器需要真实格式，两个有效PNG仍使用PNG扩展名。/ Keep actual PNG data and matching extensions.
        if n.endswith('.jpg'):n='two.png'
        shutil.copy2(R/'main/assets/display-test.png',card/'Photos'/n)
    s=frame('settings-29','t,300,450,300,450;t,300,307,300,307')
    assert s['settings_page']==9 and s['wallpaper_rotating'] and s['wallpaper_path']=='/sdcard/Photos/',s
    previous=''
    for _ in range(4):
        s=frame('lock-wallpaper');assert s['wallpaper_last'] and s['wallpaper_last']!=previous,s
        previous=s['wallpaper_last']
    # 设置备份恢复保留目录轮播，新进程继续读取配置。/ Restore preserves the folder and a new process reads it back.
    frame('settings-11','t,300,600,300,600')
    assert (card/'Pico-settings.backup').is_file()
    frame('settings-30','t,300,244,300,244')
    assert not frame('settings-9')['wallpaper_rotating']
    frame('settings-11','t,300,720,300,720;t,300,720,300,720')
    assert frame('settings-9')['wallpaper_path']=='/sdcard/Photos/'
    # 删除一张后允许重复；删除目录后回退到默认画面。/ One usable file may repeat; an unavailable folder falls back to the default.
    (card/'Photos/two.png').unlink()
    s=frame('lock-wallpaper');again=frame('lock-wallpaper');assert s['wallpaper_last']==again['wallpaper_last']=='/sdcard/Photos/one.PNG'
    shutil.rmtree(card/'Photos')
    frame('lock-wallpaper');fallback=(base/'build/simulator/frame.bin').read_bytes()
    frame('lock-default');assert fallback==(base/'build/simulator/frame.bin').read_bytes()
print('wallpaper native flow: folder validation, actual image decoding, no repeat across processes, backup restore and safe fallback passed')
