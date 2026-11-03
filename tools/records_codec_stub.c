/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅供旧格式/业务单测的逻辑KV替身；不模拟TF或迁移，真实存储由test_records_capacity.py覆盖。
 * English: Logical KV fake for legacy codec/business tests only; real SD/migration is covered by test_records_capacity.py.
 */
#include "../main/book/record_store.h"
#include <string.h>
void record_store_lock(void){}
void record_store_unlock(void){}
bool record_store_content_key(const char *space,const char *key){
    if(strcmp(space,"rp_input")||!key||key[0]!='h'||strlen(key)!=15)return false;
    for(int i=1;i<15;i++)if(!strchr("0123456789abcdef",key[i]))return false;
    return true;
}
esp_err_t records_open(const char *ns,int mode,records_handle_t *out){nvs_handle_t h;esp_err_t e=nvs_open(ns,mode,&h);if(!e)*out=(records_handle_t)h;return e;}
void records_close(records_handle_t h){nvs_close((nvs_handle_t)h);}
esp_err_t records_commit(records_handle_t h){return nvs_commit((nvs_handle_t)h);}
esp_err_t records_get_blob(records_handle_t h,const char *k,void *p,size_t *n){return nvs_get_blob((nvs_handle_t)h,k,p,n);}
esp_err_t records_set_blob(records_handle_t h,const char *k,const void *p,size_t n){return nvs_set_blob((nvs_handle_t)h,k,p,n);}
esp_err_t records_get_str(records_handle_t h,const char *k,char *p,size_t *n){return nvs_get_str((nvs_handle_t)h,k,p,n);}
esp_err_t records_set_str(records_handle_t h,const char *k,const char *p){return nvs_set_str((nvs_handle_t)h,k,p);}
esp_err_t records_get_u8(records_handle_t h,const char *k,uint8_t *p){return nvs_get_u8((nvs_handle_t)h,k,p);}
esp_err_t records_set_u8(records_handle_t h,const char *k,uint8_t p){return nvs_set_u8((nvs_handle_t)h,k,p);}
esp_err_t records_get_u32(records_handle_t h,const char *k,uint32_t *p){return nvs_get_u32((nvs_handle_t)h,k,p);}
esp_err_t records_set_u32(records_handle_t h,const char *k,uint32_t p){return nvs_set_u32((nvs_handle_t)h,k,p);}
esp_err_t records_erase_key(records_handle_t h,const char *k){return nvs_erase_key((nvs_handle_t)h,k);}
esp_err_t records_erase_all(records_handle_t h){return nvs_erase_all((nvs_handle_t)h);}
esp_err_t records_entry_find_in_handle(records_handle_t h,nvs_type_t type,records_iterator_t *out){nvs_iterator_t it=NULL;esp_err_t e=nvs_entry_find_in_handle((nvs_handle_t)h,type,&it);*out=(records_iterator_t)it;return e;}
esp_err_t records_entry_next(records_iterator_t *out){nvs_iterator_t it=(nvs_iterator_t)*out;esp_err_t e=nvs_entry_next(&it);*out=(records_iterator_t)it;return e;}
esp_err_t records_entry_info(records_iterator_t it,nvs_entry_info_t *info){return nvs_entry_info((nvs_iterator_t)it,info);}
void records_release_iterator(records_iterator_t it){nvs_release_iterator((nvs_iterator_t)it);}
