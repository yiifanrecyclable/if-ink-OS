#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute the production HTTP mutation handler with real reference/settings/storage modules, no server or user card."""
from pathlib import Path
import os,subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1]
s=(R/'components/read_pico_transfer/read_pico_transfer.c').read_text();a=s.index('static esp_err_t files_mutation_handler(');b=s.index('// 下载只作为附件输出',a);handler=s[a:b]
HEAD=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "read_pico_transfer.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "file_references.h"
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *p){memset(p,0,sizeof(*p));p->mounted=true;return ESP_OK;}
typedef struct {size_t content_len;} httpd_req_t;
static read_pico_transfer_cfg_t s_cfg;
static read_pico_transfer_status_t s_status;
static bool s_file_metadata_failed,s_stopping,s_upload_active;
static int s_lock,response;
static char request[800],reply[400];
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static bool management_request(httpd_req_t *r){(void)r;return true;}
static esp_err_t files_error(httpd_req_t *r,const char *code,const char *message){(void)r;response=atoi(code);snprintf(reply,sizeof(reply),"%s",message);return ESP_OK;}
static bool recv_small_body(httpd_req_t *r,char *out,size_t cap){if(r->content_len>=cap)return false;memcpy(out,request,r->content_len);return true;}
static bool flat_file_request(const char *p){(void)p;return true;}
static bool sd_relative_valid(const char *p,bool root){return p&&(root||*p)&&p[0]!='/'&&!strstr(p,"..");}
static bool sd_absolute(const char *p,char *out,size_t cap){return snprintf(out,cap,"/sdcard/%s",p)<(int)cap;}
static bool if_fixed_relative(const char *p){return !strcmp(p,"fonts");}
static bool if_managed_relative(const char *p){(void)p;return false;}
static bool admission_begin(bool stop,bool *active){if(stop||*active)return false;*active=true;return true;}
static int inspect_sd_tree(const char *p,int d,unsigned *n,uint64_t *b){(void)p;(void)d;*n=1;*b=1;return 200;}
static int delete_sd_tree(const char *p,int d){(void)d;return unlink(p)?507:200;}
static int copy_sd_tree(const char *a,const char *b,int d){(void)a;(void)b;(void)d;return 400;}
static void sync_upload_root_after_edit(const char *a,const char *b,const char *c){(void)a;(void)b;(void)c;}
static void notify_sd_move_tree(const char *a,const char *b,int d){(void)a;(void)b;(void)d;}
static void set_error(esp_err_t err){(void)err;}
'''
TAIL=r'''
static void call(const char *action,const char *from,const char *to,int expected){
 snprintf(request,sizeof(request),"{\"action\":\"%s\",\"path\":\"%s\",\"target\":\"%s\"}",action,from,to);
 httpd_req_t req={.content_len=strlen(request)};response=0;assert(files_mutation_handler(&req)==ESP_OK);
 if(response!=expected)fprintf(stderr,"response=%d expected=%d reply=%s\n",response,expected,reply);
 assert(response==expected&&!s_upload_active);
}
int main(void){
 app_settings_init();s_cfg.file_edit_begin_cb=file_references_begin;s_cfg.file_edit_end_cb=file_references_finish;
 const char *old="/sdcard/fonts/old.ttf",*next="/sdcard/fonts/new.ttf";
 FILE *f=fopen(old,"wb");assert(f);fputs("font",f);fclose(f);
 assert(app_settings_set_system_font_path(old)==ESP_OK);assert(app_settings_set_font_path(old)==ESP_OK);
 setenv("PICO_TEST_FSYNC_FAIL","1",1);call("rename","fonts/old.ttf","fonts/new.ttf",500);unsetenv("PICO_TEST_FSYNC_FAIL");
 struct stat st;assert(!stat(old,&st)&&stat(next,&st));
 setenv("PICO_TEST_NVS_SET_FAIL","font",1);call("rename","fonts/old.ttf","fonts/new.ttf",500);unsetenv("PICO_TEST_NVS_SET_FAIL");
 assert(!strcmp(app_settings_system_font_path(),next)&&!strcmp(app_settings_font_path(),old));assert(stat(old,&st)&&!stat(next,&st));
 call("rename","fonts/old.ttf","fonts/new.ttf",200);assert(!strcmp(app_settings_font_path(),next));
 call("delete","fonts/new.ttf","",200);assert(!*app_settings_system_font_path()&&!*app_settings_font_path());
 puts("PASS: production HTTP handler rejects failed preparation, reports partial metadata failure, completes exact retry without repeating rename, and clears both deleted references");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='reference-http-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'build/simulator').mkdir(parents=True);card=p/'card';(card/'fonts').mkdir(parents=True)
 (p/'test.c').write_text(HEAD+handler+TAIL)
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DPICO_HOST_SIMULATOR','-DPICO_SIM_CARD_PATH="unused"','-include',str(R/'tools/simulator/fs.h')]
 for inc in ['tools/simulator/include','main','main/book','main/ui','main/cloud','components/read_pico_transfer/include','tools/simulator/vendor/cjson']:flags+=['-I'+str(R/inc)]
 sources=['main/file_references.c','main/settings.c','main/wallpaper_preferences.c','main/ui/ui_i18n.c','tools/simulator/fs.c','tools/simulator/nvs.c','tools/simulator/vendor/cjson/cJSON.c']
 subprocess.run(flags+[str(p/'test.c')]+[str(R/s) for s in sources]+['-lz']+(['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections'])+['-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True,env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1','ASAN_OPTIONS':'detect_leaks=0'})
