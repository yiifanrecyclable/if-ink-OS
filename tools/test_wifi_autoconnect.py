#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实启动/候选/重试/停止代码的无线边界注入；不访问真实网络或设备。
Fault-inject radio boundaries around production startup/candidates/retries/cleanup without real networks or hardware.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'components/read_pico_transfer/read_pico_transfer.c').read_text()
def function(marker):
    start=SOURCE.index(marker);brace=SOURCE.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(SOURCE[end]=='{')-(SOURCE[end]=='}');end+=1
    return SOURCE[start:end]+'\n'

with tempfile.TemporaryDirectory(prefix='wifi-auto-') as directory:
    work=Path(directory);(work/'freertos').mkdir()
    (work/'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_NO_MEM,ESP_ERR_NOT_FOUND,ESP_ERR_TIMEOUT};
''')
    (work/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
void *heap_caps_calloc(size_t,size_t,int);
void heap_caps_free(void*);
''')
    (work/'freertos/FreeRTOS.h').write_text('#pragma once\n#define pdMS_TO_TICKS(ms) (ms)\n')
    (work/'freertos/task.h').write_text('#pragma once\nvoid vTaskDelay(unsigned);\n')
    (work/'esp_wifi.h').write_text('''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
enum {WIFI_AUTH_OPEN,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_ENTERPRISE};
enum {WIFI_IF_AP,WIFI_IF_STA,WIFI_MODE_AP,WIFI_MODE_STA,WIFI_STORAGE_RAM,WIFI_ALL_CHANNEL_SCAN,WIFI_CONNECT_AP_BY_SIGNAL,WPA3_SAE_PWE_BOTH,WIFI_COUNTRY_POLICY_MANUAL,WIFI_PS_NONE};
typedef struct {uint8_t ssid[33];int8_t rssi;int authmode;} wifi_ap_record_t;
typedef struct {struct {uint8_t ssid[32],password[65];struct {int authmode;} threshold;int scan_method,sort_method;struct {bool capable;} pmf_cfg;int sae_pwe_h2e;} sta;
struct {uint8_t ssid[33],password[65];unsigned ssid_len,channel,max_connection;int authmode;} ap;} wifi_config_t;
typedef struct {int unused;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {char cc[3];int schan,nchan,policy;} wifi_country_t;
esp_err_t esp_wifi_scan_start(const void*,bool);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t*);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*,wifi_ap_record_t*);
''')
    prelude=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "read_pico_transfer.h"
#include "transfer_auto_wifi.h"
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 192u,0u,2u,1u
#define ESP_EVENT_ANY_ID -1
#define ESP_MAC_WIFI_SOFTAP 1
enum {WIFI_EVENT=1,IP_EVENT=2,IP_EVENT_STA_LOST_IP=3,IP_EVENT_STA_GOT_IP=4};
typedef int esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef struct {int unused;} esp_netif_t;
typedef struct {esp_netif_t *esp_netif;struct {unsigned ip;} ip_info;} ip_event_got_ip_t;
typedef void *httpd_handle_t;
typedef struct {unsigned stack_size,max_uri_handlers,max_open_sockets,recv_wait_timeout,send_wait_timeout,task_caps;bool lru_purge_enable;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct {const char *uri;int method;int (*handler)(void*);} httpd_uri_t;
enum {HTTP_GET,HTTP_POST,HTTP_PUT,HTTP_DELETE};
static int lock_value;static int *s_lock=&lock_value;
static read_pico_transfer_status_t s_status;
static read_pico_transfer_cfg_t s_cfg;
static transfer_connection_t s_connection;
static transfer_wifi_profiles_t s_sta_profiles,committed;
static transfer_wifi_candidates_t s_sta_candidates;
static int s_sta_profile=-1;
static bool s_remember_connected,s_started,s_wifi,s_loop_owned,s_stopping,s_upload_active;
static esp_netif_t *s_netif;static httpd_handle_t s_http;
static esp_event_handler_instance_t s_events,s_ip_events;
static char s_root[160],*s_buffer;
static int64_t clock_ms;
static wifi_ap_record_t aps[40];static uint16_t ap_count;
static wifi_config_t driver_config;
static int scans,connects,stops,deinits,stores,scan_failures,allocation_failures,record_failures,record_live;
static bool fail_all,fail_first,online_success,deny_store;
static int mode;
static void ip_event(void*,esp_event_base_t,int32_t,void*);
static int64_t esp_timer_get_time(void){return clock_ms*1000;}
void vTaskDelay(unsigned ms){clock_ms+=ms;}
void *heap_caps_calloc(size_t n,size_t size,int caps){(void)caps;if(allocation_failures){--allocation_failures;return NULL;}void *p=calloc(n,size);if(p)++record_live;return p;}
void heap_caps_free(void *p){if(p){assert(record_live>0);--record_live;free(p);}}
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
esp_err_t esp_wifi_scan_start(const void *cfg,bool block){assert(!cfg&&block&&s_started&&mode==WIFI_MODE_STA);++scans;if(scan_failures){--scan_failures;return ESP_FAIL;}return ESP_OK;}
esp_err_t esp_wifi_scan_stop(void){return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(void){return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n){*n=ap_count;return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *out){assert(*n==ap_count);if(record_failures)return ESP_FAIL;memcpy(out,aps,*n*sizeof(*out));return ESP_OK;}
static esp_err_t load_profiles(transfer_wifi_profiles_t *p){*p=committed;return ESP_OK;}
static esp_err_t load_credentials(transfer_credentials_t *p){memset(p,0,sizeof(*p));if(committed.count)*p=committed.entries[0];return ESP_OK;}
static bool claim_config(void){return true;}
static void release_config(void){}
static esp_err_t store_profiles(const transfer_wifi_profiles_t *p){assert(profiles_valid(p));committed=*p;return ESP_OK;}
static esp_err_t store_credentials(const transfer_credentials_t *c){if(deny_store)return ESP_FAIL;++stores;profiles_upsert(&committed,c);return ESP_OK;}
static esp_err_t esp_netif_init(void){return ESP_OK;}
static esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
static void esp_event_loop_delete_default(void){}
static esp_err_t transfer_create_netif(bool ap,esp_netif_t **out){(void)ap;*out=calloc(1,sizeof(**out));return *out?ESP_OK:ESP_ERR_NO_MEM;}
static void esp_netif_destroy_default_wifi(esp_netif_t *n){free(n);}
static esp_err_t esp_wifi_init(const wifi_init_config_t *cfg){assert(cfg);return ESP_OK;}
static esp_err_t esp_wifi_deinit(void){++deinits;memset(&driver_config,0,sizeof(driver_config));return ESP_OK;}
static esp_err_t esp_wifi_start(void){return ESP_OK;}
static esp_err_t esp_wifi_stop(void){++stops;return ESP_OK;}
static esp_err_t esp_wifi_set_country(const wifi_country_t *c){assert(c->nchan==13);return ESP_OK;}
static esp_err_t esp_wifi_set_storage(int storage){assert(storage==WIFI_STORAGE_RAM);return ESP_OK;}
static esp_err_t esp_wifi_set_mode(int value){mode=value;return ESP_OK;}
static esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *wifi){assert(iface==WIFI_IF_AP||iface==WIFI_IF_STA);driver_config=*wifi;return ESP_OK;}
static esp_err_t esp_wifi_set_ps(int ps){assert(ps==WIFI_PS_NONE);return ESP_OK;}
static esp_err_t esp_wifi_disconnect(void){return ESP_OK;}
static esp_err_t esp_wifi_connect(void){
 ++connects;assert(driver_config.sta.ssid[0]);
 if(fail_all||(fail_first&&!strcmp((char*)driver_config.sta.ssid,"mobile")))return ESP_FAIL;
 if(online_success){ip_event_got_ip_t event={.esp_netif=s_netif};ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);}
 return ESP_OK;
}
static esp_err_t esp_read_mac(uint8_t *out,int type){(void)type;memset(out,1,6);return ESP_OK;}
static esp_err_t esp_event_handler_instance_register(int base,int id,void (*cb)(void*,esp_event_base_t,int32_t,void*),void *arg,esp_event_handler_instance_t *out){(void)base;(void)id;(void)cb;(void)arg;*out=(void*)1;return ESP_OK;}
static void esp_event_handler_instance_unregister(int base,int id,void *handle){(void)base;(void)id;(void)handle;}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data){(void)arg;(void)base;(void)id;(void)data;}
static int cleanup_interrupted(const char *root,unsigned *removed,unsigned *restored){assert(root);*removed=*restored=0;return 0;}
static esp_err_t httpd_start(httpd_handle_t *out,const httpd_config_t *cfg){assert(cfg);*out=(void*)1;return ESP_OK;}
static void httpd_stop(httpd_handle_t h){assert(h);}
static void editor_cancel_pending(void){}
static esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *uri){assert(h&&uri->handler);return ESP_OK;}
static int fake_handler(void *p){(void)p;return 0;}
'''
    handlers=('editor','file_download','banner_upload','index','info','upload','image_upload','font_upload','font_stage','file_upload','document_upload','files_list','files_mutation','books','clock','titles','wifi')
    prelude+=''.join(f'#define {name}_handler fake_handler\n' for name in handlers)
    prelude+='\n'.join(function(marker) for marker in (
        'void read_pico_transfer_get_status(', 'static void publish_credentials(', 'esp_err_t read_pico_transfer_forget_wifi(', 'static void set_error(',
        'static void ip_event(', 'static esp_err_t connect_saved_candidate(', 'void read_pico_transfer_service_poll(',
        'void read_pico_transfer_stop(', 'bool read_pico_transfer_try_stop_if_idle(', 'esp_err_t read_pico_transfer_start('))
    prelude+='''esp_err_t read_pico_transfer_sync_time_online(uint32_t *out){assert(s_status.network_ready);*out=1900000000;return ESP_OK;}\n'''
    prelude+=function('esp_err_t read_pico_transfer_sync_time(')
    prelude+=r'''
static void reset(void){
 read_pico_transfer_stop();memset(&s_status,0,sizeof(s_status));
 scans=connects=stops=deinits=stores=scan_failures=allocation_failures=record_failures=0;
 assert(!record_live);ap_count=0;clock_ms=0;fail_all=fail_first=online_success=deny_store=false;
 committed=(transfer_wifi_profiles_t){.version=1,.count=3};
 assert(credentials_from_text("home","home-pass",&committed.entries[0]));
 assert(credentials_from_text("office","office-pass",&committed.entries[1]));
 assert(credentials_from_text("mobile","mobile-pass",&committed.entries[2]));
}
static void offer(const char *ssid,int rssi,int auth){assert(ap_count<40);wifi_ap_record_t *ap=&aps[ap_count++];*ap=(wifi_ap_record_t){.rssi=rssi,.authmode=auth};strcpy((char*)ap->ssid,ssid);}
static void zero_private(void){const unsigned char *at=(void*)&s_sta_profiles;for(size_t i=0;i<sizeof(s_sta_profiles);++i)assert(!at[i]);assert(!s_sta_candidates.count&&s_sta_profile==-1&&!s_wifi&&!s_started&&!s_netif&&!s_http);}
static uint64_t free_bytes(void *ctx){(void)ctx;return 1000000;}
int main(void){
 read_pico_transfer_cfg_t automatic={.mode=READ_PICO_TRANSFER_MODE_STA,.network_only=true};
 reset();for(int i=0;i<20;++i){char name[32];snprintf(name,sizeof(name),"unknown-%d",i);offer(name,-10-i,WIFI_AUTH_WPA2_PSK);}
 offer("office",-70,WIFI_AUTH_WPA2_PSK);online_success=true;
 assert(read_pico_transfer_start(&automatic)==ESP_OK&&scans==1&&connects==1);
 assert(!strcmp(s_status.wifi_ssid,"office")&&s_status.network_ready);
 read_pico_transfer_service_poll();assert(stores==1&&!strcmp(committed.entries[0].ssid,"office"));
 int before=scans;uint32_t utc=0;assert(read_pico_transfer_sync_time(&utc)==ESP_OK&&utc==1900000000&&scans==before&&s_started);
 read_pico_transfer_stop();zero_private();
 reset();offer("office",-40,WIFI_AUTH_WPA2_PSK);
 assert(read_pico_transfer_start(&automatic)==ESP_OK&&!s_status.network_ready&&!strcmp(s_status.wifi_ssid,"office"));
 read_pico_transfer_stop();assert(read_pico_transfer_forget_wifi()==ESP_OK);
 assert(profiles_find(&committed,"office")<0&&profiles_find(&committed,"home")>=0&&profiles_find(&committed,"mobile")>=0);
 reset();offer("office",-65,WIFI_AUTH_WPA2_PSK);offer("mobile",-30,WIFI_AUTH_WPA3_PSK);offer("office",-75,WIFI_AUTH_WPA2_PSK);
 fail_first=online_success=true;
 assert(read_pico_transfer_start(&automatic)==ESP_OK&&connects==1&&!strcmp(s_status.wifi_ssid,"mobile"));
 clock_ms+=1000;read_pico_transfer_service_poll();assert(connects==2&&s_status.network_ready&&!strcmp(s_status.wifi_ssid,"office"));
 read_pico_transfer_service_poll();assert(stores==1);read_pico_transfer_service_poll();assert(stores==1);
 reset();offer("home",-10,WIFI_AUTH_OPEN);offer("mobile",-15,WIFI_AUTH_ENTERPRISE);
 assert(read_pico_transfer_start(&automatic)==ESP_ERR_NOT_FOUND&&connects==0&&s_status.wifi_configured&&s_status.state==READ_PICO_TRANSFER_ERROR);zero_private();
 reset();read_pico_transfer_cfg_t manual=automatic;manual.use_selected_wifi=true;
 offer("office",-10,WIFI_AUTH_WPA2_PSK);online_success=true;
 assert(read_pico_transfer_start(&manual)==ESP_OK&&scans==0&&!strcmp(s_status.wifi_ssid,"home"));
 reset();committed.count=0;memset(committed.entries,0,sizeof(committed.entries));
 assert(read_pico_transfer_start(&automatic)==ESP_ERR_NOT_FOUND&&!scans&&!connects&&!s_status.wifi_configured);zero_private();
 reset();offer("home",-40,WIFI_AUTH_WPA2_PSK);scan_failures=1;
 assert(read_pico_transfer_start(&automatic)==ESP_OK&&scans==2&&connects==1);
 reset();scan_failures=2;
 assert(read_pico_transfer_start(&automatic)==ESP_FAIL&&scans==2&&!connects&&deinits==1&&stops==1);zero_private();
 reset();offer("home",-40,WIFI_AUTH_WPA2_PSK);allocation_failures=2;
 assert(read_pico_transfer_start(&automatic)==ESP_ERR_NO_MEM&&!connects&&!record_live);zero_private();
 reset();offer("home",-40,WIFI_AUTH_WPA2_PSK);record_failures=1;
 assert(read_pico_transfer_start(&automatic)==ESP_FAIL&&!connects&&!record_live);zero_private();
 reset();offer("office",-40,WIFI_AUTH_WPA2_PSK);offer("mobile",-40,WIFI_AUTH_WPA2_PSK);offer("mobile",-30,WIFI_AUTH_WPA2_PSK);fail_all=true;
 assert(read_pico_transfer_start(&automatic)==ESP_OK);
 for(int i=0;i<5;++i){clock_ms+=1000;read_pico_transfer_service_poll();}
 assert(connects==5&&s_status.state==READ_PICO_TRANSFER_ERROR&&s_status.last_error==ESP_ERR_TIMEOUT&&scans==1);
 reset();offer("office",-40,WIFI_AUTH_WPA2_PSK);
 assert(read_pico_transfer_start(&automatic)==ESP_OK);clock_ms+=30000;read_pico_transfer_service_poll();assert(connects==1&&s_status.state==READ_PICO_TRANSFER_ERROR);
 reset();offer("office",-40,WIFI_AUTH_WPA2_PSK);online_success=true;
 assert(read_pico_transfer_sync_time(&utc)==ESP_OK&&utc==1900000000&&scans==1&&connects==1&&s_status.state==READ_PICO_TRANSFER_STOPPED);zero_private();
 reset();read_pico_transfer_cfg_t ap={.mode=READ_PICO_TRANSFER_MODE_AP,.root_dir=".",.free_bytes_cb=free_bytes};
 assert(read_pico_transfer_start(&ap)==ESP_OK&&!scans&&!connects&&s_status.network_ready&&s_http);read_pico_transfer_stop();zero_private();
 puts("PASS: real startup/scan/connect/retry/stop, dense AP list, strongest saved candidate, fallback, manual choice, auth mismatch, empty/error/OOM cleanup, bounded attempts, remembered success, time reuse and AP isolation");
}
'''
    (work/'test.c').write_text(prelude)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-deprecated-declarations','-fsanitize=address,undefined','-g',
        '-I'+str(work),'-I'+str(ROOT/'components/read_pico_transfer/include'),'-I'+str(ROOT/'components/read_pico_transfer'),'-I'+str(ROOT/'managed_components/espressif__cjson/cJSON'),
        str(work/'test.c'),str(ROOT/'managed_components/espressif__cjson/cJSON/cJSON.c'),'-lm','-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],cwd=work,check=True)
