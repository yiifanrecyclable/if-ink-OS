# SPDX-License-Identifier: Apache-2.0
"""Production HTTPS fetch control flow with fake transport and isolated files; no credentials/network."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/cloud/cloud_sync.c').read_text()
protocol=(ROOT/'main/cloud/cloud_protocol.c').read_text()
def function(text,name):
    start=text.index(name+'(');start=text.rfind('\n',0,start)+1
    opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[start:end]
preamble=r'''
#include "ui/ui_i18n.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "esp_err.h"
#define CLOUD_CACHE "."
#define CLOUD_CONNECT_TIMEOUT_MS 15000
#define CLOUD_READ_TIMEOUT_MS 5000
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_METHOD_PROPFIND 1
#define HTTP_METHOD_GET 2
#define HTTP_AUTH_TYPE_BASIC 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0
#define ESP_LOGW(...) ((void)0)
static int scenario,requests,closed,cleaned,notices,failures;
static bool cancel;
static int s_lock;
static bool s_session_fatal;
static char s_base_url[1024]="https://example.invalid/";
static struct {char account[129],password[129];} s_config;
static struct {uint64_t bytes_done,bytes_total;unsigned revision;} s_status;
typedef struct {char etag[129],retry[32],encoding[32];} response_headers_t;
typedef struct {int event_id;void *user_data;char *header_key,*header_value;} esp_http_client_event_t;
typedef struct {const char *url,*username,*password;int auth_type;void *crt_bundle_attach;bool disable_auto_redirect;int timeout_ms,buffer_size;esp_err_t(*event_handler)(esp_http_client_event_t*);void *user_data;} esp_http_client_config_t;
typedef struct FakeClient {esp_http_client_config_t config;int attempt,read_count;bool open,complete;} *esp_http_client_handle_t;
static esp_err_t esp_crt_bundle_attach(void *unused){(void)unused;return ESP_OK;}
static bool cancelled(void){return cancel;}
static bool cloud_encode_path(const char *p,char *out,size_t n){snprintf(out,n,"%s",p);return true;}
static esp_err_t budget(void){return ESP_OK;}
static void notice(const char *v,const char *i){(void)v;(void)i;++notices;}
static void fail_notice(const char *v,const char *i){(void)v;(void)i;++failures;}
static void xSemaphoreTake(int a,int b){(void)a;(void)b;}
static void xSemaphoreGive(int a){(void)a;}
static void vTaskDelay(int a){(void)a;}
typedef struct {bool network_ready;} read_pico_transfer_status_t;
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->network_ready=true;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config){
    assert(config->timeout_ms>=10000&&config->disable_auto_redirect&&config->auth_type==HTTP_AUTH_TYPE_BASIC);
    struct FakeClient *c=calloc(1,sizeof(*c));c->config=*config;c->attempt=++requests;return c;
}
static void esp_http_client_set_method(esp_http_client_handle_t c,int m){(void)c;assert(m==HTTP_METHOD_GET||m==HTTP_METHOD_PROPFIND);}
static void esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v){(void)c;(void)k;(void)v;}
static esp_err_t esp_http_client_open(esp_http_client_handle_t c,int n){(void)n;
    if((scenario==0&&c->attempt==1)||scenario==7||scenario==8)return ESP_FAIL;c->open=true;return ESP_OK;}
static int esp_http_client_write(esp_http_client_handle_t c,const char *p,int n){(void)c;(void)p;return n;}
static int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c){
    if(scenario==1&&c->attempt==1)return -1;
    esp_http_client_event_t event={HTTP_EVENT_ON_HEADER,c->config.user_data,"ETag",scenario==5?"different":scenario==11?"":"token"};
    c->config.event_handler(&event);return scenario==12?0:6;
}
static int esp_http_client_get_status_code(esp_http_client_handle_t c){return c->open?(scenario==4?401:200):0;}
static esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t c,int *tls,int *flags){(void)c;*tls=scenario==8?-99:0;*flags=scenario==8?1:0;return ESP_FAIL;}
static bool esp_http_client_is_chunked_response(esp_http_client_handle_t c){(void)c;return scenario==12;}
static void esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int ms){(void)c;assert(ms>=5000);}
static int esp_http_client_read(esp_http_client_handle_t c,char *out,int cap){(void)cap;
    if((scenario==2||scenario==3)&&c->attempt==1){if(c->read_count++==0){memcpy(out,"# N",3);return 3;}return scenario==2?-1:0;}
    if(c->read_count++){c->complete=true;return 0;}
    memcpy(out,scenario==6?"# Bad\n":"# Note",6);if(scenario==10)cancel=true;return 6;
}
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){return c->complete;}
static void esp_http_client_close(esp_http_client_handle_t c){(void)c;++closed;}
static void esp_http_client_cleanup(esp_http_client_handle_t c){++cleaned;free(c);}
static esp_err_t document_store_hash_file(const char *path,char hash[65],uint64_t *bytes){
    FILE *f=fopen(path,"rb");assert(f);char body[32];size_t n=fread(body,1,sizeof(body),f);fclose(f);
    *bytes=n;strcpy(hash,n==6&&!memcmp(body,"# Note",6)?"good":"bad");return ESP_OK;
}
static const char *propfind="fixture";
'''
main=r'''
int main(void){
    for(scenario=0;scenario<=12;++scenario){
        requests=closed=cleaned=notices=failures=0;cancel=false;s_session_fatal=false;
        FILE *f=fopen("body","wb");assert(f);fwrite("old",1,3,f);fclose(f);
        char hash[65];uint64_t bytes=0;
        esp_err_t e=fetch("article.md",false,"body",scenario==9?3:100,6,"good","token",hash,&bytes);
        assert(closed==requests&&cleaned==requests);
        if(scenario<=3||scenario==11||scenario==12){assert(e==ESP_OK&&bytes==6&&!strcmp(hash,"good")&&failures==0);assert(requests==(scenario>=11?1:2));}
        else {assert(e!=ESP_OK);assert(requests==(scenario==7?3:1));}
        if(scenario==4||scenario==5||scenario==8||scenario==9){f=fopen("body","rb");char b[4]={0};fread(b,1,3,f);fclose(f);assert(!strcmp(b,"old"));}
        if(scenario==7||scenario==8)assert(failures==1);
    }
    puts("PASS: production fetch recovers transient connect/header/read/short-body failures; rejects auth/cert/version/hash/size failures, cancellation and corrupt data; chunked response accepted.");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='cloud-transport-') as tmp:
    work=Path(tmp);test=work/'test.c';binary=work/'test'
    chunks=[preamble,function(protocol,'cloud_etag_version'),function(protocol,'cloud_etag_same'),function(source,'strong_etag'),function(source,'network_diagnostic'),function(source,'http_event'),function(source,'fetch'),main]
    test.write_text('\n'.join(chunks))
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-I'+str(ROOT/'tools/simulator/include'),'-I'+str(ROOT/'main'),str(ROOT/'main/ui/ui_i18n.c'),str(test),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],cwd=work,check=True)
