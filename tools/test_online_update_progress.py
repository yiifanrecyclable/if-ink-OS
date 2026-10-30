#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实升级进度节流、阶段即时更新与局部绘制范围。/ English: Production updater throttling, immediate stages and bounded drawing."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'main/apps/app_device_settings.c').read_text()
def extract(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M)
    assert m,name
    end=m.end();depth=1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[m.start():end]
unit='''#include "ota_online.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define UI_PAGE_Y(y) ((y)-24)
#define ui_snprintf snprintf
typedef struct {int x,y,width,height;} EpdRect;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA} app_redraw_t;
static uint32_t s_online_seen;
static if_update_status_t s_online_drawn;
static bool s_online_drawn_valid,s_online_progress_only;
static int64_t s_online_drawn_at;
static unsigned fills,texts;static char shown[64];
static uint8_t pixels[684*1216];
static void epd_fill_rect(EpdRect a,int gray,uint8_t *fb){
 assert(a.x>=36&&a.y>=596&&a.x+a.width<=648&&a.y+a.height<=760);++fills;
 for(int y=a.y;y<a.y+a.height;++y)memset(fb+y*684+a.x,gray,a.width);
}
static void online_text(uint8_t *fb,int y,const char *text){(void)fb;assert(y==716);++texts;snprintf(shown,sizeof(shown),"%s",text);}
'''
unit+='\n'.join(extract(n) for n in ['online_progress_rect','online_progress_draw','online_remember','online_stage_changed','online_update_rect','online_status_redraw'])
unit+='''
int main(void){
 if_update_status_t status={.state=IF_UPDATE_DOWNLOADING,.busy=true,.revision=1,.total=100000};
 assert(online_status_redraw(100,&status)==APP_REDRAW_AREA&&!s_online_progress_only);online_remember(&status);
 status.received=6000;++status.revision;assert(online_status_redraw(1000,&status)==APP_REDRAW_NONE);
 assert(online_status_redraw(5099,&status)==APP_REDRAW_NONE);
 assert(online_status_redraw(5100,&status)==APP_REDRAW_AREA&&s_online_progress_only);online_remember(&status);
 ++status.revision;assert(online_status_redraw(20000,&status)==APP_REDRAW_NONE);
 status.received=6099;assert(online_status_redraw(20000,&status)==APP_REDRAW_NONE);
 status.received=8000;++status.revision;assert(online_status_redraw(20000,&status)==APP_REDRAW_AREA&&s_online_progress_only);online_remember(&status);
 status.state=IF_UPDATE_FAILED;status.busy=false;++status.revision;
 assert(online_status_redraw(20001,&status)==APP_REDRAW_AREA&&!s_online_progress_only);online_remember(&status);
 assert(online_status_redraw(30000,&status)==APP_REDRAW_NONE);
 status.state=IF_UPDATE_VERIFYING;++status.revision;
 assert(online_status_redraw(30001,&status)==APP_REDRAW_AREA&&!s_online_progress_only);online_remember(&status);
 status.state=IF_UPDATE_READY;++status.revision;
 assert(online_status_redraw(30002,&status)==APP_REDRAW_AREA&&!s_online_progress_only);
 online_remember(&status);
 EpdRect stage=online_update_rect(&status,false);assert(stage.x==36&&stage.width==612&&stage.y==480&&stage.height==574);
 strcpy(status.version,"0.1.8");stage=online_update_rect(&status,false);assert(stage.y==166&&stage.height==888);
 EpdRect progress=online_update_rect(&status,true);assert(progress.y==596&&progress.height==164);
 online_remember(&status);status.channel=IF_UPDATE_BETA;
 assert(online_stage_changed(&status));stage=online_update_rect(&status,false);assert(stage.y==166&&stage.height==888);
 memset(pixels,0x5a,sizeof(pixels));status.received=6000;online_progress_draw(pixels,&status);
 assert(fills==3&&texts==1&&!strcmp(shown,"6% · 5 / 98 KB"));
 EpdRect area=online_progress_rect();
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
  if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)assert(pixels[y*684+x]==0x5a);
 puts("PASS: 5-second/changed-percent throttle, immediate stages, stable no-op and progress-only framebuffer bounds");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'tools/ota_stubs'),'-I'+str(root/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
