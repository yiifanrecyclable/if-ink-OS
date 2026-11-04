/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 共享封面缓存保留旧式裁切接口，书架另用保留原图比例的有界缩略图。
 * Shared cover storage retains the legacy crop API and provides bounded aspect-preserving shelf thumbnails.
 */
#include "book_cover.h"
#include "book_index_cache.h"
#include "shelf_perf.h"
#include "book_cover_auto.h"
#include "book_epub.h"
#include "book_title.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <stddef.h>
#include <unistd.h>
#include "ui_font.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_app_desc.h"
#endif

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX cover_digest_t;
static bool cover_digest_start(cover_digest_t *h){return CC_SHA256_Init(h)==1;}
static bool cover_digest_add(cover_digest_t *h,const void *p,size_t n){return n<=UINT32_MAX&&CC_SHA256_Update(h,p,(CC_LONG)n)==1;}
static bool cover_digest_finish(cover_digest_t *h,uint8_t out[32]){return CC_SHA256_Final(out,h)==1;}
static void cover_digest_abort(cover_digest_t *h){(void)h;}
#else
#include "psa/crypto.h"
typedef psa_hash_operation_t cover_digest_t;
static bool cover_digest_start(cover_digest_t *h){*h=psa_hash_operation_init();return psa_crypto_init()==PSA_SUCCESS&&psa_hash_setup(h,PSA_ALG_SHA_256)==PSA_SUCCESS;}
static bool cover_digest_add(cover_digest_t *h,const void *p,size_t n){return psa_hash_update(h,p,n)==PSA_SUCCESS;}
static bool cover_digest_finish(cover_digest_t *h,uint8_t out[32]){size_t n=0;return psa_hash_finish(h,out,32,&n)==PSA_SUCCESS&&n==32;}
static void cover_digest_abort(cover_digest_t *h){(void)psa_hash_abort(h);}
#endif

/* ---- 共享封面缓存 / Shared cover cache ---- */

#define COVER_CACHE_MAGIC UINT32_C(0x52435042)
#define COVER_CACHE_SCHEMA 1u

typedef struct {
    uint32_t magic, schema, template_version, width, height;
    uint64_t file_size;
    int64_t modified;
    uint64_t path_hash, text_hash;
    uint32_t media_epoch;
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
        .path_hash = path_hash, .text_hash = text_hash, .media_epoch = book_index_cache_epoch(),
    };
    return snprintf(path, cap, "/sdcard/.readpico/covers/%016llx-%ux%u%s.bin",
                    (unsigned long long)path_hash, BOOK_COVER_W, BOOK_COVER_H, fitted ? "-fit2" : "") < (int)cap;
}

static bool cover_cache_read(const char *path, const cover_cache_header_t *expected,
                             uint8_t out[BOOK_COVER_W * BOOK_COVER_H], unsigned *width, unsigned *height) {
    SHELF_PERF_SCOPE(perf,SP_CACHE_PROBE,0,0);
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    cover_cache_header_t found;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
              found.magic == expected->magic && found.schema == expected->schema &&
              found.media_epoch == expected->media_epoch && expected->media_epoch == book_index_cache_epoch() &&
              found.template_version == expected->template_version &&
              found.width > 0 && found.width <= BOOK_COVER_W && found.height > 0 && found.height <= BOOK_COVER_H &&
              (expected->schema == 2u || (found.width == expected->width && found.height == expected->height)) &&
              found.file_size == expected->file_size && found.modified == expected->modified &&
              found.path_hash == expected->path_hash && found.text_hash == expected->text_hash &&
              fread(out, 1, found.width * found.height, file) == found.width * found.height &&
              fgetc(file) == EOF;
    fclose(file);
    perf.b=ok;
    if (ok) { *width=found.width; *height=found.height; }
    return ok;
}

static void cover_cache_write(const char *path, const cover_cache_header_t *header,
                              const uint8_t out[BOOK_COVER_W * BOOK_COVER_H]) {
    SHELF_PERF_SCOPE(perf,SP_CACHE_WRITE,0,0);
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
        if (!rename(temp, path)){perf.b=1;return;}
    }
    remove(temp);
}

// 持久封面只跳过图像解码，不跳过新会话的源封面核验；正文索引仍使用原epoch。
// Persistent artwork skips decoding, never fresh-session source validation or reader index epochs.
#define COVER_PERSIST_MAGIC UINT32_C(0x33504342)
#define COVER_PERSIST_SCHEMA 3u
#define COVER_SOURCE_PATH_MAX 288
typedef struct {
    uint32_t magic,schema,template_version,width,height,fitted;
    uint8_t source_id[32];
    char source[COVER_SOURCE_PATH_MAX];
    uint8_t checksum[32];
} cover_persist_header_t;
_Static_assert(sizeof(cover_persist_header_t)==376,"Stable derived cover cache header");

static bool cover_source_unchanged(const char *source,const cover_cache_header_t *key){
    struct stat st;
    return key->media_epoch==book_index_cache_epoch()&&!stat(source,&st)&&S_ISREG(st.st_mode)&&
           st.st_size>=0&&(uint64_t)st.st_size==key->file_size&&(int64_t)st.st_mtime==key->modified;
}
static bool cover_persist_identity(const char *source,const char *title,const char *author,
                                    const uint8_t *image,size_t bytes,bool png,bool fitted,uint8_t out[32]){
    if(strncmp(source,"/sdcard/",8)||strlen(source)>=COVER_SOURCE_PATH_MAX)return false;
    // 少数字符依赖可更换TTF时只用原会话缓存，避免跨启动复用错误字体的模板。
    // Keep external-font-dependent generated covers session-local rather than reuse the wrong font.
    if(!image&&(!ui_font_has_text(title)||(author&&*author&&!ui_font_has_text(author))||!ui_font_has_text("…")))return false;
    cover_digest_t hash;bool ok=cover_digest_start(&hash);
    const uint8_t domain[]={3,image?1:0,png?1:0,fitted?1:0};
    const char *strings[]={source,title?title:"",author?author:""};
    if(ok)ok=cover_digest_add(&hash,domain,sizeof(domain));
    for(unsigned i=0;i<3&&ok;++i)ok=cover_digest_add(&hash,strings[i],strlen(strings[i])+1);
#ifdef ESP_PLATFORM
    // 模板使用固件内置字形，更新固件后重新生成；真实图片由渲染版本控制。
    // Generated art depends on bundled glyphs; retire it on firmware changes.
    if(ok&&!image)ok=cover_digest_add(&hash,esp_app_get_description()->app_elf_sha256,32);
#endif
    if(ok&&image)ok=cover_digest_add(&hash,image,bytes);
    if(ok)ok=cover_digest_finish(&hash,out);
    cover_digest_abort(&hash);return ok;
}
static bool cover_persist_path(const cover_cache_header_t *key,bool fitted,char path[120]){
    int n=snprintf(path,120,"/sdcard/.readpico/covers/%016llx-%ux%u-%s3.bin",
        (unsigned long long)key->path_hash,BOOK_COVER_W,BOOK_COVER_H,fitted?"fit":"crop");
    return n>0&&n<120;
}
// out=NULL verifies an on-disk temporary without allocating another thumbnail.
static bool cover_persist_read_file(const char *path,const char *source,const uint8_t id[32],bool fitted,
                                    uint8_t *out,unsigned *width,unsigned *height){
    FILE *f=fopen(path,"rb");if(!f)return false;
    cover_persist_header_t header;
    bool ok=fread(&header,1,sizeof(header),f)==sizeof(header)&&
        header.magic==COVER_PERSIST_MAGIC&&header.schema==COVER_PERSIST_SCHEMA&&
        header.template_version==BOOK_AUTO_COVER_VERSION&&header.fitted==(unsigned)fitted&&
        header.width>0&&header.width<=BOOK_COVER_W&&header.height>0&&header.height<=BOOK_COVER_H&&
        (fitted||(header.width==BOOK_COVER_W&&header.height==BOOK_COVER_H))&&
        memchr(header.source,0,sizeof(header.source))&&!strcmp(header.source,source)&&!memcmp(header.source_id,id,32);
    cover_digest_t hash;bool attempted=false;
    if(ok){attempted=true;ok=cover_digest_start(&hash)&&cover_digest_add(&hash,&header,offsetof(cover_persist_header_t,checksum));}
    uint8_t chunk[1024];size_t offset=0,bytes=ok?(size_t)header.width*header.height:0;
    while(ok&&offset<bytes){size_t n=bytes-offset;if(n>sizeof(chunk))n=sizeof(chunk);
        uint8_t *target=out?out+offset:chunk;
        ok=fread(target,1,n,f)==n&&cover_digest_add(&hash,target,n);offset+=n;
    }
    uint8_t digest[32];
    if(ok)ok=fgetc(f)==EOF&&!ferror(f)&&cover_digest_finish(&hash,digest)&&!memcmp(digest,header.checksum,32);
    if(attempted)cover_digest_abort(&hash);
    if(fclose(f))ok=false;
    if(ok&&out){*width=header.width;*height=header.height;}
    return ok;
}
static bool cover_persist_read(const char *path,const char *source,const uint8_t id[32],bool fitted,
                                uint8_t *out,unsigned *width,unsigned *height){
    SHELF_PERF_SCOPE(perf,SP_COVER_PERSIST_READ,fitted,0);
    bool ok=cover_persist_read_file(path,source,id,fitted,out,width,height);perf.b=ok;return ok;
}
static bool cover_persist_write(const char *path,const char *source,const uint8_t id[32],bool fitted,
                                unsigned width,unsigned height,const uint8_t *out){
    SHELF_PERF_SCOPE(perf,SP_COVER_PERSIST_WRITE,fitted,0);
    if(mkdir("/sdcard/.readpico",0777)&&errno!=EEXIST)goto failed;
    if(mkdir("/sdcard/.readpico/covers",0777)&&errno!=EEXIST)goto failed;
    cover_persist_header_t header={.magic=COVER_PERSIST_MAGIC,.schema=COVER_PERSIST_SCHEMA,
        .template_version=BOOK_AUTO_COVER_VERSION,.width=width,.height=height,.fitted=fitted};
    memcpy(header.source_id,id,32);snprintf(header.source,sizeof(header.source),"%s",source);
    cover_digest_t hash;bool ok=cover_digest_start(&hash);
    if(ok)ok=cover_digest_add(&hash,&header,offsetof(cover_persist_header_t,checksum))&&
             cover_digest_add(&hash,out,(size_t)width*height)&&cover_digest_finish(&hash,header.checksum);
    cover_digest_abort(&hash);if(!ok)goto failed;
    char temp[128];if(snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))goto failed;
    FILE *f=fopen(temp,"wb");if(!f)goto failed;
    ok=fwrite(&header,1,sizeof(header),f)==sizeof(header)&&fwrite(out,1,(size_t)width*height,f)==(size_t)width*height;
    if(ok&&fflush(f))ok=false;
    if(ok&&fsync(fileno(f)))ok=false;
    if(fclose(f))ok=false;
    if(ok)ok=cover_persist_read_file(temp,source,id,fitted,NULL,NULL,NULL);
    if(ok){if(remove(path)&&errno!=ENOENT)ok=false;}
    if(ok&&!rename(temp,path)){perf.b=1;return true;}
    (void)remove(temp);
failed:
    ESP_LOGW("book_cover","Derived cover cache write failed; artwork remains available");
    return false;
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

    bool good=false,cache_result=true,identity=false,png=false;
    uint8_t *data=NULL,id[32];size_t size=0;char persistent[120];
    const char *ext=strrchr(path,'.');
    bool epub=ext&&!strcasecmp(ext,".epub");esp_err_t source_error=ESP_ERR_NOT_FOUND;
    {
        SHELF_PERF_SCOPE(perf,SP_COVER_SOURCE,epub,0);
        if(epub)source_error=book_epub_cover(path,&data,&size,&png);
        // Verify actual cover bytes even when source size and mtime were preserved.
        // No full-book digest and no hashing on scan/allow_decode=false paths.
        if((source_error==ESP_OK&&data&&size)||(!epub||source_error==ESP_ERR_NOT_FOUND))
            identity=cover_persist_identity(path,title,author,data,size,png,fitted,id)&&cover_persist_path(&key,fitted,persistent);
        perf.b=identity;
    }
    if(!cover_source_unchanged(path,&key)){free(data);return false;}
    if(identity&&cover_persist_read(persistent,path,id,fitted,out,width,height)){
        good=cover_source_unchanged(path,&key);
        if(good){key.width=*width;key.height=*height;cover_cache_write(cache_path,&key,out);}
        free(data);return good;
    }
    {
        SHELF_PERF_SCOPE(perf,SP_COVER_DECODE,epub,0);
        if(epub&&source_error==ESP_OK){
            if(fitted){unsigned source_w=0,source_h=0;
                if(book_image_dimensions(data,size,png,&source_w,&source_h)){
                    book_cover_fit_size(source_w,source_h,BOOK_COVER_W,BOOK_COVER_H,width,height);
                    good=book_image_grayscale(data,size,png,*width,*height,out);
                }
            }else good=book_cover_thumbnail(data,size,png,out);
            // Do not persist a temporary image-decoder fallback as a real-cover result.
            if(!good)identity=false;
        }
        if(source_error==ESP_ERR_NO_MEM)cache_result=false;
        if(!good){*width=BOOK_COVER_W;*height=BOOK_COVER_H;good=book_auto_cover_render(path,title,author,*width,*height,out);}
        perf.b=good;
    }
    free(data);
    if(!cover_source_unchanged(path,&key))return false;
    key.width=*width;key.height=*height;
    if(good&&cache_result){
        cover_cache_write(cache_path,&key,out);
        if(identity)(void)cover_persist_write(persistent,path,id,fitted,*width,*height,out);
    }
    return good;
}

bool book_cover_load_gray(const char *path,const char *title,const char *author,uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool allow_decode,bool *pending){
    unsigned width,height;return load_cover(path,title,author,out,allow_decode,pending,false,&width,&height);
}
bool book_cover_load_fitted_gray(const char *path,const char *title,const char *author,uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool allow_decode,bool *pending,unsigned *width,unsigned *height){
    if(!width||!height)return false;
    return load_cover(path,title,author,out,allow_decode,pending,true,width,height);
}
