/* SPDX-License-Identifier: Apache-2.0
 * 中文：只在UI任务验证网页草稿；动态日期与文字独立保存，不推屏、不操作阅读数据。
 * English: Validate web drafts on the UI task; keep date/text independent, without presenting or altering reading data.
 */
#include "transfer_appearance.h"
#include "content_library.h"
#include "settings.h"
#include "ui_banner.h"
#include "ui_wallpaper.h"
#include "ui_kit.h"
#include "book_cover.h"
#include "book_ticket.h"
#include "ttf_font.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#define WEB_FONT_ROOT "/sdcard/.web-fonts"
// 中文：随机草稿令牌只定位临时TTF，不接受客户端任意路径。/ English: random draft tokens identify staged TTFs, never arbitrary client paths.
static bool staged_font(const char *path){
    const char *prefix=WEB_FONT_ROOT "/";size_t n=strlen(prefix);
    if(strncmp(path,prefix,n)||strlen(path+n)!=36||strcmp(path+n+32,".ttf"))return false;
    for(unsigned i=0;i<32;++i)if(!strchr("0123456789abcdef",path[n+i]))return false;
    return true;
}
void transfer_appearance_cleanup(void){
    DIR *dir=opendir(WEB_FONT_ROOT);if(!dir)return;struct dirent *e;char path[160];
    while((e=readdir(dir))){
        if(snprintf(path,sizeof(path),WEB_FONT_ROOT "/%s",e->d_name)>=(int)sizeof(path))continue;
        bool temporary=staged_font(path);size_t n=strlen(path);
        if(!temporary&&n>5&&!strcmp(path+n-5,".part")){path[n-5]=0;temporary=staged_font(path);path[n-5]='.';}
        if(temporary)unlink(path);
    }
    closedir(dir);
}
static bool same_file(const char *a,const char *b){
    struct stat x,y;if(stat(a,&x)||stat(b,&y)||!S_ISREG(x.st_mode)||!S_ISREG(y.st_mode)||x.st_size!=y.st_size||x.st_size<=0)return false;
    FILE *fa=fopen(a,"rb"),*fb=fopen(b,"rb");bool same=fa&&fb;unsigned char p[2048],q[2048];
    for(size_t left=(size_t)x.st_size;same&&left;){size_t n=left<sizeof(p)?left:sizeof(p);same=fread(p,1,n,fa)==n&&fread(q,1,n,fb)==n&&!memcmp(p,q,n);left-=n;}
    if(fa)fclose(fa);
    if(fb)fclose(fb);
    return same;
}
static bool matching_font(const char *stage,char *path,size_t cap){
    DIR *dir=opendir("/sdcard/fonts");if(!dir)return false;struct dirent *e;bool found=false;
    while((e=readdir(dir))){const char *ext=strrchr(e->d_name,'.');
        if(e->d_name[0]=='.'||!ext||strcasecmp(ext,".ttf")||snprintf(path,cap,"/sdcard/fonts/%s",e->d_name)>=(int)cap)continue;
        if(same_file(stage,path)){found=true;break;}}
    closedir(dir);return found;
}
static bool promote_font(const char *stage,const char *name,char *path,size_t cap){
    if(matching_font(stage,path,cap))return true;
    if(!name||!*name||strlen(name)>80||strchr(name,'/')||strchr(name,'\\')||name[0]=='.')return false;
    for(const char *p=name;*p;++p)if((unsigned char)*p<32||strchr(":*?\"<>|",*p))return false;
    const char *ext=strrchr(name,'.');if(!ext||strcasecmp(ext,".ttf"))return false;
    if(snprintf(path,cap,"/sdcard/fonts/%.*s-%.8s.ttf",(int)(ext-name),name,stage+strlen(WEB_FONT_ROOT)+1)>=(int)cap)return false;
    struct stat st;if(!stat(path,&st))return same_file(stage,path);
    char part[256];snprintf(part,sizeof(part),"%s.part",path);FILE *in=fopen(stage,"rb"),*out=fopen(part,"wb");bool ok=in&&out;unsigned char bytes[4096];size_t n;
    if(ok)while((n=fread(bytes,1,sizeof(bytes),in)))if(fwrite(bytes,1,n,out)!=n){ok=false;break;}
    if(in){if(ferror(in))ok=false;fclose(in);}
    if(out){if(fflush(out)||fsync(fileno(out)))ok=false;if(fclose(out))ok=false;}
    if(ok)ok=rename(part,path)==0;
    if(!ok)unlink(part);
    return ok&&same_file(stage,path);
}

static esp_err_t response(read_pico_transfer_editor_reply_t *out,int status,const char *message){
    cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root,"ok",status==200);if(message)cJSON_AddStringToObject(root,"error",message);
    char *text=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    out->status=status;out->data=(uint8_t*)text;out->size=text?strlen(text):0;return text?ESP_OK:ESP_ERR_NO_MEM;
}
static void revision(char value[9]){
    content_banner_config_t cfg;content_banner_load(&cfg);uint32_t hash=2166136261u;
    const uint8_t *bytes=(const uint8_t*)&cfg;
    for(size_t i=0;i<sizeof(cfg);++i)hash=(hash^bytes[i])*16777619u;
    for(const char *p=app_settings_wallpaper_path();*p;++p)hash=(hash^(uint8_t)*p)*16777619u;
    snprintf(value,9,"%08lx",(unsigned long)hash);
}
static bool safe_asset(const char *path){
    if(strncmp(path,"/sdcard/",8)||!path[8]||strlen(path)>238)return false;
    for(const char *p=path+8;*p;++p){
        if((*p=='.'&&(p==path+8||p[-1]=='/'))||strchr("\\:*?\"<>|",*p)||(unsigned char)*p<32)return false;
        if(*p=='/'&&p[1]=='/')return false;
    }
    return path[strlen(path)-1]!='/';
}
static bool readable_image(const char *path){
    if(!safe_asset(path)||!content_banner_image_valid(path))return false;
    struct stat st;if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>2*1024*1024)return false;
    FILE *f=fopen(path,"rb");if(!f)return false;
    uint8_t *data=heap_caps_malloc((size_t)st.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    unsigned w=0,h=0;const char *ext=strrchr(path,'.');
    bool ok=data&&fread(data,1,(size_t)st.st_size,f)==(size_t)st.st_size&&
        book_image_dimensions(data,(size_t)st.st_size,!strcasecmp(ext,".png"),&w,&h)&&
        w&&h&&w<=8192&&h<=8192&&(uint64_t)w*h<=16777216;
    fclose(f);free(data);return ok;
}
static bool font_usable(const content_banner_config_t *cfg){
    if(!cfg->font[0])return true;
    if(!staged_font(cfg->font)&&(!safe_asset(cfg->font)||strncasecmp(cfg->font,"/sdcard/fonts/",14)))return false;
    struct stat st;if(stat(cfg->font,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>32*1024*1024)return false;
    char saved[TTF_FONT_PATH_MAX];snprintf(saved,sizeof(saved),"%s",ttf_font_path());int weight=ttf_get_weight();
    bool ok=ttf_font_open(cfg->font)==ESP_OK&&!strcmp(ttf_font_path(),cfg->font)&&ttf_font_has_text(cfg->text);
    if(ttf_font_open(saved)!=ESP_OK)ttf_font_open_builtin();
    ttf_set_weight(weight);return ok;
}
static void date_now(uint32_t *day,bool *valid){uint32_t days[30];*valid=book_ticket_recent_days(days,day);}
static cJSON *banner_json(const content_banner_config_t *cfg){
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    cJSON_AddBoolToObject(o,"background",cfg->mode);cJSON_AddStringToObject(o,"image",cfg->image);
    cJSON_AddBoolToObject(o,"show_date",cfg->show_date);cJSON_AddNumberToObject(o,"date_y",cfg->date_y);
    cJSON_AddBoolToObject(o,"show_text",cfg->show_text);cJSON_AddStringToObject(o,"text",cfg->text);
    cJSON_AddStringToObject(o,"font",cfg->font);cJSON_AddNumberToObject(o,"size",cfg->quote_px);
    cJSON_AddNumberToObject(o,"align",cfg->align);cJSON_AddNumberToObject(o,"valign",cfg->valign);cJSON_AddNumberToObject(o,"text_y",cfg->text_y);
    return o;
}
static esp_err_t settings_reply(read_pico_transfer_editor_reply_t *out){
    content_banner_config_t cfg;content_banner_load(&cfg);char rev[9],date[160];revision(rev);
    uint32_t day=0;bool valid=false;date_now(&day,&valid);ui_banner_date_line(day,valid,date,sizeof(date));
    cJSON *o=cJSON_CreateObject(),*fonts=cJSON_CreateArray();if(!o||!fonts){cJSON_Delete(o);cJSON_Delete(fonts);return ESP_ERR_NO_MEM;}
    cJSON_AddBoolToObject(o,"ok",true);cJSON_AddStringToObject(o,"revision",rev);
    cJSON_AddItemToObject(o,"banner",banner_json(&cfg));cJSON_AddStringToObject(o,"wallpaper",app_settings_wallpaper_path());
    cJSON_AddStringToObject(o,"date",date);cJSON_AddNumberToObject(o,"date_y",cfg.date_y==CONTENT_BANNER_DATE_AUTO?ui_banner_default_date_y(cfg.quote_px):cfg.date_y);
    cJSON_AddNumberToObject(o,"text_y",ui_banner_text_y(&cfg));
    cJSON_AddNumberToObject(o,"date_size",ui_banner_positions_size(date,cfg.quote_px).date_px);
    cJSON_AddItemToObject(o,"fonts",fonts);DIR *dir=opendir("/sdcard/fonts");struct dirent *e;unsigned count=0;
    while(dir&&count<128&&(e=readdir(dir))){
        const char *ext=strrchr(e->d_name,'.');char path[160];struct stat st;
        if(!ext||strcasecmp(ext,".ttf")||
           snprintf(path,sizeof(path),"/sdcard/fonts/%s",e->d_name)>=(int)sizeof(path)||!safe_asset(path)||stat(path,&st)||!S_ISREG(st.st_mode))continue;
        cJSON *font=cJSON_CreateObject();cJSON_AddStringToObject(font,"name",e->d_name);cJSON_AddStringToObject(font,"path",path);cJSON_AddItemToArray(fonts,font);++count;
    }
    if(dir)closedir(dir);
    char *text=cJSON_PrintUnformatted(o);cJSON_Delete(o);out->status=200;out->data=(uint8_t*)text;out->size=text?strlen(text):0;return text?ESP_OK:ESP_ERR_NO_MEM;
}
static bool number(const cJSON *o,const char *key,unsigned maximum,unsigned *out){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>maximum||v->valuedouble!=(unsigned)v->valuedouble)return false;
    *out=(unsigned)v->valuedouble;return true;
}
static bool boolean(const cJSON *o,const char *key,uint8_t *out){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsBool(v))return false;*out=cJSON_IsTrue(v);return true;}
static bool string(const cJSON *o,const char *key,char *out,size_t cap){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsString(v)||strlen(v->valuestring)>=cap)return false;snprintf(out,cap,"%s",v->valuestring);return true;}
static bool unique_keys(const cJSON *o){
    if(!cJSON_IsObject(o))return false;
    for(const cJSON *a=o->child;a;a=a->next)for(const cJSON *b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;
    return true;
}
static bool draft(const cJSON *o,content_banner_config_t *cfg){
    memset(cfg,0,sizeof(*cfg));cfg->version=3;unsigned size,align,valign,date,text;
    if(!boolean(o,"background",&cfg->mode)||!boolean(o,"show_date",&cfg->show_date)||!boolean(o,"show_text",&cfg->show_text)||
       !string(o,"image",cfg->image,sizeof(cfg->image))||!string(o,"font",cfg->font,sizeof(cfg->font))||!string(o,"text",cfg->text,sizeof(cfg->text))||
       !number(o,"size",120,&size)||!number(o,"align",3,&align)||!number(o,"valign",2,&valign)||!number(o,"date_y",65535,&date)||!number(o,"text_y",65535,&text))return false;
    cfg->quote_px=size;cfg->align=align;cfg->valign=valign;cfg->date_y=date;cfg->text_y=text;return content_banner_valid(cfg);
}
bool transfer_appearance_path_writable(const char *path){
    content_banner_config_t cfg;content_banner_load(&cfg);
    const char *assets[]={cfg.mode?cfg.image:"",cfg.show_text?cfg.font:""};size_t n=strlen(path);
    for(unsigned i=0;i<2;++i)if(assets[i][0]&&!strncasecmp(path,assets[i],n)&&(!assets[i][n]||assets[i][n]=='/'))return false;
    return true;
}
esp_err_t transfer_appearance_dispatch(const char *op,const char *body,read_pico_transfer_editor_reply_t *out,
    read_pico_transfer_cancel_fn cancelled,void *context){
    memset(out,0,sizeof(*out));
    if(!strcmp(op,"get"))return settings_reply(out);
    if(strstr(body,"\\u0000"))return response(out,400,"文字内容无效");
    const char *end=NULL;cJSON *o=cJSON_ParseWithOpts(body,&end,true);
    if(!o||!unique_keys(o)){cJSON_Delete(o);return response(out,400,"设置参数无效");}
    if(!strcmp(op,"font-release")||!strcmp(op,"font-match")){
        char stage[160];bool valid=string(o,"path",stage,sizeof(stage))&&staged_font(stage);cJSON_Delete(o);
        if(!valid)return response(out,400,"临时字体无效");
        if(!strcmp(op,"font-release")){unlink(stage);return response(out,200,NULL);}
        char path[192]={0};bool found=matching_font(stage,path,sizeof(path));
        cJSON *reply=cJSON_CreateObject();cJSON_AddStringToObject(reply,"path",found?path:"");
        out->data=(uint8_t*)cJSON_PrintUnformatted(reply);cJSON_Delete(reply);out->status=200;out->size=out->data?strlen((char*)out->data):0;return out->data?ESP_OK:ESP_ERR_NO_MEM;
    }
    char rev[9],sent[9];revision(rev);
    if(!string(o,"revision",sent,sizeof(sent))||strcmp(sent,rev)){cJSON_Delete(o);return response(out,409,"设备设置已经变化，请重新读取后再编辑");}
    if(!strcmp(op,"wallpaper")){
        char path[288];bool valid=string(o,"path",path,sizeof(path))&&readable_image(path);cJSON_Delete(o);
        if(!valid)return response(out,422,"图片无法读取、格式不支持或尺寸过大，请重新裁剪");
        uint8_t *page=heap_caps_malloc(UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!page)return response(out,500,"内存不足，请重试");
        memset(page,255,UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
        valid=ui_wallpaper_draw(page,path,(EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT});free(page);
        if(!valid)return response(out,422,"图片无法解码，请重新选择");
        if(cancelled&&cancelled(context))return response(out,503,"操作已取消，请重新读取设置核实");
        if(app_settings_save_wallpaper_path(path)!=ESP_OK)return response(out,500,"保存失败，请重新读取设置核实");
        if(strcmp(app_settings_wallpaper_path(),path))return response(out,500,"保存结果未确认，请重新读取设置核实");
        out->changed=true;return settings_reply(out);
    }
    char font_name[96]={0};uint8_t sync_font=0;
    (void)string(o,"font_name",font_name,sizeof(font_name));(void)boolean(o,"sync_font",&sync_font);
    content_banner_config_t cfg;bool valid=draft(o,&cfg);cJSON_Delete(o);
    if(!valid)return response(out,400,"请检查完整设置、文字长度、字号和位置");
    if(!font_usable(&cfg))return response(out,422,"所选字体无法读取或缺少当前文字，请更换字体");
    if(cfg.mode&&!readable_image(cfg.image))return response(out,422,"背景图片无法读取、格式不支持或尺寸过大");
    uint32_t day=0;bool have_date=false;date_now(&day,&have_date);uint8_t *pixels=NULL;bool fits=false;
    if(!ui_banner_preview_config(&cfg,day,have_date,&pixels,&fits))return response(out,422,"背景图片或字体无法生成预览");
    if(!fits){free(pixels);return response(out,422,"文字超过四行或高度，请缩小字号或缩短内容");}
    if(cancelled&&cancelled(context)){free(pixels);return response(out,503,"操作已取消，请重新读取设置核实");}
    if(!strcmp(op,"preview")){out->status=200;out->pixels=true;out->data=pixels;out->size=684u*300u/2;return ESP_OK;}
    free(pixels);
    if(strcmp(op,"banner"))return response(out,400,"不支持的操作");
    if(staged_font(cfg.font)){
        if(!sync_font)return response(out,422,"请确认将字体同步至fonts文件夹");
        char installed[sizeof(cfg.font)];
        if(!promote_font(cfg.font,font_name,installed,sizeof(installed))||!safe_asset(installed))return response(out,500,"字体同步失败，请重新读取设置核实");
        snprintf(cfg.font,sizeof(cfg.font),"%s",installed);
    }
    if(cancelled&&cancelled(context))return response(out,503,"操作已取消，请重新读取设置核实");
    content_banner_config_t before;content_banner_load(&before);
    if(content_banner_save(&cfg)!=ESP_OK){
        (void)content_banner_save(&before);
        return response(out,500,"保存失败，请重新读取设置核实");
    }
    content_banner_config_t saved;content_banner_load(&saved);
    if(memcmp(&saved,&cfg,sizeof(cfg)))return response(out,500,"保存结果未确认，请重新读取设置核实");
    out->changed=true;return settings_reply(out);
}
