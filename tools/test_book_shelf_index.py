#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check direct page reads, nested reconciliation and interrupted writes with the production catalog."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "book_progress_kv.h"
#include "book_shelf_index.h"
#include "book_shelf_disk.h"

static const uint32_t count = 1203;
static uint32_t sequence = 23;
bool record_store_ready(void) {return true;}
esp_err_t book_store_roots(book_store_root_t out[BOOK_STORE_ROOT_MAX], int *n) {
    out[0]=(book_store_root_t){.path="/sdcard/books"};
    *n=1;
    return ESP_OK;
}
bool book_store_roots_degraded(void) {return false;}
esp_err_t progress_kv_open(int mode, progress_kv_handle_t *out) {
    assert(mode==NVS_READONLY); *out=(progress_kv_handle_t){.sd=true}; return ESP_OK;
}
void progress_kv_close(progress_kv_handle_t h) {assert(h.sd);}
esp_err_t progress_kv_get_u32(progress_kv_handle_t h, const char *key, uint32_t *out) {
    assert(h.sd && !strcmp("seq",key)); *out=sequence; return ESP_OK;
}

static void read_page(const book_shelf_index_t *index, uint32_t offset, uint32_t n) {
    book_shelf_entry_t page[9];
    assert(book_shelf_index_page(index,offset,n,page));
    for (uint32_t i=0;i<n;++i) {
        char name[32];
        snprintf(name,sizeof(name),"Book %04u",offset+i);
        assert(!strcmp(name,page[i].name) && page[i].pct == (offset+i)%101);
    }
}

int main(int argc,char **argv) {
    assert(argc==2);
    const char *cmd=argv[1];
    uint64_t roots;
    uint32_t seq;
    assert(book_shelf_index_fingerprint(&roots) && book_shelf_index_sequence(&seq) && seq==23);
    book_shelf_index_t index;
    if (!strcmp(cmd,"seed") || !strcmp(cmd,"write-failure")) {
        book_shelf_entry_t *books=calloc(count,sizeof(*books));
        assert(books);
        uint64_t contents=0;
        for (uint32_t i=0;i<count;++i) {
            book_shelf_entry_t *book=&books[i];
            snprintf(book->path,sizeof(book->path),"/sdcard/books/sub/%04u.txt",i);
            snprintf(book->name,sizeof(book->name),"Book %04u",i);
            struct stat st;
            assert(!stat(book->path,&st));
            book->size=st.st_size;book->modified=st.st_mtime;
            book->pct=i%101;book->has_progress=1;book->search_match=1;
            contents+=book_shelf_index_file_stamp(book->path,book->size,book->modified);
        }
        bool failing=!strcmp(cmd,"write-failure");
        assert(book_shelf_index_save(roots,contents,failing?24:seq,3,count,books)!=failing);
        free(books);
    } else if (!strcmp(cmd,"header-miss")) {
        assert(!book_shelf_index_open(&index,roots,seq,3));
    } else {
        assert(book_shelf_index_open(&index,roots,seq,3) && index.count==count);
        assert(!book_shelf_index_open(&index,roots,seq+1,3));
        assert(!book_shelf_index_open(&index,roots,seq,5));
        assert(!book_shelf_index_open(&index,roots+1,seq,3));
        read_page(&index,0,9);read_page(&index,9,9);
        read_page(&index,4,4);
        if (!strcmp(cmd,"remote-missing")) {
            book_shelf_entry_t page[9];
            assert(!book_shelf_index_page(&index,1197,6,page));
        } else if (!strcmp(cmd,"row-damage")) {
            book_shelf_entry_t page[9];
            assert(!book_shelf_index_page(&index,1197,6,page));
        } else if (!strcmp(cmd,"patch") || !strcmp(cmd,"patch-fault")) {
            book_shelf_entry_t page[9];
            assert(book_shelf_index_page(&index,9,9,page));
            page[0].pct=100;
            bool fail=!strcmp(cmd,"patch-fault");
            assert(book_shelf_index_update_page(&index,9,9,page,24)!=fail);
            assert(book_shelf_index_open(&index,roots,24,3)!=fail);
            if (!fail) {
                assert(book_shelf_index_page(&index,9,9,page) && page[0].pct==100);
                read_page(&index,0,9);
            }
        } else if (!strcmp(cmd,"advance")) {
            assert(book_shelf_index_advance(&index,24));
            assert(!book_shelf_index_open(&index,roots,seq,3));
            assert(book_shelf_index_open(&index,roots,24,3));
        } else {
            read_page(&index,1197,6);
            int done=0;
            for (unsigned i=0;!done && i<count+32;++i)
                done=book_shelf_index_audit_step(&index);
            assert(done==(!strcmp(cmd,"nested-added")?-1:1));
            book_shelf_index_audit_stop();
        }
    }
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="shelf-index-", dir=ROOT / "build") as directory:
    work = Path(directory)
    card = work / "card"
    books = card / "books/sub"
    books.mkdir(parents=True)
    for i in range(1203):
        (books / f"{i:04}.txt").write_text("body")
    source = work / "test.c"
    source.write_text(TEST)
    binary = work / "test"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    '-DPICO_SIM_CARD_PATH="unused"', "-include", "tools/simulator/fs.h",
                    "-Itools/simulator/include", "-Imain", "-Imain/book",
                    str(source), "main/book/book_shelf_index.c", "tools/simulator/fs.c",
                    "-lz", "-o", str(binary)], cwd=ROOT, check=True)
    env = {**os.environ, "PICO_SIM_CARD_ROOT": str(card)}

    def run(cmd, **extra):
        subprocess.run([str(binary), cmd], cwd=work, env={**env, **extra}, check=True)

    catalog = card / ".readpico/shelf.ifx"
    run("header-miss")
    run("seed")
    run("match")
    saved = catalog.read_bytes()
    run("write-failure", PICO_TEST_FSYNC_FAIL="1")
    assert catalog.read_bytes() == saved
    (books / "new.txt").write_text("new")
    run("nested-added")
    (books / "new.txt").unlink()
    (books / "1202.txt").unlink()
    run("remote-missing")
    damaged = bytearray(saved)
    damaged[-10] ^= 1
    catalog.write_bytes(damaged)
    (books / "1202.txt").write_text("body")
    run("row-damage")
    catalog.write_bytes(saved + b"x")
    run("header-miss")
    catalog.write_bytes(saved)
    run("patch-fault", PICO_TEST_FSYNC_FAIL="1")
    catalog.write_bytes(saved)
    run("patch")
    run("header-miss")
    catalog.write_bytes(saved)
    run("advance")
    run("header-miss")
    print("PASS: 1,203 books; first, second, four-cell and tail pages; nested edits; sequence/sort/root fences; isolated row damage; failed write retains prior index")
