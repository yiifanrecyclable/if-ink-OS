#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production shared progress cache with storage and allocation fault injection."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include "book_progress_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned revision = 10, media = 1, walks, locks, disk_count = 2;
static bool fail_allocation, fail_walk;
static const char *book = "/sdcard/books/Test.epub", *doc = "/sdcard/markdown/Test.md";
static book_progress_t disk_book = {.file_size=40,.last_open_s=3,.pct=10};
static book_progress_t disk_doc = {.file_size=50,.last_open_s=4,.pct=20};

void record_store_lock(void) { ++locks; }
void record_store_unlock(void) { assert(locks == 1); --locks; }
unsigned progress_kv_revision(void) { return revision; }
uint32_t record_store_media_epoch(void) { return media; }
void *heap_caps_realloc(void *ptr, size_t bytes, unsigned caps) {
    assert(caps == 3);
    return fail_allocation ? NULL : realloc(ptr, bytes);
}

esp_err_t book_progress_list(book_progress_visit_fn visit, void *ctx) {
    assert(locks == 1);
    ++walks;
    if (disk_count && !visit(book,&disk_book,ctx)) return ESP_OK;
    if (fail_walk) return ESP_FAIL;
    if (disk_count > 1 && !visit(doc,&disk_doc,ctx)) return ESP_OK;
    for (unsigned i=2;i<disk_count;++i) {
        char path[40];
        snprintf(path,sizeof(path),"/sdcard/books/%u.epub",i);
        if (!visit(path,&disk_book,ctx)) break;
    }
    return ESP_OK;
}

typedef struct {unsigned count, books, docs, doc_pct, book_pct, stop;} readout_t;
static bool visit(const char *path,const book_progress_t *value,void *ctx) {
    assert(locks == 1);
    readout_t *view=ctx;
    ++view->count;
    if (!strcmp(path,doc)) {++view->docs;view->doc_pct=value->pct;}
    else {++view->books;view->book_pct=value->pct;}
    return view->count != view->stop;
}
static void known_save(const char *path, const book_progress_t *value) {
    record_store_lock();
    unsigned before=revision;
    revision+=3;
    book_progress_snapshot_note(path,value,before,revision);
    record_store_unlock();
}
static readout_t read(unsigned expect_walks) {
    readout_t result={0};
    assert(book_progress_snapshot_list(visit,&result)==ESP_OK);
    assert(walks==expect_walks&&!locks);
    return result;
}

int main(void) {
    readout_t first=read(1), shelf=read(1);
    assert(first.docs==1&&first.books==1&&shelf.count==2);
    unsigned before=revision;
    disk_book.pct=32;known_save(book,&disk_book);
    disk_book.pct=43;known_save(book,&disk_book);
    book_progress_t delta={0};
    assert(!strcmp(book_progress_snapshot_change(before,revision,&delta),book)&&delta.pct==43);
    shelf=read(1);assert(shelf.doc_pct==20&&shelf.book_pct==43);
    unsigned after_book=revision;
    disk_doc.pct=75;known_save(doc,&disk_doc);
    assert(!book_progress_snapshot_change(before,revision,&delta));
    assert(!strcmp(book_progress_snapshot_change(after_book,revision,&delta),doc)&&delta.pct==75);
    assert(read(1).doc_pct==75);

    ++revision;disk_count=1;
    assert(!book_progress_snapshot_change(after_book,revision,&delta));
    assert(read(2).count==1);
    disk_count=2;known_save(doc,&disk_doc);
    assert(read(2).docs==1);  /* A newly saved path joins the warm snapshot. */
    readout_t stopped={.stop=1};
    assert(book_progress_snapshot_list(visit,&stopped)==ESP_OK&&stopped.count==1);
    assert(read(2).count==2); /* A warm early stop retains the full cached list. */

    ++media;disk_count=1;disk_book.pct=88;
    assert(!book_progress_snapshot_change(after_book,revision,&delta));
    stopped=(readout_t){.stop=1};
    assert(book_progress_snapshot_list(visit,&stopped)==ESP_OK&&walks==3);
    assert(read(4).book_pct==88); /* An early-stopped cold walk never poisons reuse. */
    ++revision;fail_walk=true;
    stopped=(readout_t){0};
    assert(book_progress_snapshot_list(visit,&stopped)==ESP_FAIL&&stopped.count==1&&walks==5);
    fail_walk=false;assert(read(6).count==1);
    ++revision;disk_count=3;fail_allocation=true;
    assert(read(7).count==3&&read(8).count==3);
    fail_allocation=false;assert(read(9).count==3&&read(9).count==3);
    ++revision;disk_count=513;
    assert(read(10).count==513&&read(11).count==513);
    puts("PASS shared progress snapshot: one shared walk, committed single-path updates, known/unknown revision ranges, media change, early stop, corrupt backend, allocation failure and size bound");
}
'''

with tempfile.TemporaryDirectory(prefix='progress-snapshot-', dir=ROOT / 'build') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(UNIT)
    (path / 'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
void *heap_caps_realloc(void *ptr, size_t bytes, unsigned caps);
''')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(path), '-I' + str(ROOT / 'main'), '-I' + str(ROOT / 'main/book'),
                    '-I' + str(ROOT / 'tools/simulator/include'), str(path / 'test.c'),
                    str(ROOT / 'main/book/book_progress_snapshot.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
