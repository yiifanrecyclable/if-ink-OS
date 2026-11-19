/* SPDX-License-Identifier: Apache-2.0
 * 中文：缓存已经校验的进度列表，存储失败时保留原遍历语义。
 * English: Cache validated progress records while preserving the original walk on failure.
 */
#include "book_progress_snapshot.h"
#include "book_progress_kv.h"
#include "book_store.h"
#include "record_store.h"
#include "shelf_perf.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define SNAPSHOT_LIMIT 512u

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    book_progress_t progress;
} entry_t;

static entry_t* s_entries;
static size_t s_count, s_capacity;
static unsigned s_revision;
static uint32_t s_media;
static bool s_valid;
static struct {
    entry_t entry;
    unsigned before, after;
    uint32_t media;
} s_change;

static void discard(void) {
    free(s_entries);
    s_entries = NULL;
    s_count = s_capacity = 0;
    s_valid = false;
}

static bool append(const char* path, const book_progress_t* progress) {
    if (s_count == s_capacity) {
        if (s_capacity == SNAPSHOT_LIMIT) return false;
        size_t capacity = s_capacity ? s_capacity * 2 : 32;
        entry_t* entries = heap_caps_realloc(s_entries, capacity * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!entries) return false;
        s_entries = entries;
        s_capacity = capacity;
    }
    memcpy(s_entries[s_count].path, path, strlen(path) + 1);
    s_entries[s_count++].progress = *progress;
    return true;
}

typedef struct {
    book_progress_visit_fn visit;
    void* ctx;
    bool collect;
} walk_t;

static bool collect_entry(const char* path, const book_progress_t* progress, void* ctx) {
    walk_t* walk = ctx;
    if (walk->collect && !append(path, progress)) {
        discard();
        walk->collect = false;
    }
    if (walk->visit(path, progress, walk->ctx)) return true;
    walk->collect = false;
    return false;
}

esp_err_t book_progress_snapshot_list(book_progress_visit_fn visit, void* ctx) {
    if (!visit) return ESP_ERR_INVALID_ARG;
    SHELF_PERF_SCOPE(perf, SP_PROGRESS_SNAPSHOT, 0, 0);
    record_store_lock();
    const unsigned revision = progress_kv_revision();
    const uint32_t media = record_store_media_epoch();
    esp_err_t err = ESP_OK;
    if (s_valid && media == s_media && revision == s_revision) {
        perf.a = 1;
        for (size_t i = 0; i < s_count; ++i)
            if (!visit(s_entries[i].path, &s_entries[i].progress, ctx)) break;
    } else {
        discard();
        walk_t walk = {.visit = visit, .ctx = ctx, .collect = true};
        err = book_progress_list(collect_entry, &walk);
        if (err == ESP_OK && walk.collect && revision == progress_kv_revision() && media == record_store_media_epoch()) {
            s_revision = revision;
            s_media = media;
            s_valid = true;
        } else {
            perf.a = 2;
            discard();
        }
    }
    perf.b = (int)s_count;
    record_store_unlock();
    return err;
}

void book_progress_snapshot_note(const char* path, const book_progress_t* progress, unsigned before, unsigned after) {
    if (before >= after) return;
    const uint32_t media = record_store_media_epoch();
    if (s_change.media != media || s_change.after != before || strcmp(s_change.entry.path, path)) {
        s_change.before = before;
        memcpy(s_change.entry.path, path, strlen(path) + 1);
    }
    s_change.entry.progress = *progress;
    s_change.after = after;
    s_change.media = media;

    if (!s_valid || s_media != media || s_revision != before) return;
    for (size_t i = 0; i < s_count; ++i) {
        if (strcmp(s_entries[i].path, path)) continue;
        s_entries[i].progress = *progress;
        s_revision = after;
        return;
    }
    if (append(path, progress)) s_revision = after;
    else discard();
}

const char* book_progress_snapshot_change(unsigned before, unsigned after, book_progress_t* progress) {
    if (s_change.media != record_store_media_epoch() || s_change.after != after ||
        after != progress_kv_revision() || before < s_change.before || before >= after) return NULL;
    *progress = s_change.entry.progress;
    return s_change.entry.path;
}
