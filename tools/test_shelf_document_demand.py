#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production scan block, document tabs, direct-open and reader return without hardware I/O."""
from pathlib import Path
import os, subprocess, tempfile

R=Path(__file__).resolve().parents[1]
src=Path(os.environ.get('PICO_TEST_BOOK_SOURCE',str(R/'main/apps/app_book.c'))).read_text()
def block(marker):
 start=src.index(marker);at=src.index('{',start)+1;depth=1
 while depth:
  if src[at]=='{':depth+=1
  elif src[at]=='}':depth-=1
  at+=1
 return src[start:at]

unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {SHELF,READING,TOC,MANAGE,BULK,IMPORT,SEARCH,EDIT,DOCUMENTS,DOCUMENT_BULK};
enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,ESP_ERR_NOT_FINISHED=8,READER_PANEL_NONE=0,UI_GESTURE_TAP=1};
typedef int app_redraw_t;
typedef struct {uint64_t now_ms;int leaf,home;} app_ctx_t;
typedef struct {int x0,y0,type;} ui_gesture_event_t;
typedef struct {int unused;} read_pico_sd_info_t;
#define SHELF_PERF_SCOPE(...) ((void)0)
#define BOOK_STORE_PATH_MAX 288
static int s_view,s_reader_panel,shelves,documents,opens,saves,probes,returns;
static uint64_t s_poll_ms;
static bool s_scan_pending,s_document_mode,s_document_delete_confirm,s_document_clear_confirm,s_document_menu;
static bool s_requested_open_home,s_reader_return_home,open_ok=true;
static char s_requested_open[288];
static int read_pico_sd_get_info(read_pico_sd_info_t *p){(void)p;return probes;}
static void scan_shelf(app_ctx_t *c){(void)c;++shelves;}
static void scan_documents(app_ctx_t *c){c->leaf=0;++documents;}
static bool open_book(app_ctx_t *c,const char *p){(void)c;assert(p[0]);++opens;if(open_ok)s_view=READING;return open_ok;}
static void copy_text(char *d,size_t n,const char *s){snprintf(d,n,"%s",s);}
static void prepare_covers(app_ctx_t *c){(void)c;}
static int content_tab(int i){return i;}
static bool ui_rect_hit(int r,int x,int y){(void)y;return r==x;}
static void track_ticket_stats(app_ctx_t *c){(void)c;}
static void save_progress(void){++saves;}
static void free_book(void){}
static void ui_nav_request(app_ctx_t *c,int i){assert(!i);c->home=1;}
static void return_to_cached_shelf(app_ctx_t *c){(void)c;++returns;}
'''
unit+=block('static bool open_requested_book(app_ctx_t* ctx) {')+'\n'
unit+='static app_redraw_t tick_scan(app_ctx_t *ctx){'+block('if (s_scan_pending && ctx->now_ms - s_poll_ms >= 500) {')+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t tabs(app_ctx_t *ctx,const ui_gesture_event_t *ev){'+block('if((s_view==SHELF||s_view==DOCUMENTS)&&ev->type==UI_GESTURE_TAP){')+'return APP_REDRAW_NONE;}\n'
unit+=block('static app_redraw_t reader_return(app_ctx_t *ctx, bool home) {')+'\n'
unit+=r'''
static void reset(int view){s_view=view;shelves=documents=opens=saves=returns=probes=0;s_poll_ms=0;s_scan_pending=true;s_requested_open[0]=0;s_requested_open_home=s_reader_return_home=false;s_document_mode=false;open_ok=true;}
int main(void){
 app_ctx_t c={.now_ms=1000};ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=1};
 reset(SHELF);assert(tick_scan(&c)==APP_REDRAW_PAGE);assert(shelves==1&&!documents&&!s_scan_pending);
 assert(tick_scan(&c)==APP_REDRAW_NONE&&shelves==1&&!documents);
 assert(tabs(&c,&e)==APP_REDRAW_PAGE&&s_view==DOCUMENTS&&s_document_mode&&documents==1);
 assert(tabs(&c,&e)==APP_REDRAW_PAGE&&documents==2); /* Explicit entry refreshes external changes. */
 for(int i=DOCUMENTS;i<=DOCUMENT_BULK;++i){reset(i);assert(tick_scan(&c)==APP_REDRAW_PAGE&&shelves==1&&documents==1);}
 reset(SHELF);probes=ESP_ERR_NOT_FINISHED;assert(tick_scan(&c)==APP_REDRAW_NONE&&s_scan_pending&&!shelves&&!documents);
 probes=0;c.now_ms+=500;assert(tick_scan(&c)==APP_REDRAW_PAGE&&shelves==1&&!documents);
 reset(SHELF);s_scan_pending=false;strcpy(s_requested_open,"/sdcard/markdown/direct.md");s_requested_open_home=true;
 assert(open_requested_book(&c)&&opens==1&&s_view==READING&&s_reader_return_home&&!documents&&!shelves&&!s_requested_open[0]);
 assert(reader_return(&c,false)==APP_REDRAW_NONE&&c.home&&saves==1&&!documents);
 reset(READING);s_document_mode=true;c.leaf=9;assert(reader_return(&c,false)==APP_REDRAW_PAGE&&s_view==DOCUMENTS&&documents==1&&c.leaf==0&&saves==1);
 reset(READING);assert(reader_return(&c,false)==APP_REDRAW_PAGE&&s_view==SHELF&&returns==1&&!documents);
 puts("PASS real scan/entry/return paths: Books skips MD scan; Docs/Bulk/tab/reentry still scan; pending media retries; direct MD open/Home return stay independent.");
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-demand-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
