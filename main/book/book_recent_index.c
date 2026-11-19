/* SPDX-License-Identifier: Apache-2.0
 * 中文：保存有界的最近阅读摘要。原始逐本进度是唯一权威来源。
 * English: Persist a bounded recent-reading summary. Per-file progress stays authoritative.
 */
#include "book_recent_index.h"
#include "book_recent_disk.h"
#include "book_progress_kv.h"
#include "book_progress_snapshot.h"
#include "book_title.h"
#include "document_store.h"
#include "shelf_perf.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "zlib.h"

#ifdef ESP_PLATFORM
#define lstat stat
#endif

#define RECENT_MAGIC UINT32_C(0x31524649)

typedef struct {
    uint32_t magic, sequence;
    book_recent_view_t view;
    uint32_t checksum;
} recent_file_t;

static recent_file_t *s_file;
static uint32_t s_media;
static unsigned s_progress, s_titles, s_books, s_documents;
static bool s_valid, s_dirty, s_disk_checked;

static uint32_t checksum(const recent_file_t *file) {
    return (uint32_t)crc32(0, (const Bytef *)file, offsetof(recent_file_t, checksum));
}

static bool current_sequence(uint32_t *sequence) {
    progress_kv_handle_t handle;
    if (progress_kv_open(NVS_READONLY, &handle) != ESP_OK) return false;
    *sequence = 0;
    esp_err_t err = progress_kv_get_u32(handle, "seq", sequence);
    progress_kv_close(handle);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

static bool is_book(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt")) &&
           (!strncmp(path, "/sdcard/", 8) || !strncmp(path, "/flash/", 7));
}

static bool current_path(const char *path, uint32_t size) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_size >= 0 && (uint64_t)st.st_size == size;
}

static bool before_entry(const char *path, uint32_t sequence, const book_recent_entry_t *other) {
    return sequence > other->progress.last_open_s ||
           (sequence == other->progress.last_open_s && strcmp(path, other->path) < 0);
}

static void fill_title(book_recent_entry_t *entry) {
    if (!book_title_display(entry->path, entry->title, sizeof(entry->title))) {
        const char *base = strrchr(entry->path, '/') + 1;
        size_t len = strlen(base);
        if (len >= sizeof(entry->title)) {
            len = sizeof(entry->title) - 1;
            while (len && ((unsigned char)base[len] & 0xc0) == 0x80) --len;
        }
        memcpy(entry->title, base, len);
        entry->title[len] = 0;
    }
}

static void insert(const char *path, const book_progress_t *progress, bool update) {
    book_recent_view_t *view = &s_file->view;
    bool book = is_book(path);
    if (!book && !document_store_local_path(path)) return;
    book_recent_entry_t *items = book ? view->books : view->documents;
    uint32_t *count = book ? &view->book_count : &view->document_count;
    uint32_t capacity = book ? BOOK_RECENT_BOOKS : BOOK_RECENT_DOCUMENTS;
    for (uint32_t i = 0; i < *count; ++i) {
        if (strcmp(items[i].path, path)) continue;
        --*count;
        memmove(items + i, items + i + 1, (*count - i) * sizeof(*items));
        break;
    }
    // 书架只有在全部有记录的书都能由摘要覆盖时，才能跳过历史遍历。
    // The shelf can skip the history walk only when the summary covers every recorded book.
    if (book && *count == capacity) view->all_books = 0;
    uint32_t slot = 0;
    while (slot < *count && !before_entry(path, progress->last_open_s, &items[slot])) ++slot;
    if (slot == capacity) return;
    if (!current_path(path, progress->file_size)) {
        if (book) view->all_books = 0;
        return;
    }
    if (*count < capacity) ++*count;
    memmove(items + slot + 1, items + slot, (*count - slot - 1) * sizeof(*items));
    book_recent_entry_t *entry = &items[slot];
    *entry = (book_recent_entry_t){.progress = *progress};
    memcpy(entry->path, path, strlen(path) + 1);
    if (update) fill_title(entry);
}

static bool collect(const char *path, const book_progress_t *progress, void *unused) {
    (void)unused;
    insert(path, progress, false);
    return true;
}

static bool verify_entries(const book_recent_entry_t *items, uint32_t count, bool books, bool cold) {
    for (uint32_t i = 0; i < count; ++i) {
        const book_recent_entry_t *entry = &items[i];
        const book_progress_t *expected = &entry->progress;
        if (!memchr(entry->path, 0, sizeof(entry->path)) || !memchr(entry->title, 0, sizeof(entry->title)) ||
            !(books ? is_book(entry->path) : document_store_local_path(entry->path)) ||
            (i && !before_entry(items[i - 1].path, items[i - 1].progress.last_open_s, entry)) ||
            !current_path(entry->path, expected->file_size))
            return false;
        if (!cold) continue;
        book_progress_t actual;
        if (!book_progress_load(entry->path, expected->file_size, &actual) ||
            expected->chapter != actual.chapter || expected->byte_off != actual.byte_off ||
            expected->px != actual.px || expected->pct != actual.pct || expected->last_open_s != actual.last_open_s)
            return false;
        char title[sizeof(entry->title)];
        if (book_title_display(entry->path, title, sizeof(title)) && strcmp(title, entry->title))
            return false;
    }
    return true;
}

static bool load(uint32_t sequence) {
    FILE *file = fopen(BOOK_RECENT_FILE, "rb");
    if (!file) return false;
    bool ok = fread(s_file, 1, sizeof(*s_file), file) == sizeof(*s_file) && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    const book_recent_view_t *view = &s_file->view;
    return ok && s_file->magic == RECENT_MAGIC && s_file->sequence == sequence &&
           s_file->checksum == checksum(s_file) && view->all_books <= 1 &&
           view->book_count <= BOOK_RECENT_BOOKS && view->document_count <= BOOK_RECENT_DOCUMENTS &&
           verify_entries(view->books, view->book_count, true, true) &&
           verify_entries(view->documents, view->document_count, false, true);
}

static bool rebuild_index(uint32_t sequence) {
    memset(s_file, 0, sizeof(*s_file));
    s_file->magic = RECENT_MAGIC;
    s_file->sequence = sequence;
    s_file->view.all_books = 1;
    if (book_progress_snapshot_list(collect, NULL) != ESP_OK) return false;
    for (uint32_t i = 0; i < s_file->view.book_count; ++i) fill_title(&s_file->view.books[i]);
    for (uint32_t i = 0; i < s_file->view.document_count; ++i) fill_title(&s_file->view.documents[i]);
    s_dirty = true;
    return true;
}

const book_recent_view_t *book_recent_index_get(bool rebuild) {
    record_store_lock();
    const uint32_t media = record_store_media_epoch();
    const unsigned progress = progress_kv_revision(), titles = book_title_revision();
    const unsigned books = book_store_revision(), documents = document_store_revision();
    if (!record_store_ready() || s_media != media || s_progress != progress || s_titles != titles) {
        s_valid = false;
        s_dirty = false;
    }
    if (s_valid && (books != s_books || documents != s_documents)) {
        const book_recent_view_t *view = &s_file->view;
        s_valid = verify_entries(view->books, view->book_count, true, false) &&
                  verify_entries(view->documents, view->document_count, false, false);
        if (!s_valid) s_dirty = false;
    }
    if (!s_valid && record_store_ready()) {
        SHELF_PERF_SCOPE(perf, SP_RECENT_INDEX, 0, 0);
        if (!s_file)
            s_file = heap_caps_calloc(1, sizeof(*s_file), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint32_t sequence;
        if (s_file && (!s_disk_checked || rebuild) && current_sequence(&sequence)) {
            if (!s_disk_checked) {
                s_disk_checked = true;
                s_valid = load(sequence);
                if (s_valid) perf.a = 1;
            }
            if (!s_valid && rebuild) {
                s_valid = rebuild_index(sequence);
                perf.a = s_valid ? 2 : -1;
            }
            if (s_valid) {
                s_media = media;
                s_progress = progress;
                s_titles = titles;
                perf.b = (int)(s_file->view.book_count + s_file->view.document_count);
            }
        }
    }
    s_books = books;
    s_documents = documents;
    record_store_unlock();
    return s_valid ? &s_file->view : NULL;
}

void book_recent_index_note(const char *path, const book_progress_t *progress, unsigned before, unsigned after) {
    if (!s_valid || !record_store_ready() || s_media != record_store_media_epoch() ||
        s_titles != book_title_revision() || s_books != book_store_revision() ||
        s_documents != document_store_revision() || s_progress != before || before == after)
        return;
    // 写入序号应当向前；其他更改会使摘要在下次读取时重建。
    // The committed sequence must advance. Other changes force a rebuild at the next lookup.
    if (progress->last_open_s <= s_file->sequence) return;
    insert(path, progress, true);
    s_file->sequence = progress->last_open_s;
    s_progress = after;
    s_dirty = true;
}

void book_recent_index_flush(void) {
    if (!s_dirty) return;
    record_store_lock();
    if (book_recent_index_get(false)) {
        SHELF_PERF_SCOPE(perf, SP_RECENT_INDEX, 3, 0);
        uint32_t sequence;
        bool ok = current_sequence(&sequence) && sequence == s_file->sequence &&
                  (mkdir("/sdcard/.readpico", 0755) == 0 || errno == EEXIST);
        FILE *file = ok ? fopen(BOOK_RECENT_TEMP, "wb") : NULL;
        ok = file != NULL;
        if (file) {
            s_file->checksum = checksum(s_file);
            if (fwrite(s_file, 1, sizeof(*s_file), file) != sizeof(*s_file)) ok = false;
            if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
            if (fclose(file) != 0) ok = false;
        }
        if (ok) {
            // FAT 重命名前需要释放原目标；若此时掉电，则回退到进度库。
            // FAT requires freeing the existing destination; power loss here falls back to progress records.
            ok = (unlink(BOOK_RECENT_FILE) == 0 || errno == ENOENT) &&
                 rename(BOOK_RECENT_TEMP, BOOK_RECENT_FILE) == 0;
        }
        if (!ok) (void)unlink(BOOK_RECENT_TEMP);
        perf.b = ok ? 1 : 0;
        // 写失败保留本次会话中的摘要，避免空闲轮询持续写卡。
        // Keep the in-session summary on failure without repeatedly writing in idle ticks.
        s_dirty = false;
    }
    record_store_unlock();
}
