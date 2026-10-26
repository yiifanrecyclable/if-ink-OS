/* SPDX-License-Identifier: Apache-2.0
 * 中文：格言字号可调、默认96px，按实际字宽换行；保存/选字库/改字号时校验，不在绘制中打开字体。
 * English: Use adjustable type with a 96px default, wrap by real advances, and validate on save/font/size selection, without opening fonts during paint.
 */
#include "ui_banner.h"
#include "ui_kit.h"
#include "ui_bitmap.h"
#include "ui_wallpaper.h"
#include "ttf_font.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "ui_lunar.h"
#include "esp_heap_caps.h"
#define BANNER_TEXT_WIDTH (UI_LOCK_WIDTH-2*UI_BANNER_QUOTE_INSET)
static const char *const longest_date="2088年12月30日 · 星期三 · 癸亥猪年闰腊月三十";
static int width(const char *text,bool custom,int px){return custom?ttf_text_width_px(px,text):ui_text_fixed_width_px(px,text);}
static int ink_height(int px,const char *text,bool custom){if(!text[0])text="国Ag";int a=0,b=0;if(custom)ttf_measure_line_px(px,text,&a,&b);else ui_text_fixed_measure_px(px,text,&a,&b);return a+b;}
ui_banner_positions_t ui_banner_positions_size(const char *date,int quote_px){
    // 二分求最大字号，避免每次首页绘制逐字号栅格化整行日期。/ Binary-search size instead of rasterizing every intervening size on each home draw.
    int lo=12,hi=96;
    while(lo<hi){int mid=(lo+hi+1)/2;
        if(ui_text_fixed_width_px(mid,date)<=612&&ui_text_fixed_width_px(mid,longest_date)<=612)lo=mid;
        else hi=mid-1;
    }
    int px=lo;
    int dh=ink_height(px,date,false);
    int qh=ink_height(quote_px,"国Ag我将宇宙随身携带",false);if(qh<1)qh=quote_px;
    int total=dh+qh+UI_BANNER_GAP;
    int top=UI_PAGE_Y(205)-total/2,limit=UI_BANNER_BOTTOM-total-qh-UI_BANNER_GAP;
    if(top>limit)top=limit;
    if(top<UI_STATUS_HEIGHT+2)top=UI_STATUS_HEIGHT+2;
    return (ui_banner_positions_t){.date_px=px,.date_top=top,.quote_top=top+dh+UI_BANNER_GAP,.quote_height=qh,.quote_px=quote_px,.quote_align=content_home_quote_alignment(),.quote_valign=content_home_quote_valign()};
}
static bool quote_layout_size(const char *text,bool custom,int px,ui_banner_quote_t *out){
    memset(out,0,sizeof(*out));if(!content_home_quote_valid(text))return false;const char *next=text;
    for(int row=0;row<UI_BANNER_QUOTE_LINES&&(*next||row<out->count);++row){size_t used=0;out->count=row+1;
        while(*next&&*next!='\n'){
            unsigned char ch=(unsigned char)*next;size_t bytes=ch<0x80?1:ch<0xe0?2:ch<0xf0?3:4;
            memcpy(out->lines[row]+used,next,bytes);out->lines[row][used+bytes]=0;
            if(width(out->lines[row],custom,px)>BANNER_TEXT_WIDTH){out->lines[row][used]=0;break;}
            used+=bytes;next+=bytes;
        }
        if(*next=='\n'){++next;if(row==UI_BANNER_QUOTE_LINES-1){out->overflow=true;break;}out->count=row+2;}
    }
    out->overflow|=*next!=0;return !out->overflow;
}
static int quote_top(const ui_banner_quote_t *q,bool custom,ui_banner_positions_t p){
    int height=0;
    for(int i=0;i<q->count;++i)height+=ink_height(p.quote_px,q->lines[i],custom)+(i?UI_BANNER_GAP:0);
    if(p.free_y){
        int maximum=UI_BANNER_BOTTOM-height,minimum=UI_STATUS_HEIGHT+2;
        if(maximum<minimum)return minimum;
        return p.quote_top<minimum?minimum:p.quote_top>maximum?maximum:p.quote_top;
    }
    int spare=UI_BANNER_BOTTOM-p.quote_top-height;
    if(spare<0)spare=0;
    return p.quote_top+(p.quote_valign==CONTENT_QUOTE_VBOTTOM?spare:p.quote_valign==CONTENT_QUOTE_VCENTER?spare/2:0);
}
static bool fits_active(const char *text,bool custom,ui_banner_positions_t p){
    ui_banner_quote_t q;if(!quote_layout_size(text,custom,p.quote_px,&q))return false;
    int y=quote_top(&q,custom,p);
    for(int i=0;i<q.count;++i){int h=ink_height(p.quote_px,q.lines[i],custom);
        if(y+h>UI_BANNER_BOTTOM)return false;
        y+=h+UI_BANNER_GAP;}
    return true;
}
bool ui_banner_quote_fits_size(const char *text,const char *path,int px){
    if(!content_home_quote_size_valid((unsigned)px))return false;
    ui_banner_positions_t p=ui_banner_positions_size(longest_date,px);
    if(!path||!path[0])return fits_active(text,false,p);
    char saved[TTF_FONT_PATH_MAX];strlcpy(saved,ttf_font_path(),sizeof(saved));int weight=ttf_get_weight();
    bool custom=ttf_font_open(path)==ESP_OK&&ttf_font_has_text(text),fit=false;
    if(custom)fit=fits_active(text,true,p);
    if(ttf_font_open(saved)!=ESP_OK)ttf_font_open_builtin();
    ttf_set_weight(weight);
    return custom?fit:fits_active(text,false,p);
}
void ui_banner_quote_draw(uint8_t *fb,const char *text,bool custom,ui_banner_positions_t p){
    ui_banner_quote_t q;quote_layout_size(text,custom,p.quote_px,&q);
    if(q.overflow){char *last=q.lines[UI_BANNER_QUOTE_LINES-1];size_t n=strlen(last);
        while(n&&width(last,custom,p.quote_px)+width("…",custom,p.quote_px)>BANNER_TEXT_WIDTH){do{--n;}while(n&&((unsigned char)last[n]&0xc0)==0x80);last[n]=0;}
        strlcat(last,"…",sizeof(q.lines[UI_BANNER_QUOTE_LINES-1]));if(last[0]&&q.count<UI_BANNER_QUOTE_LINES)q.count=UI_BANNER_QUOTE_LINES;
    }
    int y=quote_top(&q,custom,p);
    for(int row=0;row<q.count;++row){int above=0,below=0;
        const char *measure=q.lines[row][0]?q.lines[row]:"国Ag";
        if(custom)ttf_measure_line_px(p.quote_px,measure,&above,&below);
        else ui_text_fixed_measure_px(p.quote_px,measure,&above,&below);
        if(y+above+below>UI_BANNER_BOTTOM)break;
        enum EpdFontFlags align=p.quote_align==CONTENT_QUOTE_LEFT?EPD_DRAW_ALIGN_LEFT:p.quote_align==CONTENT_QUOTE_RIGHT?EPD_DRAW_ALIGN_RIGHT:EPD_DRAW_ALIGN_CENTER;
        int x=p.quote_align==CONTENT_QUOTE_LEFT?UI_BANNER_QUOTE_INSET:p.quote_align==CONTENT_QUOTE_RIGHT?UI_LOCK_WIDTH-UI_BANNER_QUOTE_INSET:UI_LOCK_WIDTH/2;
        if(p.quote_align==CONTENT_QUOTE_JUSTIFY)ui_text_justify_ink_top(fb,UI_BANNER_QUOTE_INSET,y,p.quote_px,q.lines[row],BANNER_TEXT_WIDTH,custom);
        else if(custom)ttf_draw_text_px(fb,x,y+above,p.quote_px,q.lines[row],align,0,15);
        else ui_text_fixed_ink_top(fb,x,y,p.quote_px,q.lines[row],align);
        y+=above+below+UI_BANNER_GAP;
    }
}

ui_banner_positions_t ui_banner_positions(const char *date){return ui_banner_positions_size(date,content_home_quote_size());}
bool ui_banner_quote_layout(const char *text,bool custom,ui_banner_quote_t *out){return quote_layout_size(text,custom,content_home_quote_size(),out);}
bool ui_banner_quote_fits_font(const char *text,const char *path){return ui_banner_quote_fits_size(text,path,content_home_quote_size());}
void ui_banner_date_line(uint32_t day,bool valid,char *line,size_t cap){
    static const char *const weekdays[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
    struct tm date;char lunar[80];time_t seconds=(time_t)((uint64_t)day*86400);
    strlcpy(line,ui_i18n_english()?"Set date & time":"请设置日期与时间",cap);
    if(valid&&gmtime_r(&seconds,&date)){
        if(ui_i18n_english()){
            static const char *const months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            static const char *const days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
            snprintf(line,cap,"%s %d, %d · %s",months[date.tm_mon],date.tm_mday,date.tm_year+1900,days[date.tm_wday]);return;
        }
        bool have=ui_lunar_label((int32_t)day,lunar,sizeof(lunar));
        snprintf(line,cap,"%d年%d月%d日 · %s%s%s",date.tm_year+1900,date.tm_mon+1,date.tm_mday,weekdays[date.tm_wday],have?" · ":"",have?lunar:"");}
}
uint16_t ui_banner_default_date_y(int quote_px){
    ui_banner_positions_t p=ui_banner_positions_size(longest_date,quote_px);
    int y=p.date_top-UI_STATUS_HEIGHT;return (uint16_t)(y<4?4:y>CONTENT_BANNER_DATE_MAX?CONTENT_BANNER_DATE_MAX:y);
}
static void banner_date_draw(uint8_t *page,const char *date,ui_banner_positions_t p,bool outline){
    if(!outline){ui_text_fixed_ink_top(page,342,p.date_top,p.date_px,date,EPD_DRAW_ALIGN_CENTER);return;}
    int a=0,b=0;ui_text_fixed_measure_px(p.date_px,date,&a,&b);
    int center=p.date_top+(a+b)/2;
    for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy)if(dx*dx+dy*dy<=4)
        ui_text_fixed_vc_gray(page,342+dx,center+dy,p.date_px,date,EPD_DRAW_ALIGN_CENTER,UI_INK_WHITE,UI_INK_WHITE);
    ui_text_fixed_vc_gray(page,342,center,p.date_px,date,EPD_DRAW_ALIGN_CENTER,UI_INK_BLACK,UI_INK_WHITE);
}
// 先从白底文字层生成描边，再叠到日期与背景之上；复用输出缓存避免额外整屏分配。
// Outline a white text layer, then composite above date/background, reusing the output cache.
static int layer_gray(const uint8_t *layer,int x,int y){return (layer[y*342+x/2]>>(x%2?4:0))&15;}
static void banner_text_overlay(uint8_t *page,const uint8_t *layer){
    for(int y=0;y<300;++y)for(int x=0;x<684;){
        if(layer_gray(layer,x,y)==15){++x;continue;}
        int start=x;while(x<684&&layer_gray(layer,x,y)<15)++x;
        int left=start<2?0:start-2,right=x+2>684?684:x+2,top=y<2?0:y-2,bottom=y+3>300?300:y+3;
        epd_fill_rect((EpdRect){left,UI_STATUS_HEIGHT+top,right-left,bottom-top},UI_GRAY_WHITE,page);
    }
    for(int y=0;y<300;++y)for(int x=0;x<684;++x){int shade=layer_gray(layer,x,y);
        if(shade<15)epd_draw_pixel(x,UI_STATUS_HEIGHT+y,(uint8_t)(shade*17),page);
    }
}
uint16_t ui_banner_text_y(const content_banner_config_t *cfg){
    ui_banner_positions_t p=ui_banner_positions_size(longest_date,cfg->quote_px);p.quote_valign=cfg->valign;
    if(cfg->text_y!=CONTENT_BANNER_TEXT_AUTO){p.quote_top=UI_STATUS_HEIGHT+cfg->text_y;p.free_y=true;}
    char saved[TTF_FONT_PATH_MAX];strlcpy(saved,ttf_font_path(),sizeof(saved));int weight=ttf_get_weight();
    bool custom=cfg->font[0]&&ttf_font_open(cfg->font)==ESP_OK&&ttf_font_has_text(cfg->text);
    ui_banner_quote_t q;quote_layout_size(cfg->text,custom,cfg->quote_px,&q);int y=quote_top(&q,custom,p);
    if(cfg->font[0]){if(ttf_font_open(saved)!=ESP_OK)ttf_font_open_builtin();ttf_set_weight(weight);}
    return (uint16_t)(y-UI_STATUS_HEIGHT);
}
bool ui_banner_preview_config(const content_banner_config_t *cfg,uint32_t day,bool valid,uint8_t **out,bool *fits){
    *out=NULL;*fits=false;if(!content_banner_valid(cfg)||cfg->version!=3)return false;
    uint8_t *page=heap_caps_malloc(UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *image=heap_caps_malloc(UI_LOCK_WIDTH*300/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!page||!image){free(page);free(image);return false;}
    memset(page,255,UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);char date[160];ui_banner_date_line(day,valid,date,sizeof(date));
    ui_banner_positions_t p=ui_banner_positions_size(date,cfg->quote_px);p.quote_align=cfg->align;p.quote_valign=cfg->valign;
    if(cfg->text_y!=CONTENT_BANNER_TEXT_AUTO){p.quote_top=UI_STATUS_HEIGHT+cfg->text_y;p.free_y=true;}
    if(cfg->date_y!=CONTENT_BANNER_DATE_AUTO)p.date_top=UI_STATUS_HEIGHT+cfg->date_y;
    *fits=true;
    if(cfg->show_text){
        bool custom=false;
        if(cfg->font[0]){char saved[TTF_FONT_PATH_MAX];strlcpy(saved,ttf_font_path(),sizeof(saved));int weight=ttf_get_weight();
            custom=ttf_font_open(cfg->font)==ESP_OK&&ttf_font_has_text(cfg->text);
            if(custom){ui_banner_quote_draw(page,cfg->text,true,p);*fits=fits_active(cfg->text,true,p);}
            if(ttf_font_open(saved)!=ESP_OK)ttf_font_open_builtin();
            ttf_set_weight(weight);
        }
        if(!custom){ui_banner_quote_draw(page,cfg->text,false,p);*fits=fits_active(cfg->text,false,p);}
        ui_bitmap_capture(page,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300},image);
    }
    memset(page,255,UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
    if(cfg->mode&&!ui_wallpaper_draw(page,cfg->image,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300})){free(page);free(image);return false;}
    if(cfg->show_date)banner_date_draw(page,date,p,true);
    if(cfg->show_text)banner_text_overlay(page,image);
    ui_bitmap_capture(page,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300},image);
    free(page);*out=image;return true;
}
bool ui_banner_preview(const char *text,const char *path,int px,content_quote_align_t align,content_quote_valign_t valign,uint32_t day,bool valid,uint8_t **out,bool *fits){
    content_banner_config_t cfg;content_banner_load(&cfg);cfg.mode=0;cfg.show_text=1;cfg.show_date=1;cfg.text_y=CONTENT_BANNER_TEXT_AUTO;
    if(!content_home_quote_valid(text)||!content_home_quote_font_valid(path?path:"")||!content_home_quote_size_valid((unsigned)px)||!content_home_quote_alignment_valid((unsigned)align)||!content_home_quote_valign_valid((unsigned)valign)){*out=NULL;*fits=false;return false;}
    strlcpy(cfg.text,text,sizeof(cfg.text));strlcpy(cfg.font,path?path:"",sizeof(cfg.font));cfg.quote_px=px;cfg.align=align;cfg.valign=valign;
    return ui_banner_preview_config(&cfg,day,valid,out,fits);
}
