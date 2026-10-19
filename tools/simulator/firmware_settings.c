/* SPDX-License-Identifier: Apache-2.0
 * 中文：直接编译正式设置页；宿主禁止修改系统时钟。
 * English: Compile production settings UI; never change the host clock.
 */
#include <sys/time.h>
static int preview_settimeofday(const struct timeval *t,const void *zone){(void)t;(void)zone;return 0;}
#define settimeofday preview_settimeofday
#define app_device_settings simulator_descriptor_settings
#include "../../main/apps/app_device_settings.c"
#include "replay.h"
extern int simulator_route;
static app_redraw_t preview_settings_gesture(app_ctx_t *ctx,const ui_gesture_event_t *event){
 if(s_page==SETTINGS_ONLINE_UPDATE)(void)on_tick(ctx);
 app_redraw_t result=on_gesture(ctx,event);
 if(s_page==SETTINGS_ONLINE_UPDATE)(void)on_tick(ctx);
 return result;
}
int simulator_settings(uint8_t *fb,int page,int scroll,const char *events){
 if(page==SETTINGS_BANNER_BROWSER)page=SETTINGS_BANNER_IMAGE;
 if(page==6)page=5;if(page==8)page=9;
 bool quote=page==23;if(quote)app_device_settings_request_home_quote();
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);if(quote)home_quote_editor_open();
 if(page==SETTINGS_BANNER_IMAGE||page==SETTINGS_BANNER_BROWSER)banner_picker_open();
 if(page==SETTINGS_SYSTEM_FONT||page==SETTINGS_QUOTE_FONT)ttf_font_scan();
 if(page==SETTINGS_WALLPAPER_FIXED||page==SETTINGS_WALLPAPER_PREVIEW){wallpaper_scan();s_wallpaper_selected=s_wallpaper_count?0:-1;}
 if(page==SETTINGS_WALLPAPER_FOLDER)wallpaper_folder_open();
 if(page==SETTINGS_TEXT_EDIT)profile_editor_open(true);
 s_page=quote?SETTINGS_TEXT_EDIT:(page==SETTINGS_RETIRED_4||page==SETTINGS_RETIRED_7||page==SETTINGS_RETIRED_15||page==SETTINGS_RETIRED_16||page==SETTINGS_RETIRED_17||page==SETTINGS_RETIRED_18)?SETTINGS_MAIN:(settings_page_t)page;s_main_scroll=scroll;
 if(s_page==SETTINGS_HOME_QUOTE)quote_preview_prepare(s_quote_px,false);
 s_year=2026;s_month=10;s_day=5;s_hour=12;s_minute=0;
 if(s_page==SETTINGS_ONLINE_UPDATE)(void)if_update_check();
 simulator_replay(&ctx,preview_settings_gesture,on_key,events);render(&ctx,fb);
 if(ctx.request_app){extern const app_desc_t app_transfer;simulator_route=ctx.request_app==&app_transfer?5:app_index_of(ctx.request_app);}
 return s_home_quote_editor&&s_page==SETTINGS_TEXT_EDIT?23:s_page;
}

int simulator_settings_geometry(int item) {
 // 第四项保留为0以稳定预览元数据索引。/ Reserve index 3 as zero to keep preview metadata positions stable.
 const int values[]={SETTINGS_WIRELESS_Y,SETTINGS_DISPLAY_Y,SETTINGS_DEVICE_Y,0,SETTINGS_ROW_H,SETTINGS_SCROLL_MAX,font_list_top(),font_list_step(),font_list_rows(),s_cloud_scroll,cloud_scroll_limit(),cloud_row_y(0),cloud_row_y(1),cloud_row_y(2)};
 return item>=0 && item<(int)(sizeof(values)/sizeof(values[0]))?values[item]:0;
}

int simulator_settings_update_state(void){if_update_status_t status;if_update_get_status(&status);return status.state;}
int simulator_settings_update_confirmation(void){return s_online_confirm;}

int simulator_banner_draft(unsigned part){if(part==12)return ui_banner_text_y(&s_banner_draft);const int values[]={s_banner_draft.mode,s_banner_draft.show_date,s_quote_px,s_banner_draft.align,s_banner_draft.valign,banner_date_y(),s_banner_image_count,s_banner_image_total,font_list_step(),s_banner_image_page,banner_image_rows(),s_banner_draft.show_text};return part<12?values[part]:0;}
const char *simulator_banner_text(void){return s_banner_draft.text;}
const char *simulator_banner_font(void){return s_banner_draft.font;}
const char *simulator_banner_image(void){return s_banner_draft.image;}

const char *simulator_banner_item(unsigned i){return i<(unsigned)s_banner_image_count?s_banner_items[i].path:"";}
