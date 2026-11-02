/* SPDX-License-Identifier: Apache-2.0
 * 多网络保存在一个NVS blob，旧单网络延迟迁移；失败不更新公开快照。
 * Store network history in one NVS blob, lazily importing the legacy record; publish only successful writes.
 */
#pragma once
#include <stdlib.h>
#ifndef READ_PICO_TRANSFER_NVS_TEST
#include "nvs.h"
#endif
#include "transfer_policy.h"

static esp_err_t load_profiles(transfer_wifi_profiles_t *out){
    memset(out,0,sizeof(*out));out->version=1;
    nvs_handle_t h;esp_err_t err=nvs_open("rp_wifi",NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    size_t size=sizeof(*out);err=nvs_get_blob(h,"profiles",out,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND){
        memset(out,0,sizeof(*out));out->version=1;
        transfer_credentials_t old={0};size=sizeof(old);err=nvs_get_blob(h,"credentials",&old,&size);
        if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
        else if(err==ESP_OK){
            if(size!=sizeof(old)||!credentials_valid(&old))err=ESP_ERR_INVALID_STATE;
            else{out->entries[0]=old;out->count=1;}
        }
        clear_secret(&old,sizeof(old));
    }else if(err==ESP_OK&&(size!=sizeof(*out)||!profiles_valid(out)))err=ESP_ERR_INVALID_STATE;
    nvs_close(h);
    if(err!=ESP_OK){clear_secret(out,sizeof(*out));out->version=1;}
    return err;
}
static esp_err_t store_profiles(const transfer_wifi_profiles_t *next){
    if(!profiles_valid(next))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t err=nvs_open("rp_wifi",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    err=nvs_set_blob(h,"profiles",next,sizeof(*next));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);
    if(err!=ESP_OK)return err;
    transfer_wifi_profiles_t *check=malloc(sizeof(*check));if(!check)return ESP_ERR_NO_MEM;
    err=nvs_open("rp_wifi",NVS_READONLY,&h);
    if(err==ESP_OK){size_t size=sizeof(*check);err=nvs_get_blob(h,"profiles",check,&size);nvs_close(h);
        if(err==ESP_OK&&(size!=sizeof(*check)||memcmp(check,next,sizeof(*check))))err=ESP_ERR_INVALID_STATE;}
    clear_secret(check,sizeof(*check));free(check);return err;
}
static esp_err_t load_credentials(transfer_credentials_t *out){
    memset(out,0,sizeof(*out));transfer_wifi_profiles_t p;
    esp_err_t err=load_profiles(&p);if(err==ESP_OK&&p.count)*out=p.entries[0];
    clear_secret(&p,sizeof(p));return err;
}
static esp_err_t store_credentials(const transfer_credentials_t *next){
    if(next&&!credentials_valid(next))return ESP_ERR_INVALID_ARG;
    transfer_wifi_profiles_t p={.version=1};esp_err_t err=ESP_OK;
    if(next){err=load_profiles(&p);
        if(err==ESP_OK&&p.count&&!memcmp(&p.entries[0],next,sizeof(*next))){clear_secret(&p,sizeof(p));return ESP_OK;}
        if(err==ESP_OK)profiles_upsert(&p,next);
    }
    if(err==ESP_OK)err=store_profiles(&p);
    clear_secret(&p,sizeof(p));return err;
}
