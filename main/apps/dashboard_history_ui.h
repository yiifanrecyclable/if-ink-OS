/* SPDX-License-Identifier: Apache-2.0
 * 中文：首页阅读记录子页，按日/周分页；只保留当前页数据。
 * English: Home reading history subpage with daily/weekly pagination and a bounded page buffer.
 */
#include "reading_history_store.h"
#define HOME_HISTORY_MAX_ROWS 10
static bool s_history_open,s_history_weekly;
static uint32_t s_history_first,s_history_today;
static int s_history_page,s_history_count;
static uint64_t s_history_seconds[HOME_HISTORY_MAX_ROWS];
static bool s_history_has_card,s_history_save_failed;

static int history_row_height(void) {
    int height=ui_text_line_height(UI_TEXT_BODY)+52;
    return height<92?92:height;
}
static int history_rows(void) {
    int rows=(UI_NAV_PAGER_TOP-(UI_STATUS_HEIGHT+182))/history_row_height();
    if(rows<1)rows=1;
    if(rows>HOME_HISTORY_MAX_ROWS)rows=HOME_HISTORY_MAX_ROWS;
    return rows;
}
static uint32_t history_latest(void) {
    return s_history_weekly?s_history_today-(s_history_today+3)%7:s_history_today;
}
static uint32_t history_earliest(void) {
    return s_history_weekly?s_history_first-(s_history_first+3)%7:s_history_first;
}
static int history_count(void) {
    if(!s_history_today)return 0;
    return (int)((history_latest()-history_earliest())/(s_history_weekly?7:1))+1;
}
static int history_pages(void) {int count=history_count();return count?(count+history_rows()-1)/history_rows():1;}
static uint64_t history_day_seconds(uint32_t day,uint32_t archived) {
    if(day>s_history_today)return 0;
    uint32_t recent=0;
    if(s_reading_days_valid&&day<=s_reading_end_day&&s_reading_end_day-day<30)
        recent=s_reading_days[29-(s_reading_end_day-day)];
    return archived>recent?archived:recent;
}
static void history_load_page(void) {
    int pages=history_pages();if(s_history_page>=pages)s_history_page=pages-1;if(s_history_page<0)s_history_page=0;
    memset(s_history_seconds,0,sizeof(s_history_seconds));
    int remaining=history_count()-s_history_page*history_rows();
    s_history_count=remaining<history_rows()?remaining:history_rows();if(s_history_count<0)s_history_count=0;
    if(!s_history_count)return;
    unsigned stride=s_history_weekly?7:1;
    uint32_t newest=history_latest()-(uint32_t)(s_history_page*history_rows())*stride;
    uint32_t oldest=newest-(uint32_t)(s_history_count-1)*stride;
    uint32_t days[HOME_HISTORY_MAX_ROWS*7];
    reading_history_range(oldest,(unsigned)s_history_count*stride,days);
    for(int row=0;row<s_history_count;++row){
        uint32_t day=newest-(uint32_t)row*stride;
        for(unsigned d=0;d<stride;++d)s_history_seconds[row]+=history_day_seconds(day+d,days[day+d-oldest]);
    }

}
static void history_show(void) {
    s_history_save_failed=!book_ticket_archive_days();
    s_history_today=s_reading_days_valid?s_reading_end_day:0;
    s_history_first=reading_history_first(s_history_today);
    for(unsigned i=0;i<30&&s_reading_days_valid;++i)if(s_reading_days[i]){
        uint32_t day=s_reading_end_day-(29-i);
        if(!s_history_first||day<s_history_first)s_history_first=day;
    }
    if(!s_history_first)s_history_first=s_history_today;
    read_pico_sd_info_t sd={0};s_history_has_card=read_pico_sd_get_info(&sd)==ESP_OK&&sd.mounted;
    s_history_open=true;s_history_weekly=false;s_history_page=0;history_load_page();
}
static EpdRect history_tab(int index) {return (EpdRect){164+index*178,UI_STATUS_HEIGHT+86,178,60};}
static void history_duration(char *out,size_t cap,uint64_t seconds) {
    uint64_t minutes=seconds/60;
    if(s_history_weekly)snprintf(out,cap,"%llu小时%llu分",(unsigned long long)(minutes/60),(unsigned long long)(minutes%60));
    else snprintf(out,cap,"%llu分钟",(unsigned long long)minutes);

}
static void history_date(char *out,size_t cap,uint32_t day,bool weekly) {
    time_t stamp=(time_t)((uint64_t)day*86400);struct tm first;gmtime_r(&stamp,&first);
    if(weekly){
        stamp=(time_t)((uint64_t)(day+6)*86400);struct tm last;gmtime_r(&stamp,&last);
        if(first.tm_year!=last.tm_year)snprintf(out,cap,"%04d/%02d/%02d–%04d/%02d/%02d",first.tm_year+1900,first.tm_mon+1,first.tm_mday,last.tm_year+1900,last.tm_mon+1,last.tm_mday);
        else snprintf(out,cap,"%04d/%02d/%02d–%02d/%02d",first.tm_year+1900,first.tm_mon+1,first.tm_mday,last.tm_mon+1,last.tm_mday);
    }else{
        static const char *weekdays[]={"周一","周二","周三","周四","周五","周六","周日"};
        snprintf(out,cap,"%04d-%02d-%02d %s",first.tm_year+1900,first.tm_mon+1,first.tm_mday,weekdays[(day+3)%7]);
    }
}
static void history_render(uint8_t *fb) {
    ui_clear_page(fb);ui_nav_status(fb);
    ui_nav_back(fb,36,UI_STATUS_HEIGHT+2);
    ui_text_vc(fb,342,UI_STATUS_HEIGHT+38,UI_TEXT_TITLE,"阅读记录",EPD_DRAW_ALIGN_CENTER,false);
    for(int i=0;i<2;++i){
        EpdRect tab=history_tab(i);bool active=s_history_weekly==(i==1);
        ui_text_vc(fb,tab.x+tab.width/2,tab.y+tab.height/2,UI_TEXT_BODY,i?"按周":"按日",EPD_DRAW_ALIGN_CENTER,false);
        if(active)epd_fill_rect((EpdRect){tab.x+40,tab.y+tab.height-4,tab.width-80,3},0x44,fb);
    }
    epd_fill_rect((EpdRect){36,UI_STATUS_HEIGHT+158,612,2},UI_GRAY_SECTION_LINE,fb);
    uint64_t maximum=0;for(int i=0;i<s_history_count;++i)if(s_history_seconds[i]>maximum)maximum=s_history_seconds[i];
    for(int row=0;row<s_history_count;++row){
        int top=UI_STATUS_HEIGHT+182+row*history_row_height();
        uint32_t day=history_latest()-(uint32_t)(s_history_page*history_rows()+row)*(s_history_weekly?7:1);
        char date[80],duration[64];history_date(date,sizeof(date),day,s_history_weekly);history_duration(duration,sizeof(duration),s_history_seconds[row]);
        int duration_px=ui_text_effective_px(UI_TEXT_BODY),duration_w=ui_text_fixed_width_px(duration_px,duration);
        int date_px=ui_text_fit_px(UI_TEXT_BODY,date,612-duration_w-24,ui_text_line_height(UI_TEXT_BODY));
        ui_text_fixed_vc(fb,36,top+ui_text_line_height(UI_TEXT_BODY)/2,date_px,date,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_fixed_vc(fb,648,top+ui_text_line_height(UI_TEXT_BODY)/2,duration_px,duration,EPD_DRAW_ALIGN_RIGHT,false);
        EpdRect bar={36,top+ui_text_line_height(UI_TEXT_BODY)+12,612,12};ui_fill_round_rect(fb,bar,6,0xee);
        if(s_history_seconds[row]&&maximum){bar.width=(int)((s_history_seconds[row]*612+maximum-1)/maximum);ui_fill_round_rect(fb,bar,6,0x44);}
        epd_fill_rect((EpdRect){36,top+history_row_height()-12,612,1},UI_GRAY_ITEM_LINE,fb);
    }
    if(!s_history_today)ui_text_vc(fb,342,440,UI_TEXT_BODY,"请先设置日期与时间",EPD_DRAW_ALIGN_CENTER,false);
    if(!s_history_has_card)ui_text_vc(fb,342,UI_NAV_PAGER_TOP-24,UI_TEXT_CAPTION,"未挂载TF卡，仅显示近期记录",EPD_DRAW_ALIGN_CENTER,false);
    else if(s_history_save_failed)ui_text_vc(fb,342,UI_NAV_PAGER_TOP-24,UI_TEXT_CAPTION,"TF卡记录保存失败，近期记录仍保留",EPD_DRAW_ALIGN_CENTER,false);
    ui_nav_draw_pager(fb,history_count(),s_history_weekly?"周":"天",s_history_page,history_pages());ui_nav_draw(fb,0);
}
static app_redraw_t history_turn(int direction) {
    int next=s_history_page+direction;if(next<0||next>=history_pages())return APP_REDRAW_NONE;
    s_history_page=next;history_load_page();return APP_REDRAW_PAGE;
}
static app_redraw_t history_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int tab=ui_nav_hit(ev->x0,ev->y0);if(tab>=0){
        if(!tab){s_history_open=false;return APP_REDRAW_PAGE;}
        ui_nav_request(ctx,tab);return APP_REDRAW_NONE;
    }
    if(ev->x0<104&&ev->y0>=UI_STATUS_HEIGHT&&ev->y0<UI_STATUS_HEIGHT+80){s_history_open=false;return APP_REDRAW_PAGE;}
    for(int i=0;i<2;++i)if(ui_rect_hit(history_tab(i),ev->x0,ev->y0)){
        if(s_history_weekly==(i==1))return APP_REDRAW_NONE;
        s_history_weekly=i==1;s_history_page=0;history_load_page();return APP_REDRAW_PAGE;
    }
    for(int direction=-1;direction<=1;direction+=2)if(ui_rect_hit(ui_nav_pager_rect(direction),ev->x0,ev->y0))return history_turn(direction);
    return APP_REDRAW_NONE;
}
