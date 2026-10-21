/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * Modified derivative: compact root layout and move firmware upgrade here.
 * 自定义固件修改：精简根页标题并承接固件升级；移除书架样式与对比度设置，显示分组收敛为四行。
 * IF change: Retire shelf styles and contrast; keep four display rows.
 *
 * 中文：分组设置页；网络短时对时后 PMU 持续走时，开机恢复系统时钟。
 * English: Grouped settings; temporary WiFi sync seeds the PMU, whose RTC restores time at boot.
 * 用户修订：设置列表滑动仅差分刷新内容，滑动过程中不周期插入黑白清屏。
 * User revision: settings lists scroll with content-only differentials and no periodic black/white wipe during swipes.
 */
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "app_cloud.h"
#include "cloud_sync.h"
#include "app_content_open.h"
#include "app_registry.h"
#include "app_transfer_mode.h"
#include "settings.h"
#include "time_service.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "esp_log.h"
#include "pmu_selftest.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_nav.h"
#include "ui_wallpaper.h"
#include "ui_wallpaper_rotation.h"
#include "read_pico_search.h"
#include "book_store.h"
#include "book_ticket.h"
#include "content_library.h"
#include "ui_banner.h"
#include "ui_bitmap.h"
#include "battery_history.h"
#include "app_content_open.h"
#include "ota_update.h"
#include "ota_online.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "pmu_selftest.h"
#include "soc/rtc_cntl_reg.h"

static void fit_value(char *value, int width);
static char s_notice[96];
typedef enum { SETTINGS_MAIN, SETTINGS_WIFI, SETTINGS_TIME,
               SETTINGS_TIME_EDIT, SETTINGS_RETIRED_4, SETTINGS_SYSTEM_FONT,
               SETTINGS_RETIRED_6, SETTINGS_RETIRED_7, SETTINGS_RETIRED_8,
               SETTINGS_WALLPAPER, SETTINGS_WALLPAPER_PREVIEW,
               SETTINGS_CONFIG, SETTINGS_UPGRADE, SETTINGS_BOOT,
               SETTINGS_POWER_SLEEP, SETTINGS_RETIRED_15, SETTINGS_RETIRED_16,
               SETTINGS_RETIRED_17, SETTINGS_RETIRED_18, SETTINGS_TEXT_EDIT, SETTINGS_CLOUD, SETTINGS_HOME_QUOTE, SETTINGS_QUOTE_FONT, SETTINGS_BATTERY = 24, SETTINGS_STATUS_DATE_FORMAT, SETTINGS_IDLE_SLEEP, SETTINGS_FIRMWARE_INFO, SETTINGS_ONLINE_UPDATE, SETTINGS_WALLPAPER_FOLDER, SETTINGS_WALLPAPER_FIXED, SETTINGS_BANNER_IMAGE, SETTINGS_BANNER_BROWSER } settings_page_t;
static settings_page_t s_page;
static int s_main_scroll, s_font_page, s_wallpaper_page, s_cloud_scroll;
static bool s_battery_week;
static int s_battery_percent = -2;
static bool s_battery_charging;
static uint32_t s_battery_at;
static const char *const size_names[] = {"小", "标准", "大", "特大"};
static const uint8_t size_values[] = {100, 120, 140, 160};
static const char *const status_date_labels[]={"2026-10-11","2026/10/11","2026.10.11","10-11","10/11"};
static int status_date_step(void){return ui_list_row_height(false)+12;}
static EpdRect status_date_rect(int i){return (EpdRect){36,UI_PAGE_Y(300+i*status_date_step()),612,status_date_step()-12};}
static int status_display_row_y(int i){return 702+i*(ui_list_row_height(false)+12);}
static const uint8_t idle_sleep_minutes[]={0,1,3,5,10,30};
static const char *const idle_sleep_labels[]={"关闭","1分钟","3分钟","5分钟","10分钟","30分钟"};
static const char *idle_sleep_label(void){
    for(int i=0;i<6;++i)if(idle_sleep_minutes[i]==app_settings_idle_sleep_minutes())return idle_sleep_labels[i];
    return "5分钟";
}
static int system_size_index(void) {
    for (int i = 0; i < 4; ++i) if (app_settings_system_font_size() == size_values[i]) return i;
    return 1;
}
static EpdRect system_size_rect(int i) { return (EpdRect){58 + 156 * i,UI_PAGE_Y(250), 100, 100}; }
static int font_list_top(void) { return s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_QUOTE_FONT ? UI_PAGE_Y(446) : UI_PAGE_Y(292); }
static int font_list_step(void) { return ui_list_row_height(false)+12; }
static int font_list_rows(void) { return ui_list_visible_rows(font_list_top(),1028,font_list_step()); }
static EpdRect font_list_rect(int slot) { return (EpdRect){36,font_list_top()+slot*font_list_step(),612,font_list_step()-12}; }
static int wallpaper_rows(void) { return ui_list_visible_rows(UI_PAGE_Y(292),1028,font_list_step()); }
static EpdRect wallpaper_rect(int slot) { return (EpdRect){36,UI_PAGE_Y(292+slot*font_list_step()),612,font_list_step()-12}; }

static bool s_scroll_drag_consumed, s_scroll_present_pending;
static const char *const cloud_guide[]={
            "同步文件夹填写坚果云根目录下已存在的文件夹",
            "账号为坚果云账号",
            "应用密码不是账户密码，在 设置-第三方应用管理 中添加生成",
            "支持md/txt/epub、PNG/JPEG图片及TTF字体，自动分类保存",
            "建议使用Agent管理电脑端的该文件夹",
            "obsidian端一键推送插件可联系作者获取",
            "设置页「保存与恢复」功能会将本页设置一并备份"
        };
static bool s_sync_pending;
static bool s_config_confirm;
static bool s_boot_pending;
static bool s_upgrade_confirm, s_local_pending, s_upgrade_restart;
static pico_ota_info_t s_local_update;
static bool s_online_pending, s_online_confirm;
static uint32_t s_online_seen;
static int64_t s_online_drawn_at, s_online_reboot_at;
static char s_upgrade_notice[96];
static void upgrade_open(void) {
    (void)pico_ota_inspect(PICO_OTA_UPDATE_PATH, &s_local_update);
    s_upgrade_confirm = s_local_pending = s_upgrade_restart = false;
    s_upgrade_notice[0] = 0;
    s_page = SETTINGS_UPGRADE;
}
static const char *const TAG = "device_settings";
#define SETTINGS_ROW_H (ui_list_row_height(false))
#define SETTINGS_GROUP_GAP (ui_text_line_height(UI_TEXT_TITLE)+36)
#define SETTINGS_WIRELESS_ROWS 2
#define SETTINGS_DISPLAY_ROWS 5
#define SETTINGS_DEVICE_ROWS 6
#define SETTINGS_INFO_H 64
#define SETTINGS_WIRELESS_Y (UI_STATUS_HEIGHT+4+SETTINGS_INFO_H+SETTINGS_GROUP_GAP)
// 分组布局从上一组底边推导，绘制与命中共用坐标。/ Derive groups from the previous bottom; drawing and hits share coordinates.
#define SETTINGS_DISPLAY_Y (SETTINGS_WIRELESS_Y + SETTINGS_WIRELESS_ROWS * SETTINGS_ROW_H + SETTINGS_GROUP_GAP)
#define SETTINGS_DEVICE_Y (SETTINGS_DISPLAY_Y + SETTINGS_DISPLAY_ROWS * SETTINGS_ROW_H + SETTINGS_GROUP_GAP)
#define SETTINGS_ICON_PX 32
#define SETTINGS_ICON_INK 0x58
#define SETTINGS_SCROLL_MAX (SETTINGS_DEVICE_Y + SETTINGS_DEVICE_ROWS * SETTINGS_ROW_H - UI_NAV_TOP + 12)
static char s_editor[257], s_editor_notice[80];
static ui_text_input_t s_text_input;
static bool s_home_quote_editor, s_home_quote_requested;
void app_device_settings_request_home_quote(void){s_home_quote_requested=true;}
bool app_device_settings_home_quote_requested(void){return s_home_quote_requested;}
static int s_cloud_editor;
static bool s_cloud_requested;
void app_device_settings_request_cloud_config(void){s_cloud_requested=true;}
bool app_device_settings_cloud_requested(void){return s_cloud_requested;}
static uint8_t *s_quote_preview;
static content_banner_config_t s_banner_draft;
static int s_quote_px;
static bool s_quote_dragging,s_quote_preview_attempted,s_quote_committed;
bool app_device_settings_quote_committed(void){return s_quote_committed;}
int app_device_settings_quote_size(void){return s_quote_px?s_quote_px:content_home_quote_size();}
static void quote_preview_clear(void){free(s_quote_preview);s_quote_preview=NULL;s_quote_preview_attempted=false;}
// 草稿只在最终保存时写NVS；字体栅格化和图片解码只在进入/松手/确认时执行。/ Only final Save writes NVS; rasterize/decode on entry, release or confirmation.
static bool banner_preview_apply(const content_banner_config_t *next,bool require_fit){
    uint32_t days[30],day=0;bool valid=book_ticket_recent_days(days,&day);s_quote_preview_attempted=true;
#ifdef PICO_HOST_SIMULATOR
    const char *sample=getenv("PICO_SIM_HOME_DAY");if(sample){unsigned long d=strtoul(sample,NULL,10);if(d>=19723&&d<50000){day=(uint32_t)d;valid=true;}}
#endif
    uint8_t *image=NULL;bool fits=false;
    if(!ui_banner_preview_config(next,day,valid,&image,&fits)){
        strlcpy(s_notice,next->mode?"图片无法读取，请重新选择":"预览暂不可用，请重试",sizeof(s_notice));return false;}
    if(require_fit&&!fits){free(image);strlcpy(s_notice,"文字超过四行或高度，请缩小字号或缩短文字",sizeof(s_notice));return false;}
    s_banner_draft=*next;free(s_quote_preview);s_quote_preview=image;s_quote_px=next->quote_px;
    strlcpy(s_notice,fits?"":"当前文字超出四行或高度，请修改文字",sizeof(s_notice));return true;
}
static bool quote_preview_prepare(int px,bool validate){content_banner_config_t next=s_banner_draft;next.quote_px=px;return banner_preview_apply(&next,validate);}
#define BANNER_TEXT_ROW_TOP 764
#define BANNER_TEXT_ROW_H 72
#define BANNER_FOOTER_TRACKING 14
static int banner_text_row_center(int i){return BANNER_TEXT_ROW_TOP+i*BANNER_TEXT_ROW_H+BANNER_TEXT_ROW_H/2;}
static EpdRect quote_alignment_rect(int i){return (EpdRect){368+72*i,banner_text_row_center(2)-28,64,56};}
static EpdRect banner_footer(int i){
    int width=ui_text_fixed_width_px(ui_text_effective_px(UI_TEXT_BODY),i?"保存":"取消")+BANNER_FOOTER_TRACKING+80;
    if(width<156)width=156;
    return (EpdRect){(i?466:218)-width/2,1070,width,56};
}
static int banner_date_y(void){return s_banner_draft.date_y==CONTENT_BANNER_DATE_AUTO?ui_banner_default_date_y(s_banner_draft.quote_px):s_banner_draft.date_y;}
static void quote_alignment_draw(uint8_t *fb,int option,bool selected){
    EpdRect r=quote_alignment_rect(option);
    if(selected)epd_fill_rect(r,UI_NAV_ACTIVE_GRAY,fb);
    else epd_draw_rect(r,0xbb,fb);
    int cx=r.x+r.width/2,cy=r.y+r.height/2;
    static const int lengths[]={40,24,32};
    for(int row=0;row<3;++row){
        int width=option==CONTENT_QUOTE_JUSTIFY?40:lengths[row];
        int x=option==CONTENT_QUOTE_LEFT?cx-20:option==CONTENT_QUOTE_RIGHT?cx+20-width:cx-width/2;
        epd_fill_rect((EpdRect){x,cy-16+row*14,width,4},selected?UI_GRAY_WHITE:0x38,fb);
    }
}
static int quote_slider_size(int x){
    if(x<72)x=72;
    if(x>612)x=612;
    return CONTENT_QUOTE_SIZE_MIN+2*(((x-72)*36+270)/540);
}
static app_redraw_t quote_slider_gesture(const ui_gesture_event_t *ev,bool *handled){
    bool hit=s_page==SETTINGS_QUOTE_FONT&&ui_rect_hit((EpdRect){36,268,612,64},ev->x0,ev->y0);
    *handled=s_quote_dragging||hit;if(!*handled)return APP_REDRAW_NONE;
    if(ev->type==UI_GESTURE_CANCEL){s_quote_dragging=false;s_quote_px=s_banner_draft.quote_px;return APP_REDRAW_AREA;}
    if(ev->type==UI_GESTURE_PRESS)s_quote_dragging=true;
    if(ev->type==UI_GESTURE_PRESS||ev->type==UI_GESTURE_MOVE){s_quote_px=quote_slider_size(ev->x);return APP_REDRAW_AREA;}
    if(ev->type==UI_GESTURE_LONG_PRESS)return APP_REDRAW_NONE;
    bool release=ev->type==UI_GESTURE_TAP||ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R||ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D;
    if(!release)return APP_REDRAW_NONE;
    s_quote_dragging=false;content_banner_config_t next=s_banner_draft;next.quote_px=quote_slider_size(ev->x);
    if(!banner_preview_apply(&next,true))s_quote_px=s_banner_draft.quote_px;
    return APP_REDRAW_PAGE;
}

static const char *system_font_label(const char *path) {
    if (!path || !path[0]) return "思源黑体";
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    static char stem[TTF_FONT_NAME_MAX];
    snprintf(stem, sizeof(stem), "%s", name);
    size_t len = strlen(stem);
    if (len >= 4 && !strcasecmp(stem + len - 4, ".ttf")) stem[len - 4] = 0;
    if (!strcasecmp(stem, "Hei")) return "思源黑体（TF 卡）";
    return ttf_font_localized_name(stem);
}
static int s_year, s_month, s_day, s_hour, s_minute;
#define WALLPAPER_MAX 64
typedef struct { char path[288]; char name[96]; } wallpaper_item_t;
// 壁纸列表放 PSRAM，理由同上。
// The wallpaper list lives in PSRAM for the same reason.
static wallpaper_item_t *s_wallpapers;

static bool wallpapers_alloc(void) {
    if (!s_wallpapers)
        s_wallpapers = heap_caps_calloc(WALLPAPER_MAX, sizeof(wallpaper_item_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_wallpapers != NULL;
}
static int s_wallpaper_count;
static int s_wallpaper_selected;
static bool s_wallpaper_confirm, s_wallpaper_preview_ok;

static void wallpaper_scan_dir(const char *root) {
    if (!wallpapers_alloc()) return;
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while (s_wallpaper_count < WALLPAPER_MAX && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strncmp(entry->d_name, "._", 2)) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))) continue;
        wallpaper_item_t *item = &s_wallpapers[s_wallpaper_count];
        if (snprintf(item->path, sizeof(item->path), "%s/%s", root, entry->d_name) >= sizeof(item->path)) continue;
        struct stat st;
        if (stat(item->path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 2 * 1024 * 1024) continue;
        size_t name_len = strnlen(entry->d_name, sizeof(item->name) - 1);
        memcpy(item->name, entry->d_name, name_len);
        item->name[name_len] = 0;
        ++s_wallpaper_count;
    }
    closedir(dir);
}
static void wallpaper_scan(void) {
    s_wallpaper_count = s_wallpaper_page = 0;
    wallpaper_scan_dir("/sdcard/pictures");
    wallpaper_scan_dir("/sdcard/images");
    wallpaper_scan_dir("/sdcard");
}

static char s_wallpaper_directory[288]="/sdcard";
static int wallpaper_folder_rows(void){return ui_list_visible_rows(UI_PAGE_Y(416),1028,font_list_step());}
static void wallpaper_folder_scan(void){
    s_wallpaper_count=s_wallpaper_page=0;
    if(!wallpapers_alloc())return;
    DIR *dir=opendir(s_wallpaper_directory);if(!dir)return;
    struct dirent *entry;
    while(s_wallpaper_count<WALLPAPER_MAX&&(entry=readdir(dir))){
        if(entry->d_name[0]=='.')continue;
        wallpaper_item_t *item=&s_wallpapers[s_wallpaper_count];
        if(snprintf(item->path,sizeof(item->path),"%s/%s",s_wallpaper_directory,entry->d_name)>=sizeof(item->path))continue;
        struct stat st;if(stat(item->path,&st)||!S_ISDIR(st.st_mode))continue;
        snprintf(item->name,sizeof(item->name),"%.*s",(int)sizeof(item->name)-1,entry->d_name);++s_wallpaper_count;
    }
    closedir(dir);
}
static void wallpaper_folder_open(void){
    snprintf(s_wallpaper_directory,sizeof(s_wallpaper_directory),"%s",app_settings_wallpaper_rotating()?app_settings_wallpaper_path():"/sdcard");
    size_t n=strlen(s_wallpaper_directory);if(n>7&&s_wallpaper_directory[n-1]=='/')s_wallpaper_directory[n-1]=0;
    wallpaper_folder_scan();s_notice[0]=0;s_page=SETTINGS_WALLPAPER_FOLDER;
}

static void profile_editor_open(bool signature) {
    s_home_quote_editor=false;
    s_cloud_editor=0; s_editor_notice[0]=0;
    snprintf(s_editor,sizeof(s_editor),"%s",signature?app_settings_status_signature():"");
    ui_text_input_open(&s_text_input,s_editor,96,true,false,UI_INPUT_CHINESE);
    s_page=SETTINGS_TEXT_EDIT;
}
static void home_quote_editor_open(void) {
    profile_editor_open(false); s_home_quote_editor=true;
    strlcpy(s_editor,s_banner_draft.text,sizeof(s_editor));
    ui_text_input_open(&s_text_input,s_editor,CONTENT_HOME_QUOTE_BYTES,true,false,UI_INPUT_CHINESE);s_text_input.max_lines=4;
}
static settings_page_t editor_return_page(void){return s_cloud_editor?SETTINGS_CLOUD:SETTINGS_MAIN;}
static void cloud_editor_open(int field) {
    profile_editor_open(false); s_cloud_editor=field; cloud_config_t cfg; cloud_config_load(&cfg);
    snprintf(s_editor,sizeof(s_editor),"%s",field==1?cfg.folder:field==2?cfg.account:"");
    memset(cfg.password,0,sizeof(cfg.password));
    ui_text_input_open(&s_text_input,s_editor,field==1?241:field==2?129:257,
        field==1,field==3,field==1?UI_INPUT_CHINESE:UI_INPUT_ENGLISH);
}

static int days_in_month(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return days[month - 1];
}

// 公历日期转 UTC 天数；界面统一按北京时间 UTC+8 编辑。
// Convert a Gregorian date to UTC days; the editor uses Beijing time (UTC+8).
static int64_t civil_days(int year, int month, int day) {
    year -= month <= 2;
    int era = year / 400;
    int yoe = year - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

static void time_open(void) {
    time_t now = time(NULL);
    if (now < 1704067200) {
        const pmu_snapshot_t *pmu = read_pico_pmu_get();
        if (pmu && pmu->time_synced && pmu->unix_sec >= 1704067200) now = pmu->unix_sec;
    }
    if (now < 1704067200) {
        char month[4] = "Jan"; int day = 1, year = 2024;
        if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3) {
            strcpy(month, "Jan"); day = 1; year = 2024;
        }
        static const char *const months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        int m = 1; while (m < 12 && strcmp(month, months[m - 1])) ++m;
        s_year = year; s_month = m; s_day = day; s_hour = s_minute = 0;
    } else {
        struct tm date;
        time_t local = now + 8 * 3600;
        gmtime_r(&local, &date);
        s_year = date.tm_year + 1900; s_month = date.tm_mon + 1; s_day = date.tm_mday;
        s_hour = date.tm_hour; s_minute = date.tm_min;
    }
    s_notice[0] = 0;
    s_page = SETTINGS_TIME_EDIT;
}

static void time_adjust(int field, int step) {
    if (field == 0) s_year = s_year + step > 2099 ? 2024 : s_year + step < 2024 ? 2099 : s_year + step;
    if (field == 1) s_month = s_month + step > 12 ? 1 : s_month + step < 1 ? 12 : s_month + step;
    if (field == 2) s_day = s_day + step > days_in_month(s_year, s_month) ? 1 :
                            s_day + step < 1 ? days_in_month(s_year, s_month) : s_day + step;
    if (field == 3) s_hour = (s_hour + step + 24) % 24;
    if (field == 4) s_minute = (s_minute + step + 60) % 60;
    if (s_day > days_in_month(s_year, s_month)) s_day = days_in_month(s_year, s_month);
}

static bool time_save(void) {
    int64_t epoch = civil_days(s_year, s_month, s_day) * 86400 +
                    s_hour * 3600 + s_minute * 60 - 8 * 3600;
    if (epoch < 1704067200 || epoch > 4102444799LL) return false;
    struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    if (settimeofday(&tv, NULL)) return false;
    uint8_t payload[4] = {(uint8_t)epoch, (uint8_t)(epoch >> 8),
                          (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24)};
    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
    if (err == ESP_OK) err = read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (err != ESP_OK || !pmu || !pmu->time_synced) {
        snprintf(s_notice, sizeof(s_notice), "系统时间已设置，PMU 同步失败");
        return false;
    }
    snprintf(s_notice, sizeof(s_notice), "时间已保存，并同步到电源管理芯片");
    s_page = SETTINGS_TIME;
    return true;
}

// 白底信息区只用水平分隔，不再接受圆角或边框参数。
// White information regions use horizontal dividers, without radius/border parameters.
static void settings_region(uint8_t *fb, EpdRect rect) {
    epd_fill_rect(rect,UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){rect.x,rect.y+rect.height,rect.width,2},UI_GRAY_ITEM_LINE,fb);
}
static void settings_option(uint8_t *fb,EpdRect rect,bool selected) {
    epd_fill_rect(rect,selected?0xe0:UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){rect.x,rect.y+rect.height,rect.width,2},UI_GRAY_ITEM_LINE,fb);
}
static void settings_divider(uint8_t *fb, int y, int x, int width) {
    epd_fill_rect((EpdRect){x, y, width, 2}, UI_GRAY_SECTION_LINE, fb);
}
static void row(uint8_t *fb, int y, const char *label, const char *value) {
    ui_text_vc(fb, 54, y+SETTINGS_ROW_H/2, UI_TEXT_BODY, label, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, 627, y+SETTINGS_ROW_H/2, UI_TEXT_CAPTION, value, EPD_DRAW_ALIGN_RIGHT, false);
    epd_fill_rect((EpdRect){36,y+SETTINGS_ROW_H,612,2},UI_GRAY_ITEM_LINE,fb);
}
static void section(uint8_t *fb, int y, const char *title) {
    ui_text(fb, 40, y, UI_TEXT_TITLE, title, EPD_DRAW_ALIGN_LEFT, false);
}

/* ---- 设置子页标题 / Settings sub-page headers ---- */

static void back_header(uint8_t *fb, const char *title) {
    ui_nav_back(fb, 36,UI_PAGE_Y(79));
    ui_text_vc(fb, 342,UI_PAGE_Y(107), 34, title, EPD_DRAW_ALIGN_CENTER, false);
    settings_divider(fb,UI_PAGE_Y(174), 36, 612);
}

static void setting_icon(uint8_t *fb, int index, int cx, int cy) {
    // 每个设置项对应一个 Lucide 图标；索引顺序由各分组传入的 icons[] 数组决定。
    // One Lucide icon per setting row; the index order comes from each group's icons[] array.
    if (index == 5) {
        ui_draw_round_rect(fb,(EpdRect){cx-15,cy-13,30,26},4,SETTINGS_ICON_INK);
        epd_draw_line(cx-13,cy-10,cx+13,cy-10,SETTINGS_ICON_INK,fb);
        epd_fill_circle(cx+7,cy-5,3,SETTINGS_ICON_INK,fb);
        epd_draw_line(cx-11,cy+8,cx-3,cy-1,SETTINGS_ICON_INK,fb);
        epd_draw_line(cx-3,cy-1,cx+5,cy+8,SETTINGS_ICON_INK,fb);
        epd_draw_line(cx+2,cy+4,cx+8,cy,SETTINGS_ICON_INK,fb);
        epd_draw_line(cx+8,cy,cx+12,cy+8,SETTINGS_ICON_INK,fb);
        return;
    }
    if (index == 13) {
        ui_draw_round_rect(fb,(EpdRect){cx-15,cy-9,28,18},3,SETTINGS_ICON_INK);
        epd_fill_rect((EpdRect){cx+14,cy-4,3,8},SETTINGS_ICON_INK,fb);
        epd_fill_rect((EpdRect){cx-11,cy-5,11,10},SETTINGS_ICON_INK,fb);
        return;
    }
    static const ui_icon_t icons[] = {
        UI_ICON_WIFI,              // 0 无线连接
        UI_ICON_WIFI,              // 1 保留图标索引 / Reserved icon index
        UI_ICON_TYPE,              // 2 系统字体、阅读操作
        UI_ICON_A_LARGE_SMALL,     // 3 系统字号
        UI_ICON_LIBRARY_BIG,       // 4 保留图标索引 / Reserved icon index
        UI_ICON_TICKET,            // 5 锁屏样式
        UI_ICON_CLOCK,             // 6 日期与时间
        UI_ICON_CONTRAST,          // 7 系统对比度
        UI_ICON_COPY,              // 8 保存与恢复
        UI_ICON_POWER,             // 9 关机睡眠
        UI_ICON_TEXT_ALIGN_START,  // 10 状态栏签名
        UI_ICON_REFRESH_CW,        // 11 首页强刷
        UI_ICON_NUTSTORE,          // 12 坚果云同步 / Nutstore sync
        UI_ICON_SETTINGS,          // 13 电池另绘 / Battery drawn separately
        UI_ICON_SETTINGS,          // 14 固件信息 / Firmware information
    };
    if (index < 0 || index >= (int)(sizeof(icons) / sizeof(icons[0]))) return;
    ui_draw_icon(fb, cx, cy, SETTINGS_ICON_PX, icons[index], SETTINGS_ICON_INK);
}

static void fit_value(char *value, int width) {
    while (*value && ttf_text_width_px(ui_text_effective_px(20), value) > width) {
        size_t n = strlen(value) - 1;
        while (n && ((unsigned char)value[n] & 0xc0) == 0x80) --n;
        value[n] = 0;
    }
}

static void setting_group(uint8_t *fb, int title_y, const char *title,
                          int card_y, const int icons[], const char *const labels[],
                          const char *const values[], int count) {
    const int row_height = SETTINGS_ROW_H;
    title_y -= s_main_scroll;
    card_y -= s_main_scroll;
    (void)title_y;
    int heading=ui_text_line_height(UI_TEXT_TITLE);
    settings_divider(fb,card_y-heading-22,36,612);
    ui_text_vc(fb,42,card_y-heading/2-8,UI_TEXT_TITLE,title,EPD_DRAW_ALIGN_LEFT,false);
    for (int i = 0; i < count; ++i) {
        int y = card_y + i * row_height;
        if(i)epd_fill_rect((EpdRect){36,y,612,2},UI_GRAY_ITEM_LINE,fb);
        setting_icon(fb, icons[i], 67, y + row_height / 2);
        ui_text_vc(fb, 100, y + row_height/2, UI_TEXT_BODY, labels[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, 619, y + row_height/2, UI_TEXT_CAPTION, values[i], EPD_DRAW_ALIGN_RIGHT, false);
    }
}

// 中文：说明按实际界面字形宽度折行，续行与正文对齐。/ English: wrap instructions to measured UI glyph width with hanging bullet indentation.
static int cloud_guide_bullet(uint8_t *fb, int y, const char *text) {
    if(fb)ui_text_fixed(fb, 44, y, ui_text_effective_px(UI_TEXT_CAPTION), "•", EPD_DRAW_ALIGN_LEFT, false);
    while (*text) {
        char line[192] = {0};
        size_t used = 0;
        const char *end = text;
        while (*end) {
            const char *next = end + 1;
            while (((unsigned char)*next & 0xc0) == 0x80) ++next;
            size_t bytes = (size_t)(next - end);
            if (used + bytes >= sizeof(line)) break;
            memcpy(line + used, end, bytes);
            line[used + bytes] = 0;
            if (used && ui_text_fixed_width_px(ui_text_effective_px(UI_TEXT_CAPTION),line) > 578) {
                line[used] = 0;
                break;
            }
            used += bytes;
            end = next;
        }
        if (!used) break;
        if(fb)ui_text_fixed(fb, 66, y, ui_text_effective_px(UI_TEXT_CAPTION), line, EPD_DRAW_ALIGN_LEFT, false);
        y += ui_text_line_height(UI_TEXT_CAPTION);
        text = end;
    }
    return y + 8;
}

// 电量曲线仅连接相邻实际采样；超过半小时的空档留白。
// Connect adjacent measured samples only; leave gaps longer than thirty minutes unfilled.
static int cloud_row_y(int i) { return UI_PAGE_Y(214)+i*(SETTINGS_ROW_H+20); }
static int cloud_guide_heading(void) { return cloud_row_y(3)+20; }
static int cloud_guide_top(void) { return cloud_guide_heading()+ui_text_line_height(UI_TEXT_TITLE)+16; }
static int cloud_scroll_limit(void) {
    int y=cloud_guide_top();
    for(size_t i=0;i<sizeof(cloud_guide)/sizeof(cloud_guide[0]);++i)y=cloud_guide_bullet(NULL,y,cloud_guide[i]);
    y+=24;return y>UI_NAV_TOP?y-UI_NAV_TOP:0;
}

static void draw_battery(uint8_t *fb) {
    back_header(fb, "电池管理");
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    int percent = pmu_battery_percent(pmu); bool charging = pmu_battery_charging(pmu);
    char value[64]; snprintf(value, sizeof(value), percent >= 0 ? "%d%%" : "--%%", percent);
    ui_text_fixed_vc(fb, 48,UI_PAGE_Y(246), 82, value, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, 636,UI_PAGE_Y(234), UI_TEXT_BODY, charging ? "充电中" : "未充电", EPD_DRAW_ALIGN_RIGHT, false);
    unsigned mv = pmu && pmu->present ? (pmu->quick_ok ? pmu->qb_mv : pmu->battery_mv) : 0;
    snprintf(value, sizeof(value), mv ? "电压 %.2f V" : "电压暂不可用", mv / 1000.0);
    ui_text_fixed_vc(fb, 636,UI_PAGE_Y(284), ui_text_effective_px(UI_TEXT_CAPTION), value, EPD_DRAW_ALIGN_RIGHT, false);
    for (int i = 0; i < 2; ++i) {
        EpdRect box = {36 + i*312,UI_PAGE_Y(346), 300, 68}; bool active = s_battery_week == (i == 1);
        ui_fill_round_rect(fb, box, 18, active ? UI_NAV_ACTIVE_GRAY : UI_GRAY_WHITE);
        ui_draw_round_rect(fb, box, 18, 0x90);
        ui_text_fixed_vc_gray(fb, box.x + 150,UI_PAGE_Y(380), ui_text_effective_px(UI_TEXT_BODY), i ? "近七天" : "近24小时", EPD_DRAW_ALIGN_CENTER, active?15:0, active?(UI_NAV_ACTIVE_GRAY>>4):15);
    }
    ui_text_vc(fb, 42,UI_PAGE_Y(465), UI_TEXT_HEADING, "电量记录", EPD_DRAW_ALIGN_LEFT, false);
    const int left=94, right=626, top=UI_PAGE_Y(542), bottom=UI_PAGE_Y(898);
    for (int i=0; i<=4; ++i) {
        int y=bottom-i*(bottom-top)/4; snprintf(value,sizeof(value),"%d",i*25);
        ui_text_fixed_vc(fb,76,y,ui_text_effective_px(UI_TEXT_CAPTION),value,EPD_DRAW_ALIGN_RIGHT,false);
        ui_hairline(fb,y,left,right-left,0xc0);
    }
    time_t clock=time(NULL);
    uint32_t end=clock>=1704067200&&(uint64_t)clock<=UINT32_MAX?(uint32_t)clock:0, window=s_battery_week?BATTERY_HISTORY_WINDOW:86400U;
    uint32_t begin=end>window?end-window:0;
    size_t count=0; const battery_sample_t *samples=battery_history_samples(&count);
    bool demo=false;
#ifdef PICO_HOST_SIMULATOR
    // 只在宿主明确启用时显示标注的模拟曲线，绝不存入TF记录。
    // Show a labeled demonstration only when explicitly enabled on the host; never persist it.
    battery_sample_t example[97];
    if (getenv("PICO_SIM_BATTERY_DEMO")) {
        for(size_t i=0;i<97;++i)example[i]=(battery_sample_t){begin+(uint32_t)(i*window/96),3800,
            (uint8_t)(i<40?94-i/2:i<55?74+(i-40)*26/14:100-(i-55)*15/41),(uint8_t)(i>=40&&i<55)};
        samples=example;count=97;demo=true;
    }
#endif
    int prev_x=0,prev_y=0;uint32_t prev_at=0;size_t shown=0;
    for(size_t i=0;i<count;++i) {
        const battery_sample_t *s=&samples[i];if(s->at<begin||s->at>end)continue;
        int x=left+(int)((uint64_t)(s->at-begin)*(right-left)/window);
        int y=bottom-(int)s->percent*(bottom-top)/100;
        if(prev_at && (demo || s->at-prev_at<=2*BATTERY_HISTORY_INTERVAL)) {
            epd_draw_line(prev_x,prev_y,x,y,UI_GRAY_BLACK,fb);
            epd_draw_line(prev_x,prev_y+1,x,y+1,UI_GRAY_BLACK,fb);
        }
        epd_fill_circle(x,y,2,s->charging?0x70:UI_GRAY_BLACK,fb);
        prev_x=x;prev_y=y;prev_at=s->at;++shown;
    }
    for(int i=0;i<5;++i) {
        time_t local=(time_t)(begin+(uint32_t)((uint64_t)i*window/4))+8*3600;struct tm tm;
        if(end && gmtime_r(&local,&tm))strftime(value,sizeof(value),s_battery_week?"%m-%d":"%H:%M",&tm);
        else strlcpy(value,"--",sizeof(value));
        ui_text_fixed_vc(fb,left+i*(right-left)/4,UI_PAGE_Y(934),ui_text_effective_px(UI_TEXT_CAPTION),value,
                         i==0?EPD_DRAW_ALIGN_LEFT:i==4?EPD_DRAW_ALIGN_RIGHT:EPD_DRAW_ALIGN_CENTER,false);
    }
    if(!shown)ui_text_fixed_vc(fb,360,UI_PAGE_Y(720),28,end?"尚无记录，开始使用后逐步积累":"请先设置日期与时间",EPD_DRAW_ALIGN_CENTER,false);
    else if(shown==1)ui_text_fixed_vc(fb,360,UI_PAGE_Y(720),26,"已开始记录，稍后可查看电量曲线",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_fixed_vc(fb,36,UI_PAGE_Y(1000),ui_text_effective_px(UI_TEXT_CAPTION),demo?"模拟数据 · 真机使用实际电量记录":"每15分钟记录一次，休眠和关机期间留白",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_fixed_vc(fb,36,UI_PAGE_Y(1046),ui_text_effective_px(UI_TEXT_CAPTION),"暂无分项耗电数据",EPD_DRAW_ALIGN_LEFT,false);
    ui_nav_draw(fb,3);
}

// 信息页保持一屏，在线升级使用独立来源，本地升级保留TF卡流程。
// Keep information within one screen; online updates use our feed, local updates retain the TF workflow.
static EpdRect firmware_update_rect(int index) { return (EpdRect){36+318*index,UI_PAGE_Y(1012),294,80}; }
static void firmware_info_draw(uint8_t *fb) {
    back_header(fb,"固件信息");
    ui_text_vc(fb,342,UI_PAGE_Y(256),UI_TEXT_TITLE,"if-ink OS",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_vc(fb,342,UI_PAGE_Y(322),UI_TEXT_CAPTION,esp_app_get_description()->version,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_vc(fb,36,UI_PAGE_Y(424),UI_TEXT_BODY,"为 Pico 打造的阅读系统",EPD_DRAW_ALIGN_LEFT,false);
    static const char *const features[]={"EPUB · TXT · Markdown","坚果云同步与无线传书","自定义字体与阅读布局"};
    for(int i=0;i<3;++i)ui_text_vc(fb,36,UI_PAGE_Y(532+82*i),UI_TEXT_BODY,features[i],EPD_DRAW_ALIGN_LEFT,false);
    settings_divider(fb,UI_PAGE_Y(802),36,612);
    ui_text_vc(fb,36,UI_PAGE_Y(858),UI_TEXT_CAPTION,"基于 kiikoread（原 Pico Reader）",EPD_DRAW_ALIGN_LEFT,false);
    if(s_notice[0])ui_text_vc(fb,342,UI_PAGE_Y(950),UI_TEXT_CAPTION,s_notice,EPD_DRAW_ALIGN_CENTER,false);
    ui_draw_button(fb,firmware_update_rect(0),"在线升级",false);
    ui_draw_button(fb,firmware_update_rect(1),"本地升级",false);
    ui_nav_draw(fb,3);
}

// 进度只使用灰阶差分，最多每秒一次，不以下载次数触发整屏强刷。
// Progress uses grayscale differences at most once a second, without periodic full-screen wipes.
static void online_close(void){
    if_update_close();
    if_update_status_t status;if_update_get_status(&status);
    // 已切换启动槽后必须重启，不把安装完成误报为取消。/ Once activated, restart rather than reporting a completed installation as cancelled.
    if(status.state==IF_UPDATE_READY)esp_restart();
}
static EpdRect online_action_rect(void){return (EpdRect){36,UI_PAGE_Y(990),612,88};}
static void online_text(uint8_t *fb,int y,const char *text){
    int px=ui_text_fit_px(UI_TEXT_CAPTION,text,612,60);
    ui_text_fixed_vc(fb,342,y,px,text,EPD_DRAW_ALIGN_CENTER,false);
}
static void online_draw(uint8_t *fb){
    if_update_status_t status;if_update_get_status(&status);
    back_header(fb,"在线升级");
    row(fb,UI_PAGE_Y(210),"当前版本",status.current);
    row(fb,UI_PAGE_Y(330),"可用版本",status.version[0]?status.version:"—");
    online_text(fb,530,s_online_confirm?"确认安装这个版本？":status.message);
    int filled=status.total?(int)((uint64_t)status.received*588/status.total):0;
    if(filled>588)filled=588;
    epd_fill_rect((EpdRect){48,UI_PAGE_Y(648),588,6},0xd0,fb);
    if(filled)epd_fill_rect((EpdRect){48,UI_PAGE_Y(648),filled,6},0x44,fb);
    char progress_text[64]="";
    if(status.total)snprintf(progress_text,sizeof(progress_text),"%u%% · %u / %u KB",
        (unsigned)((uint64_t)status.received*100/status.total),(unsigned)(status.received/1024),(unsigned)((status.total+1023)/1024));
    online_text(fb,716,progress_text);
    online_text(fb,846,"安装期间请保持供电");
    if(s_online_confirm){
        ui_draw_button(fb,(EpdRect){36,UI_PAGE_Y(990),294,88},"取消",false);
        ui_draw_button(fb,(EpdRect){354,UI_PAGE_Y(990),294,88},"确认安装",false);
    }else ui_draw_button(fb,online_action_rect(),status.state==IF_UPDATE_READY?"立即重启":
        status.busy?"取消":status.state==IF_UPDATE_AVAILABLE?"安装新版本":"检查更新",false);
    ui_nav_draw(fb,3);
}

#include "banner_editor_ui.h"

static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    ui_clear_page(fb);
    if (s_page == SETTINGS_MAIN)
        epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    if(s_page==SETTINGS_BATTERY){draw_battery(fb);return;}
    if(s_page==SETTINGS_FIRMWARE_INFO){firmware_info_draw(fb);return;}
    if(s_page==SETTINGS_ONLINE_UPDATE){online_draw(fb);return;}
    if(s_page==SETTINGS_CLOUD){
        cloud_config_t cfg;cloud_config_load(&cfg);back_header(fb,"坚果云同步");
        char folder[241],account[129];snprintf(folder,sizeof(folder),"%s",cfg.folder);snprintf(account,sizeof(account),"%s",cfg.account[0]?cfg.account:"未设置");fit_value(folder,330);fit_value(account,330);
        row(fb,cloud_row_y(0)-s_cloud_scroll,"同步文件夹",folder);row(fb,cloud_row_y(1)-s_cloud_scroll,"账号",account);row(fb,cloud_row_y(2)-s_cloud_scroll,"应用密码",cloud_config_valid(&cfg,true)?"已保存  ›":"未设置  ›");memset(cfg.password,0,sizeof(cfg.password));
        ui_text(fb,44,cloud_guide_heading()-s_cloud_scroll,UI_TEXT_TITLE,"使用说明",EPD_DRAW_ALIGN_LEFT,false);

        int guide_y=cloud_guide_top()-s_cloud_scroll;
        for(size_t i=0;i<sizeof(cloud_guide)/sizeof(cloud_guide[0]);++i)
            guide_y = cloud_guide_bullet(fb,guide_y,cloud_guide[i]);
        if(s_notice[0])ui_text(fb,44,guide_y+12,UI_TEXT_CAPTION,s_notice,EPD_DRAW_ALIGN_LEFT,false);
        epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,UI_PAGE_Y(190)},UI_GRAY_WHITE,fb);
        ui_nav_status(fb);back_header(fb,"坚果云同步");
        ui_nav_draw(fb,3);return;
    }
    if (s_page == SETTINGS_UPGRADE) {
        back_header(fb, "本地升级");
        settings_region(fb,(EpdRect){36,UI_PAGE_Y(202), 612, 120});
        ui_text(fb, 60,UI_PAGE_Y(224), 22, "当前版本", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60,UI_PAGE_Y(264), 29, esp_app_get_description()->version, EPD_DRAW_ALIGN_LEFT, false);
        settings_region(fb,(EpdRect){36,UI_PAGE_Y(356), 612, 344});
        ui_text(fb, 60,UI_PAGE_Y(383), 29, s_upgrade_confirm ? "确认 IF Edition 升级" : "TF 卡升级", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60,UI_PAGE_Y(447), 23, "根目录：Pico-IF-update.bin", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60,UI_PAGE_Y(510), 23, s_local_update.ready ? s_local_update.candidate_version : "请放入 IF Edition 升级包", EPD_DRAW_ALIGN_LEFT, false);
        if (s_upgrade_confirm) {
            ui_text(fb, 60,UI_PAGE_Y(559), 21, "升级期间请保持供电", EPD_DRAW_ALIGN_LEFT, false);
            ui_draw_button(fb, (EpdRect){60,UI_PAGE_Y(613), 260, 64}, "取消", false);
            ui_draw_button(fb, (EpdRect){364,UI_PAGE_Y(613), 260, 64}, "确认升级", true);
        } else {
            ui_draw_button(fb, (EpdRect){60,UI_PAGE_Y(613), 564, 64}, s_local_pending ? "正在安装，请勿断电" : s_local_update.ready ? "安装 TF 卡升级包" : "重新检查 TF 卡", s_local_update.ready);
        }
        if (s_upgrade_notice[0]) ui_text(fb, 48,UI_PAGE_Y(760), 21, s_upgrade_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3); return;
    }
    if(s_page==SETTINGS_HOME_QUOTE){banner_editor_draw(fb);return;}
    if(s_page==SETTINGS_BANNER_IMAGE||s_page==SETTINGS_BANNER_BROWSER){banner_picker_draw(fb);return;}
    if (s_page == SETTINGS_TEXT_EDIT) {
        ui_text_input_draw(fb,&s_text_input,
            s_home_quote_editor?"首页格言":s_cloud_editor==1?"同步文件夹":s_cloud_editor==2?"坚果云账号":s_cloud_editor==3?"应用密码":"状态栏签名",
            s_home_quote_editor?"最多四行，超出请缩短后保存；留空隐藏":s_cloud_editor?"应用密码独立于坚果云登录密码":"状态栏中间显示，留空则隐藏",
            NULL,"完成",s_editor_notice);
        return;
    }
    if (s_page == SETTINGS_TIME_EDIT) {
        ui_nav_back(fb, 36,UI_PAGE_Y(79));
        ui_text(fb, 36,UI_PAGE_Y(151), 47, "日期与时间", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 36,UI_PAGE_Y(216), 23, "北京时间 · UTC+8", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb,UI_PAGE_Y(259), 36, 612, UI_GRAY_LIGHT);
        static const char *const labels[] = {"年", "月", "日", "时", "分"};
        int values[] = {s_year, s_month, s_day, s_hour, s_minute};
        for (int i = 0; i < 5; ++i) {
            int x = 36 + i * 122;
            ui_text(fb, x + 52,UI_PAGE_Y(300), 24, labels[i], EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_round_rect(fb, (EpdRect){x,UI_PAGE_Y(354), 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53,UI_PAGE_Y(385), 30, "+", EPD_DRAW_ALIGN_CENTER, false);
            char value[8]; snprintf(value, sizeof(value), i == 0 ? "%04d" : "%02d", values[i]);
            ui_text(fb, x + 53,UI_PAGE_Y(456), i == 0 ? 34 : 39, value, EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_round_rect(fb, (EpdRect){x,UI_PAGE_Y(535), 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53,UI_PAGE_Y(566), 30, "−", EPD_DRAW_ALIGN_CENTER, false);
        }
        ui_text(fb, 36,UI_PAGE_Y(649), 23, "点上方 + 或下方 − 调整数字", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36,UI_PAGE_Y(767), 612, 78}, "保存时间", false);
        ui_draw_button(fb, (EpdRect){36,UI_PAGE_Y(874), 612, 78}, "取消", false);
        if (s_notice[0]) ui_text(fb, 36,UI_PAGE_Y(996), 22, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    if (s_page == SETTINGS_WIFI) {
        back_header(fb, "WiFi");
        char ssid[33] = {0}; bool saved = false;
        (void)read_pico_transfer_get_saved_wifi(ssid, &saved);
        section(fb,UI_PAGE_Y(253), "已保存网络");
        row(fb,UI_PAGE_Y(296), saved ? ssid : "尚未配置", saved ? "已保存" : "");
        row(fb,UI_PAGE_Y(408), "连接或更改网络", "前往传输  ›");
        ui_text(fb, 54,UI_PAGE_Y(529), 21, "配网后可在日期与时间中进行 WiFi 对时。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_QUOTE_FONT) {
        back_header(fb, s_page==SETTINGS_QUOTE_FONT?"首页文字字体":"系统文字");
        if (s_page == SETTINGS_SYSTEM_FONT) {
            for (int i = 0; i < 4; ++i) {
                EpdRect box = system_size_rect(i); bool active = i == system_size_index();
                epd_fill_circle(box.x+50,box.y+50,50,active?UI_NAV_ACTIVE_GRAY:UI_GRAY_WHITE,fb);
                epd_draw_circle(box.x+50,box.y+50,50,0x90,fb);
                ui_text_fixed_vc_gray(fb, box.x + box.width/2, box.y + box.height/2, ui_text_effective_px(UI_TEXT_BODY),
                                      (const char *const[]){"S","M","L","XL"}[i], EPD_DRAW_ALIGN_CENTER, active?15:0, active?(UI_NAV_ACTIVE_GRAY>>4):15);
            }
            ui_text_vc(fb,342,UI_PAGE_Y(210),UI_TEXT_TITLE,"字号",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_vc(fb,342,UI_PAGE_Y(405),UI_TEXT_TITLE,"字体",EPD_DRAW_ALIGN_CENTER,false);
        } else {
            ui_text_vc(fb,36,220,UI_TEXT_BODY,"修改字号",EPD_DRAW_ALIGN_LEFT,false);
            char size[32];snprintf(size,sizeof(size),"%d px",s_quote_px);ui_text_vc(fb,648,220,UI_TEXT_BODY,size,EPD_DRAW_ALIGN_RIGHT,false);
            banner_slider_draw(fb,300,s_quote_px,CONTENT_QUOTE_SIZE_MIN,CONTENT_QUOTE_SIZE_MAX,true);
            ui_text_vc(fb,36,UI_PAGE_Y(405),UI_TEXT_BODY,"选择字体",EPD_DRAW_ALIGN_LEFT,false);
            if(s_notice[0])ui_text_fixed_vc(fb,36,346,20,s_notice,EPD_DRAW_ALIGN_LEFT,false);
        }
        const char *selected = s_page==SETTINGS_QUOTE_FONT?s_banner_draft.font:app_settings_system_font_path();
        int count = ttf_font_count();
        int first = s_font_page * font_list_rows();
        for (int slot = 0; slot < font_list_rows(); ++slot) {
            int item_index = first + slot;
            if (item_index > count) break;
            EpdRect box = font_list_rect(slot);
            const ttf_font_item_t *item = item_index ? ttf_font_item(item_index - 1) : NULL;
            bool active = item ? !strcmp(selected, item->path) : !selected[0];
            settings_option(fb,box,active);
            char label[TTF_FONT_NAME_MAX];
            snprintf(label, sizeof(label), "%s", item ? system_font_label(item->path) : s_page==SETTINGS_QUOTE_FONT?"跟随系统字体":"默认字体（完整字库补字）");
            while (label[0] && ttf_text_width_px(ui_text_effective_px(26), label) > box.width - 102) {
                size_t len = strlen(label) - 1;
                while (len && ((unsigned char)label[len] & 0xc0) == 0x80) --len;
                label[len] = 0;
            }
            ui_text_vc(fb, 60, box.y + box.height/2, 26, label, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + box.height/2, 6, UI_GRAY_BLACK, fb);
        }
        char pages[32]; snprintf(pages, sizeof(pages), "%d / %d", s_font_page + 1, (count + font_list_rows()) / font_list_rows());
        ui_text(fb, 648,UI_PAGE_Y(1050), 20, pages, EPD_DRAW_ALIGN_RIGHT, false);
        ui_nav_draw(fb, s_page==SETTINGS_QUOTE_FONT?0:3);
        return;
    }
    if(s_page==SETTINGS_WALLPAPER){
        back_header(fb,"锁屏壁纸");
        const bool rotating=app_settings_wallpaper_rotating();
        for(int i=0;i<2;++i){
            EpdRect row={36,UI_PAGE_Y(236+i*190),612,170};settings_option(fb,row,i==rotating);
            ui_text_vc(fb,54,row.y+48,UI_TEXT_HEADING,i?"壁纸轮播":"固定壁纸",EPD_DRAW_ALIGN_LEFT,false);
            const char *value=i?(rotating?app_settings_wallpaper_path()+7:"选择 TF 卡中的图片文件夹"):(!rotating&&app_settings_wallpaper_path()[0]?strrchr(app_settings_wallpaper_path(),'/')+1:"选择单张图片或默认壁纸");
            char label[288];snprintf(label,sizeof(label),"%s",value);fit_value(label,510);
            ui_text_vc(fb,54,row.y+112,UI_TEXT_CAPTION,label,EPD_DRAW_ALIGN_LEFT,false);
            if(i==rotating)epd_fill_circle(612,row.y+48,6,UI_GRAY_BLACK,fb);
        }
        ui_text(fb,54,UI_PAGE_Y(690),UI_TEXT_CAPTION,"每次休眠随机更换，多张图片连续不重复",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,54,UI_PAGE_Y(744),UI_TEXT_CAPTION,"JPG / JPEG / PNG · 单张不超过 2 MB",EPD_DRAW_ALIGN_LEFT,false);
        ui_nav_draw(fb,3);return;
    }
    if(s_page==SETTINGS_WALLPAPER_FOLDER){
        back_header(fb,"选择轮播文件夹");
        char path[288];snprintf(path,sizeof(path),"%s",s_wallpaper_directory+7);fit_value(path,440);
        ui_text_vc(fb,42,UI_PAGE_Y(220),UI_TEXT_BODY,path[0]?path:"/",EPD_DRAW_ALIGN_LEFT,false);
        if(strcmp(s_wallpaper_directory,"/sdcard"))ui_text_vc(fb,642,UI_PAGE_Y(220),UI_TEXT_BODY,"上一级",EPD_DRAW_ALIGN_RIGHT,false);
        settings_option(fb,(EpdRect){36,UI_PAGE_Y(266),612,82},false);
        ui_text_vc(fb,342,UI_PAGE_Y(307),UI_TEXT_BODY,"使用此文件夹",EPD_DRAW_ALIGN_CENTER,false);
        ui_text_vc(fb,42,UI_PAGE_Y(378),UI_TEXT_CAPTION,s_notice[0]?s_notice:"至少一张可用图片 · 只读取本层文件",EPD_DRAW_ALIGN_LEFT,false);
        for(int i=0;i<wallpaper_folder_rows();++i){
            int index=s_wallpaper_page*wallpaper_folder_rows()+i;if(index>=s_wallpaper_count)break;
            EpdRect row={36,UI_PAGE_Y(416+i*font_list_step()),612,font_list_step()-12};settings_option(fb,row,false);
            char name[96];snprintf(name,sizeof(name),"%s",s_wallpapers[index].name);fit_value(name,510);
            ui_text_vc(fb,54,row.y+row.height/2,UI_TEXT_BODY,name,EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,624,row.y+row.height/2,UI_TEXT_BODY,"›",EPD_DRAW_ALIGN_RIGHT,false);
        }
        if(!s_wallpaper_count)ui_text(fb,54,UI_PAGE_Y(464),UI_TEXT_CAPTION,"此处没有子文件夹",EPD_DRAW_ALIGN_LEFT,false);
        ui_nav_draw(fb,3);return;
    }
    if (s_page == SETTINGS_WALLPAPER_FIXED) {
        back_header(fb, "固定壁纸");
        ui_text_vc(fb,54,UI_PAGE_Y(244),UI_TEXT_BODY,"使用默认壁纸",EPD_DRAW_ALIGN_LEFT,false);
        if (!s_wallpaper_count)
            ui_text(fb, 54,UI_PAGE_Y(325), 25, "未找到图片，请放入 TF 卡的 pictures 文件夹", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < wallpaper_rows(); ++i) {
            int index = s_wallpaper_page * wallpaper_rows() + i;
            if (index >= s_wallpaper_count) break;
            EpdRect box = wallpaper_rect(i);
            bool active = !strcmp(s_wallpapers[index].path, app_settings_wallpaper_path());
            settings_option(fb,box,active);
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[index].name);
            fit_value(name, 490);
            ui_text_vc(fb, 60, box.y + box.height/2, 24, name, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + box.height/2, 6, UI_GRAY_BLACK, fb);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        back_header(fb, "预览壁纸");
        EpdRect image = {166,UI_PAGE_Y(194), 352, 626};
        epd_fill_rect(image,UI_GRAY_WHITE,fb);
        s_wallpaper_preview_ok = s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count &&
            ui_wallpaper_draw(fb, s_wallpapers[s_wallpaper_selected].path, image);
        epd_fill_rect((EpdRect){image.x,image.y+image.height,image.width,2},UI_GRAY_ITEM_LINE,fb);
        if (!s_wallpaper_preview_ok)
            ui_text_vc(fb, 342,UI_PAGE_Y(507), 23, "图片无法预览", EPD_DRAW_ALIGN_CENTER, false);
        if (s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count) {
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[s_wallpaper_selected].name);
            fit_value(name, 570);
            ui_text_vc(fb, 342,UI_PAGE_Y(859), 24, name, EPD_DRAW_ALIGN_CENTER, false);
            char position[40];
            snprintf(position, sizeof(position), "%d / %d · 上下滑动切换", s_wallpaper_selected + 1, s_wallpaper_count);
            ui_text_vc(fb, 342,UI_PAGE_Y(908), 20, position, EPD_DRAW_ALIGN_CENTER, false);
        }
        if (s_wallpaper_preview_ok) {
            EpdRect button = {94,UI_PAGE_Y(962), 496, 70};
            ui_fill_round_rect(fb, button, 22, 0x30);
            ui_text_vc(fb, 342,UI_PAGE_Y(997), 25, "设为锁屏壁纸", EPD_DRAW_ALIGN_CENTER, true);
        }
        if (s_wallpaper_confirm) {
            EpdRect dialog = {68,UI_PAGE_Y(435), 548, 306};
            ui_fill_round_rect(fb, dialog, 26, UI_GRAY_WHITE);
            ui_draw_round_rect(fb, dialog, 26, 0x50);
            ui_text_vc(fb, 342,UI_PAGE_Y(503), 31, "设为壁纸锁屏？", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 342,UI_PAGE_Y(565), 20, "确认后，短按电源键将显示此图片", EPD_DRAW_ALIGN_CENTER, false);
            EpdRect cancel = {94,UI_PAGE_Y(643), 232, 68}, confirm = {358, 643, 232, 68};
            ui_fill_round_rect(fb, cancel, 18, 0xe0);
            ui_fill_round_rect(fb, confirm, 18, 0x30);
            ui_text_vc(fb, 210,UI_PAGE_Y(677), 24, "取消", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 474,UI_PAGE_Y(677), 24, "确认", EPD_DRAW_ALIGN_CENTER, true);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if(s_page==SETTINGS_STATUS_DATE_FORMAT){
        back_header(fb,"日期格式");
        ui_text_vc(fb,36,UI_PAGE_Y(232),UI_TEXT_CAPTION,"选择左上角显示的日期格式",EPD_DRAW_ALIGN_LEFT,false);
        for(int i=0;i<APP_STATUS_DATE_FORMAT_COUNT;++i){
            EpdRect r=status_date_rect(i);bool selected=(int)app_settings_status_date_format()==i;
            ui_text_vc(fb,54,r.y+r.height/2,UI_TEXT_BODY,status_date_labels[i],EPD_DRAW_ALIGN_LEFT,false);
            if(selected)epd_fill_circle(617,r.y+r.height/2,7,UI_GRAY_BLACK,fb);
            epd_fill_rect((EpdRect){36,r.y+r.height,612,2},UI_GRAY_ITEM_LINE,fb);
        }
        ui_nav_draw(fb,3);return;
    }
    if (s_page == SETTINGS_TIME) {
        back_header(fb, "日期与时间");
        time_t now = time(NULL);
        char shown[64] = "未设置";
        if (now >= 1704067200) {
            struct tm tm; time_t local = now + 8 * 3600;
            if (gmtime_r(&local, &tm)) snprintf(shown, sizeof(shown), "%04d-%02d-%02d  %02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        }
        section(fb,UI_PAGE_Y(253),"当前时间 · 北京时间");row(fb,UI_PAGE_Y(296),shown,"");
        row(fb,UI_PAGE_Y(407),"通过 WiFi 对时","立即同步  ›");row(fb,UI_PAGE_Y(518),"手动设置","编辑  ›");
        settings_divider(fb,UI_PAGE_Y(624),36,612);section(fb,UI_PAGE_Y(640),"状态栏显示");
        row(fb,status_display_row_y(0),"显示日期",app_settings_status_show_date()?"开启  ›":"关闭  ›");
        row(fb,status_display_row_y(1),"日期格式",status_date_labels[app_settings_status_date_format()]);
        if(s_notice[0]){
            int px=ui_text_fit_px(UI_TEXT_CAPTION,s_notice,576,40);
            ui_text_fixed_vc(fb,54,UI_PAGE_Y(1048),px,s_notice,EPD_DRAW_ALIGN_LEFT,false);
        }
        ui_nav_draw(fb,3);return;
    }
    if(s_page==SETTINGS_IDLE_SLEEP){
        back_header(fb,"自动休眠");
        ui_text_vc(fb,36,UI_PAGE_Y(232),UI_TEXT_CAPTION,"多久不操作后进入休眠",EPD_DRAW_ALIGN_LEFT,false);
        for(int i=0;i<6;++i){EpdRect r=status_date_rect(i);
            ui_text_vc(fb,54,r.y+r.height/2,UI_TEXT_BODY,idle_sleep_labels[i],EPD_DRAW_ALIGN_LEFT,false);
            if(app_settings_idle_sleep_minutes()==idle_sleep_minutes[i])epd_fill_circle(617,r.y+r.height/2,7,UI_GRAY_BLACK,fb);
            epd_fill_rect((EpdRect){36,r.y+r.height,612,2},UI_GRAY_ITEM_LINE,fb);
        }
        ui_nav_draw(fb,3);return;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        back_header(fb, "关机睡眠");
        section(fb,UI_PAGE_Y(248), "选择电源菜单中“关机”的方式");
        const char *labels[] = {"彻底关机", "先浅睡，10 分钟后深睡"};
        const char *details[] = {"完全断电，长按电源键开机",
                                 "锁屏画面保留；深睡后短按电源键开机"};
        for (int i = 0; i < 2; ++i) {
            EpdRect box = {36,UI_PAGE_Y(300 + i * 154), 612, 132};
            bool active = app_settings_staged_shutdown() == (i == 1);
            settings_option(fb,box,active);
            ui_text(fb, 64, box.y + 23, 29, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 64, box.y + 79, 20, details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 66, 8, UI_GRAY_BLACK, fb);
        }
        settings_divider(fb,UI_PAGE_Y(640),36,612);section(fb,UI_PAGE_Y(656),"自动休眠");
        row(fb,UI_PAGE_Y(714),"无操作后休眠",idle_sleep_label());
        ui_text_vc(fb,54,UI_PAGE_Y(866),UI_TEXT_CAPTION,"无操作时自动锁屏并休眠",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,54,UI_PAGE_Y(910),UI_TEXT_CAPTION,"传输期间暂停计时",EPD_DRAW_ALIGN_LEFT,false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_BOOT) {
        back_header(fb, "电脑刷机");
        settings_region(fb,(EpdRect){36,UI_PAGE_Y(254), 612, 298});
        ui_text(fb, 60,UI_PAGE_Y(288), 29, "进入 BOOT 模式", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60,UI_PAGE_Y(350), 22, "用于官网完整刷机或故障恢复", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){60,UI_PAGE_Y(438), 564, 78}, s_boot_pending ? "正在进入" : "进入 BOOT 模式", false);
        ui_text(fb, 48,UI_PAGE_Y(620), 22, "请用 USB 连接电脑，再打开官网刷机页", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3); return;
    }

    if (s_page == SETTINGS_CONFIG) {
        back_header(fb, "保存与恢复配置");
        section(fb,UI_PAGE_Y(248), "换机或刷机后，快速恢复个性化设置");
        settings_region(fb,(EpdRect){36,UI_PAGE_Y(299), 612, 197});
        ui_text(fb, 60,UI_PAGE_Y(326), 26, "TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60,UI_PAGE_Y(377), 23, "Pico-settings.backup", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60,UI_PAGE_Y(435), 19, "文字排版、设备资料、阅读记录与书签", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36,UI_PAGE_Y(561), 612, 83}, "保存当前配置到 TF 卡", false);
        ui_draw_button(fb, (EpdRect){36,UI_PAGE_Y(681), 612, 83},
                       s_config_confirm ? "再次点按，确认恢复配置" : "从 TF 卡恢复配置", false);
        if (s_notice[0]) ui_text(fb, 48,UI_PAGE_Y(819), 21, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48,UI_PAGE_Y(899), 19, "包含阅读记录、设备资料、WiFi和坚果云。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48,UI_PAGE_Y(939), 19, "书籍、字体和图片仍需留在 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48,UI_PAGE_Y(979), 19, "备份含WiFi和坚果云应用密码，请妥善保管。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }

    // 名称、版本与口号共用紧凑单行，按可用宽度适配，避免大字号互相覆盖。
    // Fit the brand, version and slogan on one compact line without overlap at large presets.
    const int info_y = UI_PAGE_Y(84) - s_main_scroll;
    if (info_y + SETTINGS_INFO_H > UI_STATUS_HEIGHT) {
        int center=info_y+SETTINGS_INFO_H/2;
        int title_px=ui_text_fit_px(UI_TEXT_TITLE,"if-ink OS",196,48);
        ui_text_fixed_vc(fb,36,center,title_px,"if-ink OS",EPD_DRAW_ALIGN_LEFT,false);
        int version_x=36+ui_text_fixed_width_px(title_px,"if-ink OS")+12;
        int version_px=ui_text_fit_px(UI_TEXT_CAPTION,"v0.1.4",92,48);
        ui_text_fixed_vc(fb,version_x,center,version_px,"v0.1.4",EPD_DRAW_ALIGN_LEFT,false);
        int width=648-version_x-ui_text_fixed_width_px(version_px,"v0.1.4")-24;
        const char *slogan="支持md文档的Pico固件";
        int slogan_px=ui_text_fit_px(UI_TEXT_CAPTION,slogan,width,48);
        ui_text_fixed_vc_gray(fb,648,center,slogan_px,slogan,EPD_DRAW_ALIGN_RIGHT,8,15);
    }
    char wifi_ssid[33] = {0}; bool wifi_saved = false;
    (void)read_pico_transfer_get_saved_wifi(wifi_ssid, &wifi_saved);
    const char *wireless_labels[] = {"WiFi", "坚果云同步"};
    cloud_config_t cloud; cloud_config_load(&cloud);
    const bool cloud_ready = cloud_config_valid(&cloud, true);
    memset(cloud.password, 0, sizeof(cloud.password));
    char wireless_value[56]; snprintf(wireless_value, sizeof(wireless_value), "%s  ›", wifi_saved ? wifi_ssid : "未配置");
    fit_value(wireless_value, 235);
    const char *wireless_values[] = {wireless_value, cloud_ready ? "已配置  ›" : "未配置  ›"};
    static const int wireless_icons[] = {0, 12};
    setting_group(fb, SETTINGS_WIRELESS_Y - 34, "账号与链接", SETTINGS_WIRELESS_Y,
                  wireless_icons, wireless_labels, wireless_values, SETTINGS_WIRELESS_ROWS);
    const char *reading_labels[] = {"系统文字", "状态栏签名", "首页强刷", "锁屏壁纸", "首页Banner"};
    char font[96];
    const char *chosen_font = app_settings_system_font_path();
    snprintf(font, sizeof(font), "%s  ›", system_font_label(chosen_font));
    fit_value(font, 235);
    snprintf(font, sizeof(font), "%s · %s  ›", size_names[system_size_index()], system_font_label(chosen_font));
    fit_value(font, 235);
    char signature_value[96];
    snprintf(signature_value, sizeof(signature_value), "%s  ›",
             app_settings_status_signature()[0] ? app_settings_status_signature() : "未设置");
    fit_value(signature_value, 235);
    const char *reading_values[] = {font,
                                    signature_value, app_settings_home_full_refresh() ? "开启  ›" : "关闭  ›", "选择  ›", "编辑  ›"};
    static const int reading_icons[] = {2, 10, 11, 5, 5};
    setting_group(fb, SETTINGS_DISPLAY_Y - 34, "显示与个性化", SETTINGS_DISPLAY_Y,
                  reading_icons, reading_labels, reading_values, SETTINGS_DISPLAY_ROWS);
    const char *device_labels[] = {"日期与时间", "关机与休眠", "电池管理", "保存与恢复", "固件信息", "BOOT 刷机"};
    const char *device_values[] = {"设置  ›", app_settings_staged_shutdown() ? "先浅后深  ›" : "彻底断电  ›",
                                   "查看  ›", "配置  ›", "v0.1.4  ›", "电脑刷机  ›"};
    static const int device_icons[] = {6, 9, 13, 8, 14, 11};
    setting_group(fb, SETTINGS_DEVICE_Y - 34, "系统与设备", SETTINGS_DEVICE_Y,
                  device_icons, device_labels, device_values, SETTINGS_DEVICE_ROWS);
    epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_STATUS_HEIGHT}, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    ui_nav_draw(fb, 3);
}

static void on_enter(app_ctx_t *ctx) {
    (void)ctx;
    quote_preview_clear();content_banner_load(&s_banner_draft);s_quote_px=s_banner_draft.quote_px;s_quote_dragging=false;s_quote_committed=false;
    s_notice[0] = 0;
    s_page = s_cloud_requested?SETTINGS_CLOUD:SETTINGS_MAIN;s_cloud_requested=false;s_cloud_editor=0;
    s_main_scroll = s_font_page = s_wallpaper_page = s_cloud_scroll = 0;
    s_scroll_drag_consumed = s_scroll_present_pending = false;
    s_wallpaper_selected = -1;
    s_wallpaper_confirm = s_wallpaper_preview_ok = false;
    s_sync_pending = s_boot_pending = false;
    s_config_confirm = false;
    s_boot_pending = s_local_pending = s_upgrade_restart = false;
    s_upgrade_confirm = false;
    s_online_pending=s_online_confirm=false;s_online_seen=0;s_online_drawn_at=s_online_reboot_at=0;
    s_home_quote_editor=false;
    if(s_home_quote_requested){s_home_quote_requested=false;s_page=SETTINGS_HOME_QUOTE;quote_preview_prepare(s_quote_px,false);}
}

static app_redraw_t on_tick(app_ctx_t *ctx) {
    if(s_page==SETTINGS_ONLINE_UPDATE){
        if(s_online_pending){s_online_pending=false;(void)if_update_check();}
        if_update_poll();if_update_status_t status;if_update_get_status(&status);
        if(status.state==IF_UPDATE_READY&&!status.busy){
            if(!s_online_reboot_at)s_online_reboot_at=ctx->now_ms+1500;
            else if(ctx->now_ms>=s_online_reboot_at){esp_restart();return APP_REDRAW_NONE;}
        }
        if(status.revision!=s_online_seen&&(!status.busy||ctx->now_ms-s_online_drawn_at>=1000)){
            s_online_seen=status.revision;s_online_drawn_at=ctx->now_ms;return APP_REDRAW_AREA;
        }
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_HOME_QUOTE&&!s_quote_preview&&!s_quote_preview_attempted){quote_preview_prepare(s_banner_draft.quote_px,false);return APP_REDRAW_PAGE;}
    (void)ctx;
    if (s_page == SETTINGS_BATTERY) {
        const pmu_snapshot_t *p = read_pico_pmu_get();
        size_t n=0; const battery_sample_t *history=battery_history_samples(&n);
        uint32_t at=n?history[n-1].at:0;
        int percent=pmu_battery_percent(p); bool charging=pmu_battery_charging(p);
        if(percent!=s_battery_percent || charging!=s_battery_charging || at!=s_battery_at) {
            s_battery_percent=percent; s_battery_charging=charging; s_battery_at=at;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_boot_pending) {
        s_boot_pending = false;
        pmu_selftest_prepare_powerdown();
        uint8_t req[2] = {0, 0};
        esp_err_t error = read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_RESET, req, sizeof(req));
        if (error != ESP_OK) ESP_LOGW(TAG, "BOOT PMU notice: %s", esp_err_to_name(error));
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart(); return APP_REDRAW_NONE;
    }
    if (s_upgrade_restart) { s_upgrade_restart = false; esp_restart(); return APP_REDRAW_NONE; }
    if (s_page == SETTINGS_UPGRADE) {
        if (s_local_pending) {
            s_local_pending = false;
            esp_err_t error = pico_ota_install(PICO_OTA_UPDATE_PATH, s_upgrade_notice, sizeof(s_upgrade_notice));
            s_upgrade_restart = error == ESP_OK;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (!s_sync_pending) return APP_REDRAW_NONE;
    s_sync_pending = false;
    uint32_t utc = 0;
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    app_time_service_stop();
    esp_err_t err = read_pico_transfer_sync_time(&utc);
    bool network_time_received = err == ESP_OK;
    if (network_time_received) {
        uint8_t payload[4] = {(uint8_t)utc, (uint8_t)(utc >> 8), (uint8_t)(utc >> 16), (uint8_t)(utc >> 24)};
        err = read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
        if (err == ESP_OK) err = read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
        if (err == ESP_OK) {
            const pmu_snapshot_t *pmu = read_pico_pmu_get();
            if (!pmu || !pmu->time_synced || pmu->unix_sec < utc || pmu->unix_sec - utc > 10)
                err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGI(TAG, "time sync network=%d ready=%d utc=%lu result=%s",
             network.mode, network.network_ready, (unsigned long)utc, esp_err_to_name(err));
    const char *notice = err == ESP_OK ? "WiFi 对时完成，内部时钟已接管" :
        network_time_received ? "已获取网络时间，内部时钟写入失败" :
        err == ESP_ERR_NOT_FOUND ? "请先在 WiFi 设置中配置网络" :
        err == ESP_ERR_INVALID_STATE ? "WiFi 尚未连接，请稍后再试" :
        err == ESP_ERR_TIMEOUT ? "网络对时超时，请检查网络后重试" :
        "网络对时失败，请检查网络后重试";
    snprintf(s_notice, sizeof(s_notice), "%s", notice);
    return APP_REDRAW_PAGE;
}

static app_redraw_t profile_editor_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    ui_input_result_t result=ui_text_input_gesture(&s_text_input,ev);
    if(result==UI_INPUT_NONE)return APP_REDRAW_NONE;
    if(result==UI_INPUT_CANCEL){ui_text_input_clear(&s_text_input);if(s_home_quote_editor){s_home_quote_editor=false;s_page=SETTINGS_HOME_QUOTE;quote_preview_prepare(s_banner_draft.quote_px,false);return APP_REDRAW_PAGE;}s_page=editor_return_page();return APP_REDRAW_PAGE;}
    if(result==UI_INPUT_CHANGED){s_editor_notice[0]=0;return APP_REDRAW_AREA;}
    if(s_home_quote_editor){
        content_banner_config_t next=s_banner_draft;strlcpy(next.text,s_editor,sizeof(next.text));
        if(!banner_preview_apply(&next,true)){strlcpy(s_editor_notice,"文字最多四行，请缩短内容或缩小字号",sizeof(s_editor_notice));return APP_REDRAW_PAGE;}
        ui_text_input_clear(&s_text_input);s_home_quote_editor=false;s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;
    }
    if(s_cloud_editor){
        cloud_config_t cfg;cloud_config_load(&cfg);
        if(s_cloud_editor==1)strlcpy(cfg.folder,s_editor,sizeof(cfg.folder));
        else if(s_cloud_editor==2){if(strcmp(cfg.account,s_editor))memset(cfg.password,0,sizeof(cfg.password));strlcpy(cfg.account,s_editor,sizeof(cfg.account));}
        else if(s_editor[0])snprintf(cfg.password,sizeof(cfg.password),"%s",s_editor);
        esp_err_t e=cloud_config_save(&cfg);memset(cfg.password,0,sizeof(cfg.password));
        if(e!=ESP_OK){snprintf(s_editor_notice,sizeof(s_editor_notice),"%s",e==ESP_ERR_NOT_SUPPORTED?"模拟器请使用页面下方连接配置":"内容无效或保存失败");return APP_REDRAW_PAGE;}
        memset(s_editor,0,sizeof(s_editor));s_page=SETTINGS_CLOUD;return APP_REDRAW_PAGE;
    }
    app_settings_set_status_signature(s_editor);
    const char *saved = app_settings_status_signature();
    if (strcmp(saved, s_editor)) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "保存失败，请检查设置存储空间");
        return APP_REDRAW_PAGE;
    }
    s_page = editor_return_page();
    return APP_REDRAW_PAGE;
}

// 手指越过阈值就推进至少三行；一轮拖动只刷新一次，松手不再重复滚动。
// Advance at least three rows on crossing the drag threshold; refresh once per drag, without repeating on release.
static app_redraw_t scroll_gesture(const ui_gesture_event_t *ev, int *offset, int limit, bool *handled) {
    *handled = ev->type != UI_GESTURE_TAP || s_scroll_drag_consumed;
    if (ev->type == UI_GESTURE_PRESS) {
        s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_CANCEL) {
        s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_MOVE && ev->type != UI_GESTURE_SWIPE_U && ev->type != UI_GESTURE_SWIPE_D)
        return APP_REDRAW_NONE;
    if (ev->y0 < (s_page == SETTINGS_MAIN ? UI_STATUS_HEIGHT : UI_PAGE_Y(190)) || ev->y0 >= UI_NAV_TOP)
        return APP_REDRAW_NONE;
    if (s_scroll_drag_consumed) {
        if (ev->type != UI_GESTURE_MOVE) s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    int dy = (int)ev->y - ev->y0, dx = (int)ev->x - ev->x0;
    int distance = abs(dy);
    if (ev->type == UI_GESTURE_MOVE && (distance < 108 || distance <= abs(dx))) return APP_REDRAW_NONE;
    int step = distance < 204 ? 204 : distance > 450 ? 450 : distance;
    bool up = ev->type == UI_GESTURE_SWIPE_U || (ev->type == UI_GESTURE_MOVE && dy < 0);
    int next = *offset + (up ? step : -step);
    if (limit < 0) limit = 0;
    if (next < 0) next = 0;
    if (next > limit) next = limit;
    if (ev->type == UI_GESTURE_MOVE) s_scroll_drag_consumed = true;
    if (next == *offset) return APP_REDRAW_NONE;
    *offset = next;
    s_scroll_present_pending = true;
    return APP_REDRAW_AREA;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if(s_page==SETTINGS_ONLINE_UPDATE){
        if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
        int tab=ui_nav_hit(ev->x0,ev->y0);
        if((ev->y0<UI_PAGE_Y(190)&&ev->x0<170)||tab>=0){
            online_close();s_online_pending=s_online_confirm=false;
            if(tab>=0)ui_nav_request(ctx,tab);else s_page=SETTINGS_FIRMWARE_INFO;
            return APP_REDRAW_PAGE;
        }
        if(ui_rect_hit(online_action_rect(),ev->x0,ev->y0)){
            if_update_status_t status;if_update_get_status(&status);
            if(s_online_confirm){s_online_confirm=false;if(ev->x0>=354)(void)if_update_install();}
            else if(status.busy)online_close();
            else if(status.state==IF_UPDATE_READY){esp_restart();return APP_REDRAW_NONE;}
            else if(status.state==IF_UPDATE_AVAILABLE)s_online_confirm=true;
            else (void)if_update_check();
            return APP_REDRAW_AREA;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BOOT) {
        if (ev->type != UI_GESTURE_TAP || s_boot_pending) return APP_REDRAW_NONE;
        if (ev->y0 < UI_PAGE_Y(190) && ev->x0 < 170) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
        if (ui_rect_hit((EpdRect){60,UI_PAGE_Y(438), 564, 78}, ev->x0, ev->y0)) { s_boot_pending = true; return APP_REDRAW_PAGE; }
        int tab = ui_nav_hit(ev->x0, ev->y0);
        if (tab >= 0) ui_nav_request(ctx, tab);
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_UPGRADE) {
        if (ev->type != UI_GESTURE_TAP || s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        int y = ev->y0;
        if (y < UI_PAGE_Y(190) && ev->x0 < 170) {
            if (s_upgrade_confirm) s_upgrade_confirm = false; else s_page = SETTINGS_FIRMWARE_INFO;
            return APP_REDRAW_PAGE;
        }
        int tab = ui_nav_hit(ev->x0, y);
        if (tab >= 0) { s_upgrade_confirm = false; ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        if (y >= UI_PAGE_Y(613) && y < UI_PAGE_Y(677) && ev->x0 >= 60 && ev->x0 < 624) {
            if (s_upgrade_confirm) { s_upgrade_confirm = false; if (ev->x0 >= 364) s_local_pending = true; }
            else { (void)pico_ota_inspect(PICO_OTA_UPDATE_PATH, &s_local_update); s_upgrade_confirm = s_local_update.ready; if (!s_local_update.ready) snprintf(s_upgrade_notice,sizeof(s_upgrade_notice),"%s",s_local_update.message); }
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_HOME_QUOTE)return banner_editor_gesture(ctx,ev);
    if(s_page==SETTINGS_BANNER_IMAGE||s_page==SETTINGS_BANNER_BROWSER)return banner_picker_gesture(ctx,ev);
    if(s_page==SETTINGS_QUOTE_FONT){bool handled;app_redraw_t redraw=quote_slider_gesture(ev,&handled);if(handled)return redraw;}
    if(s_page==SETTINGS_QUOTE_FONT&&ev->type==UI_GESTURE_TAP&&ev->y0<UI_PAGE_Y(190)){s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;}
    if (s_page == SETTINGS_TEXT_EDIT) return profile_editor_gesture(ctx,ev);
    if (s_page == SETTINGS_CLOUD) {
        bool handled;app_redraw_t redraw=scroll_gesture(ev,&s_cloud_scroll,cloud_scroll_limit(),&handled);
        if(handled)return redraw;
    }
    if (s_page == SETTINGS_MAIN) {
        int limit = SETTINGS_SCROLL_MAX;
        int *offset = &s_main_scroll;

        bool handled;
        app_redraw_t redraw = scroll_gesture(ev, offset, limit, &handled);
        if (handled) return redraw;
    }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW && !s_wallpaper_confirm &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int next = s_wallpaper_selected + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
        if (next < 0 || next >= s_wallpaper_count) return APP_REDRAW_NONE;
        s_wallpaper_selected = next;
        s_wallpaper_page = next / wallpaper_rows();
        return APP_REDRAW_PAGE;
    }
    if ((s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_QUOTE_FONT || s_page == SETTINGS_WALLPAPER_FIXED || s_page == SETTINGS_WALLPAPER_FOLDER) &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int *page = (s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_QUOTE_FONT) ? &s_font_page : &s_wallpaper_page;
        int count = (s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_QUOTE_FONT) ? ttf_font_count() + 1 : s_wallpaper_count;
        int rows = s_page == SETTINGS_WALLPAPER_FOLDER ? wallpaper_folder_rows() : s_page == SETTINGS_WALLPAPER_FIXED ? wallpaper_rows() : font_list_rows();
        int pages = count > 0 ? (count + rows - 1) / rows : 1;
        int old = *page;
        if (ev->type == UI_GESTURE_SWIPE_U && *page + 1 < pages) ++*page;
        if (ev->type == UI_GESTURE_SWIPE_D && *page > 0) --*page;
        if (*page == old) return APP_REDRAW_NONE;
        s_scroll_present_pending = true;
        return APP_REDRAW_AREA;
    }
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (s_page == SETTINGS_WALLPAPER_PREVIEW && s_wallpaper_confirm) {
        if (ev->y0 >= UI_PAGE_Y(643) && ev->y0 < UI_PAGE_Y(711) && ev->x0 >= 358 && ev->x0 < 590) {
            const char *path = s_wallpapers[s_wallpaper_selected].path;
            app_settings_set_wallpaper_path(path);
            if (!strcmp(app_settings_wallpaper_path(), path)) {
                app_settings_set_lock_style(1);
                s_page = SETTINGS_WALLPAPER;
            }
        }
        s_wallpaper_confirm = false;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_TIME_EDIT) {
        if (ev->y0 < UI_PAGE_Y(190)) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
        if (ev->y0 >= UI_PAGE_Y(354) && ev->y0 < UI_PAGE_Y(597)) {
            int field = (ev->x0 - 36) / 122;
            if (ev->x0 >= 36 && field >= 0 && field < 5 && (ev->x0 - 36) % 122 < 106) {
                if (ev->y0 < UI_PAGE_Y(416)) time_adjust(field, 1);
                else if (ev->y0 >= UI_PAGE_Y(535)) time_adjust(field, -1);
                else return APP_REDRAW_NONE;
                return APP_REDRAW_PAGE;
            }
        }
        if (ev->y0 >= UI_PAGE_Y(767) && ev->y0 < UI_PAGE_Y(845)) { (void)time_save(); return APP_REDRAW_PAGE; }
        if (ev->y0 >= UI_PAGE_Y(874) && ev->y0 < UI_PAGE_Y(952)) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    int y = ev->y0;
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        if (y < UI_PAGE_Y(190)) { s_page = SETTINGS_WALLPAPER_FIXED; return APP_REDRAW_PAGE; }
        if (s_wallpaper_preview_ok && y >= UI_PAGE_Y(194) && y < UI_PAGE_Y(1032)) {
            s_wallpaper_confirm = true;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_WALLPAPER && y < UI_PAGE_Y(190)) {
        s_page = SETTINGS_MAIN;
        return APP_REDRAW_PAGE;
    }

    if((s_page==SETTINGS_WALLPAPER_FIXED||s_page==SETTINGS_WALLPAPER_FOLDER)&&y<UI_PAGE_Y(190)){s_page=SETTINGS_WALLPAPER;s_notice[0]=0;return APP_REDRAW_PAGE;}
    if(s_page==SETTINGS_WALLPAPER){
        if(y>=UI_PAGE_Y(236)&&y<UI_PAGE_Y(406)){wallpaper_scan();s_page=SETTINGS_WALLPAPER_FIXED;return APP_REDRAW_PAGE;}
        if(y>=UI_PAGE_Y(426)&&y<UI_PAGE_Y(596)){wallpaper_folder_open();return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_WALLPAPER_FOLDER){
        if(y>=UI_PAGE_Y(190)&&y<UI_PAGE_Y(250)&&ev->x0>=520&&strcmp(s_wallpaper_directory,"/sdcard")){
            char *slash=strrchr(s_wallpaper_directory,'/');if(slash&&slash>s_wallpaper_directory+6)*slash=0;
            wallpaper_folder_scan();s_notice[0]=0;return APP_REDRAW_PAGE;
        }
        if(ui_rect_hit((EpdRect){36,UI_PAGE_Y(266),612,82},ev->x0,y)){
            if(!ui_wallpaper_check_directory(ctx->fb,s_wallpaper_directory)){snprintf(s_notice,sizeof(s_notice),"此文件夹没有可用图片，请重新选择");return APP_REDRAW_PAGE;}
            if(app_settings_set_wallpaper_directory(s_wallpaper_directory)!=ESP_OK){snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;}
            s_notice[0]=0;s_page=SETTINGS_WALLPAPER;return APP_REDRAW_PAGE;
        }
        if(ev->x0>=36&&ev->x0<648&&y>=UI_PAGE_Y(416)&&y<UI_PAGE_Y(416)+wallpaper_folder_rows()*font_list_step()&&(y-UI_PAGE_Y(416))%font_list_step()<font_list_step()-12){
            int index=s_wallpaper_page*wallpaper_folder_rows()+(y-UI_PAGE_Y(416))/font_list_step();if(index>=s_wallpaper_count)return APP_REDRAW_NONE;
            snprintf(s_wallpaper_directory,sizeof(s_wallpaper_directory),"%s",s_wallpapers[index].path);wallpaper_folder_scan();s_notice[0]=0;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_IDLE_SLEEP&&y<UI_PAGE_Y(190)){s_page=SETTINGS_POWER_SLEEP;return APP_REDRAW_PAGE;}
    if(s_page==SETTINGS_STATUS_DATE_FORMAT && y<UI_PAGE_Y(190)){s_page=SETTINGS_TIME;return APP_REDRAW_PAGE;}
    if (s_page != SETTINGS_MAIN && y < UI_PAGE_Y(190)) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_FIRMWARE_INFO) {
        if (ui_rect_hit(firmware_update_rect(0),ev->x0,y)) {
            s_page=SETTINGS_ONLINE_UPDATE;s_online_pending=true;s_online_confirm=false;
            s_online_seen=0;s_online_drawn_at=s_online_reboot_at=0;
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(firmware_update_rect(1),ev->x0,y)) {
            s_notice[0] = 0; upgrade_open(); return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BATTERY) {
        if (y>=UI_PAGE_Y(346) && y<UI_PAGE_Y(414) && ev->x0>=36 && ev->x0<648) {
            s_battery_week = ev->x0>=348; return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_WIFI) {
        if (y >= UI_PAGE_Y(408) && y < UI_PAGE_Y(408)+SETTINGS_ROW_H) {
            extern const app_desc_t app_transfer;
            app_transfer_request_wifi_setup();
            ctx->request_app = &app_transfer;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_QUOTE_FONT) {
        if (s_page == SETTINGS_SYSTEM_FONT) {
            for (int i = 0; i < 4; ++i) if ((ev->x0-(system_size_rect(i).x+50))*(ev->x0-(system_size_rect(i).x+50))+(y-UI_PAGE_Y(300))*(y-UI_PAGE_Y(300))<=2500) {
                app_settings_set_system_font_size(size_values[i]);
                s_font_page=0; s_main_scroll=0;
                ui_text_set_system_scale(true);
                return APP_REDRAW_PAGE;
            }
        }
        int top = font_list_top(), rows = font_list_rows();
        if (y < top || y >= top + rows*font_list_step() || (y-top)%font_list_step() >= font_list_step()-12 || ev->x0 < 36 || ev->x0 >= 648) return APP_REDRAW_NONE;
        int index = s_font_page * rows + (y - top) / font_list_step();
        int count = ttf_font_count();
        if (index > count) return APP_REDRAW_NONE;
        const ttf_font_item_t *item = index ? ttf_font_item(index - 1) : NULL;
        if(s_page==SETTINGS_QUOTE_FONT){
            content_banner_config_t next=s_banner_draft;strlcpy(next.font,item?item->path:"",sizeof(next.font));
            if(!banner_preview_apply(&next,true))return APP_REDRAW_PAGE;
            s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;
        }
        app_settings_set_system_font_path(item ? item->path : "");
        app_font_activate_system();
        s_font_page=index/font_list_rows();
        if(s_main_scroll>SETTINGS_SCROLL_MAX)s_main_scroll=SETTINGS_SCROLL_MAX;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_WALLPAPER_FIXED) {
        if(y>=UI_PAGE_Y(200)&&y<UI_PAGE_Y(282)){app_settings_set_wallpaper_path("");s_page=SETTINGS_WALLPAPER;return APP_REDRAW_PAGE;}
        if (y < UI_PAGE_Y(292) || y >= UI_PAGE_Y(292)+wallpaper_rows()*font_list_step() || (y-UI_PAGE_Y(292))%font_list_step()>=font_list_step()-12) return APP_REDRAW_NONE;
        int index = s_wallpaper_page * wallpaper_rows() + (y - UI_PAGE_Y(292)) / font_list_step();
        if (index >= s_wallpaper_count) return APP_REDRAW_NONE;
        s_wallpaper_selected = index;
        s_wallpaper_confirm = false;
        s_page = SETTINGS_WALLPAPER_PREVIEW;
        return APP_REDRAW_PAGE;
    }
    if(s_page==SETTINGS_STATUS_DATE_FORMAT){
        for(int i=0;i<APP_STATUS_DATE_FORMAT_COUNT;++i)if(ui_rect_hit(status_date_rect(i),ev->x0,y)){
            app_settings_set_status_date_format((app_status_date_format_t)i);s_page=SETTINGS_TIME;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_TIME){
        if(y>=UI_PAGE_Y(407)&&y<UI_PAGE_Y(407)+SETTINGS_ROW_H){s_notice[0]=0;s_sync_pending=true;snprintf(s_notice,sizeof(s_notice),"正在获取网络时间…");return APP_REDRAW_PAGE;}
        if(y>=UI_PAGE_Y(518)&&y<UI_PAGE_Y(518)+SETTINGS_ROW_H){time_open();return APP_REDRAW_PAGE;}
        if(y>=status_display_row_y(0)&&y<status_display_row_y(0)+SETTINGS_ROW_H){
            app_settings_set_status_show_date(!app_settings_status_show_date());return APP_REDRAW_PAGE;
        }
        if(y>=status_display_row_y(1)&&y<status_display_row_y(1)+SETTINGS_ROW_H){s_page=SETTINGS_STATUS_DATE_FORMAT;return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_CLOUD){
        int content_y=y+s_cloud_scroll;
        for(int i=0;i<3;++i)if(content_y>=cloud_row_y(i)&&content_y<cloud_row_y(i)+SETTINGS_ROW_H){cloud_editor_open(i+1);break;}
        return APP_REDRAW_PAGE;
    }
    if(s_page==SETTINGS_IDLE_SLEEP){
        for(int i=0;i<6;++i)if(ui_rect_hit(status_date_rect(i),ev->x0,y)){
            app_settings_set_idle_sleep_minutes(idle_sleep_minutes[i]);s_page=SETTINGS_POWER_SLEEP;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        if (y >= UI_PAGE_Y(300) && y < UI_PAGE_Y(432)) {
            app_settings_set_staged_shutdown(false);
            return APP_REDRAW_PAGE;
        }
        if (y >= UI_PAGE_Y(454) && y < UI_PAGE_Y(586)) {
            app_settings_set_staged_shutdown(true);
            return APP_REDRAW_PAGE;
        }
        if(y>=UI_PAGE_Y(714)&&y<UI_PAGE_Y(714)+SETTINGS_ROW_H){s_page=SETTINGS_IDLE_SLEEP;return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_CONFIG) {
        if (y >= UI_PAGE_Y(561) && y < UI_PAGE_Y(644)) {
            s_config_confirm = false;
            esp_err_t err = app_settings_backup_save();
            snprintf(s_notice, sizeof(s_notice), "%s",
                     err == ESP_OK ? "已保存到 TF 卡根目录" :
                     err == ESP_ERR_INVALID_STATE ? "请检查TF卡、结束同步或先重试恢复" :
                     "保存失败，请检查 TF 卡剩余空间");
            return APP_REDRAW_PAGE;
        }
        if (y >= UI_PAGE_Y(681) && y < UI_PAGE_Y(764)) {
            if (!s_config_confirm) {
                s_config_confirm = true;
                snprintf(s_notice, sizeof(s_notice), "恢复将覆盖当前设置，请再点按一次确认");
                return APP_REDRAW_PAGE;
            }
            s_config_confirm = false;
            esp_err_t err = app_settings_backup_restore();
            if (err == ESP_OK) {
                ttf_font_scan();
                app_font_activate_system();
                ui_text_set_system_scale(true);
                // 阅读记录恢复后令首页和书架重新取进度、时长与排序。
                // Rebuild home and shelf caches after restoring reading records.
                book_store_notify_changed();
            }
            char saved_ssid[33] = {0};
            bool wifi_saved = false;
            if (err == ESP_OK)
                (void)read_pico_transfer_get_saved_wifi(saved_ssid, &wifi_saved);
            snprintf(s_notice, sizeof(s_notice), "%s",
                     err == ESP_OK ? (wifi_saved ? "配置与阅读记录已恢复；WiFi 请重新连接" :
                                                   "配置与阅读记录已恢复") :
                     err == ESP_ERR_INVALID_STATE ? "请检查TF卡，并先结束坚果云同步" :
                     err == ESP_ERR_NOT_FOUND ? "未找到 Pico-settings.backup" :
                     err == ESP_ERR_INVALID_RESPONSE ? "配置文件损坏或版本不兼容" :
                     "恢复未完成，请重试");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }

    if (y < UI_PAGE_Y(80) || y >= UI_NAV_TOP) return APP_REDRAW_NONE;
    y += s_main_scroll;
    if (y >= SETTINGS_WIRELESS_Y && y < SETTINGS_WIRELESS_Y + SETTINGS_ROW_H) {
        extern const app_desc_t app_transfer;
        app_transfer_request_wifi_setup();
        ctx->request_app = &app_transfer;
        return APP_REDRAW_NONE;
    }
    if (y >= SETTINGS_WIRELESS_Y + SETTINGS_ROW_H && y < SETTINGS_WIRELESS_Y + 2 * SETTINGS_ROW_H) {
        s_page = SETTINGS_CLOUD; s_cloud_scroll=0; s_notice[0] = 0; return APP_REDRAW_PAGE;
    }

    if (y >= SETTINGS_DISPLAY_Y && y < SETTINGS_DISPLAY_Y + SETTINGS_ROW_H) {
        ttf_font_scan();
        s_font_page = 0;
        s_page = SETTINGS_SYSTEM_FONT;
        return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_DISPLAY_Y+SETTINGS_ROW_H&&y<SETTINGS_DISPLAY_Y+2*SETTINGS_ROW_H){profile_editor_open(true);return APP_REDRAW_PAGE;}
    if(y>=SETTINGS_DISPLAY_Y+2*SETTINGS_ROW_H&&y<SETTINGS_DISPLAY_Y+3*SETTINGS_ROW_H){app_settings_set_home_full_refresh(!app_settings_home_full_refresh());return APP_REDRAW_PAGE;}
    if (y >= SETTINGS_DISPLAY_Y + 3 * SETTINGS_ROW_H && y < SETTINGS_DISPLAY_Y + 4 * SETTINGS_ROW_H) {
        wallpaper_scan(); s_page = SETTINGS_WALLPAPER; return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_DISPLAY_Y+4*SETTINGS_ROW_H&&y<SETTINGS_DISPLAY_Y+SETTINGS_DISPLAY_ROWS*SETTINGS_ROW_H){
        quote_preview_clear();content_banner_load(&s_banner_draft);s_quote_px=s_banner_draft.quote_px;s_quote_committed=false;s_notice[0]=0;
        s_page=SETTINGS_HOME_QUOTE;quote_preview_prepare(s_quote_px,false);return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DEVICE_Y && y < SETTINGS_DEVICE_Y + SETTINGS_DEVICE_ROWS * SETTINGS_ROW_H) {
        switch ((y - SETTINGS_DEVICE_Y) / SETTINGS_ROW_H) {
        case 0: s_page = SETTINGS_TIME; break;
        case 1: s_page = SETTINGS_POWER_SLEEP; break;
        case 2: s_battery_week = false; s_page = SETTINGS_BATTERY; break;
        case 3: s_page = SETTINGS_CONFIG; s_config_confirm = false; s_notice[0] = 0; break;
        case 4: s_page = SETTINGS_FIRMWARE_INFO; s_notice[0] = 0; break;
        case 5: s_boot_pending = false; s_page = SETTINGS_BOOT; break;
        }
        return APP_REDRAW_PAGE;
    }

    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if(key!=1&&(s_page==SETTINGS_SYSTEM_FONT||s_page==SETTINGS_QUOTE_FONT||s_page==SETTINGS_WALLPAPER_FIXED||s_page==SETTINGS_WALLPAPER_FOLDER)){
        bool wallpaper=s_page==SETTINGS_WALLPAPER_FIXED||s_page==SETTINGS_WALLPAPER_FOLDER;
        int *page=wallpaper?&s_wallpaper_page:&s_font_page;
        int rows=s_page==SETTINGS_WALLPAPER_FOLDER?wallpaper_folder_rows():wallpaper?wallpaper_rows():font_list_rows();
        int count=wallpaper?s_wallpaper_count:ttf_font_count()+1;
        if(count>rows){int next=*page+(key==0?-1:1);if(next<0||next*rows>=count)return APP_REDRAW_NONE;*page=next;return APP_REDRAW_PAGE;}
    }
    if(s_page==SETTINGS_ONLINE_UPDATE){
        online_close();s_online_pending=s_online_confirm=false;
        if(key==1)ui_nav_request(ctx,0);else s_page=SETTINGS_FIRMWARE_INFO;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_UPGRADE) {
        if (s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        s_upgrade_confirm = false;
        if (key != 1) { s_page = SETTINGS_FIRMWARE_INFO; return APP_REDRAW_PAGE; }
    }

    if (key == 1) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        if (s_wallpaper_confirm) s_wallpaper_confirm = false;
        else s_page = SETTINGS_WALLPAPER_FIXED;
        return APP_REDRAW_PAGE;
    }
    if(s_page==SETTINGS_WALLPAPER_FIXED||s_page==SETTINGS_WALLPAPER_FOLDER){s_page=SETTINGS_WALLPAPER;s_notice[0]=0;return APP_REDRAW_PAGE;}
    if (s_page == SETTINGS_WALLPAPER) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
    if(s_page==SETTINGS_HOME_QUOTE){ui_nav_request(ctx,0);return APP_REDRAW_NONE;}
    if(s_page==SETTINGS_BANNER_BROWSER){s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;}
    if(s_page==SETTINGS_QUOTE_FONT||s_page==SETTINGS_BANNER_IMAGE){s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;}
    if (s_page == SETTINGS_TEXT_EDIT) { memset(s_editor,0,sizeof(s_editor));if(s_home_quote_editor){s_home_quote_editor=false;s_page=SETTINGS_HOME_QUOTE;quote_preview_prepare(s_banner_draft.quote_px,false);return APP_REDRAW_PAGE;}s_page = editor_return_page(); return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TIME_EDIT) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
    if(s_page==SETTINGS_STATUS_DATE_FORMAT){s_page=SETTINGS_TIME;return APP_REDRAW_PAGE;}
    if(s_page==SETTINGS_IDLE_SLEEP){s_page=SETTINGS_POWER_SLEEP;return APP_REDRAW_PAGE;}
    if (s_page != SETTINGS_MAIN) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
    ui_nav_request(ctx, 0);
    return APP_REDRAW_NONE;
}
static void settings_exit(app_ctx_t *ctx) {
    (void)ctx; online_close();quote_preview_clear();s_quote_dragging=false;ui_text_input_clear(&s_text_input);memset(s_editor,0,sizeof(s_editor));
}
static void on_before_lock(app_ctx_t *ctx) {
    (void)ctx; online_close();s_quote_dragging=false;
    // 锁屏保留普通草稿；密码清除内容、光标及显示状态，唤醒后可重新输入。
    // Keep ordinary drafts across lock; wipe secret text, cursor and reveal state together.
    if (s_page == SETTINGS_TEXT_EDIT && s_text_input.secret) ui_text_input_clear(&s_text_input);
}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }
static bool settings_present(app_ctx_t *ctx, app_redraw_t redraw) {
    if(redraw==APP_REDRAW_AREA&&s_page==SETTINGS_ONLINE_UPDATE){
        render(ctx,ctx->fb);
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){0,UI_PAGE_Y(190),UI_LOCK_WIDTH,UI_NAV_TOP-UI_PAGE_Y(190)}));
        return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_page==SETTINGS_QUOTE_FONT){render(ctx,ctx->fb);guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){0,190,UI_LOCK_WIDTH,160}));return true;}
    if(redraw==APP_REDRAW_AREA && s_page==SETTINGS_TEXT_EDIT){
        render(ctx,ctx->fb);
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_text_input_area()));
        return true;
    }
    if (redraw == APP_REDRAW_AREA && s_scroll_present_pending &&
        (s_page == SETTINGS_MAIN ||
         s_page == SETTINGS_CLOUD || s_page == SETTINGS_SYSTEM_FONT ||
         s_page == SETTINGS_WALLPAPER_FIXED || s_page == SETTINGS_WALLPAPER_FOLDER)) {
        s_scroll_present_pending = false;
        render(ctx, ctx->fb);
        int top = s_page == SETTINGS_MAIN ? UI_STATUS_HEIGHT : UI_PAGE_Y(190);
        EpdRect area = {0, top, UI_LOCK_WIDTH, UI_NAV_TOP - top};
        // 只驱动变化像素，滚动次数不触发黑白清屏；题头与底栏保持稳定。
        // Drive changed pixels only; scroll counts never trigger a wipe, and header/nav stay stable.
        guard_draw_result(ctx->hl, update_display_natural_area(ctx->hl, area));
        return true;
    }
    if(redraw==APP_REDRAW_PAGE){render(ctx,ctx->fb);guard_draw_result(ctx->hl,update_display_natural_page(ctx->hl,(EpdRect){0}));return true;}
    return false;
}

static bool settings_prevent_idle_sleep(app_ctx_t *ctx){
    (void)ctx;if_update_status_t status;if_update_get_status(&status);
    return status.busy||s_local_pending||s_upgrade_restart;
}
const app_desc_t app_device_settings = {
    .title = "设置 Settings", .detail = "显示、连接与设备", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .prevent_idle_sleep=settings_prevent_idle_sleep,
    .on_enter = on_enter, .on_exit = settings_exit, .on_before_lock = on_before_lock, .render = render, .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key,
    .present = settings_present,
};
