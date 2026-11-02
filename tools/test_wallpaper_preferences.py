#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""SD wallpaper persistence under failed NVS and torn writes; real Settings/file/web/lock flows.
中文：内部NVS不可写和TF半写下的壁纸持久化，真实设置/文件/网页/锁屏路径。
"""
from pathlib import Path
import subprocess,tempfile,os,json,shutil,struct,zlib,hashlib
from PIL import Image
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wallpaper-prefs-',dir=R/'build') as tmp:
 work=Path(tmp);card=work/'card';out=work/'build/simulator';out.mkdir(parents=True)
 for name in ('pictures','Photos','books','fonts','markdown'):(card/name).mkdir(parents=True)
 for file,tone in (('A.png',85),('B.png',170)):Image.new('L',(684,1216),tone).save(card/'pictures'/file)
 (out/'titles.txt').touch();ns=out/'nvs/read_pico';ns.mkdir(parents=True);(ns/'wallpaper').write_bytes(b'/sdcard/pictures/A.png\0')
 originals={f:hashlib.sha256(f.read_bytes()).digest() for f in card.rglob('*.png')}
 def frame(view,events='',fault=False):
  env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_NVS_COMMIT_FAIL':'always'}
  if fault:env['PICO_TEST_WALLPAPER_WRITE_FAIL']='1'
  p=subprocess.run([str(R/'build/simulator/render'),view,'','0','48','build/simulator/frame.bin','build/simulator/titles.txt','0','0','builtin',events],cwd=work,env=env,capture_output=True,text=True,timeout=30)
  assert p.returncode==0,p.stderr[-1200:]
  return json.loads(p.stdout),(out/'frame.bin').read_bytes()
 assert frame('settings-9')[0]['wallpaper_path']=='/sdcard/pictures/A.png' # Legacy NVS remains readable.
 listing=frame('settings-30')[0];g=listing['settings_geometry'];step=g[7];index=listing['wallpaper_items'].index('/sdcard/pictures/B.png');y=268+step*index+step//2;select=f't,180,{y},180,{y}'
 # Preview then confirm the second image, using the actual lowest part of the confirm target.
 events=select+';t,342,970,342,970;t,474,681,474,681'
 s,_=frame('settings-30',events);assert s['wallpaper_path']=='/sdcard/pictures/B.png' and s['settings_page']==9,s
 assert frame('settings-9')[0]['wallpaper_path']=='/sdcard/pictures/B.png'
 frame('lock-wallpaper');b=(out/'frame.bin').read_bytes();assert sum(b)//len(b)>120
 # The original NVS selection was never touched; TF has priority after a fresh process.
 assert (ns/'wallpaper').read_bytes()==b'/sdcard/pictures/A.png\0'
 # A torn inactive write cannot remove the last complete choice, and failure remains visible in preview.
 index=listing['wallpaper_items'].index('/sdcard/pictures/A.png');y=268+step*index+step//2;first=f't,180,{y},180,{y};t,342,970,342,970;t,474,653,474,653'
 failed,_=frame('settings-30',first,fault=True);assert failed['wallpaper_path']=='/sdcard/pictures/B.png' and failed['settings_page']==10 and failed['settings_notice']=='壁纸保存失败，请重试',failed
 assert frame('settings-9')[0]['wallpaper_path']=='/sdcard/pictures/B.png'
 # Default/reset also commits explicitly on TF, overriding a nonempty legacy NVS key.
 assert frame('settings-30','t,180,244,180,244')[0]['wallpaper_path']==''
 assert frame('settings-9')[0]['wallpaper_path']==''
 assert all(hashlib.sha256(f.read_bytes()).digest()==v for f,v in originals.items())
 # Probe the fixed-record backend under ASan/UBSan, independently of the large renderer.
 test=work/'unit.c';test.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wallpaper_preferences.h"
int main(void){wallpaper_preferences_t value={0},out={0};
 snprintf(value.path,sizeof(value.path),"%s/A.png",WALLPAPER_CARD_ROOT);assert(wallpaper_preferences_save(&value)==ESP_OK);assert(wallpaper_preferences_load(&out)==ESP_OK&&!strcmp(value.path,out.path));
 snprintf(value.path,sizeof(value.path),"%s/B.png",WALLPAPER_CARD_ROOT);assert(wallpaper_preferences_save(&value)==ESP_OK);
 char slot[512];snprintf(slot,sizeof(slot),"%s/.readpico/settings/wallpaper.b",WALLPAPER_CARD_ROOT);FILE *f=fopen(slot,"r+b");assert(f);fputc(0,f);fclose(f);assert(wallpaper_preferences_load(&out)==ESP_OK&&strstr(out.path,"/A.png"));
 assert(wallpaper_preferences_save(&value)==ESP_OK);setenv("PICO_TEST_WALLPAPER_WRITE_FAIL","1",1);snprintf(value.path,sizeof(value.path),"%s/C.png",WALLPAPER_CARD_ROOT);assert(wallpaper_preferences_save(&value)!=ESP_OK);assert(wallpaper_preferences_load(&out)==ESP_OK&&strstr(out.path,"/B.png"));
 puts("PASS: wallpaper CRC fallback and short-write preservation under ASan/UBSan");return 0;}
''')
 unitcard=work/'unit-card';unitcard.mkdir();exe=work/'test'
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DPICO_HOST_SIMULATOR',f'-DWALLPAPER_CARD_ROOT="{unitcard}"','-I'+str(R/'tools/simulator/include'),'-I'+str(R/'main'),str(test),str(R/'main/wallpaper_preferences.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: legacy NVS read, settings checked save under failed NVS, actual lock image, reboot, torn TF write, explicit default and untouched source images.')
