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
#include <stdlib.h>
#include <string.h>
enum {SHELF,READING,TOC,MANAGE,BULK,IMPORT,SEARCH,EDIT,DOCUMENTS,DOCUMENT_BULK};
enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,ESP_OK=0,ESP_ERR_NOT_FINISHED=8,READER_PANEL_NONE=0};
typedef int app_redraw_t;
typedef int esp_err_t;
typedef struct {bool touched;} cst836u_touch_t;
typedef struct {int64_t now_ms;int leaf,home;bool released;const cst836u_touch_t *touch;} app_ctx_t;
typedef struct {int unused;} read_pico_sd_info_t;
#define SHELF_PERF_SCOPE(name,...) struct {int a;} name __attribute__((unused))={0}
#define shelf_perf_event(...) ((void)0)
#define BOOK_STORE_PATH_MAX 288
#define DOCUMENT_STORE_CHANGE_LIMIT 8
static int s_view,s_reader_panel,shelves,documents,opens,saves,probes,returns,clears;
static int64_t s_poll_ms,s_cover_next_ms;
static bool s_document_scan_pending,s_document_cache_valid,media_ready=true;
static unsigned s_document_revision,source_revision,s_document_import_revision,import_revision;
static uint32_t s_document_media_epoch,media_epoch;
static unsigned progress_reads,document_sorts;
static size_t s_document_count;
typedef struct { char path[288],name[256],date[11]; uint32_t size; bool selected,cleanup_pending; } document_entry_t;
static document_entry_t *s_documents;
static unsigned first_known_change;
static bool s_text;
static char s_path[288];
static uint32_t s_file_size;
static bool record_store_ready(void){return media_ready;}
static uint32_t record_store_media_epoch(void){return media_epoch;}
static unsigned book_store_revision(void){return source_revision;}
static unsigned document_store_revision(void){return import_revision;}
static bool load_document_progress(document_entry_t *d){assert(d->path[0]);++progress_reads;return true;}
static void sort_documents(void){++document_sorts;}
static void book_title_display(const char *path,char *name,size_t n){snprintf(name,n,"%s",strrchr(path,'/')+1);}
static bool filter_documents(const char *query){assert(!query[0]);return true;}
static esp_err_t document_store_read_entry(const char *path,document_entry_t *d){(void)path;(void)d;return 1;}
static esp_err_t document_store_read_changes(unsigned after,unsigned through,document_entry_t *d,size_t capacity){
 assert(through==import_revision&&through-after<=capacity);
 if(!first_known_change||after+1<first_known_change)return 1;
 for(size_t i=0;i<capacity;++i){
  d[i]=(document_entry_t){.size=7};
  snprintf(d[i].path,sizeof(d[i].path),"/sdcard/markdown/%u.md",after+1+(unsigned)i);
 }
 return ESP_OK;
}
static char s_document_message[64];
static bool s_scan_pending,s_document_mode,s_document_delete_confirm,s_document_clear_confirm,s_document_menu;
static bool s_requested_open_home,s_reader_return_home,open_ok=true;
static char s_requested_open[288];
static int read_pico_sd_get_info(read_pico_sd_info_t *p){(void)p;return probes;}
static void scan_shelf(app_ctx_t *c){(void)c;++shelves;}
static void scan_documents(app_ctx_t *c){
 c->leaf=0;s_document_scan_pending=false;
 s_document_cache_valid=media_ready;
 if(media_ready){
  ++documents;s_document_media_epoch=media_epoch;s_document_revision=source_revision;s_document_import_revision=import_revision;
  free(s_documents);s_documents=malloc(sizeof(*s_documents));assert(s_documents);
  s_documents[0]=(document_entry_t){.path="/sdcard/markdown/direct.md",.size=13};s_document_count=1;
 }
}
static bool open_book(app_ctx_t *c,const char *p){(void)c;assert(p[0]);++opens;if(open_ok)s_view=READING;return open_ok;}
static void copy_text(char *d,size_t n,const char *s){snprintf(d,n,"%s",s);}
static void prepare_covers(app_ctx_t *c){(void)c;}
static void clear_content_search(app_ctx_t *c){(void)c;++clears;}
static void track_ticket_stats(app_ctx_t *c){(void)c;}
static void save_progress(void){++saves;}
static void free_book(void){}
static void ui_nav_request(app_ctx_t *c,int i){assert(!i);c->home=1;}
static void return_to_cached_shelf(app_ctx_t *c){(void)c;++returns;}
'''
for name in ('static void discard_document_cache(void) {',
             'static bool document_cache_same_media(void) {',
             'static bool document_cache_current(void) {',
             'static bool update_cached_document_progress(const char *path,uint32_t size) {',
             'static size_t find_document(',
             'static bool refresh_document_imports(unsigned limit) {',
             'static void ensure_documents(void) {',
             'static void refresh_finished_document(void) {'):
 unit+=block(name)+'\n'
unit+=block('static bool open_requested_book(app_ctx_t* ctx) {')+'\n'
unit+='static app_redraw_t tick_scan(app_ctx_t *ctx){const cst836u_touch_t *touch=ctx->touch;'+block('if (s_document_scan_pending &&')+block('if (s_scan_pending && (s_view == SHELF || s_view == BULK) &&')+'return APP_REDRAW_NONE;}\n'
unit+=block('static app_redraw_t content_tab_action(app_ctx_t *ctx, bool documents) {')+'\n'
unit+=block('static app_redraw_t reader_return(app_ctx_t *ctx, bool home) {')+'\n'
unit+=r'''
static void reset(int view){discard_document_cache();s_view=view;shelves=documents=opens=saves=returns=probes=clears=0;s_poll_ms=0;s_cover_next_ms=0;s_scan_pending=true;media_ready=true;progress_reads=document_sorts=0;s_text=false;s_requested_open[0]=0;s_requested_open_home=s_reader_return_home=false;s_document_mode=false;open_ok=true;first_known_change=0;}
int main(void){
 app_ctx_t c={.now_ms=1000};
 reset(SHELF);assert(tick_scan(&c)==APP_REDRAW_PAGE);assert(shelves==1&&!documents&&!s_scan_pending);
 assert(tick_scan(&c)==APP_REDRAW_NONE&&shelves==1&&!documents);
 c.released=true;
 assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&s_view==DOCUMENTS&&s_document_mode&&!documents&&clears==1&&s_document_scan_pending);
 assert(tick_scan(&c)==APP_REDRAW_NONE&&!documents);
 c.released=false;
 assert(tick_scan(&c)==APP_REDRAW_PAGE&&documents==1&&!s_document_scan_pending);
 assert(content_tab_action(&c,false)==APP_REDRAW_PAGE&&s_view==SHELF&&documents==1);
 assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&documents==1&&clears==3);
 first_known_change=++import_revision;
 assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&documents==1&&s_document_count==2&&!s_document_scan_pending&&progress_reads==1);
 import_revision+=2;
 assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&documents==1&&s_document_scan_pending);
 assert(tick_scan(&c)==APP_REDRAW_PAGE&&documents==1&&s_document_count==4&&progress_reads==3);
 ++source_revision;assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&s_document_scan_pending&&documents==1);
 assert(tick_scan(&c)==APP_REDRAW_PAGE&&documents==2);
 first_known_change=0;
 ++import_revision;assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&s_document_scan_pending&&documents==2);
 assert(tick_scan(&c)==APP_REDRAW_PAGE&&documents==3);
 ++media_epoch;assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&s_document_scan_pending&&documents==3);
 assert(tick_scan(&c)==APP_REDRAW_PAGE&&documents==4);
 media_ready=false;assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&documents==4&&!s_document_cache_valid);
 media_ready=true;assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&s_document_scan_pending&&documents==4);
 assert(tick_scan(&c)==APP_REDRAW_PAGE&&documents==5);
 for(int i=DOCUMENTS;i<=DOCUMENT_BULK;++i){
  reset(i);assert(tick_scan(&c)==APP_REDRAW_NONE&&!shelves&&!documents&&s_scan_pending);
  s_document_scan_pending=true;
  assert(tick_scan(&c)==APP_REDRAW_PAGE&&!shelves&&documents==1&&s_scan_pending);
 }
 reset(SHELF);s_cover_next_ms=c.now_ms+150;
 assert(tick_scan(&c)==APP_REDRAW_NONE&&!shelves);
 c.now_ms+=150;c.released=true;
 assert(tick_scan(&c)==APP_REDRAW_NONE&&!shelves);
 assert(content_tab_action(&c,true)==APP_REDRAW_PAGE&&s_view==DOCUMENTS);
 c.released=false;assert(tick_scan(&c)==APP_REDRAW_PAGE&&!shelves&&documents==1);
 reset(SHELF);probes=ESP_ERR_NOT_FINISHED;assert(tick_scan(&c)==APP_REDRAW_NONE&&s_scan_pending&&!shelves&&!documents);
 probes=0;c.now_ms+=500;assert(tick_scan(&c)==APP_REDRAW_PAGE&&shelves==1&&!documents);
 reset(SHELF);s_scan_pending=false;strcpy(s_requested_open,"/sdcard/markdown/direct.md");s_requested_open_home=true;
 assert(open_requested_book(&c)&&opens==1&&s_view==READING&&s_reader_return_home&&!documents&&!shelves&&!s_requested_open[0]);
 assert(reader_return(&c,false)==APP_REDRAW_NONE&&c.home&&saves==1&&!documents);
 reset(READING);scan_documents(&c);s_document_mode=s_text=true;c.leaf=9;
 strcpy(s_path,s_documents[0].path);s_file_size=17;
 assert(reader_return(&c,false)==APP_REDRAW_PAGE&&s_view==DOCUMENTS&&documents==1&&c.leaf==0&&saves==1);
 assert(progress_reads==1&&document_sorts==1&&s_documents[0].size==17);
 reset(READING);assert(reader_return(&c,false)==APP_REDRAW_PAGE&&s_view==SHELF&&returns==1&&!documents);
 discard_document_cache();
 puts("PASS loading frame before directory work, warm reuse, incremental imports, unknown and media reconciliation, and one-record reader return");
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-demand-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
