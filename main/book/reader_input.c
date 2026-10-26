/* SPDX-License-Identifier: Apache-2.0
 * 快捷设置原子保存、逐内容引导标记及双指收尾。/ Atomic shortcut preferences, per-content help flags and multi-contact completion.
 */
#include "ui/ui_i18n.h"

#include "reader_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"

static reader_input_preferences_t s_prefs = {.version=1};
static bool s_loaded;
const char *reader_shortcut_name(unsigned slot) {
    static const char *const names[]={UI_MSG("单指向左滑动"),UI_MSG("单指向右滑动"),UI_MSG("单指向上滑动"),UI_MSG("单指向下滑动"),UI_MSG("长按正文"),UI_MSG("双指轻点"),UI_MSG("双指捏合"),UI_MSG("双指张开")};
    return slot<READER_SHORTCUT_COUNT?ui_message(names[slot]):"";
}
const char *reader_action_name(unsigned action) {
    static const char *const names[]={UI_MSG("不启用"),UI_MSG("上一页"),UI_MSG("下一页"),UI_MSG("打开目录"),UI_MSG("书签列表"),UI_MSG("阅读面板"),UI_MSG("切换阅读布局"),UI_MSG("缩小字号"),UI_MSG("放大字号"),UI_MSG("添加或移除书签"),UI_MSG("立刻刷新")};
    return action<READER_ACTION_COUNT?ui_message(names[action]):"";
}
bool reader_input_preferences_valid(const reader_input_preferences_t *p) {
    if(!p||(p->version!=1&&p->version!=2)||p->hide_help>1)return false;
    unsigned count=p->version==1?READER_STORED_SHORTCUT_COUNT:READER_SHORTCUT_COUNT;
    for(unsigned i=0;i<count;++i)if(p->actions[i]>=READER_ACTION_COUNT)return false;
    if(p->version==2){if(p->actions[8]>READER_TAP_ROWS)return false;for(int i=9;i<12;++i)if(p->actions[i])return false;}
    return true;
}
// 兼容旧配置：同一功能只保留最先设置的手势。/ Normalize legacy duplicate actions to their first slot.
static void unique_actions(reader_input_preferences_t *p){
    for(unsigned i=p->version==2?9:READER_SHORTCUT_COUNT;i<READER_STORED_SHORTCUT_COUNT;++i)p->actions[i]=0;
    unsigned seen=0;
    for(unsigned i=0;i<READER_SHORTCUT_COUNT;++i){unsigned a=p->actions[i];if(!a)continue;if(seen&(1u<<a))p->actions[i]=0;else seen|=1u<<a;}
}
static void load(void) {
    if(s_loaded)return;
    nvs_handle_t h;
    if(nvs_open("rp_input",NVS_READONLY,&h)==ESP_OK){
        reader_input_preferences_t p;size_t size=sizeof(p);
        if(nvs_get_blob(h,"preferences",&p,&size)==ESP_OK&&size==sizeof(p)&&reader_input_preferences_valid(&p)){unique_actions(&p);s_prefs=p;}
        nvs_close(h);
    }
    s_loaded=true;
}
void reader_input_export(reader_input_preferences_t *p){load();if(p)*p=s_prefs;}
esp_err_t reader_input_import(const reader_input_preferences_t *p){
    if(!reader_input_preferences_valid(p))return ESP_ERR_INVALID_ARG;
    reader_input_preferences_t normalized=*p;unique_actions(&normalized);p=&normalized;
    nvs_handle_t h;esp_err_t err=nvs_open("rp_input",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    err=nvs_set_blob(h,"preferences",p,sizeof(*p));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);
    if(err==ESP_OK){s_prefs=*p;s_loaded=true;}return err;
}
reader_action_t reader_input_action(unsigned slot){load();return slot<READER_SHORTCUT_COUNT?(reader_action_t)s_prefs.actions[slot]:READER_ACTION_NONE;}
reader_tap_layout_t reader_input_tap_layout(void){load();return s_prefs.version==2?(reader_tap_layout_t)s_prefs.actions[8]:READER_TAP_COLUMNS;}
esp_err_t reader_input_set_tap_layout(reader_tap_layout_t layout){
    if(layout!=READER_TAP_COLUMNS&&layout!=READER_TAP_ROWS)return ESP_ERR_INVALID_ARG;
    reader_input_preferences_t p;reader_input_export(&p);p.version=2;p.actions[8]=(uint8_t)layout;
    return reader_input_import(&p);
}
reader_tap_region_t reader_input_tap_region(reader_tap_layout_t layout,unsigned zone,int width,int height){
    if(zone>=3||width<=0||height<=0)return (reader_tap_region_t){0};
    if(layout==READER_TAP_ROWS){const int ends[]={0,30,50,100};int y=height*ends[zone]/100;return (reader_tap_region_t){0,y,width,height*ends[zone+1]/100-y};}
    int x=width*(int)zone/3;return (reader_tap_region_t){x,0,width*(int)(zone+1)/3-x,height};
}
esp_err_t reader_input_assign(unsigned slot,unsigned action){
    if(slot>=READER_SHORTCUT_COUNT||action>=READER_ACTION_COUNT)return ESP_ERR_INVALID_ARG;
    reader_input_preferences_t p;reader_input_export(&p);
    for(unsigned i=0;action&&i<READER_SHORTCUT_COUNT;++i)if(i!=slot&&p.actions[i]==action)return ESP_ERR_INVALID_STATE;
    p.actions[slot]=(uint8_t)action;return reader_input_import(&p);
}
static void help_key(const char *path,char key[16]){
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*UINT64_C(1099511628211);
    ui_snprintf(key,16,"h%014llx",(unsigned long long)(hash&UINT64_C(0xffffffffffffff)));
}
bool reader_input_needs_help(const char *path){
    load();if(s_prefs.hide_help||!path||!*path)return false;
    nvs_handle_t h;uint8_t seen=0;char key[16];help_key(path,key);
    if(nvs_open("rp_input",NVS_READONLY,&h)==ESP_OK){(void)nvs_get_u8(h,key,&seen);nvs_close(h);}
    return seen!=1;
}
esp_err_t reader_input_ack_help(const char *path,bool hide_all){
    if(!path||!*path)return ESP_ERR_INVALID_ARG;
    reader_input_preferences_t p;reader_input_export(&p);p.hide_help=hide_all?1:0;
    nvs_handle_t h;esp_err_t err=nvs_open("rp_input",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    char key[16];help_key(path,key);err=nvs_set_u8(h,key,1);
    if(err==ESP_OK)err=nvs_set_blob(h,"preferences",&p,sizeof(p));
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK)s_prefs=p;
    return err;
}
static int span(int dx,int dy){
    uint64_t value=(int64_t)dx*dx+(int64_t)dy*dy;unsigned low=0,high=131072;
    while(high-low>1){unsigned mid=(low+high)/2;if((uint64_t)mid*mid<=value)low=mid;else high=mid;}
    return (int)low;
}
int reader_multi_feed(reader_multi_t *s,bool enabled,bool down,unsigned count,int ax,int ay,int bx,int by,int64_t now){
    if(!s)return -1;
    if(!enabled||count>2||(down&&count==0)||now<0||(s->active&&now<s->started)){
        *s=(reader_multi_t){.blocked=down};return -1;
    }
    if(s->blocked){if(!down)*s=(reader_multi_t){0};return -1;}
    if(!s->active){
        if(!down||count!=2)return -1;
        int distance=span(ax-bx,ay-by);
        if(distance<40){s->blocked=true;return -1;}
        *s=(reader_multi_t){.active=true,.x0=(ax+bx)/2,.y0=(ay+by)/2,.x=(ax+bx)/2,.y=(ay+by)/2,.span0=distance,.span=distance,.started=now};return -1;
    }
    if(down){
        if(count==2){s->x=(ax+bx)/2;s->y=(ay+by)/2;s->span=span(ax-bx,ay-by);int m=span(s->x-s->x0,s->y-s->y0);if(m>s->movement)s->movement=m;}
        return -1;
    }
    int result=-1,change=s->span-s->span0;
    if(now-s->started<=5000){
        if(abs(change)>=60&&abs(change)*100>=s->span0*20)result=change>0?READER_PINCH_OUT:READER_PINCH_IN;
        else if(s->movement<=24&&abs(change)<=24&&now-s->started<=600)result=READER_TWO_TAP;
    }
    *s=(reader_multi_t){0};return result;
}
