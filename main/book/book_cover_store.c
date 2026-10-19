/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 共享封面缓存保留旧式裁切接口，书架另用保留原图比例的有界缩略图。
 * Shared cover storage retains the legacy crop API and provides bounded aspect-preserving shelf thumbnails.
 */
#include "book_cover.h"
#include "book_cover_auto.h"
#include "book_epub.h"
#include "book_title.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* ---- 共享封面缓存 / Shared cover cache ---- */

#define COVER_CACHE_MAGIC UINT32_C(0x52435041)
#define COVER_CACHE_SCHEMA 1u

typedef struct {
    uint32_t magic, schema, template_version, width, height;
    uint64_t file_size;
    int64_t modified;
    uint64_t path_hash, text_hash;
} cover_cache_header_t;

static uint64_t cover_hash(uint64_t hash, const char *text) {
    if (!text) text = "";
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    return (hash ^ 0xffu) * UINT64_C(1099511628211);
}

static bool cover_cache_key(const char *source, const char *title, const char *author,
                            char *path, size_t cap, cover_cache_header_t *header, bool fitted) {
    struct stat st;
    if (stat(source, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) return false;
    uint64_t path_hash = cover_hash(UINT64_C(14695981039346656037), source);
    uint64_t text_hash = cover_hash(cover_hash(UINT64_C(14695981039346656037), title), author);
    *header = (cover_cache_header_t){
        .magic = COVER_CACHE_MAGIC, .schema = fitted ? 2u : COVER_CACHE_SCHEMA,
        .template_version = BOOK_AUTO_COVER_VERSION,
        .width = BOOK_COVER_W, .height = BOOK_COVER_H,
        .file_size = (uint64_t)st.st_size, .modified = (int64_t)st.st_mtime,
        .path_hash = path_hash, .text_hash = text_hash,
    };
    return snprintf(path, cap, "/sdcard/.readpico/covers/%016llx-%ux%u%s.bin",
                    (unsigned long long)path_hash, BOOK_COVER_W, BOOK_COVER_H, fitted ? "-fit2" : "") < (int)cap;
}

static bool cover_cache_read(const char *path, const cover_cache_header_t *expected,
                             uint8_t out[BOOK_COVER_W * BOOK_COVER_H], unsigned *width, unsigned *height) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    cover_cache_header_t found;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
              found.magic == expected->magic && found.schema == expected->schema &&
              found.template_version == expected->template_version &&
              found.width > 0 && found.width <= BOOK_COVER_W && found.height > 0 && found.height <= BOOK_COVER_H &&
              (expected->schema == 2u || (found.width == expected->width && found.height == expected->height)) &&
              found.file_size == expected->file_size && found.modified == expected->modified &&
              found.path_hash == expected->path_hash && found.text_hash == expected->text_hash &&
              fread(out, 1, found.width * found.height, file) == found.width * found.height &&
              fgetc(file) == EOF;
    fclose(file);
    if (ok) { *width=found.width; *height=found.height; }
    return ok;
}

static void cover_cache_write(const char *path, const cover_cache_header_t *header,
                              const uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    if (mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) return;
    if (mkdir("/sdcard/.readpico/covers", 0777) && errno != EEXIST) return;
    char temp[128];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return;
    FILE *file = fopen(temp, "wb");
    if (!file) return;
    bool ok = fwrite(header, 1, sizeof(*header), file) == sizeof(*header) &&
              fwrite(out, 1, header->width * header->height, file) == header->width * header->height;
    if (fclose(file)) ok = false;
    if (ok) {
        remove(path);
        if (!rename(temp, path)) return;
    }
    remove(temp);
}

static bool load_cover(const char *path, const char *title, const char *author,
                          uint8_t out[BOOK_COVER_W * BOOK_COVER_H],
                          bool allow_decode, bool *pending, bool fitted, unsigned *width, unsigned *height) {
    if (pending) *pending = false;
    *width=BOOK_COVER_W; *height=BOOK_COVER_H;
    if (!path || !*path || !out) return false;
    char canonical_title[256];
    if (book_title_from_path(path, canonical_title, sizeof(canonical_title))) title = canonical_title;
    else if (!title || !*title) return false;
    char cache_path[120];
    cover_cache_header_t key;
    bool cacheable = cover_cache_key(path, title, author, cache_path, sizeof(cache_path), &key, fitted);
    if (!cacheable) return false;
    if (cover_cache_read(cache_path, &key, out, width, height)) return true;
    if (!allow_decode) { if (pending) *pending = true; return false; }

    bool good = false;
    bool cache_result = true;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        uint8_t *data = NULL;
        size_t size = 0;
        bool png = false;
        esp_err_t err = book_epub_cover(path, &data, &size, &png);
        if (err == ESP_OK) {
            if (fitted) {
                unsigned source_w=0,source_h=0;
                if (book_image_dimensions(data,size,png,&source_w,&source_h)) {
                    book_cover_fit_size(source_w,source_h,BOOK_COVER_W,BOOK_COVER_H,width,height);
                    good=book_image_grayscale(data,size,png,*width,*height,out);
                }
            } else good=book_cover_thumbnail(data,size,png,out);
        }
        free(data);
        if (err == ESP_ERR_NO_MEM) cache_result = false;
    }
    if (!good) { *width=BOOK_COVER_W; *height=BOOK_COVER_H;good=book_auto_cover_render(path,title,author,*width,*height,out); }
    key.width=*width;key.height=*height;
    if (good && cache_result) cover_cache_write(cache_path, &key, out);
    return good;
}

bool book_cover_load_gray(const char *path,const char *title,const char *author,uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool allow_decode,bool *pending){
    unsigned width,height;return load_cover(path,title,author,out,allow_decode,pending,false,&width,&height);
}
bool book_cover_load_fitted_gray(const char *path,const char *title,const char *author,uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool allow_decode,bool *pending,unsigned *width,unsigned *height){
    if(!width||!height)return false;
    return load_cover(path,title,author,out,allow_decode,pending,true,width,height);
}
