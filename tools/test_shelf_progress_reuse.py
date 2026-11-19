#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual shelf fast path and its fallbacks against a local fixture."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/apps/app_book.c").read_text()

def extract(name):
    match = re.search(r"^static [^;\n]+\b" + name + r"\([^;\n]*\) \{", SOURCE, re.M)
    assert match, name
    at, depth = match.end(), 1
    while depth:
        depth += (SOURCE[at] == "{") - (SOURCE[at] == "}")
        at += 1
    return SOURCE[match.start():at]

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define BOOK_STORE_PATH_MAX 288
#define SHELF_HIDDEN_NS "hidden"
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
typedef int esp_err_t;
typedef int records_handle_t;
enum {NVS_READONLY=0};
typedef struct {int leaf;} app_ctx_t;
typedef struct {uint8_t pct;uint16_t chapter;uint32_t last_open_s;} book_progress_t;
typedef struct {char path[288];uint32_t size;time_t modified;bool removed,has_progress;
                unsigned pct,chapter,recent;} shelf_entry_t;
typedef struct {char path[288];book_progress_t value;bool dirty,progress_saved;} pending_progress_t;
typedef struct {records_handle_t hidden;bool truncated;} shelf_backfill_t;
static shelf_entry_t s_shelf[3];
static int s_count, scans, reads, sorts, locks;
static bool fail_save, s_shelf_progress_valid=true;
static bool s_shelf_index_dirty;
static unsigned revision,s_shelf_progress_revision;
static char s_latest_path[288],s_shelf_progress_path[288];
static unsigned progress_kv_revision(void){return revision;}
static void copy_text(char *out,size_t cap,const char *path){snprintf(out,cap,"%s",path);}
static void record_store_lock(void){++locks;}
static void record_store_unlock(void){assert(locks==1);--locks;}
static unsigned snapshot_notes;
static int book_progress_save_with_sequence(const char *path,const book_progress_t *p,uint32_t *seq){
    assert(locks==1&&path&&p);revision+=2;if(!fail_save)*seq=123;return fail_save?-1:0;
}
static void book_recent_index_note(const char *path,const book_progress_t *p,unsigned before,unsigned after) {
    (void)path;(void)p;(void)before;(void)after;
}
static void book_progress_snapshot_note(const char *path,const book_progress_t *p,unsigned before,unsigned after){
    assert(locks==1&&path&&p->last_open_s==123&&after==revision&&before<after);++snapshot_notes;
}
static int book_progress_set_last_path(const char *path){assert(locks==1&&path);++revision;return 0;}
static bool book_progress_load(const char *path,uint32_t size,book_progress_t *p){
    assert(path&&size==3);++reads;*p=(book_progress_t){.pct=65,.chapter=2,.last_open_s=123};return true;
}
static void scan_shelf(app_ctx_t *c){
    (void)c;++scans;s_shelf_progress_revision=revision;s_shelf_progress_valid=true;s_shelf_progress_path[0]=0;
}
static void sort_shelf(app_ctx_t *c){c->leaf=0;++sorts;}
static int records_open(const char *ns,int mode,records_handle_t *h){(void)ns;(void)mode;*h=1;return 0;}
static void records_close(records_handle_t h){assert(h==1);}
static bool shelf_backfill_visit(const char *path,const book_progress_t *p,void *context){
    assert(((shelf_backfill_t*)context)->hidden==1);
    copy_text(s_shelf[s_count].path,sizeof(s_shelf[s_count].path),path);
    s_shelf[s_count++].pct=p->pct;return true;
}
'''
unit += "\n".join(extract(name) for name in ("pending_flush", "apply_cached_progress", "refresh_cached_progress"))
unit += r'''
int main(void){
    FILE *f=fopen("A.epub","wb");assert(f&&fputs("abc",f)>=0&&!fclose(f));
    f=fopen("B.txt","wb");assert(f&&fputs("abc",f)>=0&&!fclose(f));
    s_count=1;strcpy(s_shelf[0].path,"A.epub");s_shelf[0].size=3;
    struct stat st;assert(!stat("A.epub",&st));s_shelf[0].modified=st.st_mtime;
    pending_progress_t a={.path="A.epub",.dirty=true},b={.path="B.txt",.dirty=true};
    app_ctx_t c={0};
    refresh_cached_progress(&c);assert(!reads&&!scans&&!sorts);
    strcpy(s_latest_path,a.path);assert(pending_flush(&a)&&!a.dirty);
    a.dirty=true;a.progress_saved=false;assert(pending_flush(&a));
    refresh_cached_progress(&c);assert(reads==1&&!scans&&sorts==1&&s_shelf[0].pct==65&&s_shelf[0].recent==123);
    refresh_cached_progress(&c);assert(reads==1&&!scans);
    strcpy(s_latest_path,b.path);assert(pending_flush(&b));
    refresh_cached_progress(&c);assert(reads==2&&!scans&&s_count==2&&s_shelf[1].pct==65);
    ++revision;refresh_cached_progress(&c);assert(scans==1&&reads==2);
    fail_save=true;a.dirty=true;a.progress_saved=false;assert(!pending_flush(&a)&&a.dirty);
    assert(!locks&&!s_shelf_progress_valid&&snapshot_notes==3);
    refresh_cached_progress(&c);assert(scans==2&&reads==2);
    fail_save=false;a.dirty=true;b.dirty=true;b.progress_saved=false;
    assert(pending_flush(&a)&&pending_flush(&b)&&!s_shelf_progress_valid);
    refresh_cached_progress(&c);assert(scans==3&&reads==2);
    puts("PASS unchanged shelf, one saved book, out-of-root entry, external revisions, failed save and multiple paths");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="shelf-progress-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(unit)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], cwd=path, check=True)
