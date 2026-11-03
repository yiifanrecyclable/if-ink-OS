/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实配置模块的读错/写错/读回故障回归；仅使用内存NVS。
 * English: Real configuration modules under load/write/readback failures, using memory-only NVS.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nvs.h"
#include "cloud/cloud_config.h"
#include "book/record_store.h"
#include <sys/stat.h>
typedef struct { char space[20],key[20];unsigned char data[1024];size_t n;int type; } record_t;
static record_t records[64];static int count,handles,writes;
static char active_space[20];static int active_mode;
static bool fail_open,fail_read,fail_set,fail_commit,fail_readback,corrupt_readback;
static bool wrote;
static record_t *find(const char *key){for(int i=0;i<count;++i)if(!strcmp(records[i].space,active_space)&&!strcmp(records[i].key,key))return &records[i];return NULL;}
esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *h){if(fail_open)return ESP_FAIL;assert(!handles);strcpy(active_space,space);active_mode=mode;*h=1;++handles;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1&&handles==1);--handles;}
static esp_err_t get(const char *key,void *out,size_t *n,int type){
    assert(handles==1);if(fail_read||(fail_readback&&wrote&&active_mode==NVS_READONLY))return ESP_FAIL;
    record_t *r=find(key);if(!r)return ESP_ERR_NVS_NOT_FOUND;if(r->type!=type)return ESP_ERR_INVALID_RESPONSE;
    if(out&&*n<r->n)return ESP_ERR_INVALID_SIZE;if(out){memcpy(out,r->data,r->n);if(corrupt_readback&&wrote&&active_mode==NVS_READONLY&&r->n)((unsigned char*)out)[0]^=1;}
    *n=r->n;return ESP_OK;
}
static esp_err_t put(const char *key,const void *in,size_t n,int type){
    assert(handles==1&&active_mode==NVS_READWRITE);if(fail_set)return ESP_FAIL;
    record_t *r=find(key);if(!r){assert(count<64);r=&records[count++];strcpy(r->space,active_space);strcpy(r->key,key);}
    assert(n<=sizeof(r->data));memcpy(r->data,in,n);r->n=n;r->type=type;++writes;wrote=true;return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*v){(void)h;size_t n=1;return get(k,v,&n,1);}
esp_err_t nvs_set_u8(nvs_handle_t h,const char*k,uint8_t v){(void)h;return put(k,&v,1,1);}
esp_err_t nvs_get_str(nvs_handle_t h,const char*k,char*v,size_t*n){(void)h;return get(k,v,n,2);}
esp_err_t nvs_set_str(nvs_handle_t h,const char*k,const char*v){(void)h;return put(k,v,strlen(v)+1,2);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*v,size_t*n){(void)h;return get(k,v,n,3);}
esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*v,size_t n){(void)h;return put(k,v,n,3);}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return fail_commit?ESP_FAIL:ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char*k){(void)h;record_t *r=find(k);if(!r)return ESP_ERR_NVS_NOT_FOUND;*r=records[--count];return ESP_OK;}
esp_err_t nvs_erase_all(nvs_handle_t h){(void)h;for(int i=count-1;i>=0;--i)if(!strcmp(records[i].space,active_space))records[i]=records[--count];return ESP_OK;}
esp_err_t nvs_get_u32(nvs_handle_t h,const char*k,uint32_t*v){(void)h;size_t n=4;return get(k,v,&n,4);}
esp_err_t nvs_set_u32(nvs_handle_t h,const char*k,uint32_t v){(void)h;return put(k,&v,4,4);}
esp_err_t nvs_entry_find(const char*p,const char*n,nvs_type_t t,nvs_iterator_t*i){(void)p;(void)n;(void)t;*i=NULL;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_entry_info(nvs_iterator_t i,nvs_entry_info_t *v){(void)i;(void)v;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_entry_next(nvs_iterator_t *i){*i=NULL;return ESP_ERR_NVS_NOT_FOUND;}
void nvs_release_iterator(nvs_iterator_t i){(void)i;}
#include "../main/book/reader_input.c"
#include "../main/book/content_library.c"
// 网络校验不属于此存储回归；实际配置函数由运行器原样抽取。/ Network validation is outside this storage test; runner extracts real configuration functions verbatim.
static bool cloud_relative_path(const char *p){return p&&*p;}
static bool cloud_utf8_valid(const char *p,size_t n){(void)p;(void)n;return true;}
static void *s_task;
static void clear_secret(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
#include "cloud_config_under_test.inc"
static void clear_faults(void){fail_open=fail_read=fail_set=fail_commit=fail_readback=corrupt_readback=wrote=false;}
int main(void){
    assert(!mkdir(RECORDS_CARD_ROOT,0700));record_store_media(true);
    cloud_config_t cloud={0};assert(cloud_config_load_checked(&cloud)==ESP_OK);assert(!strcmp(cloud.folder,CLOUD_DEFAULT_FOLDER));
    strcpy(cloud.folder,"Folder");strcpy(cloud.account,"user");strcpy(cloud.password,"test-secret");assert(cloud_config_save(&cloud)==ESP_OK);
    cloud_config_t preserved=cloud;int oldwrites=writes;
    fail_read=true;assert(cloud_config_load_checked(&preserved)==ESP_FAIL);assert(!memcmp(&cloud,&preserved,sizeof(cloud)));assert(writes==oldwrites);
    assert(cloud_config_backup_export(&preserved)==ESP_FAIL);for(size_t i=0;i<sizeof(preserved);++i)assert(((unsigned char*)&preserved)[i]==0);clear_faults();
    fail_readback=true;assert(cloud_config_save(&cloud)==ESP_FAIL);clear_faults();assert(cloud_config_save(&cloud)==ESP_OK);
    corrupt_readback=true;assert(cloud_config_save(&cloud)!=ESP_OK);clear_faults();assert(cloud_config_save(&cloud)==ESP_OK);
    assert(cloud_config_forget()==ESP_OK);assert(cloud_config_load_checked(&preserved)==ESP_OK&&!preserved.account[0]);

    reader_input_preferences_t p={.version=2};p.actions[0]=READER_ACTION_TOC;assert(reader_input_import(&p)==ESP_OK);
    s_loaded=false;fail_read=true;oldwrites=writes;assert(reader_input_assign(1,READER_ACTION_NEXT)==ESP_FAIL);assert(writes==oldwrites&&!s_loaded);clear_faults();
    assert(reader_input_assign(1,READER_ACTION_NEXT)==ESP_OK);assert(reader_input_export_checked(&p)==ESP_OK&&p.actions[0]==READER_ACTION_TOC&&p.actions[1]==READER_ACTION_NEXT);
    reader_input_preferences_t prior=s_prefs;p.actions[1]=READER_ACTION_PREV;fail_readback=true;assert(reader_input_import(&p)==ESP_FAIL);assert(!memcmp(&prior,&s_prefs,sizeof(prior)));clear_faults();assert(reader_input_import(&p)==ESP_OK);
    assert(reader_input_ack_help("/sdcard/books/test.txt",true)==ESP_OK);
    fail_open=true;oldwrites=writes;assert(reader_input_ack_help("/sdcard/books/test.txt",false)==ESP_FAIL&&writes==oldwrites);clear_faults();

    content_banner_config_t banner;assert(content_banner_load_checked(&banner)==ESP_OK);strcpy(banner.text,"Preserved quote");assert(content_banner_save(&banner)==ESP_OK);
    content_banner_config_t sentinel=banner;fail_read=true;oldwrites=writes;assert(content_banner_load_checked(&sentinel)==ESP_FAIL&&!memcmp(&sentinel,&banner,sizeof(banner)));
    assert(content_home_quote_alignment_set(CONTENT_QUOTE_LEFT)==ESP_FAIL&&writes==oldwrites);clear_faults();
    assert(content_home_quote_alignment_set(CONTENT_QUOTE_LEFT)==ESP_OK);assert(content_banner_load_checked(&banner)==ESP_OK&&!strcmp(banner.text,"Preserved quote")&&banner.align==CONTENT_QUOTE_LEFT);
    fail_readback=true;assert(content_banner_save(&banner)==ESP_FAIL);clear_faults();assert(content_banner_save(&banner)==ESP_OK);
    corrupt_readback=true;assert(content_banner_save(&banner)==ESP_ERR_INVALID_RESPONSE);clear_faults();
    uint8_t sizes[4]={40,70,60,50},loaded[4]={0};assert(content_markdown_sizes_set(sizes)==ESP_OK);assert(content_markdown_sizes_checked(loaded)==ESP_OK&&!memcmp(sizes,loaded,4));
    fail_read=true;memset(loaded,99,4);assert(content_markdown_sizes_checked(loaded)==ESP_FAIL&&loaded[0]==99);clear_faults();
    assert(content_markdown_font_set("builtin")==ESP_OK);char font[192];assert(content_markdown_font_checked(font,sizeof(font))==ESP_OK&&!strcmp(font,"builtin"));
    assert(content_reading_target_set(120)==ESP_OK);uint8_t target;assert(content_reading_target_load_checked(&target)==ESP_OK&&target==120);
    content_options_t options={.mode=CONTENT_ADDED,.ascending=true},out;assert(content_options_save(false,options)==ESP_OK);assert(content_options_load_checked(false,false,&out)==ESP_OK&&out.mode==options.mode&&out.ascending);
    fail_set=true;assert(content_reading_target_set(150)==ESP_FAIL);clear_faults();assert(content_reading_target_load_checked(&target)==ESP_OK&&target==120);
    fail_commit=true;assert(content_options_save(false,options)==ESP_FAIL);clear_faults();
    // 新快照缺失才读取旧键；坏快照不被旧setter覆盖。/ Legacy keys apply only when the snapshot is absent; corruption must not be overwritten.
    nvs_handle_t h;assert(nvs_open("if_content",NVS_READWRITE,&h)==ESP_OK);assert(nvs_erase_key(h,"banner_v1")==ESP_OK);
    assert(nvs_set_str(h,"home_quote","Legacy quote")==ESP_OK);nvs_close(h);
    assert(content_banner_load_checked(&banner)==ESP_OK&&!strcmp(banner.text,"Legacy quote"));
    banner.version=1;banner.show_text=0;banner.mode=0;
    assert(nvs_open("if_content",NVS_READWRITE,&h)==ESP_OK);assert(nvs_set_blob(h,"banner_v1",&banner,652)==ESP_OK);nvs_close(h);
    assert(content_banner_load_checked(&banner)==ESP_OK&&banner.version==3&&banner.show_text==1&&banner.text_y==CONTENT_BANNER_TEXT_AUTO);
    assert(nvs_open("if_content",NVS_READWRITE,&h)==ESP_OK);unsigned char broken=255;assert(nvs_set_blob(h,"banner_v1",&broken,1)==ESP_OK);nvs_close(h);
    oldwrites=writes;assert(content_home_quote_set("replacement")==ESP_ERR_INVALID_RESPONSE&&writes==oldwrites);
    assert(content_banner_load_checked(&banner)==ESP_ERR_INVALID_RESPONSE);
    assert(content_book_pin_set("/sdcard/books/test.txt",true)==ESP_OK);assert(content_book_pinned("/sdcard/books/test.txt"));
    assert(content_book_pin_set("/sdcard/books/test.txt",false)==ESP_OK);assert(!content_book_pinned("/sdcard/books/test.txt"));
    assert(handles==0);puts("PASS checked auxiliary loads, read-error RMW rejection, exact readback, retry, secret clearing and legacy formats");return 0;
}
