/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实存储模块的容量、迁移失败、合并备份与跨进程回读回归；隔离测试卡。
 * English: Real storage under capacity pressure, migration faults, merged backup and fresh-process reads on an isolated card.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "book/record_store.h"
#include "book/storage_maintenance.h"
#include "book/book_history_backup.h"
#include "book/book_progress.h"
#include "book/book_title.h"
#include "book/reader_input.h"
#include "settings_storage.h"
#include "settings.h"
#include "read_pico_sd.h"
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){memset(out,0,sizeof(*out));return ESP_OK;}
static uint32_t hash(const char *p){uint32_t h=2166136261u;for(;*p;p++)h=(h^(unsigned char)*p)*16777619u;return h;}
static void raw(const char *ns,const char *key,nvs_type_t type,const void *v,size_t n){nvs_handle_t h;assert(!nvs_open(ns,NVS_READWRITE,&h));esp_err_t e=type==NVS_TYPE_U8?nvs_set_u8(h,key,*(const uint8_t*)v):type==NVS_TYPE_U32?nvs_set_u32(h,key,*(const uint32_t*)v):type==NVS_TYPE_STR?nvs_set_str(h,key,v):nvs_set_blob(h,key,v,n);assert(!e);assert(!nvs_commit(h));nvs_close(h);}
static bool exists(const char *ns,const char *key){nvs_handle_t h;esp_err_t e=nvs_open(ns,NVS_READONLY,&h);if(e==ESP_ERR_NVS_NOT_FOUND)return false;assert(!e);size_t n=0;e=nvs_get_blob(h,key,NULL,&n);nvs_close(h);assert(!e||e==ESP_ERR_NVS_NOT_FOUND);return e==ESP_OK;}
static void migration(void){storage_maintenance_result_t r={0};assert(!storage_maintenance_run(&r));assert(!r.failed);}
static size_t used(void){nvs_iterator_t it=NULL;size_t total=0;esp_err_t e=nvs_entry_find(NVS_DEFAULT_PART_NAME,NULL,NVS_TYPE_ANY,&it);while(e==ESP_OK){nvs_entry_info_t info;assert(!nvs_entry_info(it,&info));nvs_handle_t h;assert(!nvs_open(info.namespace_name,NVS_READONLY,&h));size_t n=0;assert(!nvs_get_blob(h,info.key,NULL,&n));nvs_close(h);total+=n<=4?1:2+(n+31)/32;e=nvs_entry_next(&it);}nvs_release_iterator(it);assert(e==ESP_ERR_NVS_NOT_FOUND);return total;}
static void seed(void){
    assert(!settings_storage_put_u8("read_pico","sys_size",100));
    assert(!settings_storage_put_str("wifi","password","private-do-not-copy"));
    for(int i=0;i<80;i++){char path[288],key[16],blob[416];snprintf(path,sizeof(path),"/sdcard/books/Book-%d.epub",i);snprintf(key,sizeof(key),"t_%08x",hash(path));size_t n=strlen(path)+1;memcpy(blob,path,n);strcpy(blob+n,"Reading title");raw("rp_titles",key,NVS_TYPE_BLOB,blob,n+14);}
    const char *legacy="/sdcard/books/legacy.epub";char legacy_key[16];snprintf(legacy_key,sizeof(legacy_key),"b_%08x",hash(legacy));
    uint8_t old_progress[100]={'R','P','B',2};old_progress[4]=16;old_progress[5]=39;old_progress[14]=40;old_progress[15]=10;old_progress[22]=(uint8_t)(strlen(legacy)+1);memcpy(old_progress+24,legacy,strlen(legacy)+1);
    raw("rp_books",legacy_key,NVS_TYPE_BLOB,old_progress,25+strlen(legacy));
    uint8_t one=1;raw("rp_input","h00000000000001",NVS_TYPE_U8,&one,1);
    raw("rp_input","h00000000000002",NVS_TYPE_U8,&one,1);
    raw("rp_titles","unknown",NVS_TYPE_U8,&one,1);
    // 无效旧记录必须保留，不能因为键前缀相同就删除。/ Preserve invalid sources even when their prefixes match.
    char bad[4]={0};raw("rp_titles","t_12345678",NVS_TYPE_BLOB,bad,4);
    raw("rp_books","last",NVS_TYPE_STR,"/sdcard/books/old.epub",25);
    uint32_t seq=42;raw("rp_books","seq",NVS_TYPE_U32,&seq,4);
    for(unsigned i=256;used()<504;i++){char key[16];snprintf(key,sizeof(key),"h%014x",i);raw("rp_input",key,NVS_TYPE_U8,&one,1);}
    assert(used()==504);
}
int main(int argc,char **argv){
    assert(argc==2);record_store_media(true);app_settings_init();
    if(!strcmp(argv[1],"seed")){seed();return 0;}
    if(!strcmp(argv[1],"recover")){
        if(used()==504){setenv("PICO_TEST_NVS_ENTRY_LIMIT","504",1);assert(app_settings_set_system_font_size(140)==0x1105);assert(app_settings_system_font_size()==100);(void)app_settings_take_save_error();unsetenv("PICO_TEST_NVS_ENTRY_LIMIT");}
        // 已更新TF与删除墓碑优先，迁移不能把旧值覆盖回来。/ New SD values and tombstones outrank legacy values during migration.
        char path[80];snprintf(path,sizeof(path),"/sdcard/books/Book-0.epub");assert(!book_title_set(path,"New title"));
        records_handle_t h;assert(!records_open("rp_input",NVS_READWRITE,&h));assert(!records_erase_key(h,"h00000000000002"));records_close(h);
        book_progress_t newer={.file_size=10000,.chapter=3,.byte_off=3456,.px=40,.pct=51};assert(!book_progress_save("/sdcard/books/legacy.epub",&newer));
        migration();book_progress_t kept;assert(book_progress_load("/sdcard/books/legacy.epub",10000,&kept)&&kept.pct==51&&kept.byte_off==3456);char title[128];assert(book_title_get(path,title,sizeof(title))&&!strcmp(title,"New title"));
        assert(!exists("rp_input","h00000000000001")&&!exists("rp_input","h00000000000002"));
        assert(exists("rp_titles","unknown")&&exists("rp_titles","t_12345678")&&exists("wifi","password"));
        assert(!exists("rp_books","seq")&&!exists("rp_books","last"));
        uint8_t v;assert(!records_open("rp_input",NVS_READONLY,&h));assert(!records_get_u8(h,"h00000000000001",&v)&&v==1);assert(records_get_u8(h,"h00000000000002",&v)==ESP_ERR_NVS_NOT_FOUND);records_close(h);
        // 释放内容后，有界设置可写；实际芯片容量仍待真机验证。/ Bounded preferences can write after retirement; physical-chip capacity still needs device verification.
        setenv("PICO_TEST_NVS_ENTRY_LIMIT","100",1);assert(!app_settings_set_system_font_size(140));assert(!app_settings_set_system_font_path("/sdcard/fonts/test.ttf"));assert(!app_settings_set_font_path("/sdcard/fonts/book.ttf"));assert(!app_settings_set_staged_shutdown(false));assert(!app_settings_set_idle_sleep_minutes(10));assert(!app_settings_set_book_margin(8));unsetenv("PICO_TEST_NVS_ENTRY_LIMIT");
        book_progress_t p={.file_size=10000,.chapter=2,.byte_off=2345,.px=40,.pct=37};assert(!book_progress_save("/sdcard/books/new.epub",&p));
        assert(!book_progress_set_last_path("/sdcard/books/new.epub"));
        migration();puts("PASS capacity recovery, unknown preservation, newer SD/tombstones, idempotence");return 0;
    }
    if(!strcmp(argv[1],"read")){
        nvs_handle_t h;uint8_t v;assert(!nvs_open("read_pico",NVS_READONLY,&h));assert(!nvs_get_u8(h,"sys_size",&v)&&v==140);nvs_close(h);
        assert(app_settings_system_font_size()==140&&!strcmp(app_settings_system_font_path(),"/sdcard/fonts/test.ttf"));assert(!strcmp(app_settings_font_path(),"/sdcard/fonts/book.ttf")&&!app_settings_staged_shutdown()&&app_settings_idle_sleep_minutes()==10&&app_settings_book_margin()==8);
        book_progress_t p;assert(book_progress_load("/sdcard/books/new.epub",10000,&p)&&p.pct==37&&p.byte_off==2345);char last[288];assert(book_progress_last_path(last,sizeof(last))&&!strcmp(last,"/sdcard/books/new.epub"));
        puts("PASS fresh process preferences, progress and recent path");return 0;
    }
    if(!strcmp(argv[1],"fault")){
        storage_maintenance_result_t r;esp_err_t e=storage_maintenance_run(&r);assert(e!=ESP_OK&&r.failed);assert(exists("rp_titles","t_12345678"));puts("PASS migration failure preserves source");return 0;
    }
    if(!strcmp(argv[1],"no-card")){
        records_handle_t h;assert(!records_open("rp_input",NVS_READWRITE,&h));record_store_media(false);assert(records_set_u8(h,"h00000000000003",1)!=ESP_OK);storage_maintenance_result_t deferred={0};assert(storage_maintenance_run(&deferred)==ESP_ERR_INVALID_STATE&&deferred.error==ESP_ERR_INVALID_STATE);
        assert(!records_open("rp_input",NVS_READWRITE,&h));assert(records_set_u8(h,"h00000000000003",1)!=ESP_OK);
        book_progress_t p={.file_size=100,.px=40};assert(book_progress_save("/sdcard/books/no-card.epub",&p)!=ESP_OK);assert(!exists("rp_input","h00000000000003"));record_store_media(true);assert(records_set_u8(h,"h00000000000003",1)!=ESP_OK);puts("PASS no-card writes rejected, stale handles rejected");return 0;
    }
    if(!strcmp(argv[1],"backup")){
        // 未知/坏旧记录让导出失败，不能封成缺项备份。/ Unknown or bad legacy records must fail export, never seal an incomplete backup.
        FILE *f=fopen("build/simulator/bad-history.bin","w+b");assert(f);assert(book_history_backup_write(f)!=ESP_OK);fclose(f);
        nvs_handle_t rawh;assert(!nvs_open("rp_titles",NVS_READWRITE,&rawh));assert(!nvs_erase_key(rawh,"unknown"));assert(!nvs_erase_key(rawh,"t_12345678"));nvs_close(rawh);
        f=fopen("build/simulator/history.bin","w+b");assert(f);assert(!book_history_backup_write(f));rewind(f);assert(book_history_backup_validate(f));rewind(f);assert(!book_history_backup_restore(f));fclose(f);
        assert(exists("wifi","password"));puts("PASS merged backup/restore and credentials preserved");return 0;
    }
    if(!strcmp(argv[1],"source-changed")){
        storage_maintenance_result_t r;assert(storage_maintenance_run(&r)==ESP_ERR_INVALID_STATE);
        assert(exists("rp_books","last"));nvs_handle_t h;char path[100];size_t n=sizeof(path);
        assert(!nvs_open("rp_books",NVS_READONLY,&h));assert(!nvs_get_str(h,"last",path,&n)&&path[0]=='!');nvs_close(h);
        char sd[288];assert(book_progress_last_path(sd,sizeof(sd))&&sd[0]=='/');
        puts("PASS source mutation blocks retirement while archived and SD copies remain intact");return 0;
    }
    if(!strcmp(argv[1],"growth-limit")){
        size_t before=used();records_handle_t h;assert(!records_open("rp_input",NVS_READWRITE,&h));
        for(unsigned i=0;i<600;i++){char k[16];snprintf(k,sizeof(k),"h%014x",0x1000000+i);assert(!records_set_u8(h,k,1));}
        records_close(h);assert(used()==before);
        FILE *f=fopen("build/simulator/over-limit.bin","w+b");assert(f);assert(book_history_backup_write(f)==ESP_ERR_INVALID_STATE);rewind(f);assert(!book_history_backup_validate(f));fclose(f);
        puts("PASS growing content does not grow NVS; over-limit backup fails rather than truncating");return 0;
    }
    if(!strcmp(argv[1],"bad-domain")){
        records_handle_t h;assert(!records_open("rp_input",NVS_READWRITE,&h));uint8_t invalid=2;assert(!records_set_u8(h,"h00000000000001",invalid));records_close(h);
        storage_maintenance_result_t r;assert(storage_maintenance_run(&r)==ESP_ERR_INVALID_RESPONSE);assert(exists("rp_input","h00000000000001"));puts("PASS semantically invalid SD payload blocks retirement");return 0;
    }
    if(!strcmp(argv[1],"corrupt")){
        records_handle_t h;assert(!records_open("rp_input",NVS_READWRITE,&h));assert(!records_set_u8(h,"h00000000000004",1));records_close(h);
        FILE *f=fopen("/sdcard/.readpico/records/rp_input/h00000000000004.b","wb");assert(f);fputs("bad",f);fclose(f);
        uint8_t one=1;raw("rp_input","h00000000000004",NVS_TYPE_U8,&one,1);storage_maintenance_result_t r;assert(storage_maintenance_run(&r)!=ESP_OK);assert(exists("rp_input","h00000000000004"));
        f=fopen("build/simulator/corrupt-history.bin","w+b");assert(f);assert(book_history_backup_write(f)!=ESP_OK);fclose(f);puts("PASS corrupt alternate slot blocks source retirement and backup");return 0;
    }
    assert(!"unknown case");return 1;
}
