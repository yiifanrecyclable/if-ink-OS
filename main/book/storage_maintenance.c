/* SPDX-License-Identifier: Apache-2.0
* 中文：只读盘点与逐键安全迁移；恢复材料/TF目标核实后才精确清退已知旧NVS键。
* English: Inventory and safe per-key migration; erase known legacy keys only after validating recovery material and SD targets.
*/
#include "storage_maintenance.h"
#include "record_store.h"
#include "book_progress_kv.h"
#include "book_history_backup.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "read_pico_sd.h"
#endif
#define ROOT "/sdcard/diagnostics"
static const char *const spaces[]={
    "rp_books","rp_ticket","rp_marks","rp_favs","rp_titles","rp_shelf","if_content","rp_input"};
static bool mounted(void){
#ifdef ESP_PLATFORM
    read_pico_sd_info_t s={0};
    return read_pico_sd_get_info(&s)==ESP_OK&&s.present&&s.mounted;
#else
    struct stat s;
    return record_store_ready()&&!stat("/sdcard",&s)&&S_ISDIR(s.st_mode);
#endif
}
static esp_err_t raw_read(nvs_handle_t h,const char *k,nvs_type_t t,void *out,size_t *n){
    if(t==NVS_TYPE_U8){
        uint8_t v;
        esp_err_t e=nvs_get_u8(h,k,&v);
        if(e==ESP_OK){
            if(out&&*n<1)return ESP_ERR_INVALID_SIZE;
            if(out)memcpy(out,&v,1);
            *n=1;
        }
        return e;
    }
    if(t==NVS_TYPE_U32){
        uint32_t v;
        esp_err_t e=nvs_get_u32(h,k,&v);
        if(e==ESP_OK){
            if(out&&*n<4)return ESP_ERR_INVALID_SIZE;
            if(out)memcpy(out,&v,4);
            *n=4;
        }
        return e;
    }
    if(t==NVS_TYPE_STR)return nvs_get_str(h,k,out,n);
    if(t==NVS_TYPE_BLOB)return nvs_get_blob(h,k,out,n);
    return ESP_ERR_INVALID_ARG;
}
static size_t available(void){
#ifdef ESP_PLATFORM
    nvs_stats_t s={0};
    return nvs_get_stats(NULL,&s)==ESP_OK?s.available_entries:SIZE_MAX;
#else
    return SIZE_MAX;
#endif
}
static void inventory(void){
    if(mkdir(ROOT,0755)&&errno!=EEXIST)return;
    FILE *f=fopen(ROOT "/nvs-inventory.csv","wb");
    if(!f)return;
    fputs("namespace,key,type,bytes,read_error\n",f);
    nvs_iterator_t it=NULL;
    esp_err_t err=nvs_entry_find(NVS_DEFAULT_PART_NAME,NULL,NVS_TYPE_ANY,&it);
    while(err==ESP_OK){
        nvs_entry_info_t info;
        err=nvs_entry_info(it,&info);
        if(err!=ESP_OK)break;
        nvs_handle_t h;
        esp_err_t e=nvs_open(info.namespace_name,NVS_READONLY,&h);
        size_t n=0;
        if(e==ESP_OK){
            e=raw_read(h,info.key,info.type,NULL,&n);
            nvs_close(h);
        }
        // 仅枚举标识符和长度，绝不输出密码或正文。/ Identifiers and lengths only, never passwords or document content.
        fprintf(f,"%s,%s,%u,%u,%d\n",info.namespace_name,info.key,(unsigned)info.type,(unsigned)n,e);
        err=nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}
esp_err_t storage_maintenance_run(storage_maintenance_result_t *out){
    storage_maintenance_result_t r={0};
    if(!out)return ESP_ERR_INVALID_ARG;
    if(!record_store_ready()||!mounted()){
        r.error=ESP_ERR_INVALID_STATE;
        *out=r;
        return r.error;
    }
    record_store_lock();
    r.available_before=available();
    inventory();
    for(unsigned i=0;i<sizeof(spaces)/sizeof(spaces[0]);++i){
        nvs_iterator_t it=NULL;
        esp_err_t err=nvs_entry_find(NVS_DEFAULT_PART_NAME,spaces[i],NVS_TYPE_ANY,&it);
        // 先保存键清单，删除时不使NVS迭代器失效。/ Collect identifiers before erasing to avoid invalidating a live NVS iterator.
        struct key {
            char key[16];
            nvs_type_t type;
            struct key *next;
        }
        *head=NULL,**tail=&head;
        while(err==ESP_OK){
            nvs_entry_info_t info;
            err=nvs_entry_info(it,&info);
            if(err!=ESP_OK)break;
            if(record_store_content_key(spaces[i],info.key)){
                struct key *k=calloc(1,sizeof(*k));
                if(!k){
                    err=ESP_ERR_NO_MEM;
                    break;
                }
                strcpy(k->key,info.key);
                k->type=info.type;
                *tail=k;
                tail=&k->next;
            }
            err=nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        if(err!=ESP_ERR_NVS_NOT_FOUND&&err!=ESP_OK){
            r.error=err;
            r.failed++;
        }
        while(head){
            struct key *k=head;
            head=k->next;
            ++r.examined;
            if(r.error!=ESP_OK||!mounted()){
                free(k);
                r.failed++;
                if(!r.error)r.error=ESP_ERR_INVALID_STATE;
                continue;
            }
            nvs_handle_t source;
            err=nvs_open(spaces[i],NVS_READONLY,&source);
            uint8_t *value=NULL;
            size_t n=0;
            if(err==ESP_OK){
                err=raw_read(source,k->key,k->type,NULL,&n);
                if(err==ESP_OK&&(!n||n>4096))err=ESP_ERR_INVALID_SIZE;
                if(err==ESP_OK){
                    value=malloc(n);
                    if(!value)err=ESP_ERR_NO_MEM;
                    else err=raw_read(source,k->key,k->type,value,&n);
                }
                nvs_close(source);
            }
            if(err==ESP_OK&&!book_history_record_valid(spaces[i],k->type,k->key,value,n)){
                r.skipped++;
                free(value);
                free(k);
                continue;
            }
            if(err==ESP_OK)err=records_archive_legacy(spaces[i],k->key,k->type,value,n);
            if(err==ESP_OK)err=i==0?progress_kv_migration_copy(k->key,k->type,value,n,book_history_record_valid):records_migrate_key(spaces[i],k->key,k->type,value,n);
            if(err==ESP_OK&&i!=0)err=records_sd_validate(spaces[i],k->key,book_history_record_valid);
            // 删除前重新读源，防止把迁移期间变更的记录清掉。/ Re-read the source before deletion to reject concurrent changes.
            uint8_t *check=err==ESP_OK?malloc(n):NULL;
            if(err==ESP_OK&&!check)err=ESP_ERR_NO_MEM;
            if(err==ESP_OK&&mounted()){
                err=nvs_open(spaces[i],NVS_READWRITE,&source);
                if(err==ESP_OK){
                    size_t bytes=n;
                    err=raw_read(source,k->key,k->type,check,&bytes);
                    if(err==ESP_OK&&(bytes!=n||memcmp(check,value,n)))err=ESP_ERR_INVALID_STATE;
                    if(err==ESP_OK)err=nvs_erase_key(source,k->key);
                    if(err==ESP_OK)err=nvs_commit(source);
                    nvs_close(source);
                    if(err==ESP_OK){
                        err=nvs_open(spaces[i],NVS_READONLY,&source);
                        if(err==ESP_OK){
                            bytes=n;
                            esp_err_t gone=raw_read(source,k->key,k->type,check,&bytes);
                            nvs_close(source);
                            err=gone==ESP_ERR_NVS_NOT_FOUND?ESP_OK:gone==ESP_OK?ESP_ERR_INVALID_RESPONSE:gone;
                        }
                    }
                }
            }
            else if(err==ESP_OK)err=ESP_ERR_INVALID_STATE;
            free(check);
            free(value);
            free(k);
            if(err==ESP_OK)r.migrated++;
            else{
                r.error=err;
                r.failed++;
            }
        }
    }
    r.available_after=available();
    record_store_unlock();
    *out=r;
    ESP_LOGI("storage","migration examined=%u retired=%u skipped=%u failed=%u available=%u->%u error=%d",r.examined,r.migrated,r.skipped,r.failed,(unsigned)r.available_before,(unsigned)r.available_after,r.error);
    FILE *f=fopen(ROOT "/nvs-migration.csv","ab");
    if(f){
        if(ftell(f)==0)fputs("examined,retired,skipped,failed,available_before,available_after,error\n",f);
        fprintf(f,"%u,%u,%u,%u,%zu,%zu,%d\n",r.examined,r.migrated,r.skipped,r.failed,r.available_before,r.available_after,r.error);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
    return r.error;
}
