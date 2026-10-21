# SPDX-License-Identifier: Apache-2.0
"""生产C下载编排与真实事务；仅HTTP替身、临时TF/NVS。 / Production C download orchestration and transactions; fake HTTP and private TF/NVS."""
from pathlib import Path
import os, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
source=(ROOT/'main/cloud/cloud_sync.c').read_text()
def function(name):
    start=source.index(name+'(');start=source.rfind('\n',0,start)+1
    at=source.index('{',start);end=at+1;depth=1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
preamble=r'''
#include "cloud_sync.h"
#include "sync_file_policy.h"
#include "book_progress.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#define CLOUD_CACHE "/sdcard/.pico-cloud"
#define ESP_LOGW(...) ((void)0)
static bool s_notice_error, cancel_download, fail_verify;
static int fetched;
static const unsigned char *body=(const unsigned char*)"# old\n";
static size_t body_length=6;
static const char *book_folder="/sdcard/books";
static bool cancelled(void){return cancel_download;}
static void message(const char *v){(void)v;}
static void fail_notice(const char *v,const char *i){(void)v;(void)i;}
static const char *app_settings_books_dir(void){return book_folder;}
static const char *app_settings_system_font_path(void){return "/sdcard/fonts/used.ttf";}
static const char *app_settings_font_path(void){return "";}
static const char *content_markdown_font(void){return "";}
static const char *content_home_quote_font(void){return "";}
static esp_err_t fetch(const char *path,bool listing,const char *target,uint64_t limit,uint64_t expected,
 const char *sha,const char *etag,char actual[65],uint64_t *actual_bytes){
 (void)etag;assert(!listing&&path&&*path);++fetched;
 if(fail_verify&&fetched==2)return ESP_FAIL;
 if(body_length>limit||(expected!=UINT64_MAX&&expected!=body_length))return ESP_ERR_INVALID_SIZE;
 FILE *f=fopen(target,"wb");assert(f);assert(fwrite(body,1,body_length,f)==body_length);fclose(f);
 char hash[65];uint64_t n;assert(document_store_hash_file(target,hash,&n)==ESP_OK);
 if(sha&&strcmp(sha,hash))return ESP_ERR_INVALID_RESPONSE;
 if(actual)strcpy(actual,hash);if(actual_bytes)*actual_bytes=n;return ESP_OK;
}
'''
main=r'''
static cloud_entry_t entry(const char *name,const char *tag){
 cloud_entry_t e={0};snprintf(e.href,sizeof(e.href),"%s",name);snprintf(e.filename,sizeof(e.filename),"%s",name);
 snprintf(e.etag,sizeof(e.etag),"%s",tag);e.bytes=body_length;return e;
}
static void check_body(const char *path,const char *expected,size_t n){
 FILE *f=fopen(path,"rb");assert(f);char b[256];size_t offset=0,k;
 while((k=fread(b,1,sizeof(b),f))){assert(k<=n-offset&&!memcmp(b,expected+offset,k));offset+=k;}
 assert(offset==n&&!ferror(f));fclose(f);
}
int main(void){
 mkdir("/sdcard/books",0700);mkdir("/sdcard/library",0700);mkdir(CLOUD_CACHE,0700);
 cloud_entry_t md=entry("周记1.md","");
 assert(download_one(&md)==ESP_OK&&fetched==2);check_body("/sdcard/markdown/周记1.md","# old\n",6);
 assert(document_store_set_flags("/sdcard/markdown/周记1.md",true,false)==ESP_OK);
 book_progress_t progress={.file_size=6,.px=48,.pct=33,.byte_off=1};
 assert(book_progress_save("/sdcard/markdown/周记1.md",&progress)==ESP_OK);
 fetched=0;assert(download_one(&md)==ESP_OK&&fetched==2);
 assert(book_progress_load("/sdcard/markdown/周记1.md",6,&progress));
 document_entry_t *docs=NULL;size_t count=0;
 assert(document_store_scan(&docs,&count)==ESP_OK&&count==1&&docs[0].pinned);char date[11];strcpy(date,docs[0].date);free(docs);
 body=(const unsigned char*)"# new\n";fetched=0;fail_verify=true;
 assert(download_one(&md)!=ESP_OK&&fetched==2);check_body("/sdcard/markdown/周记1.md","# old\n",6);
 fail_verify=false;fetched=0;assert(download_one(&md)==ESP_OK);check_body("/sdcard/markdown/周记1.md","# new\n",6);
 assert(!book_progress_load("/sdcard/markdown/周记1.md",6,&progress));
 assert(document_store_scan(&docs,&count)==ESP_OK&&count==1&&docs[0].pinned&&!strcmp(date,docs[0].date));free(docs);
 cloud_entry_t txt=entry("随笔.txt","\"strong\"");fetched=0;book_folder="/sdcard/library";
 assert(download_one(&txt)==ESP_OK&&fetched==1);check_body("/sdcard/library/随笔.txt","# new\n",6);
 static const unsigned char zip[]={80,75,3,4,69,80,85,66};body=zip;body_length=sizeof(zip);cloud_entry_t epub=entry("Book.epub","W/\"weak\"");fetched=0;
 assert(download_one(&epub)==ESP_OK&&fetched==2);check_body("/sdcard/library/Book.epub",(const char*)zip,sizeof(zip));
 epub.manifest=true;fetched=0;assert(download_one(&epub)==ESP_ERR_NOT_SUPPORTED&&fetched==0);
 const char *files[]={"fixture.png","fixture.ttf"};
 const char *names[]={"壁纸.PNG","used.ttf"};const char *targets[]={"/sdcard/pictures/壁纸.PNG","/sdcard/fonts/used.ttf"};
 for(unsigned i=0;i<2;++i){
  FILE *f=fopen(files[i],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
  unsigned char *data=malloc((size_t)n+1);assert(data&&fread(data,1,n,f)==(size_t)n);fclose(f);
  body=data;body_length=n;cloud_entry_t resource=entry(names[i],"\"strong\"");fetched=0;
  assert(download_one(&resource)==ESP_OK&&fetched==1);check_body(targets[i],(const char*)data,n);
  // 正在使用的字体同文可重下，变化时不触碰原文件。/ Active font no-ops succeed; changed content cannot replace its open file.
  fetched=0;assert(download_one(&resource)==ESP_OK);data[n]=0;body_length=n+1;resource.bytes=n+1;
  if(i==1){assert(download_one(&resource)!=ESP_OK);check_body(targets[i],(const char*)data,n);}
  body=(const unsigned char*)"bad file";body_length=8;resource.bytes=8;
  assert(download_one(&resource)!=ESP_OK);check_body(targets[i],(const char*)data,n);free(data);
 }
 cloud_entry_t ignored=entry("font.otf","\"strong\"");fetched=0;
 assert(download_one(&ignored)==ESP_ERR_NOT_SUPPORTED&&fetched==0);
 ignored=entry("huge.png","\"strong\"");ignored.bytes=2*1024*1024+1;
 assert(download_one(&ignored)==ESP_ERR_INVALID_SIZE&&fetched==0);
 puts("PASS: production C flat MD/TXT/EPUB downloads, native pin/date/noop progress, failed verification preserves old body and legacy selection makes no request.");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='flat-device-') as temp:
    work=Path(temp);(work/'build/simulator').mkdir(parents=True);(work/'card').mkdir()
    shutil.copyfile(ROOT/'assets/banner/default-banner.png',work/'fixture.png')
    shutil.copyfile(ROOT/'main/assets/builtin.ttf',work/'fixture.ttf')
    test=work/'test.c';binary=work/'test'
    test.write_text('\n'.join([preamble,function('strong_etag'),function('active_font_replace'),function('download_one'),main]))
    args=['cc','-std=gnu11','-D_DEFAULT_SOURCE','-DPICO_HOST_SIMULATOR=1','-DPICO_SIM_CARD_PATH="'+str(work/'card')+'"','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-Wno-misleading-indentation','-Wno-deprecated-declarations','-fsanitize=address,undefined','-g','-include',str(ROOT/'tools/simulator/fs.h')]
    args += ['-I'+str(ROOT/p) for p in ('tools/simulator/include','tools/simulator/vendor/cjson','main/cloud','main/book','components/md4c')]
    args += [str(test)] + [str(ROOT/p) for p in ('tools/simulator/fs.c','tools/simulator/nvs.c','tools/simulator/vendor/cjson/cJSON.c','main/cloud/cloud_protocol.c','main/book/document_store.c','main/book/document_image.c','main/book/book_progress.c','main/book/book_markdown.c','main/book/markdown_text.c','main/book/html_text.c','components/md4c/md4c.c')]
    args += ['-lz','-lm','-o',str(binary)]
    subprocess.run(args,check=True)
    subprocess.run([str(binary)],cwd=work,env={**os.environ,'PICO_SIM_CARD_ROOT':str(work/'card'),'ASAN_OPTIONS':'detect_leaks=0','PICO_TEST_FATFS_RENAME':'1'},check=True)
