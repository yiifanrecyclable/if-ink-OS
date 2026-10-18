/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界NVS记录；散列键须核对完整路径，碰撞或存储失败不覆盖其他书。
 * English: Bounded NVS records validate complete paths before hashed-key updates.
 */
#include "content_library.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stddef.h>
bool content_home_quote_alignment_valid(unsigned align){return align<=CONTENT_QUOTE_JUSTIFY;}
content_quote_align_t content_home_quote_alignment(void){
    uint8_t align=CONTENT_QUOTE_CENTER;nvs_handle_t h;
    if(nvs_open("if_content",NVS_READONLY,&h)==ESP_OK){uint8_t value;
        if(nvs_get_u8(h,"quote_align",&value)==ESP_OK&&content_home_quote_alignment_valid(value))align=value;
        nvs_close(h);}
    return (content_quote_align_t)align;
}
esp_err_t content_home_quote_alignment_set(unsigned align){
    if(!content_home_quote_alignment_valid(align))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t e=nvs_open("if_content",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    e=nvs_set_u8(h,"quote_align",(uint8_t)align);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e;
}
bool content_home_quote_size_valid(unsigned px){return px>=CONTENT_QUOTE_SIZE_MIN&&px<=CONTENT_QUOTE_SIZE_MAX&&px%2==0;}
uint8_t content_home_quote_size(void){
    uint8_t px=96;nvs_handle_t h;
    if(nvs_open("if_content",NVS_READONLY,&h)==ESP_OK){uint8_t value;
        if(nvs_get_u8(h,"quote_px",&value)==ESP_OK&&content_home_quote_size_valid(value))px=value;
        nvs_close(h);}
    return px;
}
esp_err_t content_home_quote_size_set(unsigned px){
    if(!content_home_quote_size_valid(px))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t e=nvs_open("if_content",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    e=nvs_set_u8(h,"quote_px",(uint8_t)px);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e;
}
bool content_home_quote_valid(const char *text) {
    if (!text || strnlen(text, CONTENT_HOME_QUOTE_BYTES) == CONTENT_HOME_QUOTE_BYTES) return false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned first = *p++; uint32_t cp; int count;
        if (first < 0x80) { if (first < 0x20 || first == 0x7f) return false; continue; }
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; count = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; count = 3; }
        else return false;
        for (int i = 0; i < count; ++i) { if ((*p & 0xc0) != 0x80) return false; cp = (cp << 6) | (*p++ & 63); }
        if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff || (cp >= 0x80 && cp <= 0x9f)) return false;
    }
    return true;
}
const char *content_home_quote(void) {
    static char text[CONTENT_HOME_QUOTE_BYTES];
    nvs_handle_t h; size_t n = sizeof(text);
    if (nvs_open("if_content", NVS_READONLY, &h) == ESP_OK) {
        esp_err_t e = nvs_get_str(h, "home_quote", text, &n); nvs_close(h);
        if (e == ESP_OK && content_home_quote_valid(text)) return text;
    }
    return "我将宇宙随身携带";
}
esp_err_t content_home_quote_set(const char *text) {
    if (!content_home_quote_valid(text)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t e = nvs_open("if_content", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_str(h, "home_quote", text);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h); return e;
}
bool content_home_quote_font_valid(const char *path) {
    if(!path||strnlen(path,CONTENT_HOME_QUOTE_FONT_BYTES)==CONTENT_HOME_QUOTE_FONT_BYTES||!content_home_quote_valid(path))return false;
    if(!path[0])return true;
    size_t n=strlen(path);
    return (!strncmp(path,"/sdcard/",8)||!strncmp(path,"/flash/",7))&&n>4&&!strcasecmp(path+n-4,".ttf")&&
        !strstr(path,"/../")&&!strstr(path,"/./")&&!strstr(path,"//")&&!strchr(path,'\\');
}
const char *content_home_quote_font(void) {
    static char path[CONTENT_HOME_QUOTE_FONT_BYTES];nvs_handle_t h;size_t n=sizeof(path);
    if(nvs_open("if_content",NVS_READONLY,&h)==ESP_OK){esp_err_t e=nvs_get_str(h,"quote_font",path,&n);nvs_close(h);
        if(e==ESP_OK&&content_home_quote_font_valid(path))return path;}
    return "";
}
esp_err_t content_home_quote_font_set(const char *path) {
    if(!content_home_quote_font_valid(path))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t e=nvs_open("if_content",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    e=nvs_set_str(h,"quote_font",path);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e;
}
static void added_key(const char *path, char key[16]) {
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*16777619u;
    snprintf(key,16,"a_%08lx",(unsigned long)hash);
}
bool content_added_record_valid(const char *key,const content_added_record_t *r) {
    if(!key||!r||memcmp(r->magic,"IFA1",4)||strnlen(r->path,sizeof(r->path))==sizeof(r->path)||
       (strncmp(r->path,"/sdcard/",8)&&strncmp(r->path,"/flash/",7)))return false;
    char expected[16];added_key(r->path,expected);return !strcmp(key,expected);
}
content_options_t content_options_load(bool documents,bool legacy_recent) {
    content_options_t o={.mode=documents?CONTENT_RECENT:legacy_recent?CONTENT_RECENT:CONTENT_NAME,
        .ascending=!documents&&!legacy_recent};
    nvs_handle_t h;uint8_t bits;
    if(nvs_open("if_content",NVS_READONLY,&h)!=ESP_OK)return o;
    esp_err_t e=nvs_get_u8(h,documents?"docs":"books",&bits);nvs_close(h);
    if(e==ESP_OK&&bits<=31){o.mode=bits&3;o.ascending=(bits&4)!=0;o.marked_first=(bits&8)!=0;o.marked_only=(bits&16)!=0;}
    // 收藏/星标筛选已取消，旧备份位只保留格式兼容。/ Retired favorite/star bits remain compatible but never filter or reorder content.
    o.marked_first=o.marked_only=false;
    return o;
}
esp_err_t content_options_save(bool documents,content_options_t o) {
    if(o.mode>CONTENT_RECENT)return ESP_ERR_INVALID_ARG;
    // 收藏/星标筛选已取消，旧备份位只保留格式兼容。/ Retired favorite/star bits remain compatible but never filter or reorder content.
    o.marked_first=o.marked_only=false;
    nvs_handle_t h;esp_err_t e=nvs_open("if_content",NVS_READWRITE,&h);
    if(e!=ESP_OK)return e;
    uint8_t bits=o.mode|(o.ascending?4:0)|(o.marked_first?8:0)|(o.marked_only?16:0);
    e=nvs_set_u8(h,documents?"docs":"books",bits);
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);return e;
}
uint32_t content_first_added(const char *path) {
    if(!path||strlen(path)>=288||(strncmp(path,"/sdcard/",8)&&strncmp(path,"/flash/",7)))return 0;
    nvs_handle_t h;if(nvs_open("if_content",NVS_READWRITE,&h)!=ESP_OK)return 0;
    char key[16];added_key(path,key);content_added_record_t r={0};size_t n=sizeof(r);
    esp_err_t e=nvs_get_blob(h,key,&r,&n);
    if(e==ESP_ERR_NVS_NOT_FOUND){
        time_t now=time(NULL);
        if(now>=1704067200&&(uint64_t)now<=UINT32_MAX){
            memcpy(r.magic,"IFA1",4);strcpy(r.path,path);
            for(int i=0;i<4;++i)r.stamp[i]=(uint8_t)((uint64_t)now>>(8*i));
            e=nvs_set_blob(h,key,&r,offsetof(content_added_record_t,path)+strlen(path)+1);
            if(e==ESP_OK)e=nvs_commit(h);
        }
    }else if(e==ESP_OK&&(n<9||n>sizeof(r)||!content_added_record_valid(key,&r)||
        n!=offsetof(content_added_record_t,path)+strlen(r.path)+1||strcmp(path,r.path)))e=ESP_ERR_INVALID_RESPONSE;
    nvs_close(h);if(e!=ESP_OK)return 0;
    uint32_t stamp=0;for(int i=0;i<4;++i)stamp|=(uint32_t)r.stamp[i]<<(8*i);return stamp;
}
int content_name_compare(const char *a,const char *b) {
    // 数字片段按长度和数值排序，不转换为可能溢出的整数。/ Compare digit runs without overflowing integer conversion.
    while(*a&&*b){
        if(isdigit((unsigned char)*a)&&isdigit((unsigned char)*b)){
            const char *ae=a,*be=b;while(isdigit((unsigned char)*ae))++ae;while(isdigit((unsigned char)*be))++be;
            const char *az=a,*bz=b;while(az<ae&&*az=='0')++az;while(bz<be&&*bz=='0')++bz;
            if(ae-az!=be-bz)return ae-az<be-bz?-1:1;
            int c=memcmp(az,bz,(size_t)(ae-az));if(c)return c;
            a=ae;b=be;continue;
        }
        unsigned char ac=(unsigned char)tolower((unsigned char)*a),bc=(unsigned char)tolower((unsigned char)*b);
        if(ac!=bc)return ac<bc?-1:1;
        ++a;++b;
    }
    return *a?1:*b?-1:0;
}

static bool pin_path_valid(const char *path){
    if(!path||!*path||strnlen(path,288)>=288||(strncmp(path,"/sdcard/",8)&&strncmp(path,"/flash/",7)))return false;
    const char *ext=strrchr(path,'.');return ext&&(!strcasecmp(ext,".txt")||!strcasecmp(ext,".epub"));
}
static void pin_key(const char *path,char key[16]){added_key(path,key);key[0]='p';}
bool content_book_pin_valid(const char *key,const char *path){
    if(!key||!pin_path_valid(path))return false;
    char expected[16];pin_key(path,expected);return !strcmp(key,expected);
}
bool content_book_pinned(const char *path){
    if(!pin_path_valid(path))return false;
    nvs_handle_t h;if(nvs_open("if_content",NVS_READONLY,&h)!=ESP_OK)return false;
    char key[16],stored[288];size_t len=sizeof(stored);pin_key(path,key);
    esp_err_t e=nvs_get_str(h,key,stored,&len);nvs_close(h);
    return e==ESP_OK&&len>1&&len<=sizeof(stored)&&stored[len-1]==0&&strnlen(stored,len)==len-1&&!strcmp(stored,path);
}
esp_err_t content_book_pin_set(const char *path,bool pinned){
    if(!pin_path_valid(path))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t e=nvs_open("if_content",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    char key[16],stored[288];size_t len=sizeof(stored);pin_key(path,key);
    e=nvs_get_str(h,key,stored,&len);
    if(e==ESP_OK&&(len<2||len>sizeof(stored)||stored[len-1]!=0||strnlen(stored,len)!=len-1||strcmp(stored,path)))e=ESP_ERR_INVALID_RESPONSE;
    if(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND){
        if(pinned)e=nvs_set_str(h,key,path);
        else{e=nvs_erase_key(h,key);if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;}
        if(e==ESP_OK)e=nvs_commit(h);
    }
    nvs_close(h);return e;
}

bool content_markdown_sizes_valid(const uint8_t sizes[4]) {
    if (!sizes || sizes[0] < 36 || sizes[0] > 72) return false;
    for (int i = 1; i < 4; ++i) if (sizes[i] < 24 || sizes[i] > 96) return false;
    return true;
}
void content_markdown_sizes(uint8_t sizes[4]) {
    const uint8_t defaults[4] = {48,72,64,56}; memcpy(sizes,defaults,4);
    nvs_handle_t h;
    if (nvs_open("if_content",NVS_READONLY,&h) == ESP_OK) {
        uint8_t stored[4]; size_t n = sizeof(stored);
        if (nvs_get_blob(h,"md_sizes",stored,&n) == ESP_OK && n == sizeof(stored) && content_markdown_sizes_valid(stored))
            memcpy(sizes,stored,4);
        nvs_close(h);
    }
}
esp_err_t content_markdown_sizes_set(const uint8_t sizes[4]) {
    if (!content_markdown_sizes_valid(sizes)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t err = nvs_open("if_content",NVS_READWRITE,&h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h,"md_sizes",sizes,4);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err;
}

const char *content_markdown_font(void){
    static char path[CONTENT_MARKDOWN_FONT_BYTES];size_t n=sizeof(path);nvs_handle_t h;
    if(nvs_open("if_content",NVS_READONLY,&h)==ESP_OK){esp_err_t e=nvs_get_str(h,"md_font",path,&n);nvs_close(h);
        if(e==ESP_OK&&content_markdown_font_valid(path))return path;}
    return "";
}
bool content_markdown_font_valid(const char *path){return path&&path[0]&&strlen(path)<CONTENT_MARKDOWN_FONT_BYTES&&(!strcmp(path,"builtin")||content_home_quote_font_valid(path));}
esp_err_t content_markdown_font_set(const char *path){
    if(!content_markdown_font_valid(path))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t e=nvs_open("if_content",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    e=nvs_set_str(h,"md_font",path);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e;
}
