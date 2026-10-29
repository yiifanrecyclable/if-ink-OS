#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production read handlers: exact streaming, path isolation and retired wallpaper apply route.
中文：真实下载和旧壁纸入口回归，不连接硬件。/ English: Real download/legacy-wallpaper regressions without hardware.
"""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];source=(ROOT/'components/read_pico_transfer/read_pico_transfer.c').read_text()
def extract(name):
 m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M);assert m,name
 i=m.end();depth=1
 while depth:
  if source[i]=='{':depth+=1
  elif source[i]=='}':depth-=1
  i+=1
 return source[m.start():i]
unit='''#define _DEFAULT_SOURCE
#define READ_PICO_TRANSFER_HOST_TEST
#include "read_pico_transfer.h"
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include "../components/read_pico_transfer/read_pico_transfer.c"
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_ERR_NOT_FOUND 0x105
static int s_lock;static bool s_stopping,s_upload_active;
static read_pico_transfer_cfg_t s_cfg;
static read_pico_transfer_status_t s_status;
static char storage[16384],*s_buffer=storage,root[256];
static unsigned receives,wallpapers;
typedef struct {int content_len,code;const char *query;bool management,fail_send,ended;size_t count;unsigned char data[65536];char type[100],disposition[100],nosniff[30];} httpd_req_t;
static bool management_request(httpd_req_t *r){return r->management;}
static int wifi_response(httpd_req_t *r,const char *status,const char *body){(void)body;r->code=atoi(status);return 0;}
static int respond_error(httpd_req_t *r,int code){r->code=code;return -1;}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t cap){snprintf(out,cap,"%s",r->query);return 0;}
static int httpd_query_key_value(const char *query,const char *key,char *out,size_t cap){
 const char *p=query;size_t n=strlen(key);
 while(p&&*p){if(!strncmp(p,key,n)&&p[n]=='='){p+=n+1;size_t len=strcspn(p,"&");if(len>=cap)return -1;memcpy(out,p,len);out[len]=0;return 0;}p=strchr(p,'&');if(p)++p;}return ESP_ERR_NOT_FOUND;
}
static bool admission_begin(bool stopping,bool *active){if(stopping||*active)return false;*active=true;return true;}
static int receive_http(void *r,char *b,size_t n){(void)r;(void)b;(void)n;++receives;return -1;}
static void upload_progress(size_t n){(void)n;}
static bool receive_stopped(void *ignored){(void)ignored;return s_stopping;}
static void set_error(int e){(void)e;s_status.state=READ_PICO_TRANSFER_READY;}
static int file_response(httpd_req_t *r,file_result_t result,bool d,const char *p){(void)d;(void)p;r->code=result.status;return 0;}
static int httpd_resp_set_type(httpd_req_t *r,const char *type){snprintf(r->type,sizeof(r->type),"%s",type);return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){if(!strcmp(k,"Content-Disposition"))snprintf(r->disposition,sizeof(r->disposition),"%s",v);if(!strcmp(k,"X-Content-Type-Options"))snprintf(r->nosniff,sizeof(r->nosniff),"%s",v);return 0;}
static int httpd_resp_send_chunk(httpd_req_t *r,const char *data,size_t size){if(r->fail_send)return -1;if(!size){r->ended=true;return 0;}assert(r->count+size<=sizeof(r->data));memcpy(r->data+r->count,data,size);r->count+=size;return 0;}
static FILE *mapped_fopen(const char *path,const char *mode){char target[512];snprintf(target,sizeof(target),"%s%s",root,path+7);return fopen(target,mode);}
'''
unit+=extract('media_upload_handler')+'\n#define fopen mapped_fopen\n'+extract('download_path_public')+'\n'+extract('file_download_handler')+'\n#undef fopen\n'
unit+='''
static bool wallpaper(const char *p){(void)p;++wallpapers;return true;}
static uint64_t free_space(void *c){(void)c;return 1000000;}
int main(void){
 const char *fixed[]={"books","markdown","fonts","pictures","banner pic","BOOKS"};
 for(unsigned i=0;i<sizeof(fixed)/sizeof(fixed[0]);++i)assert(if_fixed_relative(fixed[i]));
 assert(!if_fixed_relative("books/sub"));assert(!if_fixed_relative("notes"));

 char temp[]="/tmp/transfer-http-XXXXXX";assert(mkdtemp(temp));snprintf(root,sizeof(root),"%s",temp);
 char target[512];snprintf(target,sizeof(target),"%s/books",root);assert(!mkdir(target,0700));snprintf(target,sizeof(target),"%s/books/你好.html",root);
 FILE *f=fopen(target,"wb");assert(f);for(int i=0;i<32773;++i)fputc(i%251,f);assert(!fclose(f));
 httpd_req_t r={.query="path=books/%E4%BD%A0%E5%A5%BD.html",.management=true};assert(file_download_handler(&r)==ESP_OK&&r.count==32773&&r.ended&&!s_upload_active);
 for(size_t i=0;i<r.count;++i)assert(r.data[i]==i%251);
 assert(!strcmp(r.type,"application/octet-stream")&&!strcmp(r.disposition,"attachment")&&!strcmp(r.nosniff,"nosniff"));
 r=(httpd_req_t){.query="path=books/%E4%BD%A0%E5%A5%BD.html",.management=true,.fail_send=true};assert(file_download_handler(&r)!=ESP_OK&&!r.ended&&!s_upload_active);
 for(const char **p=(const char*[]){"path=../outside","path=%2Fetc/passwd","path=books/.hidden/x","path=markdown/.index/secret","path=books/x.part",NULL};*p;++p){r=(httpd_req_t){.query=*p,.management=true};assert(file_download_handler(&r)!=ESP_OK&&r.code==400);}
 r=(httpd_req_t){.query="path=books",.management=true};assert(file_download_handler(&r)!=ESP_OK&&r.code==404&&!s_upload_active);
 r=(httpd_req_t){.query="path=books/anything",.management=false};assert(file_download_handler(&r)!=ESP_OK&&r.code==403);
 s_upload_active=true;r=(httpd_req_t){.query="path=books/anything",.management=true};file_download_handler(&r);assert(r.code==409);s_upload_active=false;
 s_cfg.wallpaper_set_cb=wallpaper;s_cfg.free_bytes_cb=free_space;
 r=(httpd_req_t){.query="name=bad.jpg&wallpaper=1&overwrite=1",.management=true,.content_len=16};media_upload_handler(&r,root,1,2000000);assert(r.code==400&&!receives&&!wallpapers&&!s_upload_active);
 assert(unlink(target)==0);snprintf(target,sizeof(target),"%s/books",root);assert(!rmdir(target));assert(!rmdir(root));
 puts("PASS: production download byte identity/Chinese names/attachment headers, hidden/traversal/busy/directory/send failure and retired wallpaper bypass rejection");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'tools',prefix='.transfer-read-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 # The test source uses a repository-relative include, so compile through tools/ with a normalized header path.
 (p/'test.c').write_text(unit.replace('../components/',str(ROOT/'components')+'/'))
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(ROOT/'components/read_pico_transfer/include'),'-I'+str(ROOT/'tools/ota_stubs'),'-I'+str(ROOT/'components/read_pico_search/include'),str(p/'test.c'),str(ROOT/'components/read_pico_search/read_pico_search.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
