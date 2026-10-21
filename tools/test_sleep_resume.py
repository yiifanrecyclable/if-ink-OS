#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：真实恢复存储与隔离卡/NVS，覆盖消费、断卡、损坏、换固件和MD路径。/ Real restore storage on isolated media/NVS: consumption, removal, corruption, build identity and Markdown.
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sleep_resume.h"
#include "nvs.h"
int resume_test_commit_fail;
#define BYTES (684u*1216u/2u)
static unsigned char front[BYTES],back[BYTES],build[32]={1};
static void pattern(void){memset(front,0x38,sizeof(front));memset(back,0x71,sizeof(back));}
static void document(const char *name){FILE *f=fopen(name,"wb");assert(f);fputs("# Test",f);assert(!fclose(f));}
static void save(const char *path){sleep_resume_set_target(1,path);pattern();assert(sleep_resume_save(front,sizeof(front),3));}
static bool restore(void){sleep_resume_target_t out={0};return sleep_resume_restore(front,back,sizeof(front),3,&out);}
int main(void){
 document("/sdcard/test.md");document("/sdcard/test.txt");document("/sdcard/test.epub");
 sleep_resume_init(build,true);
 for(int i=0;i<3;++i){
  const char *p=i==0?"/sdcard/test.md":i==1?"/sdcard/test.txt":"/sdcard/test.epub";save(p);
  sleep_resume_init(build,true);assert(sleep_resume_pending());pattern();sleep_resume_target_t out={0};
  assert(sleep_resume_restore(front,back,sizeof(front),3,&out));assert(out.tab==1&&!strcmp(out.path,p));assert(!memcmp(front,back,sizeof(front)));
  assert(!restore());sleep_resume_init(build,true);assert(!sleep_resume_pending());
 }
 save("/sdcard/test.md");resume_test_commit_fail=1;sleep_resume_init(build,true);assert(!sleep_resume_pending()&&!restore());resume_test_commit_fail=0;
 save("/sdcard/test.md");sleep_resume_init(build,true);FILE *cut=fopen("/sdcard/.readpico/sleep-frame.bin","r+b");assert(cut);assert(!ftruncate(fileno(cut),30));fclose(cut);assert(!restore());
 save("/sdcard/test.md");sleep_resume_init(build,false);assert(!restore());sleep_resume_init(build,true);assert(!restore());
 save("/sdcard/test.md");build[1]=2;sleep_resume_init(build,true);assert(!restore());build[1]=0;
 save("/sdcard/test.md");sleep_resume_init(build,true);FILE *f=fopen("/sdcard/.readpico/sleep-frame.bin","r+b");assert(f);assert(!fseek(f,-1,SEEK_END));fputc(0,f);fclose(f);pattern();assert(!restore());for(unsigned i=0;i<BYTES;++i)assert(back[i]==0x71);
 save("/sdcard/test.md");sleep_resume_init(build,true);assert(!unlink("/sdcard/test.md"));assert(!restore());document("/sdcard/test.md");
 save("/sdcard/test.md");sleep_resume_init(build,true);assert(!unlink("/sdcard/.readpico/sleep-frame.bin"));assert(!restore());
 save("/sdcard/test.md");sleep_resume_init(build,true);document("/sdcard/test.md");f=fopen("/sdcard/test.md","ab");fputc('x',f);fclose(f);assert(!restore());
 sleep_resume_set_target(3,NULL);pattern();assert(sleep_resume_save(front,BYTES,3));sleep_resume_init(build,true);sleep_resume_target_t out={0};assert(sleep_resume_restore(front,back,BYTES,3,&out));assert(out.tab==3&&!out.path[0]);
 sleep_resume_set_target(1,"/sdcard/../bad.md");pattern();assert(sleep_resume_save(front,BYTES,3));sleep_resume_init(build,true);assert(sleep_resume_restore(front,back,BYTES,3,&out)&&!out.path[0]);
 puts("PASS: one-shot TXT/EPUB/MD/root restore, corrupt/truncated/missing cache, source change, reset/build rejection and old-back preservation");
}
'''
with tempfile.TemporaryDirectory(prefix='sleep-resume-',dir=ROOT/'build') as d:
 p=Path(d);(p/'card').mkdir();(p/'build/simulator').mkdir(parents=True)
 (p/'test.c').write_text(HARNESS)
 wrapped='#include \"nvs.h\"\nextern int resume_test_commit_fail;\nstatic esp_err_t checked_commit(nvs_handle_t h){return resume_test_commit_fail?ESP_FAIL:nvs_commit(h); }\n#define nvs_commit checked_commit\n'+(ROOT/'main/sleep_resume.c').read_text()
 (p/'wrapped.c').write_text(wrapped)
 out=p/'test'
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g','-DPICO_SIM_CARD_PATH="unused"','-include',str(ROOT/'tools/simulator/fs.h'),'-I'+str(ROOT/'tools/simulator/include'),'-I'+str(ROOT/'main'),str(p/'test.c'),str(p/'wrapped.c'),str(ROOT/'tools/simulator/nvs.c'),str(ROOT/'tools/simulator/fs.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],cwd=p,env={**os.environ,'PICO_SIM_CARD_ROOT':str(p/'card'),'ASAN_OPTIONS':'detect_leaks=0'},check=True)
