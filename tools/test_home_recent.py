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
#include <string.h>
#define BOOK_STORE_PATH_MAX 288
#define ESP_ERR_NOT_FINISHED 7
typedef struct { char path[BOOK_STORE_PATH_MAX]; } home_book_t;
typedef struct { bool present, mounted; } read_pico_sd_info_t;
typedef struct { int unused; } app_ctx_t;
typedef int esp_err_t;
static home_book_t s_current, s_recent[6];
static void refresh_recent_documents(void){}
static void prepare_quote_bitmap(void){}
static bool s_history_open;
static bool s_books_cache_valid,s_cached_sd_present,s_cached_sd_mounted,s_scan_pending,s_home_force_full_once;
static unsigned s_cached_store_revision;
static char s_cached_books_dir[128],s_cached_last_path[BOOK_STORE_PATH_MAX];
static uint32_t s_reading_days[30];
static bool s_reading_days_valid;
static uint32_t s_reading_end_day;
static int s_cover_next,scans,refreshes,probes;
static char latest[BOOK_STORE_PATH_MAX];
static bool book_ticket_recent_days(uint32_t *out,uint32_t *end){(void)out;*end=1;return true;}
static esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){*out=(read_pico_sd_info_t){.present=true,.mounted=true};return 0;}
static unsigned book_store_revision(void){return 7;}
static const char *app_settings_books_dir(void){return "/sdcard/books";}
static bool app_settings_home_full_refresh(void){return false;}
static bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",latest);return latest[0]!=0;}
static void scan_books(void){++scans;snprintf(s_current.path,sizeof(s_current.path),"%s",latest);}
static void refresh_progress(void){++refreshes;}
static void restore_cover_cache(void){}
static void read_pico_sd_start_probe(void){++probes;}
'''
UNIT += function("remember_scan_state") + "\n" + function("on_enter")
UNIT += function("home_heatmap_cell_month") + "\n" + function("home_heatmap_row_month") + "\n"
UNIT += function("home_heatmap_history_index") + "\n" + function("home_heatmap_gray")
UNIT += r'''
int main(void){
    app_ctx_t ctx={0};
    strcpy(latest,"/sdcard/books/old.epub");on_enter(&ctx);
    assert(scans==1&&probes==1&&!strcmp(s_current.path,latest));
    strcpy(latest,"/sdcard/books/new.epub");on_enter(&ctx);
    assert(scans==2&&probes==1&&!s_scan_pending&&!strcmp(s_current.path,latest));
    on_enter(&ctx);
    assert(scans==2&&refreshes==1&&probes==1);
    latest[0]=0;on_enter(&ctx);
    assert(scans==3&&!s_current.path[0]);
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
    assert(home_heatmap_gray(0)==255&&home_heatmap_gray(1)<255);
    unsigned previous=255;
    for(uint32_t sec=1;sec<7200;++sec){
        unsigned gray=home_heatmap_gray(sec);
        assert(gray<=previous&&gray>0x33&&gray%17==0);previous=gray;
    }
    assert(home_heatmap_gray(7200)==0x33&&home_heatmap_gray(UINT32_MAX)==0x33);
    puts("home_heatmap: Monday/Sunday, year boundary, future days and 0/1/120-minute gray levels pass");
    puts("home_recent: latest book invalidates the home cache without an SD reprobe");
}
'''

with tempfile.TemporaryDirectory(prefix="home-recent-") as temp:
    source = Path(temp) / "test.c"
    binary = Path(temp) / "test"
    source.write_text(UNIT, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
