#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real shelf cover reconciliation with scoped file and global media invalidation."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
book = (root / 'main/apps/app_book.c').read_text()

def extract(name):
    start = re.search(r'^static [^\n]+\b' + name + r'\([^;\n]*\) \{', book, re.M)
    assert start, name
    end = start.end()
    depth = 1
    while depth:
        depth += (book[end] == '{') - (book[end] == '}')
        end += 1
    return book[start.start():end]

unit = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "book_index_cache.h"
#define BOOK_ROWS 13
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
typedef struct { int leaf; int nav_request; } app_ctx_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE } app_redraw_t;
static int64_t esp_timer_get_time(void){return 500000;}
enum { MODE_GL16=16 };
static int s_mode;
static EpdRect s_area;
static EpdRect row_rect(int row){return (EpdRect){row%3*20,row/3*30,20,30};}
static EpdRect ui_rect_union(EpdRect first, EpdRect last){
    assert(first.y==last.y);
    first.width=last.x+last.width-first.x;return first;
}
static unsigned painted,batches;
static void render_shelf_cover(app_ctx_t *ctx,int row){(void)ctx;painted|=1u<<row;}
#define SP_SHELF_COVER_BATCH 1
#define SP_SHELF_READER_COVERS 2
static int retained, release_reason;
static void shelf_perf_event(int event,int a,int b,int c,int d) {
    if(event==SP_SHELF_COVER_BATCH){assert(b>0);++batches;return;}
    assert(event==SP_SHELF_READER_COVERS && b>=0 && c>=0);
    retained=a;release_reason=d;
}
typedef struct { char path[288],name[256],author[128]; uint32_t size; time_t modified; bool removed; } shelf_entry_t;
static shelf_entry_t s_shelf[2*BOOK_ROWS];
static int s_visible_count, loads, s_view;
static const shelf_entry_t *shelf_entry_at(int index) {
    return index >= 0 && index < s_visible_count ? &s_shelf[index] : NULL;
}
static bool s_scan_pending;
enum { SHELF, MANAGE, DOCUMENTS };
static const char *ttf_font_path(void) { return "/sdcard/fonts/system.ttf"; }
static int ttf_get_weight(void) { return 400; }
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static size_t free_psram=2*1024*1024, largest_psram=1024*1024;
static size_t heap_caps_get_free_size(int caps){(void)caps;return free_psram;}
static size_t heap_caps_get_largest_free_block(int caps){(void)caps;return largest_psram;}
static int shelf_grid_side(void) { return 3; }
static int shelf_rows(void) { return 9; }
static uint8_t *load_cover_gray(const char *path, const char *title, const char *author,
                               bool decode, bool *pending, unsigned *width, unsigned *height) {
    (void)title; (void)author; (void)width; (void)height;
    assert(decode);
    ++loads;
    *pending = !strcmp(path,"D");
    if (*pending) return NULL;
    uint8_t *bitmap = malloc(1);
    assert(bitmap);
    *bitmap = (uint8_t)*path;
    return bitmap;
}
'''
cover_type = book[book.rindex('typedef struct {', 0, book.index('} shelf_cover_t;')):
                  book.index('static char s_shelf_progress_path[')]
unit += '\n' + cover_type + '\n'
unit += book[book.index('typedef enum { READER_COVERS_OPEN,'):book.index('static bool trim_reader_covers(')]
unit += '\n'.join(extract(name) for name in ('release_spare_covers', 'invalidate_covers', 'trim_reader_covers',
                                              'shelf_cover_identity', 'prepare_covers', 'shelf_cover_tick'))
unit += r'''
static bool s_document_mode, s_reader_return_home;
static int s_reader_panel, page_count = 2;
enum { READER_PANEL_NONE };
static void track_ticket_stats(app_ctx_t *ctx) {(void)ctx;}
static void book_recent_index_flush(void) {}
static void shelf_index_checkpoint(app_ctx_t *ctx,bool allow_large){(void)ctx;(void)allow_large;}
static bool shelf_load_page(int leaf){(void)leaf;return true;}
static void save_progress(void) {}
static void ui_nav_request(app_ctx_t *ctx, int item) {ctx->nav_request = item + 1;}
static void refresh_finished_document(void) {}
static void free_book(void) {}
static void ensure_documents(void) {}
static void return_to_cached_shelf(app_ctx_t *ctx) {ctx->leaf = 0;}
static int leaves(void) {return page_count;}
'''
unit += extract('reader_return')
unit += r'''
int main(void) {
    app_ctx_t ctx = {0};
    s_shelf[0] = (shelf_entry_t){.path="A",.name="A",.size=4,.modified=123};
    s_shelf[1] = (shelf_entry_t){.path="B",.name="B",.size=29,.modified=456};
    s_visible_count = 2;
    prepare_covers(&ctx);
    assert(!loads && s_cover_pending_mask==3);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE && loads==1 && !painted);
    s_view=DOCUMENTS;
    prepare_covers(&ctx);
    assert(loads==1);
    s_view=SHELF;
    prepare_covers(&ctx);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE && loads==2 && !painted);
    assert(s_cover_next_ms==620);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_AREA && painted==3 && batches==1);
    assert(s_mode==MODE_GL16 && s_area.x==0 && s_area.width==40);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE && loads==2);
    uint8_t *a=s_covers[0].gray, *b=s_covers[1].gray;

    s_scan_pending=true;
    s_visible_count=0;
    prepare_covers(&ctx);
    assert(loads==2 && s_covers[0].gray==a && s_covers[1].gray==b);
    s_scan_pending=false;
    s_visible_count=3;
    s_shelf[2]=s_shelf[0];
    s_shelf[0]=(shelf_entry_t){.path="D",.name="D",.size=97,.modified=555};
    prepare_covers(&ctx);
    assert(loads==2 && s_cover_pending_mask==1 && s_covers[2].gray==a && s_covers[1].gray==b);

    book_index_cache_invalidate_file(s_shelf[2].size,s_shelf[2].modified);
    prepare_covers(&ctx);
    assert(loads==2 && s_covers[1].gray==b && s_cover_pending_mask==5);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE && loads==3);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE && loads==4 && *s_covers[2].gray=='A');
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_AREA && batches==2 && painted==7);
    book_index_cache_invalidate_all();
    prepare_covers(&ctx);
    assert(loads==4 && s_cover_pending_mask==7 && !s_covers[1].gray);
    invalidate_covers();
    assert(!s_cover_pending_mask);
    ctx.leaf=0;
    for(int i=0;i<18;++i){
        snprintf(s_shelf[i].path,sizeof(s_shelf[i].path),"E%d",i);
        s_shelf[i].size=(unsigned)i+800;
    }
    s_visible_count=18;
    prepare_covers(&ctx);
    for(int i=0;i<13;++i)(void)shelf_cover_tick(&ctx);
    uint8_t *first_page=s_covers[0].gray;
    assert(first_page&&!s_cover_pending_mask);
    ctx.leaf=1;prepare_covers(&ctx);
    for(int i=0;i<13;++i)(void)shelf_cover_tick(&ctx);
    uint8_t *second_page=s_covers[0].gray;
    assert(second_page&&second_page!=first_page&&!s_cover_pending_mask);
    ctx.leaf=0;prepare_covers(&ctx);
    assert(s_covers[0].gray==first_page&&!s_cover_pending_mask);
    ctx.leaf=1;prepare_covers(&ctx);
    assert(s_covers[0].gray==second_page&&!s_cover_pending_mask);
    const unsigned before_reader_batches=batches;
    const int before_reader_loads=loads;
    release_spare_covers();
    assert(!trim_reader_covers(READER_COVERS_OPEN) && retained==9 && release_reason==0);
    s_reader_shelf_leaf=1;
    ctx.leaf=4; // The reader directory uses its own current page.
    assert(reader_return(&ctx,false)==APP_REDRAW_PAGE && ctx.leaf==1);
    prepare_covers(&ctx);
    assert(s_covers[0].gray==second_page&&!s_cover_pending_mask);
    assert(shelf_cover_tick(&ctx)==APP_REDRAW_NONE);
    assert(loads==before_reader_loads && batches==before_reader_batches);
    s_document_mode=true;
    assert(reader_return(&ctx,false)==APP_REDRAW_PAGE && ctx.leaf==0 && s_view==DOCUMENTS);
    assert(s_covers[0].gray==second_page && loads==before_reader_loads);
    s_document_mode=false;
    page_count=1;
    assert(reader_return(&ctx,false)==APP_REDRAW_PAGE && ctx.leaf==0);
    assert(reader_return(&ctx,true)==APP_REDRAW_NONE && ctx.nav_request==1);
    page_count=2;

    largest_psram--;
    assert(trim_reader_covers(READER_COVERS_CHECK) && !retained && release_reason==1);
    assert(!s_covers[0].gray && !trim_reader_covers(READER_COVERS_RETRY));
    largest_psram++;
    ctx.leaf=1;prepare_covers(&ctx);
    for(int i=0;i<13;++i)(void)shelf_cover_tick(&ctx);
    free_psram--;
    assert(trim_reader_covers(READER_COVERS_OPEN) && !retained && release_reason==1);
    free_psram++;
    prepare_covers(&ctx);
    for(int i=0;i<13;++i)(void)shelf_cover_tick(&ctx);
    assert(trim_reader_covers(READER_COVERS_RETRY) && !retained && release_reason==2);

    ctx.leaf=0;prepare_covers(&ctx);
    for(int i=0;i<13;++i)(void)shelf_cover_tick(&ctx);
    book_index_cache_invalidate_file(s_shelf[0].size,s_shelf[0].modified);
    ctx.leaf=0;prepare_covers(&ctx);
    assert(!s_covers[0].gray && s_cover_pending_mask==1);
    free_psram=0;prepare_covers(&ctx);
    for(int i=0;i<SHELF_SPARE_COVERS;++i)assert(!s_spare_covers[i].gray);
    invalidate_covers();
    puts("PASS covers survive reader return, tab switch and reorder; pressure, allocation retry, replacement and media change release stale covers");
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-reuse-', dir=root / 'build') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'main'), '-I' + str(root / 'main/book'), str(path / 'test.c'),
                    str(root / 'main/book/book_index_cache.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
