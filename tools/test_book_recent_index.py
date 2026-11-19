#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real recent-reading file and authoritative SD progress across cold starts."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_progress_kv.h"
#include "book_progress_snapshot.h"
#include "book_recent_index.h"
#include "book_title.h"
#include "shelf_perf.h"

static int walks, commits;
static unsigned documents;
unsigned book_store_revision(void) {return 0;}
unsigned document_store_revision(void) {return documents;}
bool document_store_local_path(const char *path) {return !strncmp(path,"/sdcard/markdown/",17);}

shelf_perf_span_t shelf_perf_begin(shelf_perf_stage_t stage,int a,int b) {
    if (stage == SP_PROGRESS_SNAPSHOT) ++walks;
    if (stage == SP_RECENT_INDEX && a == 3) ++commits;
    return (shelf_perf_span_t){.stage=stage,.a=a,.b=b};
}
void shelf_perf_end(shelf_perf_span_t *span) {(void)span;}

static const char *book="/sdcard/books/b1.epub";
static const char *doc="/sdcard/markdown/d1.md";
static const char *moved="/sdcard/elsewhere/moved.epub";
static void write_progress(const char *path, unsigned percentage, bool notify) {
    book_progress_t progress={.file_size=5,.chapter=1,.byte_off=3,.px=48,.pct=percentage};
    record_store_lock();
    unsigned before=progress_kv_revision();
    assert(book_progress_save_with_sequence(path,&progress,&progress.last_open_s)==ESP_OK);
    assert(book_progress_set_last_path(path)==ESP_OK);
    if (notify) {
        unsigned after=progress_kv_revision();
        book_progress_snapshot_note(path,&progress,before,after);
        book_recent_index_note(path,&progress,before,after);
    }
    record_store_unlock();
}

int main(int argc,char **argv) {
    assert(argc == 2);
    record_store_media(true);
    const char *command=argv[1];
    if (!strcmp(command,"seed")) {
        for(int i=2;i>=1;--i) {
            char path[64];snprintf(path,sizeof(path),"/sdcard/books/b%d.epub",i);
            write_progress(path,11+i,false);
        }
        for(int i=7;i>=1;--i) {
            char path[64];snprintf(path,sizeof(path),"/sdcard/markdown/d%d.md",i);
            write_progress(path,21+i,false);
        }
        assert(book_title_set(book,"Book One")==ESP_OK);
        const book_recent_view_t *view=book_recent_index_get(true);
        assert(view && view->all_books && view->book_count==2 && view->document_count==6);
        assert(!strcmp(view->books[0].path,book) && !strcmp(view->books[0].title,"Book One"));
        assert(!strcmp(view->documents[0].path,doc));
        assert(!strcmp(view->documents[5].title,"d6") && walks==1);
        book_recent_index_flush();
        book_recent_index_flush();
        assert(commits==1);
    } else if (!strcmp(command,"note")) {
        assert(book_recent_index_get(false) && walks==0);
        write_progress("/sdcard/markdown/d7.md",66,true);
        write_progress(book,80,true);
        const book_recent_view_t *view=book_recent_index_get(false);
        assert(view && view->documents[0].progress.pct==66 && view->books[0].progress.pct==80);
        assert(commits==0 && walks==0);
        book_recent_index_flush();
        assert(commits==1 && walks==0);
    } else if (!strcmp(command,"warm-change")) {
        const book_recent_view_t *view=book_recent_index_get(false);
        assert(view && walks==0);
        ++documents;
        assert(book_recent_index_get(true) && walks==0);
        assert(unlink(doc)==0);
        ++documents;
        assert(!book_recent_index_get(false));
        view=book_recent_index_get(true);
        assert(view && view->document_count==6 && walks==1);
        for(uint32_t i=0;i<view->document_count;++i) assert(strcmp(view->documents[i].path,doc));
        book_recent_index_flush();
    } else if (!strcmp(command,"overflow")) {
        assert(book_recent_index_get(false));
        for(int i=3;i<=11;++i) {
            char path[64];snprintf(path,sizeof(path),"/sdcard/books/b%d.epub",i);
            write_progress(path,44,true);
        }
        const book_recent_view_t *view=book_recent_index_get(false);
        assert(view && !view->all_books && view->book_count==9 && walks==0);
        assert(!strcmp(view->books[0].title,"b11") && !strcmp(view->books[8].title,"b3"));
        book_recent_index_flush();
    } else if (!strcmp(command,"miss")) {
        assert(!book_recent_index_get(false) && walks==0);
        const book_recent_view_t *view=book_recent_index_get(true);
        assert(view && walks==1);
        book_recent_index_flush();
    } else if (!strcmp(command,"hit")) {
        assert(book_recent_index_get(false) && walks==0);
    } else if (!strcmp(command,"move")) {
        assert(book_progress_copy_for_move(book,moved,5)==ESP_OK);
        assert(book_progress_forget(book)==ESP_OK);
        book_progress_t p;
        assert(book_progress_load(moved,5,&p) && p.pct==80);
    } else if (!strcmp(command,"clear")) {
        assert(book_progress_clear(moved)==ESP_OK);
    } else if (!strcmp(command,"stale")) {
        write_progress(book,45,false);
    } else if (!strcmp(command,"rename-title")) {
        assert(book_title_set("/sdcard/books/b11.epub","Renamed book")==ESP_OK);
    } else if (!strcmp(command,"media")) {
        assert(book_recent_index_get(false));
        record_store_media(false);
        assert(!book_recent_index_get(true));
        record_store_media(true);
        assert(!book_recent_index_get(false));
        assert(book_recent_index_get(true) && walks==1);
    } else {
        assert(false);
    }
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="recent-index-", dir=ROOT / "build") as work:
    base = Path(work)
    card = base / "card"
    for folder, names in (("books", [f"b{i}.epub" for i in range(1, 12)]),
                          ("markdown", [f"d{i}.md" for i in range(1, 8)]),
                          ("elsewhere", ["moved.epub"])):
        (card / folder).mkdir(parents=True)
        for name in names:
            (card / folder / name).write_text("12345")
    (base / "test.c").write_text(TEST)
    sources = ("book_recent_index", "book_progress_snapshot", "book_progress",
               "book_progress_kv", "record_store", "book_title")
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-fsanitize=address,undefined", '-DPICO_SIM_CARD_PATH="unused"', "-DSHELF_PERF_HOST_TEST",
                    "-include", "tools/simulator/fs.h", "-Itools/simulator/include", "-Imain", "-Imain/book",
                    str(base / "test.c"), *(f"main/book/{source}.c" for source in sources),
                    "tools/simulator/fs.c", "tools/simulator/nvs.c", "-lpthread", "-lz", "-o", str(base / "test")],
                   cwd=ROOT, check=True)
    env = {**os.environ, "PICO_SIM_CARD_ROOT": str(card)}

    def run(*commands, **extra):
        for command in commands:
            subprocess.run([str(base / "test"), command], cwd=base, env={**env, **extra}, check=True)

    cache = card / ".readpico/recent-reading.ifr"
    run("seed", "hit", "note", "hit", "overflow", "hit", "warm-change", "hit")
    assert cache.stat().st_size < 9 * 1024
    run("media", "rename-title", "miss", "hit")
    run("move")
    assert not cache.exists()
    run("miss", "hit", "clear", "miss", "hit", "stale")
    run("miss", PICO_TEST_FSYNC_FAIL="1")
    run("miss", "hit")
    original = cache.read_bytes()
    cache.write_bytes(original + b"\x00")
    run("miss", "hit")
    cache.write_bytes(original[:-1] + bytes((original[-1] ^ 1,)))
    run("miss", "hit")
    print("PASS: recent-index cold hit, 9+6 ordering, committed-only updates, no repeated flush, oversize fallback, move/clear/title/media invalidation, failed sync, extra bytes and CRC rejection")
