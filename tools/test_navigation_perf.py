#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production navigation boundaries: exact timing, preserved callback order, no idle logging, request-ID-safe analysis."""
from pathlib import Path
import importlib.util, json, subprocess, tempfile
R=Path(__file__).resolve().parents[1]
src=(R/'main/app/app_loop.c').read_text()
home=(R/'main/apps/app_dashboard.c').read_text()
def function(text,marker):
 start=text.index(marker);at=text.index('{',start)+1;depth=1
 while depth:
  if text[at]=='{':depth+=1
  elif text[at]=='}':depth-=1
  at+=1
 return text[start:at]+'\n'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "shelf_perf.h"
enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE};
enum EpdDrawError {EPD_DRAW_SUCCESS};
typedef int app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;} app_ctx_t;
typedef struct app_desc {
 int id;const char *title;bool enter_full;
 void (*on_exit)(app_ctx_t*);void (*on_enter)(app_ctx_t*);void (*render)(app_ctx_t*,uint8_t*);
 bool (*present)(app_ctx_t*,app_redraw_t);EpdRect (*area_hint)(app_ctx_t*);
} app_desc_t;
#define APP_PAGE_FORCE_FULL 0
#define APP_PAGE_REFRESH_MODE 0
#define APP_DYNAMIC_REFRESH_MODE 0
#define MODE_GL16 0
#define UI_STATUS_HEIGHT 56
#define UI_LOCK_WIDTH 684
#define UI_NAV_TOP 1136
#define OPLOG_PAGE 0
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define UI_MSG(s) (s)
#define ESP_OK 0
static int E0470_WAVEFORM;
static int64_t clock_us=1000000;
static unsigned request=1,count;
static bool active=true,s_home_force_full_once;
static char order[128];static unsigned used,updates;
static EpdRect s_area;
static struct {shelf_perf_span_t span;int64_t end;} records[256];
shelf_perf_span_t shelf_perf_begin(shelf_perf_stage_t stage,int a,int b){return active?(shelf_perf_span_t){clock_us,request,stage,a,b}:(shelf_perf_span_t){0};}
void shelf_perf_end(shelf_perf_span_t *p){if(p->stage){assert(count<256);records[count++]=(typeof(records[0])){*p,clock_us};p->stage=0;}}
static void step(char c,int ms){if(used+1<sizeof(order)){order[used++]=c;order[used]=0;}clock_us+=1000*ms;}
static int app_index_of(const app_desc_t *a){return a?a->id:-1;}
static void operation_log_event(int a,int b,int c,int d,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static int app_settings_take_save_error(void){return 0;}
static void storage_notice(const char *s){(void)s;assert(!"unexpected save error");}
static bool display_resume_clean_pending(void){return false;}
static void ui_text_set_system_scale(bool b){assert(b);step('S',0);}
static void render(app_ctx_t *c,uint8_t *fb){(void)c;(void)fb;SHELF_PERF_SCOPE(p,SP_NAV_RENDER,0,0);step('R',300);}
static EpdRect ui_nav_rect(void){return (EpdRect){0,1136,684,80};}
static EpdRect ui_content_refresh_area(void){return (EpdRect){0,0,684,1136};}
static int output(void){++updates;step('D',500);return 0;}
static int update_display_area_with(void *h,const int *w,int m,EpdRect a){(void)h;(void)w;(void)m;(void)a;return output();}
static int update_display_full(void *h){(void)h;return output();}
static int update_display_mode(void *h,int m){(void)h;(void)m;return output();}
static int update_display_fast_page(void *h){(void)h;return output();}
static int update_display_home_clean(void *h){(void)h;return output();}
static int update_display_navigation_page(void *h,EpdRect a,EpdRect b){(void)h;(void)a;(void)b;return output();}
static void guard_draw_result(void *h,int e){(void)h;assert(!e);step('G',5);}
static void exited(app_ctx_t *c){(void)c;step('X',7);}
static bool app_font_activate_system(void){step('F',11);return true;}
static void entered(app_ctx_t *c){(void)c;step('N',13);++request;}
'''
unit+=function(home,'static bool present(app_ctx_t *ctx, app_redraw_t redraw) {')
unit+=function(src,'void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {')
unit+=function(src,'static void switch_to(')
unit+=r'''
int main(void){
 app_ctx_t ctx={0};app_desc_t first={.id=1,.title="Books",.on_exit=exited};
 const app_desc_t *current=&first;
 app_desc_t target={.id=0,.title="Home",.on_enter=entered,.on_exit=exited,.render=render,.present=present};
 switch_to(&ctx,&current,&target);assert(current==&target&&!strcmp(order,"XFNSRDG")&&updates==1&&count==7);
 assert(records[count-1].span.stage==SP_NAV_SWITCH&&records[count-1].end-records[count-1].span.start==836000);
 for(unsigned i=0;i<count;++i){
  if(records[i].span.stage==SP_NAV_PRESENT)assert(records[i].end-records[i].span.start==805000);
  if(records[i].span.stage==SP_NAV_OUTPUT)assert(records[i].end-records[i].span.start==505000);
 }
 unsigned before=count;switch_to(&ctx,&current,&target);switch_to(&ctx,&current,NULL);assert(count==before&&updates==1);
 for(int i=0;i<10000;++i)app_present(&ctx,&target,APP_REDRAW_NONE);assert(count==before&&updates==1);
 target.present=NULL;target.id=2;used=0;order[0]=0;current=&first;
 switch_to(&ctx,&current,&target);assert(!strcmp(order,"XFNSRDG")&&updates==2&&count==14);
 target.id=3;target.enter_full=true;used=0;order[0]=0;current=&first;
 switch_to(&ctx,&current,&target);assert(!strcmp(order,"XFNSRDG")&&updates==3&&count==21);
 target.id=0;target.present=present;target.enter_full=false;s_home_force_full_once=true;used=0;order[0]=0;current=&first;
 switch_to(&ctx,&current,&target);assert(!strcmp(order,"XFNSRDG")&&updates==4&&count==28&&!s_home_force_full_once);
 puts("start_us,end_us,duration_us,kind,stage,request,a,b,c,d");
 const char *names[]={"nav_switch","nav_exit","nav_font","nav_enter","nav_present","nav_render","nav_output"};
 for(unsigned i=0;i<count;++i){shelf_perf_span_t p=records[i].span;
  printf("%lld,%lld,%lld,span,%s,%u,%d,%d,0,0\n",(long long)p.start,(long long)records[i].end,(long long)(records[i].end-p.start),names[p.stage-SP_NAV_SWITCH],p.request,p.a,p.b);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='nav-perf-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DSHELF_PERF_HOST_TEST','-I'+str(R/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 result=subprocess.run([str(p/'test')],capture_output=True,text=True)
 assert result.returncode==0,result.stderr+result.stdout
 csv=p/'nav.csv';csv.write_text('# shelf_perf schema=1 version=0.2.0-r9t5 elf_sha256=host window_ms=300000 elapsed_us=9999999 records=28 dropped=0\n'+result.stdout+'# complete=1\n')
 spec=importlib.util.spec_from_file_location('analyze',R/'tools/analyze_shelf_perf.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 j=m.analyze(csv);nav=j['navigation'];assert len(nav)==4
 for row in nav:
  assert row['total_ms']==836 and row['exit_ms']==7 and row['font_ms']==11 and row['enter_ms']==13,row
  assert row['present_ms']==805 and row['output_ms']==505 and row['output_groups']==1,row
 assert nav[0]['render_ms']==300 and nav[0]['present_other_ms']==0
 assert not nav[0]['home_clean_output'] and nav[-1]['home_clean_output']
 # Overlapping render spans must not be double-counted.
 import csv as csv_module,io
 rows=list(csv_module.DictReader(io.StringIO(result.stdout)))
 for row in rows:
  for key in ('start_us','end_us','duration_us','request','a','b','c','d'):row[key]=int(row[key])
 nested=next(dict(row) for row in rows if row['stage']=='nav_render');nested['start_us']+=1000;nested['end_us']-=1000;nested['duration_us']-=2000;rows.append(nested)
 assert m.navigation_summary(rows)[0]['render_ms']==300
 # A Banner scope is preparation work, never a second render/output or a second entry.
 enter=next(row for row in rows if row['stage']=='nav_enter')
 rows.append({**enter,'stage':'home_banner','b':1})
 first=m.navigation_summary(rows)[0]
 assert first['banner_prepare_ms']==13 and first['banner_cache_hit'] and first['enter_ms']==13 and first['render_ms']==300
 print('PASS real switch_to/app_present/Home output: exact phase timing and callback/output order; same/null switches and 10000 idle presents emit no records; time-window joins survive request-ID changes and nested renders.')
