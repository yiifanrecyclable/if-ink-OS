/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOOK_SHELF_SCAN_DEPTH 12
#include "book_store.h"

typedef struct {
    char name[256];
    char author[128];
    char path[BOOK_STORE_PATH_MAX];
    int64_t modified;
    uint32_t size, recent, added;
    uint16_t chapter;
    uint8_t is_flash, has_progress, pct, selected, removed, search_match, pinned;
} book_shelf_entry_t;

typedef struct {
    uint64_t roots, contents;
    uint32_t sequence, order, count, build;
} book_shelf_index_t;

bool book_shelf_index_sequence(uint32_t *sequence);
bool book_shelf_index_fingerprint(uint64_t *fingerprint);
bool book_shelf_index_open(book_shelf_index_t *index, uint64_t roots, uint32_t sequence, uint32_t order);
bool book_shelf_index_page(const book_shelf_index_t *index, uint32_t first, uint32_t count,
                           book_shelf_entry_t *entries);
/// 全量读取时核对各条目文件；目录有无新增项由调用方另行确认。
/// Validate stored files on a full load; callers separately check for new directory entries.
bool book_shelf_index_load(const book_shelf_index_t *index, book_shelf_entry_t *entries);
bool book_shelf_index_save(uint64_t roots, uint64_t contents, uint32_t sequence, uint32_t order, uint32_t count,
                           const book_shelf_entry_t *entries);
bool book_shelf_index_advance(book_shelf_index_t *index, uint32_t sequence);

/// 仅对书根内普通书籍计算顺序无关的目录校验。/ Order-independent check of regular books in the roots.
uint64_t book_shelf_index_file_stamp(const char *path, uint64_t size, int64_t modified);
/// 每次处理一条目录项；返回 0 继续、1 一致、-1 变化或读取失败。
/// Step over one directory entry. Return 0 to continue, 1 on match, -1 on change or read failure.
int book_shelf_index_audit_step(const book_shelf_index_t *index);
void book_shelf_index_audit_stop(void);
bool book_shelf_index_update_page(book_shelf_index_t *index, uint32_t first, uint32_t count,
                                  const book_shelf_entry_t *entries, uint32_t sequence);
