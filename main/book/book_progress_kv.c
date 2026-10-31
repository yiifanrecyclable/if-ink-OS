/*
 * SPDX-FileCopyrightText: 2026 ifRecyclable
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读进度使用TF卡上的CRC双槽，每次关闭、同步并读回后才确认成功。
 * Progress uses CRC-checked SD dual slots; acknowledge only after flush, sync, close and readback.
 *
 * 冻结：本次为修复内部NVS不可写时进度丢失，TF可用时不再写NVS。
 * 不挂载/格式化卡，不擦除NVS分区；无TF时保留原NVS路径，旧进度可读。
 * Frozen: fix progress loss when internal NVS is unwritable by writing SD whenever available.
 * Never mount/format cards or erase NVS partitions; retain NVS fallback without SD and legacy reads.
 */
#include "book_progress_kv.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define KV_DIR "/sdcard/.readpico/progress"
#define KV_MAX 312
#define KV_HEAD 24
static const char *const legacy_ns = "rp_books";

typedef struct {
    bool exists, valid, deleted;
    uint32_t generation;
    nvs_type_t type;
    size_t len;
    uint8_t data[KV_MAX];
} slot_t;

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void write32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t crc_step(uint32_t c, const uint8_t *p, size_t n) {
    while(n--) { c^=*p++; for(unsigned i=0;i<8;++i) c=(c>>1) ^ (UINT32_C(0xedb88320)&(0-(c&1))); }
    return c;
}
static uint32_t checksum(const uint8_t *head, const uint8_t *data, size_t len) {
    return ~crc_step(crc_step(UINT32_MAX,head,16),data,len);
}
static bool key_valid(const char *key) {
    if (!key) return false;
    if (!strcmp(key,"seq") || !strcmp(key,"last") || !strcmp(key,"legacy_off")) return true;
    if (strlen(key)!=10 || key[0]!='b' || key[1]!='_') return false;
    for(unsigned i=2;i<10;++i) if(!((key[i]>='0'&&key[i]<='9')||(key[i]>='a'&&key[i]<='f'))) return false;
    return true;
}
static bool sd_ready(void) {
    struct stat st;
    return stat("/sdcard",&st)==0 && S_ISDIR(st.st_mode);
}
static bool directory(const char *path) {
    if(mkdir(path,0755)==0) return true;
    struct stat st;
    return errno==EEXIST && stat(path,&st)==0 && S_ISDIR(st.st_mode);
}
static void slot_path(const char *key, int slot, char out[96]) {
    snprintf(out,96,KV_DIR "/%s.%c",key,slot?'b':'a');
}
static slot_t read_slot(const char *key, int index) {
    slot_t value={0}; char path[96]; slot_path(key,index,path);
    FILE *f=fopen(path,"rb");
    if(!f) { value.exists=errno!=ENOENT; return value; }
    value.exists=true;
    uint8_t bytes[KV_HEAD+KV_MAX]; size_t n=fread(bytes,1,sizeof(bytes),f);
    bool good=!ferror(f) && fgetc(f)==EOF && !ferror(f);
    if(fclose(f)!=0) good=false;
    if(!good || n<KV_HEAD || memcmp(bytes,"IFP1",4) || bytes[4]!=1 || bytes[6]>1 || bytes[7] ||
       read32(bytes+20) || read32(bytes+12)>KV_MAX || n!=KV_HEAD+read32(bytes+12) ||
       !read32(bytes+8) || read32(bytes+16)!=checksum(bytes,bytes+KV_HEAD,n-KV_HEAD)) return value;
    value.type=(nvs_type_t)bytes[5]; value.deleted=bytes[6]!=0; value.len=n-KV_HEAD;
    if((value.type!=NVS_TYPE_BLOB && value.type!=NVS_TYPE_STR && value.type!=NVS_TYPE_U32) ||
       (value.deleted && value.len) || (!value.deleted && (!value.len ||
       (value.type==NVS_TYPE_U32 && value.len!=4) ||
       (value.type==NVS_TYPE_STR && (bytes[n-1] || strnlen((char*)bytes+KV_HEAD,value.len)!=value.len-1))))) return value;
    value.valid=true; value.generation=read32(bytes+8);
    memcpy(value.data,bytes+KV_HEAD,value.len); return value;
}
static int newest(const slot_t slots[2]) {
    if(!slots[0].valid) return slots[1].valid?1:-1;
    if(!slots[1].valid) return 0;
    return slots[1].generation>slots[0].generation?1:0;
}
static bool legacy_disabled(void) {
    slot_t slots[2]={read_slot("legacy_off",0),read_slot("legacy_off",1)};
    // 标记损坏时也不回读已被恢复操作取代的旧库。/ Fail closed if the restore marker is corrupt.
    return slots[0].exists || slots[1].exists;
}
static esp_err_t sd_write(const char *key, nvs_type_t type, const void *data, size_t len, bool deleted) {
    if(!key_valid(key) || len>KV_MAX || (!deleted && (!data || !len))) return ESP_ERR_INVALID_ARG;
    slot_t slots[2]={read_slot(key,0),read_slot(key,1)};
    int old=newest(slots), target=old<0?0:1-old;
    if(old>=0 && slots[old].generation==UINT32_MAX) return ESP_ERR_INVALID_STATE;
    uint32_t generation=old<0?1:slots[old].generation+1;
    // 删除同时覆盖两槽，成功返回后损坏任一副本也不复活旧进度。
    // Mirror tombstones so losing either slot after success cannot resurrect old progress.
    for(unsigned pass=0;pass<(deleted?2u:1u);++pass) {
        uint8_t bytes[KV_HEAD+KV_MAX]={ 'I','F','P','1',1,(uint8_t)type,(uint8_t)deleted,0 };
        write32(bytes+8,generation); write32(bytes+12,(uint32_t)len);
        if(len) memcpy(bytes+KV_HEAD,data,len);
        write32(bytes+16,checksum(bytes,bytes+KV_HEAD,len));
        char path[96]; slot_path(key,target,path);
        FILE *f=fopen(path,"wb"); if(!f) return ESP_FAIL;
        bool ok=fwrite(bytes,1,KV_HEAD+len,f)==KV_HEAD+len;
        if(fflush(f)!=0) ok=false;
        if(fsync(fileno(f))!=0) ok=false;
        if(fclose(f)!=0) ok=false;
        slot_t verified=read_slot(key,target);
        if(!ok || !verified.valid || verified.generation!=generation || verified.type!=type ||
           verified.deleted!=deleted || verified.len!=len || (len && memcmp(verified.data,data,len))) return ESP_FAIL;
        target=1-target;
    }
    return ESP_OK;
}
esp_err_t progress_kv_open(int mode, progress_kv_handle_t *out) {
    if(!out) return ESP_ERR_INVALID_ARG;
    *out=(progress_kv_handle_t){.sd=sd_ready()};
    if(!out->sd) return nvs_open(legacy_ns,mode,&out->legacy);
    if(mode==NVS_READWRITE && (!directory("/sdcard/.readpico") || !directory(KV_DIR))) return ESP_FAIL;
    return ESP_OK;
}
void progress_kv_close(progress_kv_handle_t h) { if(!h.sd) nvs_close(h.legacy); }
static esp_err_t legacy_get(const char *key, nvs_type_t type, void *out, size_t *len) {
    nvs_handle_t h; esp_err_t err=nvs_open(legacy_ns,NVS_READONLY,&h); if(err!=ESP_OK) return err;
    if(type==NVS_TYPE_U32) {
        // NVS使用对齐的整数，枚举的字节缓冲不保证整数对齐。/ Use an aligned integer, not an arbitrarily aligned export buffer.
        uint32_t value=0; err=nvs_get_u32(h,key,&value);
        if(err==ESP_OK) {
            if(out && *len<4) err=ESP_ERR_NVS_INVALID_LENGTH;
            else if(out) memcpy(out,&value,4);
            *len=4;
        }
    }
    else if(type==NVS_TYPE_STR) err=nvs_get_str(h,key,out,len);
    else err=nvs_get_blob(h,key,out,len);
    nvs_close(h); return err;
}
static esp_err_t sd_get(const char *key, nvs_type_t type, void *out, size_t *len) {
    if(!key_valid(key) || !len) return ESP_ERR_INVALID_ARG;
    slot_t slots[2]={read_slot(key,0),read_slot(key,1)}; int at=newest(slots);
    if(at<0) {
        if(slots[0].exists || slots[1].exists) return ESP_ERR_INVALID_STATE;
        return legacy_disabled()?ESP_ERR_NVS_NOT_FOUND:legacy_get(key,type,out,len);
    }
    slot_t *v=&slots[at];
    if(v->deleted) return ESP_ERR_NVS_NOT_FOUND;
    if(v->type!=type) return ESP_ERR_INVALID_STATE;
    if(out && *len<v->len) { *len=v->len; return ESP_ERR_NVS_INVALID_LENGTH; }
    if(out) memcpy(out,v->data,v->len);
    *len=v->len; return ESP_OK;
}
esp_err_t progress_kv_get_blob(progress_kv_handle_t h,const char *k,void *out,size_t *n) {
    return h.sd?sd_get(k,NVS_TYPE_BLOB,out,n):nvs_get_blob(h.legacy,k,out,n);
}
esp_err_t progress_kv_get_str(progress_kv_handle_t h,const char *k,char *out,size_t *n) {
    return h.sd?sd_get(k,NVS_TYPE_STR,out,n):nvs_get_str(h.legacy,k,out,n);
}
esp_err_t progress_kv_get_u32(progress_kv_handle_t h,const char *k,uint32_t *out) {
    size_t n=4; return h.sd?sd_get(k,NVS_TYPE_U32,out,&n):nvs_get_u32(h.legacy,k,out);
}
esp_err_t progress_kv_set_blob(progress_kv_handle_t h,const char *k,const void *p,size_t n) {
    return h.sd?sd_write(k,NVS_TYPE_BLOB,p,n,false):nvs_set_blob(h.legacy,k,p,n);
}
esp_err_t progress_kv_set_str(progress_kv_handle_t h,const char *k,const char *p) {
    return h.sd?sd_write(k,NVS_TYPE_STR,p,strlen(p)+1,false):nvs_set_str(h.legacy,k,p);
}
esp_err_t progress_kv_set_u32(progress_kv_handle_t h,const char *k,uint32_t v) {
    return h.sd?sd_write(k,NVS_TYPE_U32,&v,4,false):nvs_set_u32(h.legacy,k,v);
}
esp_err_t progress_kv_erase_key(progress_kv_handle_t h,const char *k) {
    return h.sd?sd_write(k,NVS_TYPE_BLOB,NULL,0,true):nvs_erase_key(h.legacy,k);
}
esp_err_t progress_kv_commit(progress_kv_handle_t h) { return h.sd?ESP_OK:nvs_commit(h.legacy); }

static bool entry_key(const char *name, char key[16], char *suffix) {
    size_t n=strlen(name); if(n<3 || n>=18 || name[n-2]!='.' || (name[n-1]!='a'&&name[n-1]!='b')) return false;
    memcpy(key,name,n-2); key[n-2]=0; *suffix=name[n-1];
    return key_valid(key) && strcmp(key,"legacy_off");
}
esp_err_t progress_kv_list(progress_kv_visit_fn visit,void *ctx) {
    if(!visit) return ESP_ERR_INVALID_ARG;
    bool sd=sd_ready(), more=true;
    if(sd) {
        DIR *d=opendir(KV_DIR); if(!d && errno!=ENOENT) return ESP_FAIL;
        if(d) {
            struct dirent *e;
            while(more && (e=readdir(d))) {
                char key[16], suffix; if(!entry_key(e->d_name,key,&suffix)) continue;
                slot_t slots[2]={read_slot(key,0),read_slot(key,1)};
                if(suffix=='b' && slots[0].exists) continue;
                int at=newest(slots);
                if(at>=0 && !slots[at].deleted) more=visit(key,slots[at].type,slots[at].data,slots[at].len,ctx);
            }
            if(closedir(d)!=0) return ESP_FAIL;
        }
        if(!more || legacy_disabled()) return ESP_OK;
    }
    nvs_iterator_t it=NULL;
    esp_err_t err=nvs_entry_find(NVS_DEFAULT_PART_NAME,legacy_ns,NVS_TYPE_ANY,&it);
    if(err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if(err!=ESP_OK) return err;
    while(more && err==ESP_OK) {
        nvs_entry_info_t info;
        err=nvs_entry_info(it,&info); if(err!=ESP_OK) break;
        if(key_valid(info.key) && strcmp(info.key,"legacy_off")) {
            slot_t a={0},b={0}; if(sd) { a=read_slot(info.key,0); b=read_slot(info.key,1); }
            if(!a.exists && !b.exists) {
                uint8_t data[KV_MAX]; size_t n=sizeof(data);
                if(info.type==NVS_TYPE_U32) n=4;
                if((info.type==NVS_TYPE_BLOB||info.type==NVS_TYPE_STR||info.type==NVS_TYPE_U32) &&
                   legacy_get(info.key,info.type,data,&n)==ESP_OK) more=visit(info.key,info.type,data,n,ctx);
            }
        }
        if(more) err=nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return err==ESP_ERR_NVS_NOT_FOUND?ESP_OK:err;
}
esp_err_t progress_kv_reset(void) {
    progress_kv_handle_t h; esp_err_t err=progress_kv_open(NVS_READWRITE,&h); if(err!=ESP_OK) return err;
    if(!h.sd) { err=nvs_erase_all(h.legacy); if(err==ESP_OK) err=nvs_commit(h.legacy); }
    else {
        uint32_t one=1; err=sd_write("legacy_off",NVS_TYPE_U32,&one,4,false);
        DIR *d=err==ESP_OK?opendir(KV_DIR):NULL;
        if(err==ESP_OK && !d) err=ESP_FAIL;
        if(d) {
            struct dirent *e;
            while(err==ESP_OK && (e=readdir(d))) {
                char key[16],suffix;
                if(entry_key(e->d_name,key,&suffix)) err=sd_write(key,NVS_TYPE_BLOB,NULL,0,true);
            }
            if(closedir(d)!=0 && err==ESP_OK) err=ESP_FAIL;
        }
    }
    progress_kv_close(h); return err;
}
