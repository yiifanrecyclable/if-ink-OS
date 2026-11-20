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
#define UI_NAV_TOP 1136
#define ui_snprintf snprintf
#define UI_MSG(text) (text)
#define SETTINGS_ROW_H 64
#define UI_GRAY_BLACK 0
#define UI_GRAY_ITEM_LINE 0xc0
#define UI_TEXT_BODY 27
#define UI_TEXT_CAPTION 20
#define UI_BTN_RADIUS 12
#define EPD_DRAW_ALIGN_CENTER 0
#define EPD_DRAW_ALIGN_LEFT 1
#define EPD_DRAW_ALIGN_RIGHT 2
typedef struct {int x,y,width,height;} EpdRect;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA} app_redraw_t;
static uint32_t s_online_seen;
static if_update_status_t s_online_drawn;
static bool s_online_drawn_valid,s_online_progress_only;
static int64_t s_online_drawn_at;
static unsigned fills,texts;static char shown[64];
static bool s_online_confirm,s_online_pending;
static bool s_online_stats_save_failed,stats_enabled;
bool ota_stats_enabled(void){return stats_enabled;}
static if_update_channel_t s_online_channel;
static bool strict_progress;
static unsigned channel_labels,dots;
static int title_y,channel_x[2],channel_y[2],dot_x,dot_y;
static if_update_status_t drawn_status;
static char prompt[96];
static const char *buttons[2];
static unsigned button_count;
static unsigned gray_install_count;
static uint8_t pixels[684*1216];
static void epd_fill_rect(EpdRect a,int gray,uint8_t *fb){
 if(strict_progress){assert(a.x>=36&&a.y>=596&&a.x+a.width<=648&&a.y+a.height<=760);++fills;}
 for(int y=a.y;y<a.y+a.height;++y)memset(fb+y*684+a.x,gray,a.width);
}
static void epd_fill_circle(int x,int y,int radius,int gray,uint8_t *fb){
 (void)fb;assert(radius==8&&gray==UI_GRAY_BLACK);++dots;dot_x=x;dot_y=y;
}
static void online_text(uint8_t *fb,int y,const char *text){
 (void)fb;
 if(y==UI_PAGE_Y(626))snprintf(prompt,sizeof(prompt),"%s",text);
 if(y==716){++texts;snprintf(shown,sizeof(shown),"%s",text);}
}
void if_update_get_status(if_update_status_t *out){*out=drawn_status;}
static void back_header(uint8_t *fb,const char *title){(void)fb;(void)title;}
static void row(uint8_t *fb,int y,const char *title,const char *value){(void)fb;(void)y;(void)title;(void)value;}
static void ui_nav_draw(uint8_t *fb,int tab){(void)fb;(void)tab;}
static int ui_text_fit_px(int role,const char *text,int width,int cap){(void)text;(void)width;(void)cap;return role;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool invert){
 (void)fb;(void)px;(void)align;(void)invert;
 assert(strcmp(text,"安装新版本")&&strcmp(text,"安装较早版本"));
 if(!strcmp(text,"在线版本")){assert(x==54);title_y=y;}
 if(!strcmp(text,"稳定版")){assert(x>54);channel_labels|=1;channel_x[0]=x;channel_y[0]=y;}
 if(!strcmp(text,"测试版")){assert(x>54);channel_labels|=2;channel_x[1]=x;channel_y[1]=y;}
}
static void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool invert){
 ui_text_vc(fb,x,y,px,text,align,invert);
}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int gray){
 (void)fb;(void)r;(void)radius;(void)gray;
}
static void ui_text_fixed_vc_gray(uint8_t *fb,int x,int y,int px,const char *text,int align,int ink,int bg){
 (void)fb;(void)x;(void)y;(void)px;(void)align;(void)ink;(void)bg;
 assert(!strcmp(text,"确认安装"));++gray_install_count;
}
static void ui_draw_button(uint8_t *fb,EpdRect r,const char *label,bool disabled){
 (void)fb;(void)r;(void)disabled;assert(button_count<2);buttons[button_count++]=label;
}
'''
unit+='\n'.join(extract(n) for n in ['online_action_rect','online_stats_rect','online_versions_top','online_channel_rect','online_channel_name','online_offer_selectable','online_channel_draw','online_stats_note','online_progress_rect','online_progress_draw','online_remember','online_stage_changed','online_update_rect','online_status_redraw','online_confirm_rect','online_draw'])
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
 EpdRect stage=online_update_rect(false);
 assert(stage.x==36&&stage.width==612&&stage.y==166&&stage.y+stage.height==UI_NAV_TOP);
 EpdRect progress=online_update_rect(true);assert(progress.y==596&&progress.height==164);
 online_remember(&status);
 status.offers[IF_UPDATE_BETA].state=IF_UPDATE_FAILED;
 assert(online_stage_changed(&status));
 assert(online_status_redraw(30003,&status)==APP_REDRAW_AREA&&!s_online_progress_only);
 memset(pixels,0x5a,sizeof(pixels));status.received=6000;strict_progress=true;
 online_progress_draw(pixels,&status);
 strict_progress=false;
 assert(fills==3&&texts==1&&!strcmp(shown,"6% · 5 / 98 KB"));
 EpdRect area=online_progress_rect();
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
  if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)assert(pixels[y*684+x]==0x5a);
 drawn_status=(if_update_status_t){.state=IF_UPDATE_AVAILABLE};
 drawn_status.offers[0]=(if_update_offer_t){.state=IF_UPDATE_AVAILABLE,.version="0.2.3"};
 drawn_status.offers[1]=(if_update_offer_t){.state=IF_UPDATE_AVAILABLE,.downgrade=true,.version="0.2.1-beta.1"};
 online_draw(pixels);assert(channel_labels==3&&button_count==2&&!strcmp(buttons[0],"检查更新"));
 assert(!strcmp(buttons[1],"同意统计上报"));
 button_count=0;stats_enabled=true;online_draw(pixels);
 assert(button_count==2&&!strcmp(buttons[1],"关闭统计上报"));
 assert(title_y>UI_PAGE_Y(210)+SETTINGS_ROW_H&&title_y<channel_y[0]&&channel_y[0]<channel_y[1]&&dots==0);
 assert(channel_y[1]-channel_y[0]>SETTINGS_ROW_H);
 assert(pixels[online_versions_top()*684+36]==UI_GRAY_BLACK);
 button_count=0;s_online_confirm=true;s_online_channel=IF_UPDATE_BETA;
 online_draw(pixels);
 assert(dots==1&&dot_y==channel_y[1]&&dot_x-8>=online_channel_rect(IF_UPDATE_BETA).x&&dot_x+8<channel_x[1]);
 assert(!strcmp(prompt,"确认安装较早版本？")&&button_count==2);
 assert(!strcmp(buttons[0],"取消")&&!strcmp(buttons[1],"确认安装"));
 drawn_status.busy=true;
 assert(online_offer_selectable(&drawn_status,IF_UPDATE_BETA));
 button_count=0;
 online_draw(pixels);
 assert(button_count==1&&!strcmp(buttons[0],"取消")&&gray_install_count==1);
 drawn_status.installing=true;
 assert(!online_offer_selectable(&drawn_status,IF_UPDATE_BETA));
 puts("PASS: progress throttling, stage redraw bounds, and explicit confirmation for an earlier version");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'tools/ota_stubs'),'-I'+str(root/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
