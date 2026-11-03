/* SPDX-License-Identifier: Apache-2.0
 * 先提交全部引用意图，再改文件。失败保留意图，重试只补仍指向旧路径的项。
 * Commit all reference intentions before editing files; retry only preferences still pointing at old paths.
 */
#include "file_references.h"
#include "settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#define REF_PATH "/sdcard/.readpico/settings/file-refs.pending"
#define REF_TEMP REF_PATH ".tmp"
#define REF_CAP 288
// 固定布局、版本及CRC；不存指针、不枚举目录、不储存凭据。
// Versioned fixed layout + CRC; no pointers, directory walks, or credentials.
typedef struct { char magic[8],source[REF_CAP],target[REF_CAP];char before[3][REF_CAP],after[3][REF_CAP];uint32_t crc; } intent_t;
static bool below(const char *path,const char *root){size_t n=strlen(root);return n&&!strncmp(path,root,n)&&(!path[n]||path[n]=='/');}
static bool valid_path(const char *p){
    if(strncmp(p,"/sdcard/",8)||!p[8])return false;
    for(const char *c=p+8;*c;++c)if(*c=='\\'||(*c=='.'&&(c==p+8||c[-1]=='/')&&(c[1]=='/'||!c[1]||(c[1]=='.'&&(c[2]=='/'||!c[2])))))return false;
    return true;
}
static const char *get_ref(int i){return i==0?app_settings_system_font_path():i==1?app_settings_font_path():app_settings_wallpaper_path();}
static esp_err_t set_ref(int i,const char *p){return i==0?app_settings_set_system_font_path(p):i==1?app_settings_set_font_path(p):app_settings_set_wallpaper_path(p);}
static uint32_t checksum(const intent_t *r){return (uint32_t)crc32(0,(const Bytef*)r,sizeof(*r)-sizeof(r->crc));}
static int exists(const char *p){struct stat st;if(!stat(p,&st))return 1;return errno==ENOENT?0:-1;}
static esp_err_t load(intent_t *r){
    FILE *f=fopen(REF_PATH,"rb");if(!f)return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
    bool ok=fread(r,1,sizeof(*r),f)==sizeof(*r)&&fgetc(f)==EOF&&!ferror(f);
    if(fclose(f))ok=false;
    if(!ok||memcmp(r->magic,"IFREFS1",8)||r->crc!=checksum(r))return ESP_FAIL;
    if(!memchr(r->source,0,REF_CAP)||!memchr(r->target,0,REF_CAP)||!valid_path(r->source)||
       (r->target[0]&&!valid_path(r->target)))return ESP_FAIL;
    for(int i=0;i<3;++i){
        if(!memchr(r->before[i],0,REF_CAP)||!memchr(r->after[i],0,REF_CAP))return ESP_FAIL;
        if(r->before[i][0]&&(!below(r->before[i],r->source)||!valid_path(r->before[i])))return ESP_FAIL;
        char expected[REF_CAP]={0};
        if(r->before[i][0]&&r->target[0]){
            int n=snprintf(expected,sizeof(expected),"%s%s",r->target,r->before[i]+strlen(r->source));
            if(n<0||n>=(int)sizeof(expected))return ESP_FAIL;
        }
        if(strcmp(expected,r->after[i]))return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t persist(intent_t *r){
    if((mkdir("/sdcard/.readpico",0775)&&errno!=EEXIST)||(mkdir("/sdcard/.readpico/settings",0775)&&errno!=EEXIST))return ESP_FAIL;
    r->crc=checksum(r);FILE *f=fopen(REF_TEMP,"wb");if(!f)return ESP_FAIL;
    bool ok=fwrite(r,1,sizeof(*r),f)==sizeof(*r)&&fflush(f)==0;
    if(ok)ok=fsync(fileno(f))==0;
    if(fclose(f))ok=false;
    // 不覆盖已有待办。FatFs的rename只提交新的文件名。
    // Do not replace an existing intent; FatFs rename commits a new name.
    if(ok)ok=rename(REF_TEMP,REF_PATH)==0;
    if(!ok){unlink(REF_TEMP);return ESP_FAIL;}
    intent_t *check=calloc(1,sizeof(*check));if(!check)return ESP_ERR_NO_MEM;
    esp_err_t e=load(check);if(e==ESP_OK&&memcmp(check,r,sizeof(*r)))e=ESP_FAIL;free(check);return e;
}
static esp_err_t repair(intent_t *r){
    int src=exists(r->source),dst=r->target[0]?exists(r->target):0;
    if(src<0||dst<0)return ESP_FAIL;
    if(r->target[0]){
        // 未执行的rename不改设置；两端并存或都缺失时不猜测。
        // An unexecuted rename leaves preferences intact; ambiguous endpoints require attention.
        if(src&&!dst)return unlink(REF_PATH)==0?ESP_OK:ESP_FAIL;
        if(src||!dst)return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result=ESP_OK;
    for(int i=0;i<3;++i){
        if(!r->before[i][0])continue;
        int old=exists(r->before[i]);if(old<0){result=ESP_FAIL;continue;}
        if(old){if(!r->target[0])continue;result=ESP_FAIL;continue;}
        if(r->target[0]&&exists(r->after[i])!=1){result=ESP_FAIL;continue;}
        // 已更新或用户另选过的引用不覆盖；其余失败仍尝试，不吞掉第二个引用。
        // Preserve already-applied or independently changed choices; attempt every remaining reference.
        if(strcmp(get_ref(i),r->before[i]))continue;
        esp_err_t err=set_ref(i,r->after[i]);if(err!=ESP_OK)result=err;
    }
    if(result==ESP_OK&&unlink(REF_PATH))result=ESP_FAIL;
    return result;
}
esp_err_t file_references_finish(void){
    intent_t *r=calloc(1,sizeof(*r));if(!r)return ESP_ERR_NO_MEM;
    esp_err_t err=load(r);if(err==ESP_OK)err=repair(r);else if(err==ESP_ERR_NOT_FOUND)err=ESP_OK;
    free(r);return err;
}
esp_err_t file_references_begin(const char *source,const char *target,bool *done){
    if(!done)return ESP_ERR_INVALID_ARG;
    *done=false;
    if(!source||!valid_path(source)||strlen(source)>=REF_CAP||(target&&*target&&(!valid_path(target)||strlen(target)>=REF_CAP||below(target,source))))return ESP_ERR_INVALID_ARG;
    if(!target)target="";
    intent_t *r=calloc(1,sizeof(*r));if(!r)return ESP_ERR_NO_MEM;
    esp_err_t err=load(r);
    if(err==ESP_OK){
        bool same=!strcmp(source,r->source)&&!strcmp(target,r->target);
        bool completed=exists(r->source)==0&&(!r->target[0]||exists(r->target)==1);
        err=repair(r);if(err!=ESP_OK){free(r);return err;}
        if(same&&completed){*done=true;free(r);return ESP_OK;}
    }else if(err!=ESP_ERR_NOT_FOUND){free(r);return err;}
    if(exists(source)!=1){free(r);return ESP_ERR_NOT_FOUND;}
    if(*target&&exists(target)!=0){free(r);return ESP_ERR_INVALID_STATE;}
    memset(r,0,sizeof(*r));memcpy(r->magic,"IFREFS1",8);strcpy(r->source,source);strcpy(r->target,target);
    bool matches=false;
    for(int i=0;i<3;++i){
        const char *p=get_ref(i);if(!below(p,source))continue;
        if(strlen(p)>=REF_CAP){free(r);return ESP_ERR_INVALID_SIZE;}
        strcpy(r->before[i],p);matches=true;
        if(*target){int n=snprintf(r->after[i],REF_CAP,"%s%s",target,p+strlen(source));if(n<0||n>=(i<2?160:REF_CAP)){free(r);return ESP_ERR_INVALID_SIZE;}}
    }
    err=matches?persist(r):ESP_OK;free(r);return err;
}
