/* SPDX-License-Identifier: Apache-2.0
 * 中文：自定义固件新增文档管理；可恢复元数据保留首次入库日期，更新按内容 SHA256 判定。
 * English: Derivative document management;
 recoverable metadata preserves first import dates and SHA256 identifies updates.
 * 用户反馈：TF不允许重命名覆盖；日志与索引使用FAT兼容替换，回滚不能报成功。
 * User feedback: FAT rejects overwrite-by-rename; journal/index replacement is FAT-compatible and rollback never means success.
 * 中文：清理只针对旧清单拥有且未被其他正文引用的本地附件；不访问云端。
 * English: Cleanup only old locally owned assets unreferenced by other documents;
 never accesses cloud storage.
 */
#include "document_store.h"
#include "book_markdown.h"
#include "book_progress.h"
#include "book_title.h"
#include "document_image.h"
#include "markdown_text.h"
#include "sync_file_policy.h"
#include "nvs.h"
#include "record_store.h"
#include "shelf_perf.h"
#include "esp_timer.h"
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifndef PICO_HOST_SIMULATOR
// TF卡使用不支持符号链接的FatFS；主机保留lstat路径防护。/ FatFS has no symlinks; the host keeps lstat checks.
#define lstat stat
#endif
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifdef PICO_HOST_SIMULATOR
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX digest_t;
#define digest_init CC_SHA256_Init
#define digest_update CC_SHA256_Update
#define digest_finish(ctx, out) CC_SHA256_Final(out, ctx)
#define digest_free(ctx) ((void)(ctx))
#else
#include "psa/crypto.h"
typedef struct {
    psa_hash_operation_t operation;
    psa_status_t status;
} digest_t;
static void digest_init(digest_t *c) {
    c->operation = psa_hash_operation_init();
    c->status = psa_crypto_init();
    if (c->status == PSA_SUCCESS)
        c->status = psa_hash_setup(&c->operation, PSA_ALG_SHA_256);
}
static void digest_update(digest_t *c, const void *data, size_t n) {
    if (c->status == PSA_SUCCESS)
        c->status = psa_hash_update(&c->operation, data, n);
}
static bool digest_finish(digest_t *c, uint8_t out[32]) {
    size_t n = 0;
    if (c->status == PSA_SUCCESS)
        c->status = psa_hash_finish(&c->operation, out, 32, &n);
    return c->status == PSA_SUCCESS && n == 32;
}
static void digest_free(digest_t *c) {
    psa_hash_abort(&c->operation);
}
#endif
static bool is_md(const char *name) {
    const char *p = strrchr(name, '.');
    return p && !strcasecmp(p, ".md");
}
static bool allowed(const char *path) {
    return path && !strncmp(path, DOCUMENT_ROOT "/", 17) && !strchr(path + 17, '/') && strlen(path) < 288 &&
           is_md(path + 17);
}
#define LOCAL_PATH_MAX 288
#define LOCAL_DEPTH_MAX 8
static bool local_path(const char *path, bool directory) {
    const size_t root = sizeof(DOCUMENT_ROOT)-1;
    if (!path || strncmp(path,DOCUMENT_ROOT,root) || (path[root] && path[root]!='/') ||
        strlen(path)>=LOCAL_PATH_MAX) return false;
    if (!path[root]) return directory;
    const char *part=path+root+1;
    for(unsigned depth=0;depth<=LOCAL_DEPTH_MAX;++depth){
        const char *slash=strchr(part,'/');
        size_t len=slash?(size_t)(slash-part):strlen(part);
        if(!len||len>240||(depth==0&&len==11&&!strncasecmp(part,"attachments",len)))return false;
        char name[241];
        memcpy(name,part,len);name[len]=0;
        if(!document_store_safe_filename(name))return false;
        if(!slash)return directory?depth<LOCAL_DEPTH_MAX:is_md(name);
        part=slash+1;
    }
    return false;
}
bool document_store_local_directory(const char *path){return local_path(path,true);}
bool document_store_local_path(const char *path){return local_path(path,false);}
// FatFs不能重命名覆盖：完整临时文件先落盘，旧索引留作回退，再安装新索引。
// FatFs cannot rename over an existing file: retain the old index for rollback before installing the synced replacement.
static bool replace_metadata(const char *temporary, const char *destination) {
    char backup[576];
    if (snprintf(backup, sizeof(backup), "%s.before", destination) >= (int)sizeof(backup)) return false;
    struct stat st;
    bool previous = !lstat(backup, &st);
    if (previous && !S_ISREG(st.st_mode)) return false;
    if (!previous && errno != ENOENT) return false;
    bool exists = !lstat(destination, &st);
    if (exists && !S_ISREG(st.st_mode)) return false;
    if (!exists && errno != ENOENT) return false;
    if (previous && !exists) {
        if (rename(backup, destination)) return false;
        exists = true; previous = false;
    }
    if (previous && unlink(backup)) return false;
    if (exists && rename(destination, backup)) return false;
    if (rename(temporary, destination)) {
        if (exists) (void)rename(backup, destination);
        return false;
    }
    return !exists || !unlink(backup);
}
// 断电窗口留下的唯一旧索引在读取或继续更新前恢复，不能当成从未入库。
// Restore a backup-only index before reading/updating instead of treating it as a first import.
static bool restore_metadata(const char *destination) {
    struct stat st;
    if (!lstat(destination, &st)) return S_ISREG(st.st_mode);
    if (errno != ENOENT) return false;
    char backup[576];
    if (snprintf(backup, sizeof(backup), "%s.before", destination) >= (int)sizeof(backup)) return false;
    if (lstat(backup, &st)) return errno == ENOENT;
    return S_ISREG(st.st_mode) && !rename(backup, destination);
}
static void index_path(const char *path,const char *suffix,char *out,size_t cap){
    const char *base=strrchr(path,'/');
    if(base==path+sizeof(DOCUMENT_ROOT)-1)snprintf(out,cap,DOCUMENT_ROOT "/.index/%s.%s",base+1,suffix);
    else snprintf(out,cap,"%.*s/.pico-index/%s.%s",(int)(base-path),path,base+1,suffix);
}
static void metadata(const char *path, char *out, size_t cap) {
    index_path(path,"pico",out,cap);
}
static void preferences(const char *path, char *out, size_t cap) {
    index_path(path,"prefs",out,cap);
}
static bool ensure_index(const char *path){
    char index[512];index_path(path,"pico",index,sizeof(index));
    *strrchr(index,'/')=0;
    struct stat st;
    if(!lstat(index,&st))return S_ISDIR(st.st_mode);
    return errno==ENOENT&&(!mkdir(index,0700)||errno==EEXIST);
}
static void read_flags(const char *path, bool *pinned, bool *starred) {
    *pinned = *starred = false;
    char file[512], magic[32];
    preferences(path, file, sizeof(file));
    if (!restore_metadata(file)) return;
    FILE *f = fopen(file, "rb");
    if (!f)
        return;
    unsigned flags = 0;
    if (fgets(magic, sizeof(magic), f) && !strcmp(magic, "pico-md-prefs-1\n") &&
        fscanf(f, "%u", &flags) == 1 && flags <= 3) {
        *pinned = (flags & 1) != 0;
        *starred = (flags & 2) != 0;
    }
    fclose(f);
}
esp_err_t document_store_set_flags(const char *path, bool pinned, bool starred) {
    if (!allowed(path)&&!document_store_local_path(path))
        return ESP_ERR_INVALID_ARG;
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode))
        return ESP_ERR_NOT_FOUND;
    if(!ensure_index(path))return ESP_FAIL;
    char file[512], temp[520];
    preferences(path, file, sizeof(file));
    if (!restore_metadata(file)) return ESP_FAIL;
    snprintf(temp, sizeof(temp), "%s.part", file);
    FILE *f = fopen(temp, "wb");
    if (!f)
        return ESP_FAIL;
    bool ok = fprintf(f, "pico-md-prefs-1\n%u\n", (pinned ? 1u : 0u) | (starred ? 2u : 0u)) > 0 &&
              !fflush(f) && !fsync(fileno(f));
    if (fclose(f))
        ok = false;
    if (ok)
        ok = replace_metadata(temp, file);
    if (!ok)
        unlink(temp);
    return ok ? ESP_OK : ESP_FAIL;
}
static esp_err_t parse_file(const char *path, html_text_t *text, digest_t *hash) {
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > MARKDOWN_INPUT_MAX)
        return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(path, "rb");
    if (!f)
        return ESP_FAIL;
    size_t n = (size_t)st.st_size;
    char *input = malloc(n + 1);
    if (!input) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    bool ok = fread(input, 1, n, f) == n && !ferror(f);
    fclose(f);
    if (hash && ok) {
        uint8_t length[8];
        for (unsigned i = 0; i < 8; i++)
            length[i] = (uint64_t)n >> (8 * i);
        digest_update(hash, length, 8);
        digest_update(hash, input, n);
    }
    esp_err_t e = ok ? markdown_to_blocks(input, n, text) : ESP_FAIL;
    free(input);
    return e;
}
static bool hash_asset(digest_t *h, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    // HTTP文档入库会嵌套校验附件，避免4KiB缓冲占用任务栈。
    // HTTP document commits validate nested assets; keep the 4 KiB buffer off the task stack.
    uint8_t *buf = malloc(4096);
    if (!buf) { fclose(f); return false; }
    size_t n;
    digest_t asset;
    digest_init(&asset);
    while ((n = fread(buf, 1, 4096, f)))
        digest_update(&asset, buf, n);
    bool ok = !ferror(f);
    free(buf);
    fclose(f);
    uint8_t sum[32];
    bool hashed = digest_finish(&asset, sum);
    digest_free(&asset);
    if (ok && hashed)
        digest_update(h, sum, 32);
    return ok && hashed;
}
static void marks_key(const char *path,char key[11]){
    uint32_t hash=UINT32_C(2166136261);
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*UINT32_C(16777619);
    snprintf(key,11,"m_%08lx",(unsigned long)hash);
}
static esp_err_t read_marks(records_handle_t h,const char *key,const char *path,uint8_t **data,size_t *size,size_t *offset){
    esp_err_t err=records_get_blob(h,key,NULL,size);
    if(err!=ESP_OK)return err;
    *data=malloc(*size);
    if(!*data)return ESP_ERR_NO_MEM;
    err=records_get_blob(h,key,*data,size);
    if(err!=ESP_OK)return err;
    uint32_t magic=0;
    if(*size>=4)memcpy(&magic,*data,4);
    *offset=magic==UINT32_C(0x52504d31)?14:magic==UINT32_C(0x52504d32)?12:SIZE_MAX;
    if(*offset==SIZE_MAX||*size<*offset+LOCAL_PATH_MAX||
       !memchr(*data+*offset,0,LOCAL_PATH_MAX)||strcmp((char *)*data+*offset,path))return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
static esp_err_t clear_reader(const char *path){
    esp_err_t err=book_progress_forget(path);
    if(err!=ESP_OK)return err;
    records_handle_t h;
    err=records_open("rp_marks",NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    char key[11];marks_key(path,key);
    uint8_t *data=NULL;size_t size=0,offset=0;
    err=read_marks(h,key,path,&data,&size,&offset);
    free(data);
    if(err==ESP_OK)err=records_erase_key(h,key);
    if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    if(err==ESP_OK)err=records_commit(h);
    records_close(h);
    return err;
}
static esp_err_t copy_marks(const char *source,const char *target){
    records_handle_t h;
    esp_err_t err=records_open("rp_marks",NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    char from[11],to[11];marks_key(source,from);marks_key(target,to);
    uint8_t *data=NULL;size_t size=0,offset=0;
    err=read_marks(h,from,source,&data,&size,&offset);
    if(err==ESP_OK){
        if(!strcmp(from,to))err=ESP_ERR_INVALID_STATE;
        else{
            memset(data+offset,0,LOCAL_PATH_MAX);
            memcpy(data+offset,target,strlen(target)+1);
            err=records_set_blob(h,to,data,size);
            if(err==ESP_OK)err=records_commit(h);
        }
    }else if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    free(data);records_close(h);
    return err;
}
typedef esp_err_t (*local_doc_visit_t)(const char *,const struct stat *,void *);
static esp_err_t walk_documents(char path[LOCAL_PATH_MAX],unsigned depth,bool strict,local_doc_visit_t visit,void *ctx){
    DIR *dir=opendir(path);
    if(!dir)return !strict&&errno==ENOENT?ESP_OK:ESP_FAIL;
    size_t len=strlen(path);
    esp_err_t err=ESP_OK;
    while(err==ESP_OK){
        errno=0;
        struct dirent *de=readdir(dir);
        if(!de){if(errno)err=ESP_FAIL;break;}
        if(de->d_name[0]=='.'||(!depth&&!strcasecmp(de->d_name,"attachments")))continue;
        int n=snprintf(path+len,LOCAL_PATH_MAX-len,"/%s",de->d_name);
        if(n<0||(size_t)n>=LOCAL_PATH_MAX-len){if(strict)err=ESP_ERR_INVALID_SIZE;continue;}
        struct stat st;
        if(lstat(path,&st)){if(strict||errno!=ENOENT)err=ESP_FAIL;continue;}
        if(S_ISDIR(st.st_mode)){
            if(document_store_local_directory(path))err=walk_documents(path,depth+1,strict,visit,ctx);
            else if(strict)err=ESP_ERR_INVALID_SIZE;
        }else if(S_ISREG(st.st_mode)&&(allowed(path)||document_store_local_path(path))){
            err=visit(path,&st,ctx);
        }
    }
    path[len]=0;
    closedir(dir);
    return err;
}

// 无法解析其他正文时保留图片，避免错误清理。
// Retain assets if another document cannot be parsed, avoiding destructive cleanup.
typedef struct {const char *asset,*except;} reference_search_t;
static esp_err_t check_reference(const char *path,const struct stat *st,void *context){
    (void)st;
    const reference_search_t *search=context;
    if(search->except&&!strcmp(path,search->except))return ESP_OK;
    html_text_t text={0};
    esp_err_t err=parse_file(path,&text,NULL);
    for(size_t i=0;err==ESP_OK&&i<text.image_count;++i){
        char asset[512];
        if(markdown_attachment_path(path,text.images[i],asset,sizeof(asset))&&!strcasecmp(asset,search->asset))
            err=ESP_ERR_INVALID_STATE;
    }
    html_text_free(&text);
    return err;
}
static bool referenced(const char *asset,const char *except){
    char path[LOCAL_PATH_MAX]=DOCUMENT_ROOT;
    reference_search_t search={asset,except};
    return walk_documents(path,0,true,check_reference,&search)!=ESP_OK;
}
static bool clean_old(const char *meta, const char *document, const html_text_t *current) {
    FILE *f = fopen(meta, "rb");
    if (!f)
        return errno==ENOENT;
    char line[512];
    bool ok = true;
    (void)fgets(line, sizeof(line), f);
    (void)fgets(line, sizeof(line), f);
    (void)fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char path[512];
        if (!markdown_attachment_path(document, line, path, sizeof(path)))
            continue;
        bool keep = false;
        if (current)
            for (size_t i = 0; i < current->image_count; i++) {
                char p[512];
                if (markdown_attachment_path(document, current->images[i], p, sizeof(p)) &&
                    !strcmp(p, path)) {
                    keep = true;
                    break;
                }
            }
        if (!keep && !referenced(path, document)) {
            struct stat st;
            if (lstat(path, &st) == 0 && unlink(path))
                ok = false;
            else
                document_image_remove_cache(path);
        }
    }
    if (ferror(f))
        ok = false;
    fclose(f);
    return ok;
}
esp_err_t document_store_validate(const char *path) {
    if (!path || strncmp(path, DOCUMENT_ROOT "/", 17) || strchr(path + 17, '/'))
        return ESP_ERR_INVALID_ARG;
    html_text_t text = {0};
    esp_err_t e = parse_file(path, &text, NULL);
    for (size_t i = 0; e == ESP_OK && i < text.image_count; i++) {
        char asset[512];
        document_image_t image;
        if (!markdown_attachment_path(path, text.images[i], asset, sizeof(asset)) ||
            !document_image_open(asset, &image))
            e = ESP_FAIL;
    }
    html_text_free(&text);
    return e;
}
typedef struct { int64_t body_us, assets_us; } document_verify_time_t;
static esp_err_t refresh_at(const char *path, bool *changed, const char *first_date,
                            document_verify_time_t *timing) {
    if (changed)
        *changed = false;
    if (!allowed(path)&&!document_store_local_path(path))
        return ESP_ERR_INVALID_ARG;
    (void)mkdir(DOCUMENT_ROOT, 0700);
    (void)mkdir(DOCUMENT_ROOT "/attachments", 0700);
    if(!ensure_index(path))return ESP_FAIL;
    digest_t hash;
    digest_init(&hash);
    html_text_t t = {0};
    int64_t start=timing?esp_timer_get_time():0;
    esp_err_t e = parse_file(path, &t, &hash);
    if(timing){
        int64_t now=esp_timer_get_time();
        timing->body_us+=now-start;
        start=now;
    }
    for (size_t i = 0; e == ESP_OK && i < t.image_count; i++) {
        char p[512];
        if (!markdown_attachment_path(path, t.images[i], p, sizeof(p)) || !hash_asset(&hash, p))
            e = ESP_FAIL;
        else {
            digest_update(&hash, t.images[i], strlen(t.images[i]) + 1);
        }
    }
    if(timing)timing->assets_us+=esp_timer_get_time()-start;
    uint8_t digest[32];
    if (!digest_finish(&hash, digest))
        e = ESP_FAIL;
    digest_free(&hash);
    if (e != ESP_OK) {
        html_text_free(&t);
        return e;
    }
    char revision[65];
    for (unsigned i = 0; i < 32; i++)
        snprintf(revision + 2 * i, 3, "%02x", digest[i]);
    char meta[512], temp[520], old[80] = {0}, date[16] = {0}, magic[32] = {0};
    metadata(path, meta, sizeof(meta));
    if (!restore_metadata(meta)) { html_text_free(&t); return ESP_FAIL; }
    FILE *f = fopen(meta, "rb");
    bool existed = f != NULL;
    if (f) {
        (void)fgets(magic, sizeof(magic), f);
        (void)fgets(date, sizeof(date), f);
        (void)fgets(old, sizeof(old), f);
        fclose(f);
        date[strcspn(date, "\r\n")] = 0;
        old[strcspn(old, "\r\n")] = 0;
    }
    if (!strcmp(magic, "pico-md-1\n") && !strcmp(old, revision) &&
        (!first_date || !strcmp(date, first_date))) {
        html_text_free(&t);
        return ESP_OK;
    }
    if (first_date && strlen(first_date) == 10)
        snprintf(date, sizeof(date), "%s", first_date);
    if (strlen(date) != 10) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        if (tm.tm_year >= 120)
            strftime(date, sizeof(date), "%Y-%m-%d", &tm);
        else
            strcpy(date, "----------");
    }
    if (existed && (e = clear_reader(path)) != ESP_OK) {
        html_text_free(&t);
        return e;
    }
    // 旧所有权清单保留到清理成功，失败下一次打开可以重试。
    // Keep old ownership until cleanup succeeds so the next open can retry.
    if (existed && !clean_old(meta, path, &t)) {
        html_text_free(&t);
        return ESP_FAIL;
    }
    snprintf(temp, sizeof(temp), "%s.part", meta);
    f = fopen(temp, "wb");
    if (!f) {
        html_text_free(&t);
        return ESP_FAIL;
    }
    bool ok = fprintf(f, "pico-md-1\n%s\n%s\n", date, revision) > 0;
    for (size_t i = 0; ok && i < t.image_count; i++)
        ok = fprintf(f, "%s\n", t.images[i]) > 0;
    if (ok)
        ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f))
        ok = false;
    if (ok)
        ok = replace_metadata(temp, meta);
    if (!ok)
        unlink(temp);
    html_text_free(&t);
    if (changed)
        *changed = existed && ok;
    return ok ? ESP_OK : ESP_FAIL;
}
esp_err_t document_store_refresh(const char *path, bool *changed) {
    esp_err_t e = document_store_sync_recover();
    if (e != ESP_OK) return e;
    document_verify_time_t timing = {0};
    SHELF_PERF_SCOPE(verify, SP_DOC_STORE, 1, 0);
    e = refresh_at(path, changed, NULL, verify.stage ? &timing : NULL);
    verify.b = e;
    if (verify.stage) shelf_perf_event(SP_DOC_VERIFY, (int)timing.body_us, (int)timing.assets_us, 1, 0);
    return e;
}
static bool remove_index(const char *path){
    char sibling[520];
    snprintf(sibling,sizeof(sibling),"%s.before",path);
    if(unlink(sibling)&&errno!=ENOENT)return false;
    snprintf(sibling,sizeof(sibling),"%s.part",path);
    if(unlink(sibling)&&errno!=ENOENT)return false;
    return !unlink(path)||errno==ENOENT;
}
esp_err_t document_store_delete(const char *path) {
    esp_err_t recovery = document_store_sync_recover();
    if (recovery != ESP_OK)
        return recovery;
    if (!allowed(path)&&!document_store_local_path(path))
        return ESP_ERR_INVALID_ARG;
    char meta[512];
    metadata(path, meta, sizeof(meta));
    struct stat st;
    if (!lstat(path,&st)) {
        if(!S_ISREG(st.st_mode))return ESP_FAIL;
        // 未打开的本地文档没有附件清单；显式删除前只为该篇补建。
        // A local document may be unopened; build only its own attachment manifest before deletion.
        if(restore_metadata(meta)&&lstat(meta,&st)&&errno==ENOENT)
            (void)refresh_at(path,NULL,NULL,NULL);
        if(unlink(path))return ESP_FAIL;
    } else if(errno!=ENOENT)return ESP_FAIL;
    esp_err_t e = clear_reader(path);
    if (e != ESP_OK)
        return e;
    if (!restore_metadata(meta)||!clean_old(meta, path, NULL))
        return ESP_FAIL;
    if(!remove_index(meta))return ESP_FAIL;
    preferences(path, meta, sizeof(meta));
    if(!remove_index(meta))return ESP_FAIL;
    index_path(path,"sync",meta,sizeof(meta));
    if(!remove_index(meta))return ESP_FAIL;
    return ESP_OK;
}
static int by_name(const void *a, const void *b) {
    const document_entry_t *x = a, *y = b;
    if (x->pinned != y->pinned)
        return x->pinned ? -1 : 1;
    return strcmp(x->name, y->name);
}
static void read_entry(const char *path, const struct stat *st, document_entry_t *entry) {
    memset(entry,0,sizeof(*entry));
    snprintf(entry->path,sizeof(entry->path),"%s",path);
    snprintf(entry->name,sizeof(entry->name),"%s",strrchr(path,'/')+1);
    entry->size=(uint32_t)st->st_size;
    char meta[512],line[80];
    metadata(path,meta,sizeof(meta));
    FILE *f=restore_metadata(meta)?fopen(meta,"rb"):NULL;
    if(f){
        if(fgets(line,sizeof(line),f)&&fgets(line,sizeof(line),f)){
            line[strcspn(line,"\r\n")]=0;
            snprintf(entry->date,sizeof(entry->date),"%.10s",line);
        }
        fclose(f);
    }
    read_flags(path,&entry->pinned,&entry->starred);
}
esp_err_t document_store_read_entry(const char *path, document_entry_t *entry) {
    if (!entry || (!allowed(path) && !document_store_local_path(path))) return ESP_ERR_INVALID_ARG;
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    if (!S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX)
        return ESP_ERR_INVALID_SIZE;
    read_entry(path, &st, entry);
    return ESP_OK;
}
typedef struct {document_entry_t **entries;size_t *count,capacity;} document_scan_t;
static esp_err_t scan_document(const char *path,const struct stat *st,void *context){
    document_scan_t *scan=context;
    if(st->st_size<0||(uint64_t)st->st_size>UINT32_MAX)return ESP_OK;
    if(*scan->count==scan->capacity){
        if (scan->capacity > SIZE_MAX / sizeof(document_entry_t) / 2) return ESP_ERR_NO_MEM;
        size_t next=scan->capacity?scan->capacity*2:16;
        document_entry_t *entries=realloc(*scan->entries,next*sizeof(*entries));
        if(!entries)return ESP_ERR_NO_MEM;
        *scan->entries=entries;scan->capacity=next;
    }
    read_entry(path, st, &(*scan->entries)[(*scan->count)++]);
    return ESP_OK;
}
static esp_err_t any_local_document(const char *path,const struct stat *st,void *context){
    (void)path;
    bool *found=context;
    *found=st->st_size>=0&&(uint64_t)st->st_size<=UINT32_MAX;
    return *found?ESP_ERR_INVALID_STATE:ESP_OK;
}
bool document_store_has_local(void){
    char root[LOCAL_PATH_MAX]=DOCUMENT_ROOT;
    bool found=false;
    (void)walk_documents(root,0,false,any_local_document,&found);
    return found;
}
esp_err_t document_store_scan(document_entry_t **entries, size_t *count) {
    if (!entries || !count)
        return ESP_ERR_INVALID_ARG;
    *entries = NULL;
    *count = 0;
    SHELF_PERF_SCOPE(store,SP_DOC_STORE,0,0);
    esp_err_t recovery = document_store_sync_recover();
    if (recovery != ESP_OK) {
        store.b=recovery;
        return recovery;
    }
    char root[LOCAL_PATH_MAX]=DOCUMENT_ROOT;
    document_scan_t scan={entries,count,0};
    esp_err_t result=walk_documents(root,0,false,scan_document,&scan);
    if (*count > 1)
        qsort(*entries, *count, sizeof(**entries), by_name);
    store.a=(int)*count;
    store.b=result;
    return result;
}

/* 同步事务：PREPARED可回滚；正文安装持久化后只向前恢复元数据。
 * Sync transactions: PREPARED rolls back;
 once body installation is durable,
 * recovery completes metadata before readers can reopen the document. */
#include <zlib.h>
#define SYNC_ROOT "/sdcard/.pico-sync"
#define SYNC_JOURNAL SYNC_ROOT "/transaction"
#define SYNC_BODY DOCUMENT_SYNC_STAGE "/body"
#define SYNC_BACKUP DOCUMENT_SYNC_STAGE "/previous"
#define SYNC_MAGIC UINT32_C(0x50535431)
typedef struct {
    uint32_t magic, phase, count, crc;
    char destination[288], body_sha256[65], previous_sha256[65], first_date[11];
    uint64_t body_bytes, previous_bytes;
    uint8_t previous, reused[DOCUMENT_SYNC_ASSETS_MAX];
    document_sync_asset_t assets[DOCUMENT_SYNC_ASSETS_MAX];
} sync_journal_t;
static atomic_flag s_sync_lock = ATOMIC_FLAG_INIT;
static atomic_uint s_import_revision;
static struct {
    unsigned revision;
    char path[LOCAL_PATH_MAX];
} s_import_changes[DOCUMENT_STORE_CHANGE_LIMIT];
unsigned document_store_revision(void) { return atomic_load(&s_import_revision); }
esp_err_t document_store_read_changes(unsigned after, unsigned through,
                                     document_entry_t *entries, size_t capacity) {
    unsigned count = through - after;
    if (!entries || !count || count > capacity || count > DOCUMENT_STORE_CHANGE_LIMIT)
        return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set(&s_sync_lock)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = through == document_store_revision() ? ESP_OK : ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; err == ESP_OK && i < count; ++i) {
        unsigned revision = after + i + 1;
        unsigned slot = revision % DOCUMENT_STORE_CHANGE_LIMIT;
        if (s_import_changes[slot].revision != revision || !s_import_changes[slot].path[0])
            err = ESP_ERR_NOT_FOUND;
        else
            err = document_store_read_entry(s_import_changes[slot].path, &entries[i]);
    }
    atomic_flag_clear(&s_sync_lock);
    return err;
}
static esp_err_t sync_recover_locked(void);
#ifdef PICO_HOST_SIMULATOR
int document_store_test_crash;
static bool sync_crash(int point) {
    return document_store_test_crash == point;
}
#else
static bool sync_crash(int point) {
    (void)point;
    return false;
}
#endif
bool document_store_safe_filename(const char *name) {
    if (!name || !*name || name[0] == '.' || strlen(name) > 240 || strpbrk(name, "/\\:*?\"<>|"))
        return false;
    size_t n = strlen(name);
    if (name[n - 1] == '.' || name[n - 1] == ' ')
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32 || *p == 127)
            return false;
    return true;
}
esp_err_t document_store_hash_file(const char *path, char hex[65], uint64_t *bytes) {
    if (!path || !hex)
        return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(path, "rb");
    if (!f)
        return ESP_ERR_NOT_FOUND;
    uint8_t *buffer = malloc(4096);
    if (!buffer) { fclose(f); return ESP_ERR_NO_MEM; }
    digest_t digest;
    digest_init(&digest);
    uint8_t sum[32];
    uint64_t total = 0;
    size_t n;
    while ((n = fread(buffer, 1, 4096, f))) {
        digest_update(&digest, buffer, n);
        total += n;
    }
    bool ok = !ferror(f);
    free(buffer);
    fclose(f);
    bool hashed = digest_finish(&digest, sum);
    digest_free(&digest);
    if (!ok || !hashed)
        return ESP_FAIL;
    for (unsigned i = 0; i < 32; ++i)
        snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    if (bytes)
        *bytes = total;
    return ESP_OK;
}
static bool sync_hex(const char *value) {
    if (strlen(value) != 64)
        return false;
    for (size_t i = 0; i < 64; ++i)
        if (!(value[i] >= '0' && value[i] <= '9') && !(value[i] >= 'a' && value[i] <= 'f'))
            return false;
    return true;
}
static bool sync_destination(const char *path) {
    if (!path || strncmp(path, "/sdcard/", 8) || strlen(path) >= 288)
        return false;
    const char *base = strrchr(path, '/');
    if (!base || !document_store_safe_filename(base + 1))
        return false;
    const char *ext = strrchr(base, '.');
    if (!ext)
        return false;
    if (!strcasecmp(ext, ".md"))
        return allowed(path);
    sync_file_kind_t kind = sync_file_kind(base + 1);
    if (kind == SYNC_FILE_IMAGE || kind == SYNC_FILE_FONT) {
        const char *directory = sync_file_directory(kind);
        return (size_t)(base - path) == strlen(directory) &&
               !strncmp(path, directory, strlen(directory));
    }
    if (kind != SYNC_FILE_BOOK)
        return false;
    for (const char *p = path + 8; p < base;) {
        const char *end = strchr(p, '/');
        if (!end)
            return false;
        size_t n = end - p;
        if (!n || (n == 1 && *p == '.') || (n == 2 && !memcmp(p, "..", 2)))
            return false;
        if (*p == '.' || memchr(p, '\\', n))
            return false;
        p = end + 1;
    }
    return base > path + 8;
}
static bool sync_asset_ok(const document_sync_asset_t *a) {
    return memchr(a->name, 0, sizeof(a->name)) && document_store_safe_filename(a->name) &&
           strrchr(a->name, '.') && !strcasecmp(strrchr(a->name, '.'), ".png") &&
           memchr(a->sha256, 0, sizeof(a->sha256)) && sync_hex(a->sha256) && a->bytes >= 67 &&
           a->bytes <= 17 * 1024 * 1024;
}
static int sync_exists(const char *path, struct stat *out) {
    struct stat local;
    if (!out)
        out = &local;
    if (!lstat(path, out))
        return 1;
    return errno == ENOENT ? 0 : -1;
}
static esp_err_t sync_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return ESP_FAIL;
    bool ok = fsync(fileno(f)) == 0;
    if (fclose(f))
        ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
static esp_err_t sync_parent(const char *path) {
#ifdef PICO_HOST_SIMULATOR
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return ESP_ERR_INVALID_ARG;
    *slash = 0;
    int fd = open(parent, O_RDONLY);
    if (fd < 0)
        return ESP_FAIL;
    bool ok = fsync(fd) == 0;
    if (close(fd))
        ok = false;
    return ok ? ESP_OK : ESP_FAIL;
#else
    // FatFs rename/remove synchronously call sync_fs; POSIX directory fds are unsupported.
    // FAT directory persistence and the SD controller's power-loss behavior still need hardware tests.
    (void)path;
    return ESP_OK;
#endif
}
static bool sync_matches(const char *path, const char *hash, uint64_t bytes) {
    char actual[65];
    uint64_t size = 0;
    return document_store_hash_file(path, actual, &size) == ESP_OK && size == bytes && !strcmp(actual, hash);
}
static esp_err_t sync_installed_valid(const sync_journal_t *j) {
    if (!sync_matches(j->destination, j->body_sha256, j->body_bytes))
        return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i = 0; i < j->count; ++i) {
        char path[512];
        snprintf(path, sizeof(path), DOCUMENT_ROOT "/attachments/%s", j->assets[i].name);
        if (!sync_matches(path, j->assets[i].sha256, j->assets[i].bytes))
            return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
// 提升前必须完整写入并校验候选日志；FatFs先移除旧名字，再提升候选。
// A fully synced CRC-valid candidate bridges FatFs removal of the old name and promotion.
static esp_err_t sync_promote_journal(void) {
    if (unlink(SYNC_JOURNAL) && errno != ENOENT) return ESP_FAIL;
    if (rename(SYNC_JOURNAL ".part", SYNC_JOURNAL)) return ESP_FAIL;
    return sync_parent(SYNC_JOURNAL);
}
static esp_err_t sync_read_record(const char *path, sync_journal_t *j);
static esp_err_t sync_write_journal(sync_journal_t *j) {
    j->crc = 0;
    j->crc = crc32(0, (const Bytef *)j, sizeof(*j));
    FILE *f = fopen(SYNC_JOURNAL ".part", "wb");
    if (!f)
        return ESP_FAIL;
    bool ok = fwrite(j, 1, sizeof(*j), f) == sizeof(*j) && !fflush(f) && !fsync(fileno(f));
    if (fclose(f))
        ok = false;
    if (!ok) return ESP_FAIL;
    // 移除旧日志前回读CRC；复用现有对象，避免另一份大日志临时分配。
    // Read back CRC before removing the old checkpoint, reusing this object to avoid another large allocation.
    uint32_t expected_crc = j->crc;
    esp_err_t checked = sync_read_record(SYNC_JOURNAL ".part", j);
    if (checked != ESP_OK) return checked;
    if (j->crc != expected_crc) return ESP_ERR_INVALID_RESPONSE;
    return sync_promote_journal();
}
static esp_err_t sync_read_record(const char *path, sync_journal_t *j) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    bool ok = fread(j, 1, sizeof(*j), f) == sizeof(*j) && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    uint32_t saved = j->crc;
    j->crc = 0;
    if (!ok || crc32(0, (const Bytef *)j, sizeof(*j)) != saved || j->magic != SYNC_MAGIC || j->phase < 1 ||
        j->phase > 3 || j->count > DOCUMENT_SYNC_ASSETS_MAX || j->previous > 1 ||
        !memchr(j->destination, 0, sizeof(j->destination)) || !sync_destination(j->destination) ||
        !memchr(j->body_sha256, 0, sizeof(j->body_sha256)) || !sync_hex(j->body_sha256) ||
        j->body_bytes > 64 * 1024 * 1024 || !memchr(j->previous_sha256, 0, sizeof(j->previous_sha256)) ||
        (j->previous && !sync_hex(j->previous_sha256)) || !memchr(j->first_date, 0, sizeof(j->first_date)))
        return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i = 0; i < j->count; ++i)
        if (!sync_asset_ok(&j->assets[i]) || j->reused[i] > 1)
            return ESP_ERR_INVALID_RESPONSE;
    j->crc = saved;
    return ESP_OK;
}
// 候选必须属于同一笔事务，不能把另一篇文章的记录当成较新的阶段。
// A candidate must describe the same transaction, not merely have a higher phase number.
static bool sync_same_transaction(const sync_journal_t *a, const sync_journal_t *b) {
    if (a->count != b->count || a->previous != b->previous || a->body_bytes != b->body_bytes ||
        a->previous_bytes != b->previous_bytes || strcmp(a->destination, b->destination) ||
        strcmp(a->body_sha256, b->body_sha256) || strcmp(a->previous_sha256, b->previous_sha256) ||
        strcmp(a->first_date, b->first_date)) return false;
    for (unsigned i = 0; i < a->count; ++i)
        if (a->reused[i] != b->reused[i] || a->assets[i].bytes != b->assets[i].bytes ||
            strcmp(a->assets[i].name, b->assets[i].name) ||
            strcmp(a->assets[i].sha256, b->assets[i].sha256)) return false;
    return true;
}
static esp_err_t sync_read_journal(sync_journal_t *j) {
    esp_err_t current = sync_read_record(SYNC_JOURNAL, j);
    // 损坏的正式日志保留恢复门控，不用候选静默掩盖。/ Corrupt committed logs stay gated rather than being silently hidden by a candidate.
    if (current != ESP_OK && current != ESP_ERR_NOT_FOUND) return current;
    sync_journal_t *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;
    esp_err_t staged = sync_read_record(SYNC_JOURNAL ".part", candidate);
    esp_err_t result = current;
    if (staged == ESP_OK) {
        if (current == ESP_OK && !sync_same_transaction(j, candidate)) result = ESP_ERR_INVALID_STATE;
        else if (current == ESP_ERR_NOT_FOUND || candidate->phase >= j->phase) {
            // 先提升有效候选再进行恢复，下一次写日志不会覆盖唯一的新阶段。
            // Promote before recovery so the next journal write cannot overwrite the only new checkpoint.
            result = sync_promote_journal();
            if (result == ESP_OK) *j = *candidate;
        }
    } else if (staged != ESP_ERR_NOT_FOUND && current == ESP_ERR_NOT_FOUND) result = staged;
    else if (staged != ESP_OK && staged != ESP_ERR_NOT_FOUND && staged != ESP_ERR_INVALID_RESPONSE)
        result = staged;
    free(candidate);
    return result;
}
static esp_err_t sync_clear_stage(void) {
    DIR *d = opendir(DOCUMENT_SYNC_STAGE "/attachments");
    bool ok = true;
    if (!d && errno != ENOENT)
        return ESP_FAIL;
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            if (!document_store_safe_filename(entry->d_name)) {
                ok = false;
                continue;
            }
            char path[512];
            snprintf(path, sizeof(path), DOCUMENT_SYNC_STAGE "/attachments/%s", entry->d_name);
            if (unlink(path) && errno != ENOENT)
                ok = false;
            document_image_remove_cache(path);
        }
        closedir(d);
    }
    if (unlink(SYNC_BODY) && errno != ENOENT)
        ok = false;
    if (unlink(SYNC_BACKUP) && errno != ENOENT)
        ok = false;
    if (unlink(DOCUMENT_SYNC_STAGE "/manifest.json") && errno != ENOENT)
        ok = false;
    if (unlink(SYNC_JOURNAL ".part") && errno != ENOENT)
        ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
static esp_err_t sync_abort_locked(void) {
    esp_err_t e = sync_recover_locked();
    if (e != ESP_OK)
        return e;
    return sync_clear_stage();
}
static esp_err_t sync_finish_metadata(sync_journal_t *j) {
    esp_err_t verified = sync_installed_valid(j);
    if (verified != ESP_OK)
        return verified;
    sync_file_kind_t kind = sync_file_kind(j->destination);
    if (kind == SYNC_FILE_MARKDOWN || kind == SYNC_FILE_BOOK) {
        verified = clear_reader(j->destination);
        if (verified != ESP_OK) return verified;
    }
    if (allowed(j->destination)) {
        bool changed;
        esp_err_t e = refresh_at(j->destination, &changed, j->first_date[0] ? j->first_date : NULL,NULL);
        if (e != ESP_OK)
            return e;
    }
    // First import metadata and local pin/star preferences are owned by document_store_refresh.
    // Only successfully committed MD is recorded as recently downloaded.
    if (allowed(j->destination)) {
        char path[512], temporary[520];
        snprintf(path, sizeof(path), DOCUMENT_ROOT "/.index/%s.sync", j->destination + 17);
        snprintf(temporary, sizeof(temporary), "%s.part", path);
        FILE *f = fopen(temporary, "wb");
        if (!f)
            return ESP_FAIL;
        struct timeval now = {0};
        gettimeofday(&now, NULL);
        bool ok = fprintf(f, "%llu\n",
                          (unsigned long long)now.tv_sec * 1000000 + (unsigned long long)now.tv_usec) > 0 &&
                  !fflush(f) && !fsync(fileno(f));
        if (fclose(f))
            ok = false;
        if (!ok || !replace_metadata(temporary, path))
            return ESP_FAIL;
    }
    if (allowed(j->destination)) {
        char meta[512];
        metadata(j->destination, meta, sizeof(meta));
        esp_err_t e = sync_file(meta);
        if (e != ESP_OK)
            return e;
        e = sync_parent(meta);
        if (e != ESP_OK)
            return e;
    }
    j->phase = 3;
    return sync_write_journal(j);
}
static esp_err_t sync_recover_locked(void) {
    sync_journal_t *j = calloc(1, sizeof(*j));
    if (!j)
        return ESP_ERR_NO_MEM;
    esp_err_t e = sync_read_journal(j);
    if (e == ESP_ERR_NOT_FOUND) {
        int backup = sync_exists(SYNC_BACKUP, NULL), partial = sync_exists(SYNC_JOURNAL ".part", NULL);
        free(j);
        if (backup < 0 || partial < 0)
            return ESP_FAIL;
        // With a lost journal, neither an orphan backup nor a partially written journal may be discarded.
        return backup || partial ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (e != ESP_OK) {
        free(j);
        return e;
    }
    if (j->phase == 1) {
        int backup = sync_exists(SYNC_BACKUP, NULL), body = sync_exists(SYNC_BODY, NULL);
        if (backup < 0 || body < 0)
            e = ESP_FAIL;
        else if (backup) {
            if (!j->previous || !sync_matches(SYNC_BACKUP, j->previous_sha256, j->previous_bytes))
                e = ESP_ERR_INVALID_RESPONSE;
            if (e == ESP_OK && unlink(j->destination) && errno != ENOENT)
                e = ESP_FAIL;
            if (e == ESP_OK && rename(SYNC_BACKUP, j->destination))
                e = ESP_FAIL;
            if (e == ESP_OK)
                e = sync_file(j->destination);
            if (e == ESP_OK)
                e = sync_parent(j->destination);
        } else if (j->previous) {
            if (!sync_matches(j->destination, j->previous_sha256, j->previous_bytes))
                e = ESP_ERR_INVALID_RESPONSE;
        } else if (!body) {
            if (unlink(j->destination) && errno != ENOENT)
                e = ESP_FAIL;
            if (e == ESP_OK)
                e = sync_parent(j->destination);
        }
        if (e == ESP_OK)
            for (unsigned i = 0; i < j->count; ++i)
                if (!j->reused[i]) {
                    char path[512];
                    snprintf(path, sizeof(path), DOCUMENT_ROOT "/attachments/%s", j->assets[i].name);
                    if (unlink(path) && errno != ENOENT) {
                        e = ESP_FAIL;
                        break;
                    }
                    document_image_remove_cache(path);
                }
    } else if (j->phase == 2)
        e = sync_finish_metadata(j);
    else
        e = sync_installed_valid(j);
    // Reverify phase 3 too before removing the only old backup.
    if (e == ESP_OK)
        e = sync_clear_stage();
    if (e == ESP_OK && unlink(SYNC_JOURNAL) && errno != ENOENT)
        e = ESP_FAIL;
    if (e == ESP_OK)
        e = sync_parent(SYNC_JOURNAL);
    free(j);
    return e;
}
static esp_err_t sync_prepare_locked(void) {
    esp_err_t e = sync_recover_locked();
    if (e != ESP_OK)
        return e;
    (void)mkdir(DOCUMENT_ROOT, 0700);
    (void)mkdir(DOCUMENT_ROOT "/attachments", 0700);
    (void)mkdir(DOCUMENT_ROOT "/.cache", 0700);
    (void)mkdir(SYNC_ROOT, 0700);
    (void)mkdir(DOCUMENT_SYNC_STAGE, 0700);
    (void)mkdir(DOCUMENT_SYNC_STAGE "/attachments", 0700);
    return sync_clear_stage();
}
static uint32_t sync_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
// 只读有界文件头，不在同步线程载入字体或改变显示上下文。/ Inspect bounded headers without loading fonts or changing rendering contexts.
static bool sync_resource_header(FILE *f, const char *ext, uint64_t bytes) {
    uint8_t head[32];
    if (fread(head, 1, sizeof(head), f) != sizeof(head)) return false;
    if (!strcasecmp(ext, ".png")) {
        const uint8_t signature[] = {137,80,78,71,13,10,26,10};
        uint32_t w = sync_be32(head + 16), h = sync_be32(head + 20);
        return bytes >= 57 && !memcmp(head, signature, 8) && sync_be32(head + 8) == 13 &&
               !memcmp(head + 12, "IHDR", 4) && w && h && w <= 8192 && h <= 8192;
    }
    if (strcasecmp(ext, ".ttf")) {
        if (head[0] != 0xff || head[1] != 0xd8 || fseek(f, -2, SEEK_END)) return false;
        return fgetc(f) == 0xff && fgetc(f) == 0xd9;
    }
    if (sync_be32(head) != 0x00010000 && memcmp(head, "true", 4)) return false;
    unsigned count = (unsigned)head[4] << 8 | head[5], found = 0;
    // 阅读器的字体表头窗口最多32项；拒绝截断/越界表及CFF字体。
    // The reader's table window holds 32 entries; reject truncated/out-of-range tables and CFF fonts.
    if (!count || count > 32 || bytes < 12 + 32 * 16 || fseek(f, 12, SEEK_SET)) return false;
    const char *required[] = {"cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp"};
    for (unsigned i = 0; i < count; ++i) {
        uint8_t table[16];
        if (fread(table, 1, sizeof(table), f) != sizeof(table)) return false;
        uint32_t offset = sync_be32(table + 8), length = sync_be32(table + 12);
        if (offset < 12 + count * 16 || offset > bytes || length > bytes - offset) return false;
        for (unsigned k = 0; k < sizeof(required) / sizeof(required[0]); ++k) {
            if (memcmp(table, required[k], 4)) continue;
            if (!length || (found & (1u << k))) return false;
            found |= 1u << k;
        }
    }
    return found == 127;
}
esp_err_t document_store_sync_validate(const char *filename, const document_sync_asset_t *assets,
                                       size_t count) {
    if (!document_store_safe_filename(filename) || count > DOCUMENT_SYNC_ASSETS_MAX || (count && !assets))
        return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(filename, '.');
    if (!ext)
        return ESP_ERR_INVALID_ARG;
    sync_file_kind_t kind = sync_file_kind(filename);
    bool md = kind == SYNC_FILE_MARKDOWN;
    if (kind == SYNC_FILE_UNSUPPORTED || (!md && count))
        return ESP_ERR_INVALID_ARG;
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!sync_asset_ok(&assets[i]))
            return ESP_ERR_INVALID_ARG;
        for (size_t k = 0; k < i; ++k)
            if (!strcasecmp(assets[k].name, assets[i].name))
                return ESP_ERR_INVALID_ARG;
        total += assets[i].bytes;
        if (total > 32 * 1024 * 1024)
            return ESP_ERR_INVALID_SIZE;
        char path[512], hash[65];
        uint64_t bytes;
        snprintf(path, sizeof(path), DOCUMENT_SYNC_STAGE "/attachments/%s", assets[i].name);
        if (document_store_hash_file(path, hash, &bytes) != ESP_OK || bytes != assets[i].bytes ||
            strcmp(hash, assets[i].sha256))
            return ESP_ERR_INVALID_RESPONSE;
        FILE *png = fopen(path, "rb");
        uint8_t head[29];
        bool dimensions = png && fread(head, 1, sizeof(head), png) == sizeof(head);
        if (png)
            fclose(png);
        uint32_t w = dimensions ? ((uint32_t)head[16] << 24) | ((uint32_t)head[17] << 16) |
                                      ((uint32_t)head[18] << 8) | head[19]
                                : 0;
        uint32_t h = dimensions ? ((uint32_t)head[20] << 24) | ((uint32_t)head[21] << 16) |
                                      ((uint32_t)head[22] << 8) | head[23]
                                : 0;
        if (!w || w > 684 || !h || (uint64_t)w * h > 32 * 1024 * 1024)
            return ESP_ERR_INVALID_SIZE;
        document_image_t image;
        if (!document_image_open(path, &image))
            return ESP_ERR_INVALID_RESPONSE;
        document_image_remove_cache(path);
    }
    if (md) {
        html_text_t text = {0};
        esp_err_t e = parse_file(SYNC_BODY, &text, NULL);
        for (size_t i = 0; e == ESP_OK && i < text.image_count; ++i) {
            char path[512];
            if (!markdown_attachment_path(DOCUMENT_ROOT "/validation.md", text.images[i], path,
                                          sizeof(path))) {
                e = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            const char *base = strrchr(path, '/') + 1;
            bool found = false;
            for (size_t k = 0; k < count; ++k)
                if (!strcmp(base, assets[k].name)) {
                    found = true;
                    break;
                }
            if (!found)
                e = ESP_ERR_INVALID_RESPONSE;
        }
        for (size_t k = 0; e == ESP_OK && k < count; ++k) {
            bool found = false;
            for (size_t i = 0; i < text.image_count; ++i) {
                char path[512];
                if (markdown_attachment_path(DOCUMENT_ROOT "/validation.md", text.images[i], path,
                                             sizeof(path)) &&
                    !strcmp(strrchr(path, '/') + 1, assets[k].name)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                e = ESP_ERR_INVALID_RESPONSE;
        }
        html_text_free(&text);
        return e;
    }
    struct stat st;
    if (stat(SYNC_BODY, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > sync_file_limit(kind))
        return ESP_ERR_INVALID_SIZE;
    if (kind == SYNC_FILE_IMAGE || kind == SYNC_FILE_FONT) {
        FILE *f = fopen(SYNC_BODY, "rb");
        bool ok = f && sync_resource_header(f, ext, (uint64_t)st.st_size);
        if (f) fclose(f);
        return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    if (!strcasecmp(ext, ".epub")) {
        FILE *f = fopen(SYNC_BODY, "rb");
        unsigned char head[4];
        bool ok = f && fread(head, 1, 4, f) == 4 && !memcmp(head, "PK\003\004", 4);
        if (f)
            fclose(f);
        if (!ok)
            return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
static esp_err_t sync_commit_locked(const char *destination, const char *body_sha256, uint64_t body_bytes,
                                    const document_sync_asset_t *assets, size_t count, bool *unchanged) {
    if (unchanged)
        *unchanged = false;
    if (!sync_destination(destination) || !body_sha256 || !sync_hex(body_sha256) ||
        body_bytes > 64 * 1024 * 1024)
        return ESP_ERR_INVALID_ARG;
    if (!sync_matches(SYNC_BODY, body_sha256, body_bytes))
        return ESP_ERR_INVALID_RESPONSE;
    esp_err_t e = sync_recover_locked();
    if (e != ESP_OK)
        return e;
    e = document_store_sync_validate(strrchr(destination, '/') + 1, assets, count);
    if (e != ESP_OK)
        return e;
    sync_file_kind_t kind = sync_file_kind(destination);
    if (kind == SYNC_FILE_IMAGE || kind == SYNC_FILE_FONT) {
        const char *directory = sync_file_directory(kind);
        struct stat folder;
        if (mkdir(directory, 0700) && errno != EEXIST) return ESP_FAIL;
        if (lstat(directory, &folder) || !S_ISDIR(folder.st_mode)) return ESP_ERR_INVALID_STATE;
    }
    sync_journal_t *j = calloc(1, sizeof(*j));
    if (!j)
        return ESP_ERR_NO_MEM;
    j->magic = SYNC_MAGIC;
    j->phase = 1;
    j->count = count;
    snprintf(j->destination, sizeof(j->destination), "%s", destination);
    strcpy(j->body_sha256, body_sha256);
    j->body_bytes = body_bytes;
    struct stat st;
    int prior = sync_exists(destination, &st);
    if (prior < 0) {
        free(j);
        return ESP_FAIL;
    }
    j->previous = prior;
    if (j->previous && !S_ISREG(st.st_mode)) {
        free(j);
        return ESP_ERR_INVALID_STATE;
    }
    char old[65], current[65];
    uint64_t before = 0, after = 0;
    bool same = j->previous && document_store_hash_file(destination, old, &before) == ESP_OK &&
                document_store_hash_file(SYNC_BODY, current, &after) == ESP_OK && before == after &&
                !strcmp(old, current);
    if (j->previous &&
        document_store_hash_file(destination, j->previous_sha256, &j->previous_bytes) != ESP_OK) {
        free(j);
        return ESP_FAIL;
    }
    if (allowed(destination)) {
        char meta[512], line[80];
        metadata(destination, meta, sizeof(meta));
        if (!restore_metadata(meta)) { free(j); return ESP_FAIL; }
        int exists = sync_exists(meta, NULL);
        if (exists < 0) {
            free(j);
            return ESP_FAIL;
        }
        if (exists) {
            FILE *f = fopen(meta, "rb");
            if (!f) {
                free(j);
                return ESP_FAIL;
            }
            if (fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (strlen(line) == 10)
                    snprintf(j->first_date, sizeof(j->first_date), "%.10s", line);
            }
            bool ok = !ferror(f);
            fclose(f);
            if (!ok) {
                free(j);
                return ESP_FAIL;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        j->assets[i] = assets[i];
        char path[512], hash[65];
        uint64_t bytes;
        snprintf(path, sizeof(path), DOCUMENT_ROOT "/attachments/%s", assets[i].name);
        int exists = sync_exists(path, &st);
        if (exists < 0) {
            free(j);
            return ESP_FAIL;
        }
        if (exists) {
            if (!S_ISREG(st.st_mode) || document_store_hash_file(path, hash, &bytes) != ESP_OK ||
                bytes != assets[i].bytes || strcmp(hash, assets[i].sha256)) {
                free(j);
                return ESP_ERR_INVALID_STATE;
            }
            j->reused[i] = 1;
        } else
            same = false;
    }
    if (same) {
        if (unchanged)
            *unchanged = true;
        free(j);
        return sync_clear_stage();
    }
    e = sync_file(SYNC_BODY);
    for (size_t i = 0; e == ESP_OK && i < count; ++i) {
        char path[512];
        snprintf(path, sizeof(path), DOCUMENT_SYNC_STAGE "/attachments/%s", assets[i].name);
        e = sync_file(path);
    }
    if (e == ESP_OK)
        e = sync_parent(SYNC_BODY);
    if (e == ESP_OK)
        e = sync_parent(DOCUMENT_SYNC_STAGE "/attachments/x");
    if (e == ESP_OK)
        e = sync_write_journal(j);
    if (e != ESP_OK) {
        free(j);
        return e;
    }
    // 文档事务已开始，失败也可能需要恢复；不使书架封面失效。/ Invalidate the document list at the journal boundary without retiring book covers.
    unsigned revision = 0;
    if (kind == SYNC_FILE_MARKDOWN) {
        revision = atomic_fetch_add(&s_import_revision, 1) + 1;
        s_import_changes[revision % DOCUMENT_STORE_CHANGE_LIMIT].path[0] = 0;
    }
    if (sync_crash(1)) {
        free(j);
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count && e == ESP_OK; ++i)
        if (!j->reused[i]) {
            char staged[512], target[512];
            snprintf(staged, sizeof(staged), DOCUMENT_SYNC_STAGE "/attachments/%s", assets[i].name);
            snprintf(target, sizeof(target), DOCUMENT_ROOT "/attachments/%s", assets[i].name);
            if (rename(staged, target))
                e = ESP_FAIL;
        }
    if (e == ESP_OK && sync_crash(2)) {
        free(j);
        return ESP_FAIL;
    }
    if (e == ESP_OK && j->previous && rename(destination, SYNC_BACKUP))
        e = ESP_FAIL;
    if (e == ESP_OK && sync_crash(3)) {
        free(j);
        return ESP_FAIL;
    }
    if (e == ESP_OK && rename(SYNC_BODY, destination))
        e = ESP_FAIL;
    if (e == ESP_OK && sync_crash(4)) {
        free(j);
        return ESP_FAIL;
    }
    if (e == ESP_OK)
        e = sync_file(destination);
    if (e == ESP_OK)
        e = sync_parent(destination);
    if (e == ESP_OK)
        e = sync_parent(SYNC_BODY);
    if (e == ESP_OK)
        e = sync_parent(DOCUMENT_ROOT "/attachments/x");
    if (e == ESP_OK) {
        j->phase = 2;
        e = sync_write_journal(j);
    }
    if (e == ESP_OK && sync_crash(5)) {
        free(j);
        return ESP_FAIL;
    }
    if (e == ESP_OK)
        e = sync_finish_metadata(j);
    if (e == ESP_OK && sync_crash(6)) {
        free(j);
        return ESP_FAIL;
    }
    free(j);
    // Roll back PREPARED failures; retain the recovery gate if forward metadata recovery fails.
    esp_err_t recovered = sync_recover_locked();
    if (recovered != ESP_OK) return recovered;
    if (!sync_matches(destination, body_sha256, body_bytes))
        return e == ESP_OK ? ESP_ERR_INVALID_RESPONSE : e;
    for (size_t i = 0; i < count; ++i) {
        char path[512];
        snprintf(path, sizeof(path), DOCUMENT_ROOT "/attachments/%s", assets[i].name);
        if (!sync_matches(path, assets[i].sha256, assets[i].bytes)) return ESP_ERR_INVALID_RESPONSE;
    }
    if (kind == SYNC_FILE_MARKDOWN) {
        unsigned slot = revision % DOCUMENT_STORE_CHANGE_LIMIT;
        s_import_changes[slot].revision = revision;
        snprintf(s_import_changes[slot].path, sizeof(s_import_changes[slot].path), "%s", destination);
    }
    return ESP_OK;
}
bool document_store_hash_bytes(const void *data, size_t length, char hex[65]) {
    digest_t digest;
    digest_init(&digest);
    digest_update(&digest, data, length);
    uint8_t sum[32];
    bool hashed = digest_finish(&digest, sum);
    digest_free(&digest);
    if (!hashed) {
        hex[0] = 0;
        return false;
    }
    for (unsigned i = 0; i < 32; ++i)
        snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    return true;
}

#define LOCAL_MOVE_FILE DOCUMENT_ROOT "/.index/.move"
#define LOCAL_MOVE_TEMP LOCAL_MOVE_FILE ".part"
typedef struct {
    char magic[8],source[LOCAL_PATH_MAX],target[LOCAL_PATH_MAX];
    uint32_t size,crc;
} local_move_t;
static uint32_t local_move_crc(const local_move_t *move){
    return (uint32_t)crc32(0,(const Bytef *)move,sizeof(*move)-sizeof(move->crc));
}
static int local_file_state(const char *path,struct stat *st){
    if(!lstat(path,st))return S_ISREG(st->st_mode)?1:-1;
    return errno==ENOENT?0:-1;
}
static const char *const move_indexes[]={"pico","prefs","sync"};
static esp_err_t move_indexes_forward(const local_move_t *move){
    if(!ensure_index(move->target))return ESP_FAIL;
    for(unsigned i=0;i<sizeof(move_indexes)/sizeof(move_indexes[0]);++i){
        char old[512],next[512],backup[520];
        index_path(move->source,move_indexes[i],old,sizeof(old));
        index_path(move->target,move_indexes[i],next,sizeof(next));
        snprintf(backup,sizeof(backup),"%s.before",old);
        struct stat st;
        if(!restore_metadata(old))return ESP_FAIL;
        int state=local_file_state(old,&st);
        if(state<0)return ESP_FAIL;
        if(state){
            if(local_file_state(next,&st)!=0)return ESP_FAIL;
            // 当前索引已完整；先清旧备份，避免中断后把旧路径的记录恢复回来。
            // Remove the superseded backup before moving the current index.
            if((unlink(backup)&&errno!=ENOENT)||rename(old,next))return ESP_FAIL;
        }
        if(sync_parent(old)!=ESP_OK||sync_parent(next)!=ESP_OK)return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t move_reader_forward(const local_move_t *move){
    esp_err_t err=book_progress_copy_for_move(move->source,move->target,move->size);
    if(err==ESP_OK)err=copy_marks(move->source,move->target);
    char title[121];
    if(err==ESP_OK){
        err=book_title_get_checked(move->source,title,sizeof(title));
        if(err==ESP_OK)err=book_title_set(move->target,title);
        else if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    }
    if(err==ESP_OK)err=clear_reader(move->source);
    if(err==ESP_OK)err=book_title_clear(move->source);
    return err;
}
static esp_err_t recover_local_move(void){
    FILE *f=fopen(LOCAL_MOVE_FILE,"rb");
    if(!f)return errno==ENOENT?ESP_OK:ESP_FAIL;
    local_move_t move;
    bool ok=fread(&move,1,sizeof(move),f)==sizeof(move)&&fgetc(f)==EOF&&!ferror(f);
    if(fclose(f))ok=false;
    if(!ok||memcmp(move.magic,"IFMDMV1",8)||move.crc!=local_move_crc(&move)||
       !memchr(move.source,0,sizeof(move.source))||!memchr(move.target,0,sizeof(move.target))||
       !document_store_local_path(move.source)||!document_store_local_path(move.target)||
       !strcasecmp(move.source,move.target))return ESP_FAIL;
    struct stat st;
    int source=local_file_state(move.source,&st),target=local_file_state(move.target,&st);
    esp_err_t err=ESP_OK;
    if(source==0&&target==1&&st.st_size>=0&&(uint64_t)st.st_size==move.size){
        if(sync_parent(move.source)!=ESP_OK||sync_parent(move.target)!=ESP_OK)return ESP_FAIL;
        err=move_indexes_forward(&move);
        if(err==ESP_OK)err=move_reader_forward(&move);
    }else if(source!=1||target!=0)return ESP_FAIL;
    if(err!=ESP_OK)return err;
    return !unlink(LOCAL_MOVE_FILE)?sync_parent(LOCAL_MOVE_FILE):ESP_FAIL;
}
static esp_err_t local_move_start(const char *source,const char *target,bool *moved){
    if(!document_store_local_path(source)||!document_store_local_path(target)||!strcasecmp(source,target))
        return ESP_ERR_INVALID_ARG;
    struct stat st;
    if(local_file_state(target,&st)!=0)return ESP_ERR_INVALID_STATE;
    if(local_file_state(source,&st)!=1||st.st_size<0||(uint64_t)st.st_size>UINT32_MAX)return ESP_FAIL;
    uint32_t size=(uint32_t)st.st_size;
    if(!ensure_index(source)||!ensure_index(target))return ESP_FAIL;
    for(unsigned i=0;i<sizeof(move_indexes)/sizeof(move_indexes[0]);++i){
        char next[512];
        index_path(target,move_indexes[i],next,sizeof(next));
        if(!restore_metadata(next)||local_file_state(next,&st)!=0)return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err=clear_reader(target);
    if(err==ESP_OK)err=book_title_clear(target);
    if(err!=ESP_OK)return err;
    local_move_t move={.magic="IFMDMV1",.size=size};
    snprintf(move.source,sizeof(move.source),"%s",source);
    snprintf(move.target,sizeof(move.target),"%s",target);
    move.crc=local_move_crc(&move);
    FILE *f=fopen(LOCAL_MOVE_TEMP,"wb");
    if(!f)return ESP_FAIL;
    bool ok=fwrite(&move,1,sizeof(move),f)==sizeof(move)&&!fflush(f)&&!fsync(fileno(f));
    if(fclose(f))ok=false;
    if(!ok||rename(LOCAL_MOVE_TEMP,LOCAL_MOVE_FILE)){unlink(LOCAL_MOVE_TEMP);return ESP_FAIL;}
    if(sync_parent(LOCAL_MOVE_FILE)!=ESP_OK)return ESP_FAIL;
    *moved=rename(source,target)==0;
    err=recover_local_move();
    return *moved?err:ESP_FAIL;
}
esp_err_t document_store_move(const char *source,const char *target,bool *moved){
    if(!moved)return ESP_ERR_INVALID_ARG;
    *moved=false;
    if(atomic_flag_test_and_set(&s_sync_lock))return ESP_FAIL;
    esp_err_t err=sync_recover_locked();
    if(err==ESP_OK)err=recover_local_move();
    if(err==ESP_OK)err=local_move_start(source,target,moved);
    atomic_flag_clear(&s_sync_lock);
    return err;
}
esp_err_t document_store_sync_recover(void) {
    if (atomic_flag_test_and_set(&s_sync_lock))
        return ESP_ERR_INVALID_STATE;
    esp_err_t e = sync_recover_locked();
    if(e==ESP_OK)e=recover_local_move();
    atomic_flag_clear(&s_sync_lock);
    return e;
}
esp_err_t document_store_sync_prepare(void) {
    if (atomic_flag_test_and_set(&s_sync_lock))
        return ESP_ERR_INVALID_STATE;
    esp_err_t e = recover_local_move();
    if(e==ESP_OK)e=sync_prepare_locked();
    atomic_flag_clear(&s_sync_lock);
    return e;
}
esp_err_t document_store_sync_abort(void) {
    if (atomic_flag_test_and_set(&s_sync_lock))
        return ESP_ERR_INVALID_STATE;
    esp_err_t e = sync_abort_locked();
    atomic_flag_clear(&s_sync_lock);
    return e;
}
esp_err_t document_store_sync_commit(const char *destination, const char *body_sha256, uint64_t body_bytes,
                                     const document_sync_asset_t *assets, size_t count, bool *unchanged) {
    if (atomic_flag_test_and_set(&s_sync_lock))
        return ESP_ERR_INVALID_STATE;
    esp_err_t e = sync_commit_locked(destination, body_sha256, body_bytes, assets, count, unchanged);
    atomic_flag_clear(&s_sync_lock);
    return e;
}
esp_err_t document_store_sync_collect_assets(document_sync_asset_t assets[64], size_t *count) {
    if (!assets || !count)
        return ESP_ERR_INVALID_ARG;
    *count = 0;
    memset(assets, 0, 64 * sizeof(*assets));
    html_text_t text = {0};
    esp_err_t e = parse_file(SYNC_BODY, &text, NULL);
    for (size_t i = 0; e == ESP_OK && i < text.image_count; ++i) {
        char path[512];
        if (!markdown_attachment_path(DOCUMENT_ROOT "/validation.md", text.images[i], path, sizeof(path))) {
            e = ESP_ERR_NOT_SUPPORTED;
            break;
        }
        const char *base = strrchr(path, '/') + 1;
        if (!document_store_safe_filename(base) || !strrchr(base, '.') ||
            strcasecmp(strrchr(base, '.'), ".png")) {
            e = ESP_ERR_NOT_SUPPORTED;
            break;
        }
        bool found = false;
        for (size_t k = 0; k < *count; ++k)
            if (!strcmp(assets[k].name, base)) {
                found = true;
                break;
            }
        if (found)
            continue;
        if (*count == 64) {
            e = ESP_ERR_INVALID_SIZE;
            break;
        }
        snprintf(assets[(*count)++].name, 241, "%s", base);
    }
    html_text_free(&text);
    return e;
}
// WiFi 接收文件先验证再替换，复用云端事务的日期、进度和附件清理。
// Validate WiFi staging before replacement, reusing cloud transaction dates, progress and ownership cleanup.
static esp_err_t import_copy(const char *source, const char *destination) {
    FILE *in = fopen(source, "rb");
    if (!in) return ESP_ERR_NOT_FOUND;
    FILE *out = fopen(destination, "wb");
    if (!out) { fclose(in); return ESP_FAIL; }
    uint8_t *buffer = malloc(4096);
    bool ok = buffer != NULL;
    size_t n;
    while (ok && (n = fread(buffer, 1, 4096, in)))
        ok = fwrite(buffer, 1, n, out) == n;
    if (ferror(in)) ok = false;
    free(buffer);
    if (fclose(in)) ok = false;
    if (fclose(out)) ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
esp_err_t document_store_import_local(const char *staging, const char *destination) {
    if (!staging || !allowed(destination)) return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set(&s_sync_lock)) return ESP_ERR_INVALID_STATE;
    esp_err_t e = sync_prepare_locked();
    bool prepared = e == ESP_OK;
    document_sync_asset_t *assets = NULL;
    size_t count = 0;
    char hash[65]; uint64_t bytes = 0;
    if (e == ESP_OK) e = import_copy(staging, SYNC_BODY);
    if (e == ESP_OK) {
        assets = calloc(DOCUMENT_SYNC_ASSETS_MAX, sizeof(*assets));
        if (!assets) e = ESP_ERR_NO_MEM;
    }
    if (e == ESP_OK) e = document_store_sync_collect_assets(assets, &count);
    for (size_t i = 0; e == ESP_OK && i < count; ++i) {
        char from[512], to[512]; uint64_t size = 0;
        snprintf(from, sizeof(from), DOCUMENT_ROOT "/attachments/%s", assets[i].name);
        snprintf(to, sizeof(to), DOCUMENT_SYNC_STAGE "/attachments/%s", assets[i].name);
        e = import_copy(from, to);
        if (e == ESP_OK) e = document_store_hash_file(to, assets[i].sha256, &size);
        if (e == ESP_OK && size > UINT32_MAX) e = ESP_ERR_INVALID_SIZE;
        assets[i].bytes = (uint32_t)size;
    }
    if (e == ESP_OK) e = document_store_hash_file(SYNC_BODY, hash, &bytes);
    bool unchanged = false;
    if (e == ESP_OK) e = sync_commit_locked(destination, hash, bytes, assets, count, &unchanged);
    if (e != ESP_OK && prepared) (void)sync_abort_locked();
    free(assets);
    atomic_flag_clear(&s_sync_lock);
    return e;
}
esp_err_t document_store_recent(document_entry_t entries[3], size_t *count) {
    if (!entries || !count)
        return ESP_ERR_INVALID_ARG;
    *count = 0;
    memset(entries, 0, 3 * sizeof(*entries));
    esp_err_t e = document_store_sync_recover();
    if (e != ESP_OK)
        return e;
    uint64_t times[3] = {0};
    DIR *dir = opendir(DOCUMENT_ROOT "/.index");
    if (!dir)
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    struct dirent *de;
    while ((de = readdir(dir))) {
        size_t n = strlen(de->d_name);
        if (n <= 5 || n - 5 > 240 || strcmp(de->d_name + n - 5, ".sync"))
            continue;
        char name[241];
        memcpy(name, de->d_name, n - 5);
        name[n - 5] = 0;
        if (!document_store_safe_filename(name) || !is_md(name))
            continue;
        char mark[512], path[288];
        snprintf(mark, sizeof(mark), DOCUMENT_ROOT "/.index/%s", de->d_name);
        snprintf(path, sizeof(path), DOCUMENT_ROOT "/%s", name);
        struct stat st;
        if (lstat(path, &st) || !S_ISREG(st.st_mode))
            continue;
        FILE *f = fopen(mark, "rb");
        unsigned long long stamp = 0;
        bool ok = f && fscanf(f, "%llu", &stamp) == 1;
        if (f)
            fclose(f);
        if (!ok || !stamp)
            continue;
        if (stamp < UINT64_C(10000000000000))
            stamp *= 1000000;
        unsigned at = 0;
        while (at < *count &&
               (times[at] > stamp || (times[at] == stamp && strcmp(entries[at].name, name) < 0)))
            ++at;
        if (at >= 3)
            continue;
        if (*count < 3)
            ++*count;
        for (size_t k = *count - 1; k > at; --k) {
            entries[k] = entries[k - 1];
            times[k] = times[k - 1];
        }
        document_entry_t *item = &entries[at];
        memset(item, 0, sizeof(*item));
        snprintf(item->name, sizeof(item->name), "%s", name);
        snprintf(item->path, sizeof(item->path), "%s", path);
        item->size = st.st_size;
        times[at] = stamp;
    }
    closedir(dir);
    return ESP_OK;
}
