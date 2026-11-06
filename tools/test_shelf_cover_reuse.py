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
typedef struct { int leaf; } app_ctx_t;
typedef struct { char path[288],name[256],author[128]; uint32_t size; time_t modified; bool removed; } shelf_entry_t;
static shelf_entry_t s_shelf[BOOK_ROWS];
static int s_visible_count, loads, s_view;
static bool s_scan_pending;
static unsigned s_cover_pending_mask;
enum { SHELF, MANAGE, DOCUMENTS };
static const char *ttf_font_path(void) { return "/sdcard/fonts/system.ttf"; }
static int ttf_get_weight(void) { return 400; }
static int shelf_rows(void) { return 9; }
static uint8_t *load_cover_gray(const char *path, const char *title, const char *author,
                               bool decode, bool *pending, unsigned *width, unsigned *height) {
    (void)title; (void)author; (void)width; (void)height;
    assert(!decode);
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
                  book.index('static int64_t s_cover_next_ms;')]
unit += '\n' + cover_type + '\n'
unit += '\n'.join(extract(name) for name in ('invalidate_covers', 'shelf_cover_identity', 'prepare_covers'))
unit += r'''
int main(void) {
    app_ctx_t ctx = {0};
    s_shelf[0] = (shelf_entry_t){.path="A",.name="A",.size=4,.modified=123};
    s_shelf[1] = (shelf_entry_t){.path="B",.name="B",.size=29,.modified=456};
    s_visible_count = 2;
    prepare_covers(&ctx);
    assert(loads==2);
    uint8_t *a=s_covers[0].gray, *b=s_covers[1].gray;

    s_view=DOCUMENTS;
    prepare_covers(&ctx);
    assert(loads==2);
    s_view=SHELF;
    s_scan_pending=true;
    s_visible_count=0;
    prepare_covers(&ctx);
    assert(loads==2 && s_covers[0].gray==a && s_covers[1].gray==b);
    s_scan_pending=false;
    s_visible_count=3;
    s_shelf[2]=s_shelf[0];
    s_shelf[0]=(shelf_entry_t){.path="D",.name="D",.size=97,.modified=555};
    prepare_covers(&ctx);
    assert(loads==3 && s_cover_pending_mask==1 && s_covers[2].gray==a && s_covers[1].gray==b);

    book_index_cache_invalidate_file(s_shelf[2].size,s_shelf[2].modified);
    prepare_covers(&ctx);
    assert(loads==4 && s_covers[1].gray==b && *s_covers[2].gray=='A');
    prepare_covers(&ctx);
    assert(loads==4 && s_cover_pending_mask==1);
    book_index_cache_invalidate_all();
    prepare_covers(&ctx);
    assert(loads==7);
    invalidate_covers();
    assert(!s_cover_pending_mask);
    puts("PASS loaded covers survive blank scan frame, tab switch and reorder; replacement and media change retire stale covers");
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-reuse-', dir=root / 'build') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'main'), '-I' + str(root / 'main/book'), str(path / 'test.c'),
                    str(root / 'main/book/book_index_cache.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
