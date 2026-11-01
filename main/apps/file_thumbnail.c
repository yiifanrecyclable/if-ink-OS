/* SPDX-License-Identifier: Apache-2.0
 * 中文：按路径/大小/修改时间校验缩略图缓存，只解码当前页的有界图片。
 * English: Validate thumbnails by path/size/mtime, decoding only bounded images on the visible page.
 * 冻结：只写.readpico/thumbnails，不修改源图，不操作进度/时长或刷新策略。
 * Frozen: write only .readpico/thumbnails, never source images, progress/time or refresh policies.
 */
#include "file_thumbnail.h"
#include "book_cover.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct {
    uint32_t magic,version,width,height,bytes,check;
    uint64_t source_size,path_hash;
    int64_t modified;
} thumb_header_t;
static uint64_t path_hash(const char *p) {
    uint64_t h=UINT64_C(14695981039346656037);
    while(*p)h=(h^(unsigned char)*p++)*UINT64_C(1099511628211);
    return h;
}
static uint32_t checksum(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;
}
static bool cached(const char *path,const thumb_header_t *expected,uint8_t *out,thumb_header_t *found) {
    FILE *f=fopen(path,"rb");if(!f)return false;
    bool ok=fread(found,1,sizeof(*found),f)==sizeof(*found) && found->magic==expected->magic &&
        found->version==expected->version && found->source_size==expected->source_size &&
        found->path_hash==expected->path_hash && found->modified==expected->modified &&
        found->width>0 && found->width<=FILE_THUMB_W && found->height>0 && found->height<=FILE_THUMB_H &&
        found->bytes==(found->width*found->height+1)/2 && found->bytes<=FILE_THUMB_BYTES &&
        fread(out,1,found->bytes,f)==found->bytes && fgetc(f)==EOF && !ferror(f) &&
        found->check==checksum(out,found->bytes);
    if(fclose(f))ok=false;
    return ok;
}
static void cache_write(const char *path,const thumb_header_t *h,const uint8_t *out) {
    // 缓存写失败不阻止查看；下次重新生成即可。/ Cache failures never block viewing; regenerate next time.
    (void)mkdir("/sdcard/.readpico",0755);(void)mkdir("/sdcard/.readpico/thumbnails",0755);
    char temporary[128];if(snprintf(temporary,sizeof(temporary),"%s.part",path)>=(int)sizeof(temporary))return;
    FILE *f=fopen(temporary,"wb");if(!f)return;
    bool ok=fwrite(h,1,sizeof(*h),f)==sizeof(*h)&&fwrite(out,1,h->bytes,f)==h->bytes;
    if(fclose(f))ok=false;
    if(ok){remove(path);if(rename(temporary,path)==0)return;}
    remove(temporary);
}
bool file_thumbnail_load(const char *path,uint8_t out[FILE_THUMB_BYTES],unsigned *width,unsigned *height) {
    if(!path||!out||!width||!height)return false;
    *width=*height=0;
    const char *ext=strrchr(path,'.');
    bool png=ext&&!strcasecmp(ext,".png");
    if(!ext||(!png&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))return false;
    struct stat st;if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>2u*1024u*1024u)return false;
    thumb_header_t h={.magic=0x49465431,.version=1,.source_size=(uint64_t)st.st_size,
        .path_hash=path_hash(path),.modified=(int64_t)st.st_mtime},found;
    char cache[112];snprintf(cache,sizeof(cache),"/sdcard/.readpico/thumbnails/%016llx.bin",(unsigned long long)h.path_hash);
    if(cached(cache,&h,out,&found)){*width=found.width;*height=found.height;return true;}
    uint8_t *source=heap_caps_malloc((size_t)st.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!source)return false;
    FILE *f=fopen(path,"rb");if(!f){free(source);return false;}
    bool ok=fread(source,1,(size_t)st.st_size,f)==(size_t)st.st_size&&!ferror(f);
    if(fclose(f))ok=false;
    unsigned w=0,t=0;
    ok=ok&&book_image_dimensions(source,(size_t)st.st_size,png,&w,&t)&&w&&t&&(uint64_t)w*t<=8u*1024u*1024u;
    if(!ok){free(source);return false;}
    book_cover_fit_size(w,t,FILE_THUMB_W,FILE_THUMB_H,&w,&t);
    uint8_t *gray=heap_caps_malloc((size_t)w*t,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ok=gray&&book_image_grayscale(source,(size_t)st.st_size,png,w,t,gray);free(source);
    if(!ok){free(gray);return false;}
    size_t pixels=(size_t)w*t;memset(out,255,FILE_THUMB_BYTES);
    for(size_t i=0;i<pixels;++i){uint8_t shade=(uint8_t)((gray[i]+8)/17);if(i&1)out[i/2]=(out[i/2]&0xf0)|shade;else out[i/2]=(out[i/2]&15)|(shade<<4);}
    free(gray);
    h.width=w;h.height=t;h.bytes=(uint32_t)((pixels+1)/2);h.check=checksum(out,h.bytes);
    // 读取期间源文件变更时丢弃本次结果，避免把旧像素缓存为新文件。
    // Discard a source changed during decoding rather than caching old pixels as the new file.
    struct stat after;if(stat(path,&after)||after.st_size!=st.st_size||after.st_mtime!=st.st_mtime)return false;
    cache_write(cache,&h,out);*width=w;*height=t;return true;
}
