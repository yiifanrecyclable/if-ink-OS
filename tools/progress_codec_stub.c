/* SPDX-License-Identifier: Apache-2.0
 * 中文：进度编解码单测的KV替身，注入的NVS错误代表下层KV错误，不是产品无卡写入行为。
 * English: KV fake for progress codec tests; injected NVS failures stand for KV errors, not production cardless writes.
 */
#include "book_progress_kv.h"
#include <string.h>
esp_err_t progress_kv_open(int mode,progress_kv_handle_t *h){memset(h,0,sizeof(*h));return nvs_open("rp_books",mode,&h->legacy);}
void progress_kv_close(progress_kv_handle_t h){nvs_close(h.legacy);}
esp_err_t progress_kv_get_blob(progress_kv_handle_t h,const char*k,void*p,size_t*n){return nvs_get_blob(h.legacy,k,p,n);}
esp_err_t progress_kv_set_blob(progress_kv_handle_t h,const char*k,const void*p,size_t n){return nvs_set_blob(h.legacy,k,p,n);}
esp_err_t progress_kv_get_str(progress_kv_handle_t h,const char*k,char*p,size_t*n){return nvs_get_str(h.legacy,k,p,n);}
esp_err_t progress_kv_set_str(progress_kv_handle_t h,const char*k,const char*p){return nvs_set_str(h.legacy,k,p);}
esp_err_t progress_kv_get_u32(progress_kv_handle_t h,const char*k,uint32_t*p){return nvs_get_u32(h.legacy,k,p);}
esp_err_t progress_kv_set_u32(progress_kv_handle_t h,const char*k,uint32_t p){return nvs_set_u32(h.legacy,k,p);}
esp_err_t progress_kv_commit(progress_kv_handle_t h){return nvs_commit(h.legacy);}
esp_err_t progress_kv_erase_key(progress_kv_handle_t h,const char*k){return nvs_erase_key(h.legacy,k);}
esp_err_t progress_kv_reset(void){nvs_handle_t h;esp_err_t e=nvs_open("rp_books",NVS_READWRITE,&h);if(e)return e;e=nvs_erase_all(h);if(!e)e=nvs_commit(h);nvs_close(h);return e;}
esp_err_t progress_kv_list(progress_kv_visit_fn visit,void *ctx){
    if(!visit)return ESP_ERR_INVALID_ARG;
    nvs_iterator_t it=NULL;esp_err_t e=nvs_entry_find(NVS_DEFAULT_PART_NAME,"rp_books",NVS_TYPE_ANY,&it);
    while(e==ESP_OK){nvs_entry_info_t info;e=nvs_entry_info(it,&info);if(e)break;uint8_t data[312];size_t n=sizeof(data);nvs_handle_t h;e=nvs_open("rp_books",NVS_READONLY,&h);if(e)break;
        if(info.type==NVS_TYPE_U32){n=4;uint32_t v;e=nvs_get_u32(h,info.key,&v);if(!e)memcpy(data,&v,4);}
        else if(info.type==NVS_TYPE_STR)e=nvs_get_str(h,info.key,(char*)data,&n);
        else e=nvs_get_blob(h,info.key,data,&n);
        nvs_close(h);if(e)break;if(!visit(info.key,info.type,data,n,ctx)){nvs_release_iterator(it);return ESP_OK;}e=nvs_entry_next(&it);
    }nvs_release_iterator(it);return e==ESP_ERR_NVS_NOT_FOUND?ESP_OK:e;
}
