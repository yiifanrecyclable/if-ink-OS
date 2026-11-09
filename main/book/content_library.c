/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界偏好留NVS，逐文件内容记录存TF；完整路径防止哈希冲突。
 * English: Bounded preferences stay in NVS; per-file records use SD with full-path collision checks.
 */
#include "content_library.h"
#include "book_policy.h"
#include "nvs.h"
#include "record_store.h"
#include "../settings_storage.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stddef.h>
_Static_assert(offsetof(content_banner_config_t,text_y)==652&&sizeof(content_banner_config_t)==656,"Stable banner v3 snapshot layout");
static bool banner_read(content_banner_config_t *config);
static esp_err_t banner_read_checked(content_banner_config_t *config);
bool content_reading_target_valid(unsigned minutes){return minutes>=30&&minutes<=180&&minutes%30==0;}
uint8_t content_reading_target_minutes(void){
    uint8_t minutes=90;records_handle_t h;
    if(records_open("if_content",NVS_READONLY,&h)==ESP_OK){uint8_t value;
        if(records_get_u8(h,"read_target",&value)==ESP_OK&&content_reading_target_valid(value))minutes=value;
        records_close(h);}
    return minutes;
}
esp_err_t content_reading_target_set(unsigned minutes){
    if(!content_reading_target_valid(minutes))return ESP_ERR_INVALID_ARG;
    return settings_storage_put_u8("if_content","read_target",(uint8_t)minutes);
}
bool content_home_quote_alignment_valid(unsigned align){return align<=CONTENT_QUOTE_JUSTIFY;}
content_quote_align_t content_home_quote_alignment(void){
    content_banner_config_t cfg;if(banner_read(&cfg))return cfg.align;
    uint8_t align=CONTENT_QUOTE_CENTER;records_handle_t h;
    if(records_open("if_content",NVS_READONLY,&h)==ESP_OK){uint8_t value;
        if(records_get_u8(h,"quote_align",&value)==ESP_OK&&content_home_quote_alignment_valid(value))align=value;
        records_close(h);}
    return (content_quote_align_t)align;
}
esp_err_t content_home_quote_alignment_set(unsigned align){
    if(!content_home_quote_alignment_valid(align))return ESP_ERR_INVALID_ARG;
    content_banner_config_t cfg;esp_err_t loaded=banner_read_checked(&cfg);if(loaded==ESP_OK){cfg.align=(uint8_t)align;return content_banner_save(&cfg);}
    if(loaded!=ESP_ERR_NVS_NOT_FOUND)return loaded;
    return settings_storage_put_u8("if_content","quote_align",(uint8_t)align);
}
bool content_home_quote_valign_valid(unsigned align){return align<=CONTENT_QUOTE_VBOTTOM;}
content_quote_valign_t content_home_quote_valign(void){
    content_banner_config_t cfg;if(banner_read(&cfg))return cfg.valign;
    uint8_t align=CONTENT_QUOTE_VTOP;records_handle_t h;
    if(records_open("if_content",NVS_READONLY,&h)==ESP_OK){uint8_t value;
        if(records_get_u8(h,"quote_valign",&value)==ESP_OK&&content_home_quote_valign_valid(value))align=value;
        records_close(h);}
    return (content_quote_valign_t)align;
}
esp_err_t content_home_quote_valign_set(unsigned align){
    if(!content_home_quote_valign_valid(align))return ESP_ERR_INVALID_ARG;
    content_banner_config_t cfg;esp_err_t loaded=banner_read_checked(&cfg);if(loaded==ESP_OK){cfg.valign=(uint8_t)align;return content_banner_save(&cfg);}
    if(loaded!=ESP_ERR_NVS_NOT_FOUND)return loaded;
    return settings_storage_put_u8("if_content","quote_valign",(uint8_t)align);
}
bool content_home_quote_size_valid(unsigned px){return px>=CONTENT_QUOTE_SIZE_MIN&&px<=CONTENT_QUOTE_SIZE_MAX&&px%2==0;}
uint8_t content_home_quote_size(void){
    content_banner_config_t cfg;if(banner_read(&cfg))return cfg.quote_px;
    uint8_t px=96;records_handle_t h;
    if(records_open("if_content",NVS_READONLY,&h)==ESP_OK){uint8_t value;
        if(records_get_u8(h,"quote_px",&value)==ESP_OK&&content_home_quote_size_valid(value))px=value;
        records_close(h);}
    return px;
}
esp_err_t content_home_quote_size_set(unsigned px){
    if(!content_home_quote_size_valid(px))return ESP_ERR_INVALID_ARG;
    content_banner_config_t cfg;esp_err_t loaded=banner_read_checked(&cfg);if(loaded==ESP_OK){cfg.quote_px=(uint8_t)px;return content_banner_save(&cfg);}
    if(loaded!=ESP_ERR_NVS_NOT_FOUND)return loaded;
    return settings_storage_put_u8("if_content","quote_px",(uint8_t)px);
}
bool content_home_quote_valid(const char *text) {
    if (!text || strnlen(text, CONTENT_HOME_QUOTE_BYTES) == CONTENT_HOME_QUOTE_BYTES) return false;
    const unsigned char *p = (const unsigned char *)text;unsigned breaks=0;
    while (*p) {
        unsigned first = *p++; uint32_t cp; int count;
        if (first < 0x80) { if(first=='\n'){if(++breaks>3)return false;continue;}if (first < 0x20 || first == 0x7f) return false; continue; }
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
    content_banner_config_t cfg;if(banner_read(&cfg)){memcpy(text,cfg.text,sizeof(text));return text;}
    records_handle_t h; size_t n = sizeof(text);
    if (records_open("if_content", NVS_READONLY, &h) == ESP_OK) {
        esp_err_t e = records_get_str(h, "home_quote", text, &n); records_close(h);
        if (e == ESP_OK && content_home_quote_valid(text)) return text;
    }
    return "我将宇宙随身携带";
}
esp_err_t content_home_quote_set(const char *text) {
    if (!content_home_quote_valid(text)) return ESP_ERR_INVALID_ARG;
    content_banner_config_t cfg;esp_err_t loaded=banner_read_checked(&cfg);if(loaded==ESP_OK){snprintf(cfg.text,sizeof(cfg.text),"%s",text);return content_banner_save(&cfg);}
    if(loaded!=ESP_ERR_NVS_NOT_FOUND)return loaded;
    return settings_storage_put_str("if_content","home_quote",text);
}
static bool content_font_path_valid(const char *path, bool allow_otf) {
    if(!path||strnlen(path,CONTENT_HOME_QUOTE_FONT_BYTES)==CONTENT_HOME_QUOTE_FONT_BYTES||!content_home_quote_valid(path)||strchr(path,'\n'))return false;
    if(!path[0])return true;
    size_t n=strlen(path);
    return (!strncmp(path,"/sdcard/",8)||!strncmp(path,"/flash/",7))&&n>4&&
        (!strcasecmp(path+n-4,".ttf")||(allow_otf&&!strcasecmp(path+n-4,".otf")))&&
        !strstr(path,"/../")&&!strstr(path,"/./")&&!strstr(path,"//")&&!strchr(path,'\\');
}
bool content_home_quote_font_valid(const char *path) { return content_font_path_valid(path, false); }
const char *content_home_quote_font(void) {
    static char path[CONTENT_HOME_QUOTE_FONT_BYTES];content_banner_config_t cfg;
    if(banner_read(&cfg)){memcpy(path,cfg.font,sizeof(path));return path;}
    records_handle_t h;size_t n=sizeof(path);
    if(records_open("if_content",NVS_READONLY,&h)==ESP_OK){esp_err_t e=records_get_str(h,"quote_font",path,&n);records_close(h);
        if(e==ESP_OK&&content_home_quote_font_valid(path))return path;}
    return "";
}
esp_err_t content_home_quote_font_set(const char *path) {
    if(!content_home_quote_font_valid(path))return ESP_ERR_INVALID_ARG;
    content_banner_config_t cfg;esp_err_t loaded=banner_read_checked(&cfg);if(loaded==ESP_OK){snprintf(cfg.font,sizeof(cfg.font),"%s",path);return content_banner_save(&cfg);}
    if(loaded!=ESP_ERR_NVS_NOT_FOUND)return loaded;
    return settings_storage_put_str("if_content","quote_font",path);
}
bool content_banner_image_valid(const char *path){
    if(!path||strnlen(path,CONTENT_BANNER_IMAGE_BYTES)==CONTENT_BANNER_IMAGE_BYTES)return false;
    if(!path[0])return true;
    if(strncmp(path,"/sdcard/",8)||strstr(path,"/../")||strstr(path,"/./")||strstr(path,"//")||strchr(path,'\\'))return false;
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)if(*p<32||*p==127)return false;
    const char *ext=strrchr(path,'.');return ext&&(!strcasecmp(ext,".png")||!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg"));
}
bool content_banner_valid(const content_banner_config_t *cfg){
    return cfg&&(cfg->version>=1&&cfg->version<=3)&&cfg->mode<=1&&cfg->show_date<=1&&
        (cfg->version==1?cfg->show_text==0:cfg->show_text<=1)&&
        (cfg->version<3||cfg->text_y==CONTENT_BANNER_TEXT_AUTO||cfg->text_y<=CONTENT_BANNER_TEXT_MAX)&&
        (cfg->date_y==CONTENT_BANNER_DATE_AUTO||(cfg->date_y>=CONTENT_BANNER_DATE_MIN&&cfg->date_y<=CONTENT_BANNER_DATE_MAX))&&
        content_home_quote_size_valid(cfg->quote_px)&&content_home_quote_alignment_valid(cfg->align)&&
        content_home_quote_valign_valid(cfg->valign)&&content_home_quote_valid(cfg->text)&&
        content_home_quote_font_valid(cfg->font)&&content_banner_image_valid(cfg->image);
}
bool content_banner_decode(const void *data,size_t size,content_banner_config_t *cfg){
    if(!data||!cfg||(size!=652&&size!=sizeof(*cfg)))return false;
    content_banner_config_t loaded={0};memcpy(&loaded,data,size);
    if((loaded.version<3&&size!=652)||(loaded.version==3&&size!=sizeof(loaded))||!content_banner_valid(&loaded))return false;
    if(loaded.version==1){loaded.show_text=!loaded.mode;if(!loaded.mode)loaded.show_date=1;}
    if(loaded.version<3)loaded.text_y=CONTENT_BANNER_TEXT_AUTO;
    loaded.version=3;
    // 随内置示例改名迁移旧默认路径，其他自选图片保持原样。/ Migrate the renamed bundled default without changing other chosen artwork.
    if(!strcmp(loaded.image,"/sdcard/banner pic/山水示例.png"))snprintf(loaded.image,sizeof(loaded.image),"%s",CONTENT_BANNER_DEFAULT_IMAGE);
    *cfg=loaded;return true;
}
static esp_err_t banner_read_checked(content_banner_config_t *cfg){
    records_handle_t h;esp_err_t err=records_open("if_content",NVS_READONLY,&h);if(err!=ESP_OK)return err;
    content_banner_config_t raw={0};size_t n=sizeof(raw);err=records_get_blob(h,"banner_v1",&raw,&n);records_close(h);
    if(err==ESP_OK&&!content_banner_decode(&raw,n,cfg))err=ESP_ERR_INVALID_RESPONSE;
    return err;
}
static bool banner_read(content_banner_config_t *cfg){return banner_read_checked(cfg)==ESP_OK;}
static void banner_defaults(content_banner_config_t *cfg){
    memset(cfg,0,sizeof(*cfg));cfg->version=3;cfg->text_y=CONTENT_BANNER_TEXT_AUTO;cfg->show_date=1;cfg->show_text=1;cfg->date_y=CONTENT_BANNER_DATE_AUTO;
    cfg->quote_px=96;cfg->align=CONTENT_QUOTE_CENTER;cfg->valign=CONTENT_QUOTE_VTOP;
    snprintf(cfg->text,sizeof(cfg->text),"%s","我将宇宙随身携带");
    snprintf(cfg->image,sizeof(cfg->image),"%s",CONTENT_BANNER_DEFAULT_IMAGE);
}
// 只有缺失键使用兼容默认值，读取错误阻止编辑和备份。/ Only missing keys use defaults; read errors block edits and backups.
esp_err_t content_banner_load_checked(content_banner_config_t *cfg){
    if(!cfg)return ESP_ERR_INVALID_ARG;
    content_banner_config_t loaded;esp_err_t err=banner_read_checked(&loaded);
    if(err==ESP_OK){*cfg=loaded;return ESP_OK;}
    if(err!=ESP_ERR_NVS_NOT_FOUND)return err;
    banner_defaults(&loaded);
    records_handle_t h;err=records_open("if_content",NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND){*cfg=loaded;return ESP_OK;}
    if(err!=ESP_OK)return err;
    const char *keys[]={"quote_px","quote_align","quote_valign"};
    uint8_t *values[]={&loaded.quote_px,&loaded.align,&loaded.valign};
    for(unsigned i=0;i<3&&err==ESP_OK;++i){uint8_t value;err=records_get_u8(h,keys[i],&value);if(err==ESP_OK)*values[i]=value;else if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;}
    char *strings[]={loaded.text,loaded.font};const char *names[]={"home_quote","quote_font"};
    size_t caps[]={sizeof(loaded.text),sizeof(loaded.font)};
    for(unsigned i=0;i<2&&err==ESP_OK;++i){size_t n=caps[i];err=records_get_str(h,names[i],strings[i],&n);if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;}
    records_close(h);
    if(err==ESP_OK&&!content_banner_valid(&loaded))err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK)*cfg=loaded;
    return err;
}
void content_banner_load(content_banner_config_t *cfg){
    if(!cfg)return;
    banner_defaults(cfg);(void)content_banner_load_checked(cfg);
}
esp_err_t content_banner_save(const content_banner_config_t *cfg){
    if(!content_banner_valid(cfg)||cfg->version!=3)return ESP_ERR_INVALID_ARG;
    return settings_storage_put_blob("if_content","banner_v1",cfg,sizeof(*cfg));
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
    records_handle_t h;uint8_t bits;
    if(records_open("if_content",NVS_READONLY,&h)!=ESP_OK)return o;
    esp_err_t e=records_get_u8(h,documents?"docs":"books",&bits);records_close(h);
    if(e==ESP_OK&&bits<=31){o.mode=bits&3;o.ascending=(bits&4)!=0;o.marked_first=(bits&8)!=0;o.marked_only=(bits&16)!=0;}
    // 收藏/星标筛选已取消，旧备份位只保留格式兼容。/ Retired favorite/star bits remain compatible but never filter or reorder content.
    o.marked_first=o.marked_only=false;
    return o;
}
esp_err_t content_options_save(bool documents,content_options_t o) {
    if(o.mode>CONTENT_RECENT)return ESP_ERR_INVALID_ARG;
    // 收藏/星标筛选已取消，旧备份位只保留格式兼容。/ Retired favorite/star bits remain compatible but never filter or reorder content.
    o.marked_first=o.marked_only=false;
    uint8_t bits=o.mode|(o.ascending?4:0);
    return settings_storage_put_u8("if_content",documents?"docs":"books",bits);
}
uint32_t content_first_added(const char *path) {
    if(!path||strlen(path)>=288||(strncmp(path,"/sdcard/",8)&&strncmp(path,"/flash/",7)))return 0;
    records_handle_t h;if(records_open("if_content",NVS_READWRITE,&h)!=ESP_OK)return 0;
    bool created=false;char key[16];added_key(path,key);content_added_record_t r={0};size_t n=sizeof(r);
    esp_err_t e=records_get_blob(h,key,&r,&n);
    if(e==ESP_ERR_NVS_NOT_FOUND){
        time_t now=time(NULL);
        if(now>=1704067200&&(uint64_t)now<=UINT32_MAX){
            memcpy(r.magic,"IFA1",4);strcpy(r.path,path);
            for(int i=0;i<4;++i)r.stamp[i]=(uint8_t)((uint64_t)now>>(8*i));
            created=true;e=records_set_blob(h,key,&r,offsetof(content_added_record_t,path)+strlen(path)+1);
            if(e==ESP_OK)e=records_commit(h);
        }
    }else if(e==ESP_OK&&(n<9||n>sizeof(r)||!content_added_record_valid(key,&r)||
        n!=offsetof(content_added_record_t,path)+strlen(r.path)+1||strcmp(path,r.path)))e=ESP_ERR_INVALID_RESPONSE;
    records_close(h);if(e!=ESP_OK)return 0;
    if(created){
        e=records_open("if_content",NVS_READONLY,&h);if(e!=ESP_OK)return 0;
        content_added_record_t stored={0};size_t bytes=sizeof(stored);
        e=records_get_blob(h,key,&stored,&bytes);records_close(h);
        size_t expected=offsetof(content_added_record_t,path)+strlen(path)+1;
        if(e!=ESP_OK||bytes!=expected||memcmp(&stored,&r,expected))return 0;
    }
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
    records_handle_t h;if(records_open("if_content",NVS_READONLY,&h)!=ESP_OK)return false;
    char key[16],stored[288];size_t len=sizeof(stored);pin_key(path,key);
    esp_err_t e=records_get_str(h,key,stored,&len);records_close(h);
    return e==ESP_OK&&len>1&&len<=sizeof(stored)&&stored[len-1]==0&&strnlen(stored,len)==len-1&&!strcmp(stored,path);
}
esp_err_t content_book_pin_set(const char *path,bool pinned){
    if(!pin_path_valid(path))return ESP_ERR_INVALID_ARG;
    records_handle_t h;esp_err_t e=records_open("if_content",NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    char key[16],stored[288];size_t len=sizeof(stored);pin_key(path,key);
    e=records_get_str(h,key,stored,&len);
    if(e==ESP_OK&&(len<2||len>sizeof(stored)||stored[len-1]!=0||strnlen(stored,len)!=len-1||strcmp(stored,path)))e=ESP_ERR_INVALID_RESPONSE;
    if(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND){
        if(pinned)e=records_set_str(h,key,path);
        else{e=records_erase_key(h,key);if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;}
        if(e==ESP_OK)e=records_commit(h);
    }
    records_close(h);
    if(e!=ESP_OK)return e;
    e=records_open("if_content",NVS_READONLY,&h);if(e!=ESP_OK)return e;
    len=sizeof(stored);e=records_get_str(h,key,stored,&len);records_close(h);
    if(!pinned)return e==ESP_ERR_NVS_NOT_FOUND?ESP_OK:e==ESP_OK?ESP_ERR_INVALID_RESPONSE:e;
    if(e==ESP_OK&&(len!=strlen(path)+1||memcmp(stored,path,len)))e=ESP_ERR_INVALID_RESPONSE;
    return e;
}

bool content_markdown_sizes_valid(const uint8_t sizes[4]) {
    if (!sizes || !book_text_size_valid(sizes[0])) return false;
    for (int i = 1; i < 4; ++i) if (sizes[i] < 24 || sizes[i] > 96) return false;
    return true;
}
void content_markdown_sizes(uint8_t sizes[4]) {
    const uint8_t defaults[4] = {48,72,64,56}; memcpy(sizes,defaults,4);
    records_handle_t h;
    if (records_open("if_content",NVS_READONLY,&h) == ESP_OK) {
        uint8_t stored[4]; size_t n = sizeof(stored);
        if (records_get_blob(h,"md_sizes",stored,&n) == ESP_OK && n == sizeof(stored) && content_markdown_sizes_valid(stored))
            memcpy(sizes,stored,4);
        records_close(h);
    }
}
esp_err_t content_markdown_sizes_set(const uint8_t sizes[4]) {
    if (!content_markdown_sizes_valid(sizes)) return ESP_ERR_INVALID_ARG;
    return settings_storage_put_blob("if_content","md_sizes",sizes,4);
}

const char *content_markdown_font(void){
    static char path[CONTENT_MARKDOWN_FONT_BYTES];size_t n=sizeof(path);records_handle_t h;
    if(records_open("if_content",NVS_READONLY,&h)==ESP_OK){esp_err_t e=records_get_str(h,"md_font",path,&n);records_close(h);
        if(e==ESP_OK&&content_markdown_font_valid(path))return path;}
    return "";
}
bool content_markdown_font_valid(const char *path){return path&&path[0]&&strlen(path)<CONTENT_MARKDOWN_FONT_BYTES&&(!strcmp(path,"builtin")||content_font_path_valid(path,true));}
esp_err_t content_markdown_font_set(const char *path){
    if(!content_markdown_font_valid(path))return ESP_ERR_INVALID_ARG;
    return settings_storage_put_str("if_content","md_font",path);
}

// 编辑/导出读取保留错误；显示用旧入口仍可采用临时默认值。/ Editing/export preserves errors; legacy display readers may use temporary defaults.
esp_err_t content_reading_target_load_checked(uint8_t *minutes){
    if(!minutes)return ESP_ERR_INVALID_ARG;
    uint8_t value=90;records_handle_t h;esp_err_t err=records_open("if_content",NVS_READONLY,&h);
    if(err==ESP_OK){err=records_get_u8(h,"read_target",&value);records_close(h);}
    if(err==ESP_ERR_NVS_NOT_FOUND){value=90;err=ESP_OK;}
    if(err==ESP_OK&&!content_reading_target_valid(value))err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK)*minutes=value;
    return err;
}
esp_err_t content_options_load_checked(bool documents,bool legacy_recent,content_options_t *out){
    if(!out)return ESP_ERR_INVALID_ARG;
    content_options_t o={.mode=documents?CONTENT_RECENT:legacy_recent?CONTENT_RECENT:CONTENT_NAME,.ascending=!documents&&!legacy_recent};
    records_handle_t h;uint8_t bits=0;esp_err_t err=records_open("if_content",NVS_READONLY,&h);
    if(err==ESP_OK){err=records_get_u8(h,documents?"docs":"books",&bits);records_close(h);}
    if(err==ESP_OK){if(bits>31)return ESP_ERR_INVALID_RESPONSE;o.mode=bits&3;o.ascending=(bits&4)!=0;}
    if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    if(err==ESP_OK)*out=o;
    return err;
}
esp_err_t content_markdown_sizes_checked(uint8_t sizes[4]){
    if(!sizes)return ESP_ERR_INVALID_ARG;
    uint8_t value[4]={48,72,64,56};records_handle_t h;esp_err_t err=records_open("if_content",NVS_READONLY,&h);
    if(err==ESP_OK){size_t n=sizeof(value);err=records_get_blob(h,"md_sizes",value,&n);records_close(h);if(err==ESP_OK&&n!=sizeof(value))err=ESP_ERR_INVALID_RESPONSE;}
    if(err==ESP_ERR_NVS_NOT_FOUND){const uint8_t defaults[4]={48,72,64,56};memcpy(value,defaults,4);err=ESP_OK;}
    if(err==ESP_OK&&!content_markdown_sizes_valid(value))err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK)memcpy(sizes,value,4);
    return err;
}
esp_err_t content_markdown_font_checked(char *path,size_t capacity){
    if(!path||!capacity)return ESP_ERR_INVALID_ARG;
    char value[CONTENT_MARKDOWN_FONT_BYTES]={0};records_handle_t h;esp_err_t err=records_open("if_content",NVS_READONLY,&h);
    if(err==ESP_OK){size_t n=sizeof(value);err=records_get_str(h,"md_font",value,&n);records_close(h);if(err==ESP_OK&&!content_markdown_font_valid(value))err=ESP_ERR_INVALID_RESPONSE;}
    if(err==ESP_ERR_NVS_NOT_FOUND){value[0]=0;err=ESP_OK;}
    if(err==ESP_OK){if(strlen(value)>=capacity)return ESP_ERR_INVALID_SIZE;strcpy(path,value);}return err;
}
