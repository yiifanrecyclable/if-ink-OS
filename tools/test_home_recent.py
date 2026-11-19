#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""Check real home cache decisions, calendar alignment and panel heat levels."""
from pathlib import Path
import re
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / "main/apps/app_dashboard.c"


def function(name):
    source = SOURCE.read_text(encoding="utf-8")
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{", source, re.M)
    assert match, name
    start, cursor, depth = match.start(), match.end(), 1
    while depth:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    return source[start:cursor]


UNIT = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#define ui_snprintf snprintf
#include <string.h>
#define BOOK_STORE_PATH_MAX 288
#define ESP_OK 0
#define SHELF_PERF_SCOPE(n,event,a,b_arg) struct { int unused,b; } n __attribute__((unused)) = {0};
#define ESP_ERR_NOT_FINISHED 7
typedef struct { char path[BOOK_STORE_PATH_MAX]; } home_book_t;
typedef struct { bool present, mounted; } read_pico_sd_info_t;
typedef struct {int64_t now_ms;bool pressed,released,consumed;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
typedef enum { HOME_READY, HOME_RECENTS, HOME_BANNER, HOME_BOOKS } home_load_t;
static home_load_t s_home_load;
static bool s_home_prepared_once;
static int64_t s_home_next_ms;
static int64_t esp_timer_get_time(void){return 1000000;}
typedef int esp_err_t;
static home_book_t s_current, s_recent[6];
static void refresh_recent_documents(void);
static bool load_recent_document_summary(void){return false;}
static int banners;
static void prepare_quote_bitmap(void){++banners;}
static bool s_history_open;
static bool s_books_cache_valid,s_cached_sd_present,s_cached_sd_mounted,s_scan_pending,s_home_force_full_once;
static unsigned s_cached_store_revision,s_home_progress_revision,s_home_title_revision;
static unsigned progress_rev,title_rev,doc_rev;
static unsigned progress_kv_revision(void){return progress_rev;}
static unsigned book_title_revision(void){return title_rev;}
static char s_cached_books_dir[128],s_cached_last_path[BOOK_STORE_PATH_MAX];
static uint32_t s_reading_days[30];
static bool s_reading_days_valid;
static uint32_t s_reading_end_day;
static int s_cover_next,scans,refreshes,probes;
static bool card_mounted = true, replace_during_scan;
static uint32_t media_epoch = 1;
static char latest[BOOK_STORE_PATH_MAX];
static bool book_ticket_recent_days(uint32_t *out,uint32_t *end){(void)out;*end=1;return true;}
static esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){
    *out=(read_pico_sd_info_t){.present=true,.mounted=card_mounted};
    return card_mounted ? ESP_OK : ESP_ERR_NOT_FINISHED;
}
static unsigned book_store_revision(void){return 7;}
static uint32_t book_index_cache_epoch(void){return media_epoch;}
static const char *app_settings_books_dir(void){return "/sdcard/books";}
static bool app_settings_home_full_refresh(void){return false;}
static bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",latest);return latest[0]!=0;}
static void scan_books(void){++scans;if(replace_during_scan)++media_epoch;snprintf(s_current.path,sizeof(s_current.path),"%s",latest);}
static void refresh_progress(void){++refreshes;}
static void restore_cover_cache(void){}
static void read_pico_sd_start_probe(void){++probes;}
'''
UNIT += r'''
#define HOME_DOCUMENT_COUNT 6
typedef struct {uint32_t file_size,last_open_s;unsigned pct;} book_progress_t;
typedef struct {char name[128],path[288];uint32_t size,recent;unsigned pct;bool has_progress;} document_entry_t;
static document_entry_t s_recent_documents[HOME_DOCUMENT_COUNT];
static size_t s_recent_document_count;
static bool s_has_documents;
static struct {uint32_t media;unsigned documents,progress,titles,books;bool valid;} s_document_snapshot;
static bool local_docs=true, bad_progress;
static unsigned lookups,walks,stats,titles_read,change_before;
static const char *changed_path;
static book_progress_t changed_value;
static unsigned document_store_revision(void){return doc_rev;}
static bool document_store_has_local(void){++lookups;return local_docs;}
static bool document_store_local_path(const char *p){return !strncmp(p,"/sdcard/markdown/",17);}
static int test_lstat(const char *p, struct stat *s){
    ++stats;s->st_mode=S_IFREG;s->st_size=5;return strstr(p,"missing") ? -1 : 0;
}
#define lstat test_lstat
static void copy_title(char *out,size_t cap,const char *text){snprintf(out,cap,"%s",text);}
static bool book_title_display(const char *p,char *title,size_t cap){(void)p;(void)title;(void)cap;++titles_read;return false;}
static const char *book_progress_snapshot_change(unsigned before,unsigned after,book_progress_t *p){
    if(after!=progress_rev||before!=change_before||!changed_path)return NULL;
    *p=changed_value;return changed_path;
}
static int book_progress_snapshot_list(bool (*cb)(const char*,const book_progress_t*,void*),void *ctx){
    ++walks;
    const book_progress_t p={.file_size=5,.last_open_s=10,.pct=40};
    cb("/sdcard/markdown/First.md",&p,ctx);
    return bad_progress?1:0;
}
'''
UNIT += "\n".join(function(name) for name in ("recent_document_current", "insert_recent_document", "consider_document",
                                             "apply_recent_progress_change", "refresh_recent_documents")) + "\n"
UNIT += "\n".join(function(name) for name in ("remember_scan_state","prepare_home_books","on_enter","finish_home_entry"))
UNIT += function("home_heatmap_cell_month") + "\n" + function("home_heatmap_row_month") + "\n"
UNIT += function("home_heatmap_history_index") + "\n" + function("home_heatmap_gray")
UNIT += r'''
int main(void){
    app_ctx_t ctx={0};
    strcpy(latest,"/sdcard/books/old.epub");on_enter(&ctx);
    assert(s_home_load==HOME_RECENTS&&!scans&&!lookups&&!banners);
    s_home_next_ms=1180;ctx.now_ms=1179;
    assert(finish_home_entry(&ctx)==APP_REDRAW_NONE&&!lookups);
    ctx.now_ms=1180;ctx.pressed=true;
    assert(finish_home_entry(&ctx)==APP_REDRAW_NONE&&!lookups);
    ctx.pressed=false;card_mounted=false;
    assert(finish_home_entry(&ctx)==APP_REDRAW_NONE&&!lookups);
    card_mounted=true;
    assert(finish_home_entry(&ctx)==APP_REDRAW_NONE&&lookups==1&&!banners&&!scans);
    assert(finish_home_entry(&ctx)==APP_REDRAW_NONE&&banners==1&&!scans);
    assert(finish_home_entry(&ctx)==APP_REDRAW_PAGE&&s_home_prepared_once&&s_home_load==HOME_READY);
    assert(scans==1&&probes==1&&!s_scan_pending&&!strcmp(s_current.path,latest));
    strcpy(latest,"/sdcard/books/new.epub");++progress_rev;on_enter(&ctx);
    assert(scans==2&&probes==1&&!s_scan_pending&&!strcmp(s_current.path,latest));
    on_enter(&ctx);
    assert(scans==2&&refreshes==0&&probes==1);
    ++progress_rev;on_enter(&ctx);
    assert(scans==2&&refreshes==1&&probes==1);
    latest[0]=0;++progress_rev;on_enter(&ctx);
    assert(scans==3&&!s_current.path[0]);

    s_books_cache_valid=false;card_mounted=false;on_enter(&ctx);
    assert(scans==4&&s_scan_pending&&!s_books_cache_valid);
    card_mounted=true;replace_during_scan=true;on_enter(&ctx);
    assert(scans==5&&s_scan_pending&&!s_books_cache_valid);
    replace_during_scan=false;on_enter(&ctx);
    assert(scans==6&&!s_scan_pending&&s_books_cache_valid);
    ++title_rev;on_enter(&ctx);
    assert(scans==7);
    unsigned found=lookups, lists=walks;
    on_enter(&ctx);
    assert(lookups==found && walks==lists && s_recent_document_count==1);
    ++doc_rev;on_enter(&ctx);
    assert(lookups==found+1 && walks==lists+1);
    ++progress_rev;bad_progress=true;on_enter(&ctx);
    assert(lookups==found+2 && walks==lists+2 && !s_recent_document_count);
    bad_progress=false;on_enter(&ctx);
    assert(lookups==found+3 && walks==lists+3 && s_recent_document_count==1);
    ++media_epoch;local_docs=false;on_enter(&ctx);
    assert(lookups==found+4 && walks==lists+3 && !s_has_documents);

    local_docs=true;refresh_recent_documents();
    unsigned before_walk=walks,before_look=lookups,before_stat=stats,before_title=titles_read;
    change_before=progress_rev++;changed_path="/sdcard/books/New.epub";
    refresh_recent_documents();
    assert(walks==before_walk&&lookups==before_look&&stats==before_stat&&titles_read==before_title);
    assert(s_recent_document_count==1&&!strcmp(s_recent_documents[0].name,"First.md"));
    change_before=progress_rev++;changed_path="/sdcard/markdown/Second.md";
    changed_value=(book_progress_t){.file_size=5,.last_open_s=20,.pct=76};
    refresh_recent_documents();
    assert(walks==before_walk&&lookups==before_look&&stats==before_stat+1&&titles_read==before_title+1);
    assert(s_recent_document_count==2&&!strcmp(s_recent_documents[0].name,"Second.md")&&s_recent_documents[0].pct==76);
    change_before=progress_rev++;changed_value.pct=82;
    refresh_recent_documents();
    assert(walks==before_walk&&s_recent_document_count==2&&s_recent_documents[0].pct==82);
    change_before=progress_rev++;changed_value.last_open_s=5;
    refresh_recent_documents();assert(walks==++before_walk&&s_recent_document_count==1);
    change_before=progress_rev++;changed_path="/sdcard/markdown/missing.md";
    refresh_recent_documents();assert(walks==++before_walk&&s_recent_document_count==1);
    /* 2026-10-05 Monday through Sunday, including future days and all 30-day edges. */
    for(uint32_t day=20731;day<20738;++day){
        int today=(day+3)%7;
        assert(home_heatmap_history_index(day,3,today)==29);
        for(int row=0;row<4;++row)for(int col=0;col<7;++col){
            int index=home_heatmap_history_index(day,row,col);
            if(row==3&&col>today)assert(index==-1);
            else{assert(index>=2&&index<30);assert(((day-(29-index)+3)%7)==(uint32_t)col);}
        }
    }
    /* Same weekday mapping across a year boundary, 2025-12-29 to 2026-01-04. */
    for(uint32_t day=20451;day<20458;++day){
        int today=(day+3)%7;
        assert(home_heatmap_history_index(day,3,today)==29);
        assert(home_heatmap_history_index(day,0,0)==29-21-today);
    }
    assert(home_heatmap_cell_month(20733,2,3)==9);
    assert(home_heatmap_cell_month(20733,2,2)==-1);
    assert(home_heatmap_cell_month(20451,3,3)==0); /* Upcoming January 1. */
    assert(home_heatmap_cell_month(20457,3,3)==0); /* Same cell once in the past. */
    assert(home_heatmap_row_month(20733,2)==9);
    assert(home_heatmap_row_month(20733,0)==-1);
    assert(home_heatmap_row_month(20457,3)==0);
    assert(home_heatmap_row_month(0,3)==-1);
    assert(home_heatmap_history_index(0,0,0)==-1);
    assert(home_heatmap_history_index(20731,4,0)==-1);
    for(unsigned minutes=30;minutes<=180;minutes+=30){
        assert(home_heatmap_gray(0,minutes)==255&&home_heatmap_gray(1,minutes)<255);
        unsigned previous=255;
        for(uint32_t sec=1;sec<minutes*60;++sec){
            unsigned gray=home_heatmap_gray(sec,minutes);
            assert(gray<=previous&&gray>0x33&&gray%17==0);previous=gray;
        }
        assert(home_heatmap_gray(minutes*60,minutes)==0x33&&home_heatmap_gray(UINT32_MAX,minutes)==0x33);
    }
    assert(home_heatmap_gray(5399,90)>0x33&&home_heatmap_gray(5400,90)==0x33);
    assert(home_heatmap_gray(5400,0)==home_heatmap_gray(5400,90));
    puts("home_heatmap: calendar boundaries, future dates and all six target scales passed");
    puts("home_recent: stable mount skips duplicate scan, pending or changed media defers, and latest book changes still rescan");
}
'''

with tempfile.TemporaryDirectory(prefix="home-recent-") as temp:
    source = Path(temp) / "test.c"
    binary = Path(temp) / "test"
    source.write_text(UNIT, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
