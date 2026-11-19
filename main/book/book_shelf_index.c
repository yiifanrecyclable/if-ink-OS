/* SPDX-License-Identifier: Apache-2.0
 * 中文：用定长行记录和独立校验支持书架随机分页；文件可随时从目录重建。
 * English: Fixed-size, independently checked rows support random-access shelf pages.
 */
#include "book_shelf_index.h"
#include "book_shelf_disk.h"
#include "book_progress_kv.h"
#include "shelf_perf.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "zlib.h"

#define SHELF_MAGIC UINT32_C(0x31584649)
#define SHELF_INDEX_LIMIT 32768u
#define SHELF_INDEX_BLOCK 9u

typedef struct {
    book_shelf_index_t index;
    uint32_t magic, row_bytes, reserved, crc;
} header_t;

typedef struct {
    book_shelf_entry_t book;
    uint32_t ordinal, crc;
} row_t;

typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} audit_frame_t;

static struct {
    audit_frame_t *frames;
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int root_count, root, depth;
    uint64_t contents;
} s_audit;
static uint32_t s_build;
_Static_assert(sizeof(header_t) + SHELF_INDEX_LIMIT * sizeof(row_t) <= LONG_MAX, "Shelf offsets fit fseek");

static uint32_t checksum(const void *data, size_t size) {
    return (uint32_t)crc32(0, data, size);
}

bool book_shelf_index_sequence(uint32_t *sequence) {
    progress_kv_handle_t handle;
    if (!sequence || !record_store_ready() || progress_kv_open(NVS_READONLY, &handle) != ESP_OK) return false;
    *sequence = 0;
    esp_err_t result = progress_kv_get_u32(handle, "seq", sequence);
    progress_kv_close(handle);
    return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

static uint64_t hash_bytes(uint64_t hash, const void *value, size_t count) {
    const unsigned char *bytes = value;
    while (count--) hash = (hash ^ *bytes++) * UINT64_C(1099511628211);
    return hash;
}

uint64_t book_shelf_index_file_stamp(const char *path, uint64_t size, int64_t modified) {
    uint64_t hash = hash_bytes(UINT64_C(14695981039346656037), path, strlen(path) + 1);
    hash = hash_bytes(hash, &size, sizeof(size));
    return hash_bytes(hash, &modified, sizeof(modified));
}

bool book_shelf_index_fingerprint(uint64_t *fingerprint) {
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int count = 0;
    if (!record_store_ready() || book_store_roots(roots, &count) != ESP_OK || book_store_roots_degraded())
        return false;
    uint64_t digest = UINT64_C(14695981039346656037);
    for (int i = 0; i < count; ++i) {
        struct stat st;
        if (stat(roots[i].path, &st) || !S_ISDIR(st.st_mode)) return false;
        digest = hash_bytes(digest, roots[i].path, strlen(roots[i].path) + 1);
        digest = hash_bytes(digest, &st.st_mtime, sizeof(st.st_mtime));
    }
    *fingerprint = digest;
    return true;
}

static bool read_header(FILE *file, header_t *header) {
    return fread(header, sizeof(*header), 1, file) == 1 && header->magic == SHELF_MAGIC &&
           header->row_bytes == sizeof(row_t) && header->reserved == 0 &&
           header->index.count <= SHELF_INDEX_LIMIT &&
           header->crc == checksum(header, offsetof(header_t, crc));
}

static bool same_index(const book_shelf_index_t *a, const book_shelf_index_t *b) {
    return a->roots == b->roots && a->contents == b->contents && a->sequence == b->sequence && a->order == b->order &&
           a->count == b->count && a->build == b->build;
}

static FILE *open_index(const book_shelf_index_t *expected) {
    header_t header;
    FILE *file = record_store_ready() ? fopen(BOOK_SHELF_INDEX_FILE, "rb") : NULL;
    if (!file) return NULL;
    if (!read_header(file, &header) || !same_index(&header.index, expected)) {
        fclose(file);
        return NULL;
    }
    return file;
}

bool book_shelf_index_open(book_shelf_index_t *index, uint64_t roots, uint32_t sequence, uint32_t order) {
    FILE *file = record_store_ready() ? fopen(BOOK_SHELF_INDEX_FILE, "rb") : NULL;
    if (!file) return false;
    header_t header;
    bool ok = read_header(file, &header) && header.index.roots == roots &&
              header.index.sequence == sequence && header.index.order == order;
    if (ok) {
        struct stat st;
        ok = !stat(BOOK_SHELF_INDEX_FILE, &st) && st.st_size == (off_t)(sizeof(header) + header.index.count * sizeof(row_t));
    }
    if (fclose(file)) ok = false;
    if (ok) *index = header.index;
    return ok;
}

static bool row_usable(const row_t *row, uint32_t ordinal) {
    const book_shelf_entry_t *book = &row->book;
    if (row->ordinal != ordinal || row->crc != checksum(row, offsetof(row_t, crc)) ||
        !memchr(book->path, 0, sizeof(book->path)) || !memchr(book->name, 0, sizeof(book->name)) ||
        !memchr(book->author, 0, sizeof(book->author)) || !book->name[0] ||
        !book_shelf_disk_book_path(book->path) || book->selected || book->removed ||
        book->search_match != 1 || book->pinned > 1 || book->has_progress > 1 || book->pct > 100 ||
        book->is_flash > 1)
        return false;
    return !strncmp(book->path, book->is_flash ? "/flash/" : "/sdcard/", book->is_flash ? 7 : 8);
}

static bool read_range(FILE *file, uint32_t first, uint32_t count, book_shelf_entry_t *entries) {
    if (!count) return true;
    if (fseek(file, (long)(sizeof(header_t) + first * sizeof(row_t)), SEEK_SET)) return false;
    row_t *block = heap_caps_malloc(SHELF_INDEX_BLOCK * sizeof(*block), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!block) return false;
    bool ok = true;
    for (uint32_t done = 0; ok && done < count;) {
        uint32_t n = count - done > SHELF_INDEX_BLOCK ? SHELF_INDEX_BLOCK : count - done;
        if (fread(block, sizeof(*block), n, file) != n) { ok = false; break; }
        for (uint32_t i = 0; i < n; ++i) {
            row_t *row = &block[i];
            if (!row_usable(row, first + done + i)) { ok = false; break; }
            struct stat st;
            if (stat(row->book.path, &st) || !S_ISREG(st.st_mode) || st.st_size != (off_t)row->book.size ||
                st.st_mtime != row->book.modified) { ok = false; break; }
            entries[done + i] = row->book;
        }
        done += n;
    }
    free(block);
    return ok;
}

bool book_shelf_index_page(const book_shelf_index_t *index, uint32_t first, uint32_t count,
                           book_shelf_entry_t *entries) {
    if (first > index->count || count > index->count - first || count > SHELF_INDEX_BLOCK) return false;
    FILE *file = open_index(index);
    if (!file) return false;
    bool ok = read_range(file, first, count, entries);
    return fclose(file) == 0 && ok;
}

bool book_shelf_index_load(const book_shelf_index_t *index, book_shelf_entry_t *entries) {
    FILE *file = open_index(index);
    if (!file) return false;
    bool ok = read_range(file, 0, index->count, entries);
    return fclose(file) == 0 && ok;
}

static bool write_rows(FILE *file, uint32_t first, uint32_t count, const book_shelf_entry_t *entries) {
    for (uint32_t i = 0; i < count; ++i) {
        row_t row = {.book = entries[i], .ordinal = first + i};
        row.book.selected = 0;
        row.crc = checksum(&row, offsetof(row_t, crc));
        if (!row_usable(&row, first + i) || fwrite(&row, sizeof(row), 1, file) != 1) return false;
    }
    return true;
}

bool book_shelf_index_save(uint64_t roots, uint64_t contents, uint32_t sequence, uint32_t order, uint32_t count,
                           const book_shelf_entry_t *entries) {
    if (!record_store_ready() || count > SHELF_INDEX_LIMIT ||
        (mkdir("/sdcard/.readpico", 0755) != 0 && errno != EEXIST))
        return false;
    FILE *file = fopen(BOOK_SHELF_INDEX_TEMP, "wb");
    if (!file) return false;
    header_t header = {.index = {.roots = roots, .contents = contents, .sequence = sequence,
                                  .order = order, .count = count, .build = ++s_build}, .magic = SHELF_MAGIC,
                       .row_bytes = sizeof(row_t)};
    header.crc = checksum(&header, offsetof(header_t, crc));
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1;
    if (ok) ok = write_rows(file, 0, count, entries);
    if (fflush(file) || fsync(fileno(file))) ok = false;
    if (fclose(file)) ok = false;
    if (ok) ok = (unlink(BOOK_SHELF_INDEX_FILE) == 0 || errno == ENOENT) &&
                 rename(BOOK_SHELF_INDEX_TEMP, BOOK_SHELF_INDEX_FILE) == 0;
    if (!ok) (void)unlink(BOOK_SHELF_INDEX_TEMP);
    return ok;
}

bool book_shelf_index_advance(book_shelf_index_t *index, uint32_t sequence) {
    FILE *file = record_store_ready() ? fopen(BOOK_SHELF_INDEX_FILE, "r+b") : NULL;
    if (!file) return false;
    header_t header;
    bool ok = read_header(file, &header) && same_index(&header.index, index) && sequence >= index->sequence;
    if (ok) {
        header.index.sequence = sequence;
        header.crc = checksum(&header, offsetof(header_t, crc));
        ok = !fseek(file, 0, SEEK_SET) && fwrite(&header, sizeof(header), 1, file) == 1 &&
             !fflush(file) && !fsync(fileno(file));
    }
    if (fclose(file)) ok = false;
    if (ok) *index = header.index;
    return ok;
}

bool book_shelf_index_update_page(book_shelf_index_t *index, uint32_t first, uint32_t count,
                                  const book_shelf_entry_t *entries, uint32_t sequence) {
    if (first > index->count || count > index->count - first || !count ||
        count > SHELF_INDEX_BLOCK || sequence <= index->sequence || !record_store_ready()) return false;
    FILE *file = fopen(BOOK_SHELF_INDEX_FILE, "r+b");
    if (!file) return false;
    header_t header;
    bool ok = read_header(file, &header) && same_index(&header.index, index) &&
              !fseek(file, (long)(sizeof(header_t) + first * sizeof(row_t)), SEEK_SET) &&
              write_rows(file, first, count, entries) && !fflush(file) && !fsync(fileno(file));
    // 行先落盘，最后推进序号；断电时旧序号不会命中真实进度库。
    // Sync rows before advancing the fence. An interrupted update fails the progress-sequence check.
    if (fclose(file)) ok = false;
    return ok && book_shelf_index_advance(index, sequence);
}

void book_shelf_index_audit_stop(void) {
    if (!s_audit.frames) return;
    for (int i = 0; i <= s_audit.depth; ++i)
        if (s_audit.frames[i].dir) closedir(s_audit.frames[i].dir);
    free(s_audit.frames);
    memset(&s_audit, 0, sizeof(s_audit));
}

static int audit_finished(bool match) {
    book_shelf_index_audit_stop();
    return match ? 1 : -1;
}

int book_shelf_index_audit_step(const book_shelf_index_t *index) {
    if (!record_store_ready()) return audit_finished(false);
    if (!s_audit.frames) {
        if (book_store_roots(s_audit.roots, &s_audit.root_count) != ESP_OK || book_store_roots_degraded())
            return audit_finished(false);
        s_audit.frames = heap_caps_calloc(BOOK_SHELF_SCAN_DEPTH + 1, sizeof(*s_audit.frames),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_audit.frames) return audit_finished(false);
    }
    audit_frame_t *frame = &s_audit.frames[s_audit.depth];
    if (!frame->dir) {
        if (s_audit.root == s_audit.root_count) return audit_finished(s_audit.contents == index->contents);
        snprintf(frame->path, sizeof(frame->path), "%s", s_audit.roots[s_audit.root].path);
        frame->dir = opendir(frame->path);
        return frame->dir ? 0 : audit_finished(false);
    }
    errno = 0;
    struct dirent *entry = readdir(frame->dir);
    if (!entry) {
        bool ok = !errno;
        closedir(frame->dir);
        frame->dir = NULL;
        if (!ok) return audit_finished(false);
        if (s_audit.depth) --s_audit.depth;
        else ++s_audit.root;
        return 0;
    }
    if (entry->d_name[0] == '.') return 0;
    char path[BOOK_STORE_PATH_MAX];
    int length = snprintf(path, sizeof(path), "%s/%s", frame->path, entry->d_name);
    if (length < 0 || (size_t)length >= sizeof(path)) return 0;
    struct stat st;
    if (stat(path, &st)) return audit_finished(false);
    if (S_ISDIR(st.st_mode) && s_audit.depth < BOOK_SHELF_SCAN_DEPTH) {
        DIR *child = opendir(path);
        if (!child) return audit_finished(false);
        frame = &s_audit.frames[++s_audit.depth];
        frame->dir = child;
        memcpy(frame->path, path, (size_t)length + 1);
    } else if (S_ISREG(st.st_mode) && book_shelf_disk_book_path(path)) {
        s_audit.contents += book_shelf_index_file_stamp(path, st.st_size, st.st_mtime);
    }
    return 0;
}
