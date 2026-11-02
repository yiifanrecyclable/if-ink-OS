/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界NVS偏好写入与独立句柄精确读回；不擦分区，不生成默认值，不记录私密内容。
 * English: Bounded preference writes with exact independent-handle readback; no partition erasure, defaults or secret logging.
 */
#pragma once
#include <stdlib.h>
#include <string.h>
#include "nvs.h"

#define SETTINGS_STORAGE_MAX_BYTES 4096u

/// 标量确认保存后才允许调用方更新已保存状态。/ Confirm a scalar before updating the caller's saved state.
static inline esp_err_t settings_storage_put_u8_stage(const char *space,const char *key,uint8_t value,const char **phase){
    *phase="open";
    nvs_handle_t h;esp_err_t err=nvs_open(space,NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    *phase="set";err=nvs_set_u8(h,key,value);if(err==ESP_OK){*phase="commit";err=nvs_commit(h);}nvs_close(h);
    if(err!=ESP_OK)return err;
    *phase="read_open";err=nvs_open(space,NVS_READONLY,&h);if(err!=ESP_OK)return err;
    uint8_t stored=0;*phase="read";err=nvs_get_u8(h,key,&stored);nvs_close(h);
    if(err==ESP_OK)*phase="compare";
    return err==ESP_OK&&stored!=value?ESP_ERR_INVALID_RESPONSE:err;
}

static inline esp_err_t settings_storage_put_u8(const char *space,const char *key,uint8_t value){
    const char *phase;return settings_storage_put_u8_stage(space,key,value,&phase);
}

/// 验证字符串类型、长度与全部字节；错误不等于“未写入”。/ Verify string type, length and every byte; an error does not prove no write occurred.
static inline esp_err_t settings_storage_put_str_stage(const char *space,const char *key,const char *value,const char **phase){
    *phase="validate";
    if(!value)return ESP_ERR_INVALID_ARG;
    size_t expected=strnlen(value,SETTINGS_STORAGE_MAX_BYTES);
    if(expected==SETTINGS_STORAGE_MAX_BYTES)return ESP_ERR_INVALID_ARG;
    ++expected;
    char *stored=malloc(expected);if(!stored)return ESP_ERR_NO_MEM;
    nvs_handle_t h;*phase="open";esp_err_t err=nvs_open(space,NVS_READWRITE,&h);
    if(err==ESP_OK){*phase="set";err=nvs_set_str(h,key,value);if(err==ESP_OK){*phase="commit";err=nvs_commit(h);}nvs_close(h);}
    if(err==ESP_OK){
        *phase="read_open";err=nvs_open(space,NVS_READONLY,&h);
        if(err==ESP_OK){size_t size=expected;*phase="read";err=nvs_get_str(h,key,stored,&size);nvs_close(h);
            if(err==ESP_OK){*phase="compare";if(size!=expected||memcmp(stored,value,expected))err=ESP_ERR_INVALID_RESPONSE;}}
    }
    volatile unsigned char *wipe=(volatile unsigned char *)stored;for(size_t i=0;i<expected;++i)wipe[i]=0;
    free(stored);return err;
}
static inline esp_err_t settings_storage_put_str(const char *space,const char *key,const char *value){
    const char *phase;return settings_storage_put_str_stage(space,key,value,&phase);
}

/// 整组偏好读回确认，不把部分写入当作成功。/ Confirm the complete preference blob, never acknowledging a partial write.
static inline esp_err_t settings_storage_put_blob(const char *space,const char *key,const void *value,size_t bytes){
    if(!value||!bytes||bytes>SETTINGS_STORAGE_MAX_BYTES)return ESP_ERR_INVALID_ARG;
    void *stored=malloc(bytes);if(!stored)return ESP_ERR_NO_MEM;
    nvs_handle_t h;esp_err_t err=nvs_open(space,NVS_READWRITE,&h);
    if(err==ESP_OK){err=nvs_set_blob(h,key,value,bytes);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
    if(err==ESP_OK){
        err=nvs_open(space,NVS_READONLY,&h);
        if(err==ESP_OK){size_t size=bytes;err=nvs_get_blob(h,key,stored,&size);nvs_close(h);
            if(err==ESP_OK&&(size!=bytes||memcmp(stored,value,bytes)))err=ESP_ERR_INVALID_RESPONSE;}
    }
    // 调用方可能包含凭据，不把验证缓冲留在堆上。/ Verification may contain credentials; wipe before releasing the buffer.
    volatile unsigned char *wipe=stored;for(size_t i=0;i<bytes;++i)wipe[i]=0;
    free(stored);return err;
}

/// 删除并验证一次性键确实不存在，不触及其他数据。/ Erase and verify an individual one-shot key without touching other data.
static inline esp_err_t settings_storage_erase(const char *space,const char *key){
    nvs_handle_t h;esp_err_t err=nvs_open(space,NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    size_t previous_size=0;esp_err_t previous=nvs_get_blob(h,key,NULL,&previous_size);nvs_close(h);
    if(previous==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    err=nvs_open(space,NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    err=nvs_erase_key(h,key);if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);if(err!=ESP_OK)return err;
    err=nvs_open(space,NVS_READONLY,&h);if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;if(err!=ESP_OK)return err;
    size_t size=0;err=nvs_get_blob(h,key,NULL,&size);nvs_close(h);
    return err==ESP_ERR_NVS_NOT_FOUND?ESP_OK:err==ESP_OK?ESP_ERR_INVALID_RESPONSE:err;
}
