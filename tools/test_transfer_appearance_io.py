#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real web-font comparison/promotion: chunks, deduplication and allocation failure on an isolated card."""
from pathlib import Path
import os, subprocess, tempfile

R=Path(__file__).resolve().parents[1]
production=(R/'main/apps/transfer_appearance.c').read_text()
helpers=production.split('#define WEB_FONT_ROOT',1)[1].split('static esp_err_t response',1)[0]
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
static bool fail_alloc;
static void *checked_malloc(size_t n){return fail_alloc?NULL:malloc(n);}
#define malloc checked_malloc
#define WEB_FONT_ROOT'''+helpers+r'''
#undef malloc
static void write_file(const char *path,unsigned n,unsigned seed){
 FILE *f=fopen(path,"wb");assert(f);for(unsigned i=0;i<n;++i)assert(fputc((i+seed)%251,f)!=EOF);assert(!fclose(f));
}
int main(void){
 assert(!mkdir("/sdcard/fonts",0700));assert(!mkdir(WEB_FONT_ROOT,0700));
 const char *a=WEB_FONT_ROOT "/0123456789abcdef0123456789abcdef.ttf",*b="/sdcard/other.ttf";
 write_file(a,8193,7);write_file(b,8193,7);assert(same_file(a,b));
 write_file(b,8193,8);assert(!same_file(a,b));write_file(b,8192,7);assert(!same_file(a,b));
 write_file(b,8193,7);fail_alloc=true;assert(!same_file(a,b));
 char target[192];assert(!promote_font(a,"Sample.ttf",target,sizeof(target)));
 assert(access("/sdcard/fonts/Sample-01234567.ttf",F_OK));assert(access("/sdcard/fonts/Sample-01234567.ttf.part",F_OK));
 fail_alloc=false;assert(promote_font(a,"Sample.ttf",target,sizeof(target)));assert(same_file(a,target));
 char again[192];assert(promote_font(a,"Other.ttf",again,sizeof(again)));assert(!strcmp(target,again));
 assert(access("/sdcard/fonts/Sample-01234567.ttf.part",F_OK));
 puts("PASS real web-font I/O: multi-chunk equality/mismatch, heap OOM fails closed without a partial/final font, verified promotion and deduplication.");
}
'''
with tempfile.TemporaryDirectory(prefix='web-font-io-',dir=R/'build') as tmp:
 p=Path(tmp);card=p/'card';card.mkdir();(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
  '-DPICO_SIM_CARD_PATH="unused"',
  '-include',str(R/'tools/simulator/fs.h'),'-I'+str(R/'tools/simulator/include'),
  str(p/'test.c'),str(R/'tools/simulator/fs.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'ASAN_OPTIONS':'detect_leaks=0'},check=True)
