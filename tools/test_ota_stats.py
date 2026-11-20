#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production consent logic with simulated NVS and HTTP."""
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = re.sub(r'^#include [^\n]*\n', '', (ROOT / 'main/ota_stats.c').read_text(), flags=re.M)
STUBS = r'''
#include "ota_online_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define NVS_READONLY 0
#define ESP_ERR_NVS_NOT_FOUND 100
#define HTTP_METHOD_POST 1
typedef int nvs_handle_t;
static bool stored,fail_save;
static uint8_t disk[17];
static unsigned random_calls,requests;
static char serial_header[33],version_header[33],consent_header[3];
static int response_code;
static esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *h){
 assert(!strcmp(space,"if_ota_stats")&&mode==NVS_READONLY);*h=1;
 return stored?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
static esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *bytes){
 assert(h==1&&!strcmp(key,"consent_v1")&&*bytes==sizeof(disk));
 memcpy(out,disk,*bytes);return ESP_OK;
}
static void nvs_close(nvs_handle_t h){assert(h==1);}
static esp_err_t settings_storage_put_blob(const char *ns,const char *key,const void *data,size_t n){
 assert(!strcmp(ns,"if_ota_stats")&&!strcmp(key,"consent_v1")&&n==sizeof(disk));
 if(fail_save)return ESP_FAIL;
 memcpy(disk,data,n);stored=true;return ESP_OK;
}
static void esp_fill_random(void *out,size_t length){
 assert(length==16);memset(out,0x11+random_calls++,length);
}
typedef struct {const char *url; int method; int (*crt_bundle_attach)(void *);
 bool disable_auto_redirect; int timeout_ms;} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
static int esp_crt_bundle_attach(void *ignored){(void)ignored;return 0;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
 assert(!strcmp(cfg->url,IF_OTA_MIRROR_ROOT "/forget")&&cfg->method==HTTP_METHOD_POST);
 assert(cfg->crt_bundle_attach&&cfg->disable_auto_redirect&&cfg->timeout_ms<=2500);
 ++requests;return (void *)1;
}
static esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value){
 assert(h==(void *)1);
 if(!strcmp(key,"X-Ifink-Serial"))snprintf(serial_header,sizeof(serial_header),"%s",value);
 else if(!strcmp(key,"X-Ifink-Version"))snprintf(version_header,sizeof(version_header),"%s",value);
 else if(!strcmp(key,"X-Ifink-Consent"))snprintf(consent_header,sizeof(consent_header),"%s",value);
 else assert(false);
 return ESP_OK;
}
static esp_err_t esp_http_client_open(esp_http_client_handle_t h,int data){assert(h&&!data);return ESP_OK;}
static int esp_http_client_fetch_headers(esp_http_client_handle_t h){assert(h);return 0;}
static int esp_http_client_get_status_code(esp_http_client_handle_t h){assert(h);return response_code;}
static void esp_http_client_close(esp_http_client_handle_t h){assert(h);}
static void esp_http_client_cleanup(esp_http_client_handle_t h){assert(h);}
'''
TEST = r'''
static void clear_headers(void){serial_header[0]=version_header[0]=consent_header[0]=0;}
int main(void){
    assert(!ota_stats_enabled());
    ota_stats_add_headers((void *)1,"0.2.5");
    ota_stats_flush_withdrawal();
    assert(!serial_header[0]&&!requests&&!random_calls);
    assert(ota_stats_set_enabled(true)==ESP_OK&&ota_stats_enabled()&&!random_calls);
    ota_stats_add_headers((void *)1,"0.2.5-beta2t");
    assert(!serial_header[0]&&!random_calls);
    fail_save=true;ota_stats_add_headers((void *)1,"0.2.5");assert(!serial_header[0]);
    fail_save=false;ota_stats_add_headers((void *)1,"0.2.5");
    assert(strlen(serial_header)==32&&!strcmp(consent_header,"1")&&!strcmp(version_header,"0.2.5"));
    char previous[33];strcpy(previous,serial_header);clear_headers();
    ota_stats_add_headers((void *)1,"0.2.6-beta.1");
    assert(!strcmp(previous,serial_header)&&!strcmp(version_header,"0.2.6-beta.1"));
    assert(ota_stats_set_enabled(false)==ESP_OK&&!ota_stats_enabled());
    clear_headers();ota_stats_add_headers((void *)1,"0.2.5");assert(!serial_header[0]);
    response_code=503;ota_stats_flush_withdrawal();
    assert(requests==1&&!strcmp(serial_header,previous)&&!version_header[0]&&!consent_header[0]);
    response_code=204;ota_stats_flush_withdrawal();ota_stats_flush_withdrawal();
    assert(requests==2&&!memcmp(disk,(uint8_t[17]){0},sizeof(disk)));
    assert(ota_stats_set_enabled(true)==ESP_OK);
    ota_stats_add_headers((void *)1,"0.2.5");assert(strcmp(previous,serial_header));
    puts("PASS: zero default traffic, explicit consent, persistent random ID, opt-out and acknowledged deletion");
}
'''


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(STUBS + SOURCE + TEST)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                    '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'tools/ota_stubs'), '-I' + str(ROOT / 'tools/simulator/vendor/cjson'),
                    '-I' + str(ROOT / 'main'), str(path / 'test.c'), str(ROOT / 'main/ota_online_policy.c'),
                    str(ROOT / 'tools/simulator/vendor/cjson/cJSON.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
