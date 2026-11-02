/* SPDX-License-Identifier: Apache-2.0
 * 中文：模拟设置仅写入被忽略的build/simulator/nvs，不接触设备NVS。
 * English: Preview settings live only in ignored build/simulator/nvs, never device NVS.
 */
#include "nvs.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
static char spaces[16][64];
static char last_written[64];
static void type_record(nvs_handle_t h,const char *key,nvs_type_t type);
static bool valid(const char *s){if(!s||!*s||strlen(s)>60)return false;for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'))return false;return true;}
static bool path(nvs_handle_t h,const char *key,char out[200]){if(h<1||h>16||!spaces[h-1][0]||!valid(key))return false;snprintf(out,200,"build/simulator/nvs/%s/%s",spaces[h-1],key);return true;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){
 const char *fault=getenv("PICO_TEST_NVS_OPEN_FAIL");if(fault&&(!strcmp(fault,"*")||!strcmp(fault,mode?"rw":"ro")))return ESP_FAIL;
 if(!valid(name)||!h)return ESP_ERR_INVALID_ARG;
 char dir[128];snprintf(dir,128,"build/simulator/nvs/%s",name);
 if(mode){mkdir("build/simulator/nvs",0700);mkdir(dir,0700);}else{struct stat st;if(stat(dir,&st))return ESP_ERR_NVS_NOT_FOUND;}
 for(int i=0;i<16;++i)if(!spaces[i][0]){strcpy(spaces[i],name);*h=i+1;return ESP_OK;}return ESP_ERR_NO_MEM;
}
void nvs_close(nvs_handle_t h){if(h>0&&h<=16)spaces[h-1][0]=0;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){
 const char *fault=getenv("PICO_TEST_NVS_GET_FAIL"),*verify=getenv("PICO_TEST_NVS_READBACK_FAIL");
 if(key&&((fault&&(!strcmp(fault,"*")||!strcmp(fault,key)))||(verify&&!strcmp(verify,key)&&!strcmp(last_written,key))))return ESP_FAIL;
 char p[200];if(!n||!path(h,key,p))return ESP_ERR_INVALID_ARG;
 FILE *f=fopen(p,"rb");if(!f)return ESP_ERR_NVS_NOT_FOUND;
 fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);if(size<0||size>65536){fclose(f);return ESP_ERR_INVALID_SIZE;}
 size_t cap=*n;*n=size;if(!out){fclose(f);return ESP_OK;}
 if(cap<(size_t)size){fclose(f);return ESP_ERR_INVALID_SIZE;}
 size_t got=fread(out,1,size,f);fclose(f);return got==(size_t)size?ESP_OK:ESP_FAIL;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t n){
 const char *fault=getenv("PICO_TEST_NVS_SET_FAIL");if(fault&&key&&(!strcmp(fault,"*")||!strcmp(fault,key)))return ESP_FAIL;
 char p[200],tmp[208];if(n>65536||(!in&&n)||!path(h,key,p))return ESP_ERR_INVALID_ARG;
 snprintf(tmp,sizeof(tmp),"%s.part",p);FILE *f=fopen(tmp,"wb");if(!f)return ESP_FAIL;
 size_t put=fwrite(in,1,n,f);int close=fclose(f);if(put!=n||close||rename(tmp,p)){unlink(tmp);return ESP_FAIL;}snprintf(last_written,sizeof(last_written),"%s",key);type_record(h,key,NVS_TYPE_BLOB);return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){size_t n=1;return nvs_get_blob(h,key,out,&n);}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){esp_err_t e=nvs_set_blob(h,key,&value,1);if(!e)type_record(h,key,NVS_TYPE_U8);return e;}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *out){size_t n=4;return nvs_get_blob(h,key,out,&n);}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t value){esp_err_t e=nvs_set_blob(h,key,&value,4);if(!e)type_record(h,key,NVS_TYPE_U32);return e;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){esp_err_t e=nvs_get_blob(h,key,out,n);if(!e&&out&&(!*n||out[*n-1]))return ESP_ERR_INVALID_ARG;return e;}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *s){esp_err_t e=s?nvs_set_blob(h,key,s,strlen(s)+1):ESP_ERR_INVALID_ARG;if(!e)type_record(h,key,NVS_TYPE_STR);return e;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){char p[200];if(!path(h,key,p))return ESP_ERR_INVALID_ARG;return unlink(p)?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;static bool failed;const char *fault=getenv("PICO_TEST_NVS_COMMIT_FAIL");if(fault&&(!failed||strcmp(fault,"once"))){failed=true;return ESP_FAIL;}return ESP_OK;}
esp_err_t nvs_flash_init(void){return ESP_OK;}
esp_err_t nvs_flash_erase(void){return ESP_ERR_NOT_SUPPORTED;}

// 类型旁表兼容已有裸文件，并让 rc83 的历史枚举保持 SDK 语义。
// A side table preserves legacy raw files and gives rc83 enumeration SDK-compatible types.
static void type_record(nvs_handle_t h,const char *key,nvs_type_t type){
 char p[200],folder[160],meta[224];if(!path(h,key,p))return;
 snprintf(folder,sizeof(folder),"build/simulator/nvs/%s/._types",spaces[h-1]);mkdir(folder,0700);
 snprintf(meta,sizeof(meta),"%s/%s",folder,key);FILE *f=fopen(meta,"wb");if(f){unsigned char t=type;fwrite(&t,1,1,f);fclose(f);}
}
struct preview_nvs_iterator {DIR *directory;char name[64];nvs_type_t filter;nvs_entry_info_t current;};
static nvs_type_t item_type(const char *space,const char *key){
 char meta[224];snprintf(meta,sizeof(meta),"build/simulator/nvs/%s/._types/%s",space,key);
 FILE *f=fopen(meta,"rb");if(f){int t=fgetc(f);fclose(f);if(t==NVS_TYPE_U8||t==NVS_TYPE_U32||t==NVS_TYPE_STR||t==NVS_TYPE_BLOB)return (nvs_type_t)t;}
 if(!strcmp(space,"rp_books")&&!strcmp(key,"seq"))return NVS_TYPE_U32;
 if((!strcmp(space,"rp_books")&&!strcmp(key,"last"))||!strcmp(space,"rp_shelf"))return NVS_TYPE_STR;
 if(!strcmp(space,"rp_favs"))return NVS_TYPE_U8;
 return NVS_TYPE_BLOB;
}
static bool iterator_advance(nvs_iterator_t it){
 struct dirent *entry;while((entry=readdir(it->directory))){
  if(!valid(entry->d_name)||strlen(entry->d_name)>=16)continue;
  char p[224];snprintf(p,sizeof(p),"build/simulator/nvs/%s/%s",it->name,entry->d_name);
  struct stat st;if(stat(p,&st)||!S_ISREG(st.st_mode))continue;
  nvs_type_t type=item_type(it->name,entry->d_name);if(it->filter!=NVS_TYPE_ANY&&type!=it->filter)continue;
  memset(&it->current,0,sizeof(it->current));snprintf(it->current.namespace_name,sizeof(it->current.namespace_name),"%s",it->name);snprintf(it->current.key,sizeof(it->current.key),"%s",entry->d_name);it->current.type=type;return true;
 }return false;
}
esp_err_t nvs_entry_find(const char *part,const char *name,nvs_type_t filter,nvs_iterator_t *out){
 if(!out||!valid(name))return ESP_ERR_INVALID_ARG;*out=NULL;
 char p[128];snprintf(p,sizeof(p),"build/simulator/nvs/%s",name);DIR *dir=opendir(p);if(!dir)return ESP_ERR_NVS_NOT_FOUND;
 nvs_iterator_t it=calloc(1,sizeof(*it));if(!it){closedir(dir);return ESP_ERR_NO_MEM;}
 it->directory=dir;it->filter=filter;snprintf(it->name,sizeof(it->name),"%s",name);
 if(!iterator_advance(it)){nvs_release_iterator(it);return ESP_ERR_NVS_NOT_FOUND;}*out=it;return ESP_OK;
}
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h,nvs_type_t type,nvs_iterator_t *out){if(h<1||h>16||!spaces[h-1][0])return ESP_ERR_INVALID_ARG;return nvs_entry_find(NVS_DEFAULT_PART_NAME,spaces[h-1],type,out);}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *out){if(!it||!out)return ESP_ERR_INVALID_ARG;*out=it->current;return ESP_OK;}
esp_err_t nvs_entry_next(nvs_iterator_t *it){if(!it||!*it)return ESP_ERR_INVALID_ARG;if(iterator_advance(*it))return ESP_OK;nvs_release_iterator(*it);*it=NULL;return ESP_ERR_NVS_NOT_FOUND;}
void nvs_release_iterator(nvs_iterator_t it){if(it){closedir(it->directory);free(it);}}
esp_err_t nvs_erase_all(nvs_handle_t h){
 nvs_iterator_t it=NULL;esp_err_t e=nvs_entry_find_in_handle(h,NVS_TYPE_ANY,&it);if(e==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;if(e)return e;
 do{nvs_entry_info_t info; nvs_entry_info(it,&info);char p[200];if(!path(h,info.key,p)||unlink(p)){nvs_release_iterator(it);return ESP_FAIL;}}while(nvs_entry_next(&it)==ESP_OK);
 return ESP_OK;
}
