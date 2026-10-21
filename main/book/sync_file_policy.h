/* SPDX-License-Identifier: Apache-2.0
 * 中文：云端文件类型、标准目录与单文件上限，供目录和事务共用。
 * English: Shared cloud file types, standard directories and transfer limits.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>

typedef enum {
    SYNC_FILE_UNSUPPORTED, SYNC_FILE_MARKDOWN, SYNC_FILE_BOOK,
    SYNC_FILE_IMAGE, SYNC_FILE_FONT
} sync_file_kind_t;

static inline sync_file_kind_t sync_file_kind(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    if (!ext) return SYNC_FILE_UNSUPPORTED;
    if (!strcasecmp(ext, ".md")) return SYNC_FILE_MARKDOWN;
    if (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt")) return SYNC_FILE_BOOK;
    if (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"))
        return SYNC_FILE_IMAGE;
    if (!strcasecmp(ext, ".ttf")) return SYNC_FILE_FONT;
    return SYNC_FILE_UNSUPPORTED;
}

static inline const char *sync_file_directory(sync_file_kind_t kind) {
    switch (kind) {
    case SYNC_FILE_MARKDOWN: return "/sdcard/markdown";
    case SYNC_FILE_BOOK: return "/sdcard/books";
    case SYNC_FILE_IMAGE: return "/sdcard/pictures";
    case SYNC_FILE_FONT: return "/sdcard/fonts";
    default: return NULL;
    }
}

static inline uint64_t sync_file_limit(sync_file_kind_t kind) {
    if (kind == SYNC_FILE_MARKDOWN) return 256 * 1024;
    if (kind == SYNC_FILE_IMAGE) return 2 * 1024 * 1024;
    return kind == SYNC_FILE_BOOK || kind == SYNC_FILE_FONT ? 64 * 1024 * 1024 : 0;
}
