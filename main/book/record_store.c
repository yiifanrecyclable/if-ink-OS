/* SPDX-License-Identifier: Apache-2.0
* 中文：白名单内容键的CRC双槽、合并枚举、墓碑与恢复材料；不保存账号凭据，不擦分区。
* English: Whitelisted content CRC slots, merged enumeration, tombstones and recovery material; no credentials or partition erasure.
* 冻结：无卡不写增长型内容到NVS；未知或损坏记录不视为空，不允许旧NVS覆盖新TF。
* Frozen: content writes never fall back to NVS without SD; corruption is not absence, and legacy NVS never replaces SD.
*/
#include "record_store.h"
#include "../settings_storage.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static _Atomic(SemaphoreHandle_t) mutex;
void record_store_lock(void){
    SemaphoreHandle_t m=atomic_load(&mutex);
    if(!m){
        SemaphoreHandle_t candidate=xSemaphoreCreateRecursiveMutex();
        configASSERT(candidate);
        SemaphoreHandle_t expected=NULL;
        if(atomic_compare_exchange_strong(&mutex,&expected,candidate))m=candidate;
        else{vSemaphoreDelete(candidate);m=expected;}
    }
    xSemaphoreTakeRecursive(m,portMAX_DELAY);
}
void record_store_unlock(void){xSemaphoreGiveRecursive(atomic_load(&mutex));}
#else
#include <pthread.h>
static pthread_mutex_t mutex;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void mutex_init(void){
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex,&a);
    pthread_mutexattr_destroy(&a);
}
void record_store_lock(void){
    pthread_once(&once,mutex_init);
    pthread_mutex_lock(&mutex);
}
void record_store_unlock(void){
    pthread_mutex_unlock(&mutex);
}
#endif
#ifndef RECORDS_CARD_ROOT
#define RECORDS_CARD_ROOT "/sdcard"
#endif
#define ROOT RECORDS_CARD_ROOT "/.readpico/records"
#define HEAD 40u
#define MAX_VALUE 4096u
static const char *const spaces[]={
    "rp_books","rp_ticket","rp_marks","rp_favs","rp_titles","rp_shelf","if_content","rp_input"};
static atomic_bool ready;
static atomic_uint epoch=1;
static bool hexkey(const char *k,char prefix,size_t count){
    if(!k||k[0]!=prefix||strlen(k)!=count)return false;
    size_t at=count==15?1:2;
    if(at==2&&k[1]!='_')return false;
    for(size_t i=at;i<count;++i)if(!strchr("0123456789abcdef",k[i]))return false;
    return true;
}
bool record_store_content_key(const char *s,const char *k){
    if(!s||!k)return false;
    if(!strcmp(s,"rp_books"))return hexkey(k,'b',10)||!strcmp(k,"seq")||!strcmp(k,"last");
    if(!strcmp(s,"rp_ticket"))return !strcmp(k,"stats")||!strcmp(k,"heatmap");
    if(!strcmp(s,"rp_marks"))return hexkey(k,'m',10);
    if(!strcmp(s,"rp_favs"))return hexkey(k,'f',10);
    if(!strcmp(s,"rp_titles"))return hexkey(k,'t',10);
    if(!strcmp(s,"rp_shelf"))return hexkey(k,'h',10);
    if(!strcmp(s,"if_content"))return hexkey(k,'a',10)||hexkey(k,'p',10);
    if(!strcmp(s,"rp_input"))return hexkey(k,'h',15);
    return false;
}
static bool safe_key(const char *k){
    if(!k||!k[0]||strlen(k)>15)return false;
    for(;*k;++k)if(!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-",*k))return false;
    return true;
}
void record_store_media(bool r){
    record_store_lock();
    if(r!=ready){
        ready=r;
        ++epoch;
    }
    record_store_unlock();
}
bool record_store_ready(void){
    return ready;
}
static int space_id(const char *s){
    for(unsigned i=0;i<sizeof(spaces)/sizeof(spaces[0]);++i)if(!strcmp(s,spaces[i]))return (int)i;
    return -1;
}
static const char *space_of(records_handle_t h){
    unsigned i=(unsigned)(h&255);
    return i&&i<=8?spaces[i-1]:NULL;
}
static bool valid(records_handle_t h){
    return space_of(h)&&(uint32_t)(h>>32)==epoch;
}
esp_err_t records_open(const char *space,int mode,records_handle_t *out){
    int id=space?space_id(space):-1;
    if(id<0||!out||(mode!=NVS_READONLY&&mode!=NVS_READWRITE))return ESP_ERR_INVALID_ARG;
    *out=((uint64_t)epoch<<32)|(unsigned)(id+1)|(mode==NVS_READWRITE?256:0);
    return ESP_OK;
}
void records_close(records_handle_t h){
    (void)h;
}
static uint32_t get32(const uint8_t *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t v){
    for(int i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t crc_step(uint32_t c,const void *data,size_t n){
    const uint8_t *p=data;
    while(n--){
        c^=*p++;
        for(int i=0;i<8;++i)c=c&1?(c>>1)^0xedb88320:c>>1;
    }
    return c;
}
static uint32_t checksum(const uint8_t head[HEAD],const void *value,size_t n){
    uint8_t h[HEAD];
    memcpy(h,head,HEAD);
    memset(h+16,0,4);
    return ~crc_step(crc_step(UINT32_MAX,h,HEAD),value,n);
}
typedef struct {
    bool exists,valid,deleted;
    uint32_t gen;
    nvs_type_t type;
    size_t bytes;
    uint8_t *data;
}
slot_t;
static void slot_free(slot_t *s){
    free(s->data);
    memset(s,0,sizeof(*s));
}
static void filename(const char *space,const char *key,int slot,char path[192]){
    snprintf(path,192,ROOT "/%s/%s.%c",space,key,slot?'b':'a');
}
static slot_t read_path(const char *path,const char *space,const char *key){
    slot_t s={0};
    FILE *f=fopen(path,"rb");
    if(!f){
        s.exists=errno!=ENOENT;
        return s;
    }
    s.exists=true;
    uint8_t h[HEAD];
    bool ok=fread(h,1,HEAD,f)==HEAD;
    if(ok){
        s.gen=get32(h+8);
        s.bytes=get32(h+12);
        s.type=(nvs_type_t)h[5];
        s.deleted=h[6];
        ok=!memcmp(h,"IFR1",4)&&h[4]==1&&h[6]<=1&&h[7]==space_id(space)&&s.gen&&s.bytes<=MAX_VALUE&&!get32(h+20)&&!memcmp(h+24,key,strlen(key)+1)&&(s.type==NVS_TYPE_BLOB||s.type==NVS_TYPE_STR||s.type==NVS_TYPE_U8||s.type==NVS_TYPE_U32)&&(!s.deleted||!s.bytes);
    }
    if(ok&&s.bytes){
        s.data=malloc(s.bytes);
        ok=s.data&&fread(s.data,1,s.bytes,f)==s.bytes;
    }
    if(ok)ok=fgetc(f)==EOF&&!ferror(f)&&get32(h+16)==checksum(h,s.data,s.bytes);
    if(ok&&!s.deleted)ok=s.bytes&&(s.type!=NVS_TYPE_U8||s.bytes==1)&&(s.type!=NVS_TYPE_U32||s.bytes==4)&&(s.type!=NVS_TYPE_STR||(!s.data[s.bytes-1]&&strnlen((char*)s.data,s.bytes)==s.bytes-1));
    if(fclose(f))ok=false;
    s.valid=ok;
    return s;
}
static slot_t read_slot(const char *s,const char *k,int i){
    char p[192];
    filename(s,k,i,p);
    return read_path(p,s,k);
}
static int latest(slot_t v[2]){
    if(!v[0].valid)return v[1].valid?1:-1;
    if(!v[1].valid)return 0;
    return v[1].gen>v[0].gen?1:0;
}
static bool directory(const char *p){
    if(!mkdir(p,0755))return true;
    struct stat s;
    return errno==EEXIST&&!stat(p,&s)&&S_ISDIR(s.st_mode);
}
static bool ensure(const char *space){
    char p[192];
    snprintf(p,sizeof(p),ROOT "/%s",space);
    return ready&&directory(RECORDS_CARD_ROOT "/.readpico")&&directory(ROOT)&&directory(p);
}
static esp_err_t write_path(const char *path,const char *space,const char *key,nvs_type_t type,const void *value,size_t bytes,bool deleted,uint32_t gen){
    uint8_t h[HEAD]={
        'I','F','R','1',1,(uint8_t)type,(uint8_t)deleted,(uint8_t)space_id(space)};
    put32(h+8,gen);
    put32(h+12,bytes);
    memcpy(h+24,key,strlen(key)+1);
    put32(h+16,checksum(h,value,bytes));
    FILE *f=fopen(path,"wb");
    if(!f)return ESP_FAIL;
    bool ok=fwrite(h,1,HEAD,f)==HEAD&&(!bytes||fwrite(value,1,bytes,f)==bytes);
    if(fflush(f)||fsync(fileno(f)))ok=false;
    if(fclose(f))ok=false;
    slot_t check=read_path(path,space,key);
    ok=ok&&check.valid&&check.gen==gen&&check.type==type&&check.deleted==deleted&&check.bytes==bytes&&(!bytes||!memcmp(check.data,value,bytes));
    slot_free(&check);
    return ok?ESP_OK:ESP_FAIL;
}
static esp_err_t sd_write(const char *space,const char *key,nvs_type_t type,const void *value,size_t bytes,bool deleted){
    if(!ready||!ensure(space))return ESP_ERR_INVALID_STATE;
    if(!safe_key(key)||bytes>MAX_VALUE||(!deleted&&(!value||!bytes)))return ESP_ERR_INVALID_ARG;
    slot_t v[2]={
        read_slot(space,key,0),read_slot(space,key,1)};
    int at=latest(v);
    esp_err_t err=ESP_OK;
    if(at<0&&(v[0].exists||v[1].exists))err=ESP_ERR_INVALID_RESPONSE;
    uint32_t gen=at<0?1:v[at].gen+1;
    int target=at<0?0:1-at;
    if(!gen)err=ESP_ERR_INVALID_STATE;
    for(unsigned pass=0;err==ESP_OK&&pass<(deleted?2u:1u);++pass){
        char p[192];
        filename(space,key,target,p);
        err=write_path(p,space,key,type,value,bytes,deleted,gen);
        target=1-target;
    }
    slot_free(&v[0]);
    slot_free(&v[1]);
    return err;
}
static bool legacy_off(const char *space){
    slot_t v[2]={
        read_slot(space,"_reset",0),read_slot(space,"_reset",1)};
    bool off=v[0].exists||v[1].exists;
    slot_free(&v[0]);
    slot_free(&v[1]);
    return off;
}
static esp_err_t raw_get(const char *space,const char *key,nvs_type_t type,void *value,size_t *bytes){
    nvs_handle_t h;
    esp_err_t e=nvs_open(space,NVS_READONLY,&h);
    if(e!=ESP_OK)return e;
    if(type==NVS_TYPE_U8){
        uint8_t v;
        e=nvs_get_u8(h,key,&v);
        if(e==ESP_OK){
            if(value&&*bytes<1)e=ESP_ERR_INVALID_SIZE;
            else if(value)memcpy(value,&v,1);
            *bytes=1;
        }
    }
    else if(type==NVS_TYPE_U32){
        uint32_t v;
        e=nvs_get_u32(h,key,&v);
        if(e==ESP_OK){
            if(value&&*bytes<4)e=ESP_ERR_INVALID_SIZE;
            else if(value)memcpy(value,&v,4);
            *bytes=4;
        }
    }
    else if(type==NVS_TYPE_STR)e=nvs_get_str(h,key,value,bytes);
    else e=nvs_get_blob(h,key,value,bytes);
    nvs_close(h);
    return e;
}
static esp_err_t get(records_handle_t h,const char *key,nvs_type_t type,void *value,size_t *bytes){
    if(!valid(h)||!safe_key(key)||!bytes)return ESP_ERR_INVALID_ARG;
    const char *space=space_of(h);
    bool sd=ready&&record_store_content_key(space,key);
    if(sd){
        slot_t v[2]={
            read_slot(space,key,0),read_slot(space,key,1)};
        int at=latest(v);
        esp_err_t e=ESP_OK;
        if(at>=0){
            slot_t *s=&v[at];
            if(s->deleted)e=ESP_ERR_NVS_NOT_FOUND;
            else if(s->type!=type)e=ESP_ERR_INVALID_RESPONSE;
            else if(value&&*bytes<s->bytes){
                *bytes=s->bytes;
                e=ESP_ERR_NVS_INVALID_LENGTH;
            }
            else{
                if(value)memcpy(value,s->data,s->bytes);
                *bytes=s->bytes;
            }
            slot_free(&v[0]);
            slot_free(&v[1]);
            return e;
        }
        bool exists=v[0].exists||v[1].exists;
        slot_free(&v[0]);
        slot_free(&v[1]);
        if(exists)return ESP_ERR_INVALID_RESPONSE;
        if(legacy_off(space))return ESP_ERR_NVS_NOT_FOUND;
    }
    return raw_get(space,key,type,value,bytes);
}
esp_err_t records_get_blob(records_handle_t h,const char *k,void *v,size_t *n){
    record_store_lock();
    esp_err_t e=get(h,k,NVS_TYPE_BLOB,v,n);
    record_store_unlock();
    return e;
}
esp_err_t records_get_str(records_handle_t h,const char *k,char *v,size_t *n){
    record_store_lock();
    esp_err_t e=get(h,k,NVS_TYPE_STR,v,n);
    record_store_unlock();
    return e;
}
esp_err_t records_get_u8(records_handle_t h,const char *k,uint8_t *v){
    size_t n=1;
    record_store_lock();
    esp_err_t e=get(h,k,NVS_TYPE_U8,v,&n);
    record_store_unlock();
    return e;
}
esp_err_t records_get_u32(records_handle_t h,const char *k,uint32_t *v){
    size_t n=4;
    record_store_lock();
    esp_err_t e=get(h,k,NVS_TYPE_U32,v,&n);
    record_store_unlock();
    return e;
}
static esp_err_t set(records_handle_t h,const char *key,nvs_type_t type,const void *value,size_t bytes,bool deleted){
    if(!valid(h)||!(h&256)||!safe_key(key)||bytes>MAX_VALUE)return ESP_ERR_INVALID_ARG;
    const char *s=space_of(h);
    if(record_store_content_key(s,key))return ready?sd_write(s,key,type,value,bytes,deleted):ESP_ERR_INVALID_STATE;
    if(deleted){
        nvs_handle_t raw;
        esp_err_t e=nvs_open(s,NVS_READWRITE,&raw);
        if(e!=ESP_OK)return e;
        e=nvs_erase_key(raw,key);
        if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;
        if(e==ESP_OK)e=nvs_commit(raw);
        nvs_close(raw);
        return e;
    }
    if(type==NVS_TYPE_U8)return settings_storage_put_u8(s,key,*(const uint8_t*)value);
    if(type==NVS_TYPE_STR)return settings_storage_put_str(s,key,value);
    if(type==NVS_TYPE_BLOB)return settings_storage_put_blob(s,key,value,bytes);
    return ESP_ERR_INVALID_ARG;
}
esp_err_t records_set_blob(records_handle_t h,const char *k,const void *v,size_t n){
    record_store_lock();
    esp_err_t e=set(h,k,NVS_TYPE_BLOB,v,n,false);
    record_store_unlock();
    return e;
}
// 类型必须保留，不能将字符串存成blob。/ Preserve types rather than storing strings as blobs.
static esp_err_t typed_set(records_handle_t h,const char *k,nvs_type_t t,const void *v,size_t n){
    record_store_lock();
    esp_err_t e=set(h,k,t,v,n,false);
    record_store_unlock();
    return e;
}
esp_err_t records_set_str(records_handle_t h,const char *k,const char *v){
    return v?typed_set(h,k,NVS_TYPE_STR,v,strlen(v)+1):ESP_ERR_INVALID_ARG;
}
esp_err_t records_set_u8(records_handle_t h,const char *k,uint8_t v){
    return typed_set(h,k,NVS_TYPE_U8,&v,1);
}
esp_err_t records_set_u32(records_handle_t h,const char *k,uint32_t v){
    return typed_set(h,k,NVS_TYPE_U32,&v,4);
}
esp_err_t records_erase_key(records_handle_t h,const char *k){
    record_store_lock();
    esp_err_t e=set(h,k,NVS_TYPE_BLOB,NULL,0,true);
    record_store_unlock();
    return e;
}
esp_err_t records_commit(records_handle_t h){
    return valid(h)?ESP_OK:ESP_ERR_INVALID_STATE;
}
struct records_iterator {
    records_handle_t h;
    nvs_type_t filter;
    DIR *dir;
    nvs_iterator_t legacy;
    bool legacy_started;
    nvs_entry_info_t info;
};
static bool pair_bad(slot_t v[2]){
    return (v[0].exists&&!v[0].valid)||(v[1].exists&&!v[1].valid);
}
esp_err_t records_sd_state(const char *space,const char *key,bool *exists,bool strict){
    if(!ready||!exists||!safe_key(key)||space_id(space)<0)return ESP_ERR_INVALID_STATE;
    slot_t v[2]={
        read_slot(space,key,0),read_slot(space,key,1)};
    *exists=v[0].exists||v[1].exists;
    esp_err_t e=(*exists&&latest(v)<0)||(strict&&pair_bad(v))?ESP_ERR_INVALID_RESPONSE:ESP_OK;
    slot_free(&v[0]);
    slot_free(&v[1]);
    return e;
}
static esp_err_t advance(records_iterator_t it){
    if(!valid(it->h))return ESP_ERR_INVALID_STATE;
    const char *space=space_of(it->h);
    if(it->dir){
        struct dirent *e;
        while((errno=0,(e=readdir(it->dir)))){
            size_t n=strlen(e->d_name);
            if(n<3||n>17||(strcmp(e->d_name+n-2,".a")&&strcmp(e->d_name+n-2,".b")))continue;
            char k[16]={0};
            memcpy(k,e->d_name,n-2);
            if(!record_store_content_key(space,k))continue;
            slot_t v[2]={
                read_slot(space,k,0),read_slot(space,k,1)};
            int at=latest(v);
            if(pair_bad(v)||at<0){
                slot_free(&v[0]);
                slot_free(&v[1]);
                return ESP_ERR_INVALID_RESPONSE;
            }
            bool emit=(!v[0].exists||e->d_name[n-1]=='a')&&!v[at].deleted&&(it->filter==NVS_TYPE_ANY||it->filter==v[at].type);
            if(emit){
                memset(&it->info,0,sizeof(it->info));
                strcpy(it->info.namespace_name,space);
                strcpy(it->info.key,k);
                it->info.type=v[at].type;
            }
            slot_free(&v[0]);
            slot_free(&v[1]);
            if(emit)return ESP_OK;
        }
        if(errno)return ESP_FAIL;
        DIR *closed=it->dir;
        it->dir=NULL;
        if(closedir(closed))return ESP_FAIL;
    }
    esp_err_t err;
    if(!it->legacy_started){
        it->legacy_started=true;
        err=nvs_entry_find(NVS_DEFAULT_PART_NAME,space,it->filter,&it->legacy);
    }
    else err=it->legacy?nvs_entry_next(&it->legacy):ESP_ERR_NVS_NOT_FOUND;
    while(err==ESP_OK){
        err=nvs_entry_info(it->legacy,&it->info);
        if(err!=ESP_OK)return err;
        bool shadow=false;
        if(ready&&record_store_content_key(space,it->info.key)){
            if(legacy_off(space))shadow=true;
            else{
                bool exists=false;
                err=records_sd_state(space,it->info.key,&exists,true);
                if(err!=ESP_OK)return err;
                shadow=exists;
            }
        }
        if(!shadow)return ESP_OK;
        err=nvs_entry_next(&it->legacy);
    }
    return err;
}
void records_release_iterator(records_iterator_t it){
    if(it){
        if(it->dir)closedir(it->dir);
        if(it->legacy)nvs_release_iterator(it->legacy);
        free(it);
    }
}
esp_err_t records_entry_find_in_handle(records_handle_t h,nvs_type_t filter,records_iterator_t *out){
    if(!out||!valid(h))return ESP_ERR_INVALID_ARG;
    *out=NULL;
    records_iterator_t it=calloc(1,sizeof(*it));
    if(!it)return ESP_ERR_NO_MEM;
    it->h=h;
    it->filter=filter;
    if(ready){
        char p[192];
        snprintf(p,sizeof(p),ROOT "/%s",space_of(h));
        it->dir=opendir(p);
        if(!it->dir&&errno!=ENOENT){
            free(it);
            return ESP_FAIL;
        }
    }
    esp_err_t e=advance(it);
    if(e!=ESP_OK){
        records_release_iterator(it);
        return e;
    }
    *out=it;
    return ESP_OK;
}
esp_err_t records_entry_next(records_iterator_t *it){
    if(!it||!*it)return ESP_ERR_NVS_NOT_FOUND;
    esp_err_t e=advance(*it);
    if(e!=ESP_OK){
        records_release_iterator(*it);
        *it=NULL;
    }
    return e;
}
esp_err_t records_entry_info(records_iterator_t it,nvs_entry_info_t *info){
    if(!it||!info)return ESP_ERR_INVALID_ARG;
    *info=it->info;
    return ESP_OK;
}
static bool bounded_known(const char *space,const char *key){
    if(!strcmp(space,"rp_input"))return false;
    if(strcmp(space,"if_content"))return false;
    const char *keys[]={
        "read_target","banner_v1","md_font","md_sizes","quote_valign","quote_align","quote_px","quote_font","home_quote","books","docs"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i)if(!strcmp(key,keys[i]))return true;
    return false;
}
esp_err_t records_erase_all(records_handle_t h){
    if(!valid(h)||!(h&256)||!ready)return ESP_ERR_INVALID_STATE;
    record_store_lock();
    const char *space=space_of(h);
    // 重置范围只包括本模块管理的内容键和既有偏好，不删除未知键。/ Reset only managed content and known preferences; preserve unknown keys.
    struct erase_item {
        char key[16];
        struct erase_item *next;
    }
    *head=NULL,**tail=&head;
    esp_err_t err=ESP_OK;
    records_iterator_t it=NULL;
    err=records_entry_find_in_handle(h,NVS_TYPE_ANY,&it);
    while(err==ESP_OK){
        const char *key=it->info.key;
        if(record_store_content_key(space,key)||bounded_known(space,key)){
            struct erase_item *item=calloc(1,sizeof(*item));
            if(!item){
                err=ESP_ERR_NO_MEM;
                break;
            }
            strcpy(item->key,key);
            *tail=item;
            tail=&item->next;
        }
        err=records_entry_next(&it);
    }
    records_release_iterator(it);
    if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    while(head){
        struct erase_item *item=head;
        head=item->next;
        if(err==ESP_OK)err=records_erase_key(h,item->key);
        free(item);
    }
    uint8_t one=1;
    if(err==ESP_OK)err=sd_write(space,"_reset",NVS_TYPE_U8,&one,1,false);
    record_store_unlock();
    return err;
}
// 恢复材料与当前记录隔离；不覆盖第一次留存的旧源。/ Keep recovery material separate and never overwrite the first archived source.
esp_err_t records_archive_legacy(const char *space,const char *key,nvs_type_t type,const void *value,size_t bytes){
    if(!value||!bytes||bytes>MAX_VALUE)return ESP_ERR_INVALID_ARG;
    if(!ready||!record_store_content_key(space,key)||!ensure(space))return ESP_ERR_INVALID_STATE;
    char path[192],temp[200];
    snprintf(path,sizeof(path),ROOT "/%s/recovery-%s.bin",space,key);
    snprintf(temp,sizeof(temp),"%s.tmp",path);
    slot_t old=read_path(path,space,key);
    esp_err_t err=ESP_OK;
    if(old.exists){
        err=old.valid&&!old.deleted&&old.type==type&&old.bytes==bytes&&!memcmp(old.data,value,bytes)?ESP_OK:ESP_ERR_INVALID_RESPONSE;
        slot_free(&old);
        return err;
    }
    slot_free(&old);
    err=write_path(temp,space,key,type,value,bytes,false,1);
    if(err==ESP_OK&&rename(temp,path))err=ESP_FAIL;
    if(err==ESP_OK){
        slot_t check=read_path(path,space,key);
        err=check.valid&&check.type==type&&check.bytes==bytes&&!memcmp(check.data,value,bytes)?ESP_OK:ESP_FAIL;
        slot_free(&check);
    }
    return err;
}
esp_err_t records_migrate_key(const char *space,const char *key,nvs_type_t type,const void *value,size_t bytes){
    if(!record_store_content_key(space,key)||!strcmp(space,"rp_books"))return ESP_ERR_INVALID_ARG;
    record_store_lock();
    esp_err_t err=records_archive_legacy(space,key,type,value,bytes);
    bool exists=false;
    if(err==ESP_OK)err=records_sd_state(space,key,&exists,true);
    if(err==ESP_OK&&!exists){
        if(legacy_off(space)){
            uint8_t ignored=0;
            (void)ignored;
            err=sd_write(space,key,NVS_TYPE_BLOB,NULL,0,true);
        }
        else err=sd_write(space,key,type,value,bytes,false);
    }
    if(err==ESP_OK)err=records_sd_state(space,key,&exists,true);
    if(err==ESP_OK&&!exists)err=ESP_ERR_INVALID_RESPONSE;
    record_store_unlock();
    return err;
}

esp_err_t records_sd_validate(const char *space,const char *key,records_validate_fn validate){
    if(!ready||!validate||!record_store_content_key(space,key))return ESP_ERR_INVALID_STATE;
    record_store_lock();slot_t v[2]={read_slot(space,key,0),read_slot(space,key,1)};int at=latest(v);
    esp_err_t err=at<0||pair_bad(v)?ESP_ERR_INVALID_RESPONSE:ESP_OK;
    if(err==ESP_OK&&!v[at].deleted&&!validate(space,(uint8_t)v[at].type,key,v[at].data,v[at].bytes))err=ESP_ERR_INVALID_RESPONSE;
    slot_free(&v[0]);slot_free(&v[1]);record_store_unlock();return err;
}
