/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#define BOOK_SHELF_INDEX_FILE "/sdcard/.readpico/shelf.ifx"
#define BOOK_SHELF_INDEX_TEMP "/sdcard/.readpico/shelf.tmp"

static inline void book_shelf_disk_invalidate(void) {
    (void)unlink(BOOK_SHELF_INDEX_FILE);
}

static inline bool book_shelf_disk_book_path(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt"));
}
