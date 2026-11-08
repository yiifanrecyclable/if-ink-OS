/* SPDX-License-Identifier: Apache-2.0
 * 先提交全部引用意图，再改文件。失败保留意图，重试只补仍指向旧路径的项。
 * Commit all reference intentions before editing files; retry only preferences still pointing at old paths.
 */
#include "file_references.h"
#include "settings.h"
#include "book/content_library.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#define REF_PATH "/sdcard/.readpico/settings/file-refs.pending"
#define REF_TEMP REF_PATH ".tmp"
#define REF_CAP 288
// 固定布局、版本及CRC；不存指针、不枚举目录、不储存凭据。
// Versioned fixed layout + CRC; no pointers, directory walks, or credentials.
enum { SYSTEM_FONT, READING_FONT, WALLPAPER, MARKDOWN_FONT, BANNER_FONT, REF_COUNT };
typedef struct {
    char magic[8],source[REF_CAP],target[REF_CAP];
    char before[3][REF_CAP],after[3][REF_CAP];
    // IFREFS1 ends here; local font resets append the extra references and a new checksum.
    // IFREFS1 到此为止；设备本地字体复位追加独立引用及校验值。
    uint32_t legacy_crc;
    char extra_fonts[2][REF_CAP];
    uint32_t crc;
} intent_t;
static bool below(const char *path,const char *root){size_t n=strlen(root);return n&&!strncasecmp(path,root,n)&&(!path[n]||path[n]=='/');}
static bool valid_path(const char *p){
    if(strncmp(p,"/sdcard/",8)||!p[8])return false;
    for(const char *c=p+8;*c;++c)if(*c=='\\'||(*c=='.'&&(c==p+8||c[-1]=='/')&&(c[1]=='/'||!c[1]||(c[1]=='.'&&(c[2]=='/'||!c[2])))))return false;
    return true;
}
static const char *get_ref(int i){
    switch(i){
    case SYSTEM_FONT: return app_settings_system_font_path();
    case READING_FONT: return app_settings_font_path();
    case WALLPAPER: return app_settings_wallpaper_path();
    case MARKDOWN_FONT: return content_markdown_font();
    default: return content_home_quote_font();
    }
}
static esp_err_t read_ref(int i,char value[REF_CAP]){
    if(i==MARKDOWN_FONT)return content_markdown_font_checked(value,REF_CAP);
    if(i==BANNER_FONT){
        content_banner_config_t banner;
        esp_err_t err=content_banner_load_checked(&banner);
        if(err==ESP_OK&&strlcpy(value,banner.font,REF_CAP)>=REF_CAP)err=ESP_ERR_INVALID_SIZE;
        return err;
    }
    return strlcpy(value,get_ref(i),REF_CAP)<REF_CAP?ESP_OK:ESP_ERR_INVALID_SIZE;
}
static esp_err_t set_ref(int i,const char *p){
    switch(i){
    case SYSTEM_FONT: return app_settings_set_system_font_path(p);
    case READING_FONT: return app_settings_set_font_path(p);
    case WALLPAPER: return app_settings_set_wallpaper_path(p);
    case MARKDOWN_FONT: return content_markdown_font_set("builtin");
    default: return content_home_quote_font_set("");
    }
}
bool file_references_font_in_use(const char *source){
    for(int i=0;i<REF_COUNT;++i)if(i!=WALLPAPER&&below(get_ref(i),source))return true;
    return false;
}
static bool resets_fonts(const intent_t *r){return r->magic[6]=='2';}
static char *before_at(intent_t *r,int i){return i<MARKDOWN_FONT?r->before[i]:r->extra_fonts[i-MARKDOWN_FONT];}
static size_t crc_offset(const intent_t *r){return resets_fonts(r)?offsetof(intent_t,crc):offsetof(intent_t,legacy_crc);}
static uint32_t checksum(const intent_t *r){return (uint32_t)crc32(0,(const Bytef*)r,crc_offset(r));}
static int exists(const char *p){struct stat st;if(!stat(p,&st))return 1;return errno==ENOENT?0:-1;}
static esp_err_t load(intent_t *r){
    FILE *f=fopen(REF_PATH,"rb");if(!f)return errno==ENOENT?ESP_ERR_NOT_FOUND:ESP_FAIL;
    memset(r,0,sizeof(*r));size_t bytes=fread(r,1,sizeof(*r),f);
    bool ok=bytes==crc_offset(r)+sizeof(uint32_t)&&fgetc(f)==EOF&&!ferror(f);
    if(fclose(f))ok=false;
    if(!ok||memcmp(r->magic,resets_fonts(r)?"IFREFS2":"IFREFS1",8)||
       (resets_fonts(r)?r->crc:r->legacy_crc)!=checksum(r))return ESP_FAIL;
    if(!memchr(r->source,0,REF_CAP)||!memchr(r->target,0,REF_CAP)||!valid_path(r->source)||
       (r->target[0]&&!valid_path(r->target)))return ESP_FAIL;
    for(int i=0;i<(resets_fonts(r)?REF_COUNT:MARKDOWN_FONT);++i){
        const char *before=before_at(r,i);
        if(!memchr(before,0,REF_CAP)||(before[0]&&(!below(before,r->source)||!valid_path(before))))return ESP_FAIL;
        if(i>=MARKDOWN_FONT)continue;
        if(!memchr(r->after[i],0,REF_CAP))return ESP_FAIL;
        char expected[REF_CAP]={0};
        if(before[0]&&r->target[0]&&(!resets_fonts(r)||i==WALLPAPER)){
            int n=snprintf(expected,sizeof(expected),"%s%s",r->target,before+strlen(r->source));
            if(n<0||n>=(int)sizeof(expected))return ESP_FAIL;
        }
        if(strcmp(expected,r->after[i]))return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t persist(intent_t *r){
    if((mkdir("/sdcard/.readpico",0775)&&errno!=EEXIST)||(mkdir("/sdcard/.readpico/settings",0775)&&errno!=EEXIST))return ESP_FAIL;
    if(resets_fonts(r))r->crc=checksum(r);else r->legacy_crc=checksum(r);
    size_t bytes=crc_offset(r)+sizeof(uint32_t);
    FILE *f=fopen(REF_TEMP,"wb");if(!f)return ESP_FAIL;
    bool ok=fwrite(r,1,bytes,f)==bytes&&fflush(f)==0;
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
    for(int i=0;i<(resets_fonts(r)?REF_COUNT:MARKDOWN_FONT);++i){
        const char *before=before_at(r,i);
        if(!before[0])continue;
        const char *after=i<MARKDOWN_FONT?r->after[i]:"";
        int old=exists(before);if(old<0){result=ESP_FAIL;continue;}
        if(old){if(!r->target[0])continue;result=ESP_FAIL;continue;}
        if(after[0]&&exists(after)!=1){result=ESP_FAIL;continue;}
        // 已更新或用户另选过的引用不覆盖；其余失败仍尝试，不吞掉第二个引用。
        // Preserve already-applied or independently changed choices; attempt every remaining reference.
        char current[REF_CAP];
        esp_err_t read=read_ref(i,current);if(read!=ESP_OK){result=read;continue;}
        if(strcmp(current,before))continue;
        esp_err_t err=set_ref(i,after);if(err!=ESP_OK)result=err;
    }
    if(result==ESP_OK&&unlink(REF_PATH))result=ESP_FAIL;
    return result;
}
esp_err_t file_references_finish(void){
    intent_t *r=calloc(1,sizeof(*r));if(!r)return ESP_ERR_NO_MEM;
    esp_err_t err=load(r);if(err==ESP_OK)err=repair(r);else if(err==ESP_ERR_NOT_FOUND)err=ESP_OK;
    free(r);return err;
}
static esp_err_t begin(const char *source,const char *target,bool *done,bool reset_fonts){
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
    memset(r,0,sizeof(*r));memcpy(r->magic,reset_fonts?"IFREFS2":"IFREFS1",8);strcpy(r->source,source);strcpy(r->target,target);
    bool matches=false;
    for(int i=0;i<(reset_fonts?REF_COUNT:MARKDOWN_FONT);++i){
        char *p=before_at(r,i);
        err=read_ref(i,p);
        if(err!=ESP_OK){free(r);return err;}
        if(!below(p,source)){memset(p,0,REF_CAP);continue;}
        matches=true;
        if(*target&&(!reset_fonts||i==WALLPAPER)){
            int n=snprintf(r->after[i],REF_CAP,"%s%s",target,p+strlen(source));
            if(n<0||n>=(i<WALLPAPER?160:REF_CAP)){free(r);return ESP_ERR_INVALID_SIZE;}
        }
    }
    err=matches?persist(r):ESP_OK;free(r);return err;
}
esp_err_t file_references_begin(const char *source,const char *target,bool *done){return begin(source,target,done,false);}
esp_err_t file_references_begin_reset_fonts(const char *source,const char *target,bool *done){return begin(source,target,done,true);}
