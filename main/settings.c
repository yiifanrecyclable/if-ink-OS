/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 读写。打开失败就用深睡默认值，不擦除整个分区。
 *
 * NVS load/store. A failed open keeps the deep-sleep default; the
 * partition is not erased.
 * 恢复前保留整组旧状态；普通失败回退，回退失败保留TF恢复材料供重试。
 * Preserve all previous state before restore; roll back ordinary failures and retain TF material if rollback fails.
 */

#include "settings.h"
#include "book/reader_input.h"

#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "book/book_history_backup.h"
#include "cloud/cloud_config.h"
#include "ui/ui_wallpaper_rotation.h"

#define TAG "settings"
#define NVS_NS "read_pico"
#define NVS_KEY_SLEEP "sleep"
#define NVS_KEY_SHUTDOWN_MODE "shutdown"
#define NVS_KEY_HOME_FULL "home_full"
#define NVS_KEY_DEVICE_NAME "dev_name"
#define NVS_KEY_AVATAR "dev_avatar"
#define NVS_KEY_STATUS_SIGNATURE "status_sig"
#define NVS_KEY_STATUS_DATE "status_date"
#define NVS_KEY_IDLE_SLEEP "idle_sleep"
#define NVS_KEY_FONT "font"
#define NVS_KEY_SYS_FONT "sys_font"
#define NVS_KEY_SYS_SIZE "sys_size"
#define NVS_KEY_LOCK_STYLE "lock_ui"
#define NVS_KEY_WALLPAPER "wallpaper"
#define NVS_KEY_WAKE "lwake"
#define NVS_KEY_BOOT "lboot"
#define NVS_KEY_PICKUP "pickup"
#define NVS_KEY_BOOK_PX "bk_px"
#define NVS_KEY_READER_FULL "rd_gc16"
#define NVS_KEY_READER_TURN "rd_turn"
#define NVS_KEY_POWER_TURN "rd_power"
#define NVS_KEY_IMMERSIVE "rd_immersive"
#define NVS_KEY_FULLSCREEN "rd_fullscreen"
#define NVS_KEY_HIDE_IMAGES "rd_no_image"
#define NVS_KEY_HOLD_REFRESH "rd_hold_full"
#define NVS_KEY_VERTICAL_TURN "rd_vertical"
#define NVS_KEY_SHELF_RECENT "shelf_recent"
#define NVS_KEY_BOOK_LINE "bk_line"
#define NVS_KEY_BOOK_PARA "bk_para"
#define NVS_KEY_BOOK_MARGIN "bk_margin"
#define NVS_KEY_BOOK_TRACK "bk_track"
#define NVS_KEY_BOOK_INDENT "bk_indent"
#define NVS_KEY_BOOK_RULE "bk_rule"
#define NVS_KEY_BOOK_RULE_OFFSET "bk_rule_y"
#define NVS_KEY_BOOKS_DIR "books_dir"
#define NVS_KEY_FONTS_DIR "fonts_dir"
#define FONT_PATH_MAX 160
#define MEDIA_DIR_MAX 96

static app_sleep_mode_t s_sleep = APP_SLEEP_DEEP;
static bool s_staged_shutdown;
static bool s_home_full_refresh;
static char s_device_name[64] = "Pico";
static char s_avatar[288];
static char s_status_signature[96];
static uint8_t s_idle_sleep_minutes=5;
static uint8_t s_status_date; // 高位隐藏日期，低位保留格式。/ High bit hides date; low bits retain format.
static char s_font[FONT_PATH_MAX];
static char s_system_font[FONT_PATH_MAX];
static uint8_t system_size_preset(uint8_t value) {
    return value < 110 ? 100 : value < 130 ? 120 : value < 150 ? 140 : 160;
}
static uint8_t s_system_size = 120;
static uint8_t s_lock_style = 1;
static char s_wallpaper[288];
static char s_wallpaper_last[288];
static uint8_t s_last_wake;
static uint8_t s_last_boot;
static bool s_pickup_wake;
static uint8_t s_book_px = 48;

static uint8_t s_reader_full_pages = 15;
static uint8_t s_reader_turn_effect;
static uint8_t s_book_line = 130, s_book_para = 50, s_book_margin = 36;
static bool s_reader_power_turn;
static bool s_reader_immersive;
static bool s_reader_fullscreen;
static bool s_reader_hide_images;
static bool s_reader_hold_refresh;
static bool s_reader_vertical_turn;
static bool s_shelf_recent_sort;
static uint8_t s_book_tracking = 2, s_book_reading_line, s_book_rule_offset = 4;
static uint8_t s_book_indent = 2;
static char s_books_dir[MEDIA_DIR_MAX] = "/sdcard/books";
static char s_fonts_dir[MEDIA_DIR_MAX] = "/sdcard/fonts";
static void nvs_put_u8(const char* key, uint8_t value);
static bool idle_sleep_valid(uint8_t value){return value==0||value==1||value==3||value==5||value==10||value==30;}
static bool status_date_valid(uint8_t value) {return !(value&0x78) && (value&7)<APP_STATUS_DATE_FORMAT_COUNT;}


static bool valid_media_dir(const char* path) {
    if (!path || strncmp(path, "/sdcard/", 8) || !path[8] ||
        strlen(path) >= MEDIA_DIR_MAX || path[strlen(path) - 1] == '/') return false;
    const char *segment = path + 8;
    for (const unsigned char *p = (const unsigned char *)segment; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == '\\' || *p == ':') return false;
        if (*p == '/' && (p == (const unsigned char *)segment || p[-1] == '/')) return false;
    }
    for (const char *p = segment; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
        if (!end) break;
        p = end + 1;
    }
    return true;
}

static void remove_legacy_weread_session(void) {
    // 卸载在线阅读功能后，清除旧版保存的登录凭据；用户的本地书籍不受影响。
    // Clear credentials left by the removed online reader without touching local books.
    nvs_handle_t h;
    if (nvs_open("pico_weread", NVS_READONLY, &h) != ESP_OK) return;
    size_t size = 0;
    esp_err_t err = nvs_get_str(h, "cookie", NULL, &size);
    nvs_close(h);
    if (err != ESP_OK || nvs_open("pico_weread", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_key(h, "cookie") == ESP_OK) (void)nvs_commit(h);
    nvs_close(h);
}

static uint8_t valid_book_px(uint8_t px) {
    return px >= 36 && px <= 72 ? px : 48;
}

void app_settings_init(void) {
    esp_err_t err = nvs_flash_init();
    // 设置分区异常时保留原数据供恢复，绝不因空间不足自动擦除用户资料。
    // Preserve settings on NVS errors; a full partition must never silently erase profile data.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs init %s, use deep sleep", esp_err_to_name(err));
        return;
    }

    remove_legacy_weread_session();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t idle=5;
    if(nvs_get_u8(h,NVS_KEY_IDLE_SLEEP,&idle)==ESP_OK&&idle_sleep_valid(idle))s_idle_sleep_minutes=idle;
    uint8_t date=0;
    if(nvs_get_u8(h,NVS_KEY_STATUS_DATE,&date)==ESP_OK && status_date_valid(date))s_status_date=date;
    uint8_t raw = APP_SLEEP_DEEP;
    if (nvs_get_u8(h, NVS_KEY_SLEEP, &raw) == ESP_OK && raw <= APP_SLEEP_OFF) {
        s_sleep = (app_sleep_mode_t)raw;
    }
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_SHUTDOWN_MODE, &raw) == ESP_OK)
        s_staged_shutdown = raw == 1;
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_HOME_FULL, &raw) == ESP_OK) s_home_full_refresh = raw == 1;
    size_t value_len = sizeof(s_device_name);
    if (nvs_get_str(h, NVS_KEY_DEVICE_NAME, s_device_name, &value_len) != ESP_OK || !s_device_name[0])
        strlcpy(s_device_name, "Pico", sizeof(s_device_name));
    value_len = sizeof(s_avatar);
    if (nvs_get_str(h, NVS_KEY_AVATAR, s_avatar, &value_len) != ESP_OK ||
        (s_avatar[0] && strncmp(s_avatar, "/sdcard/", 8))) s_avatar[0] = 0;
    value_len = sizeof(s_status_signature);
    if (nvs_get_str(h, NVS_KEY_STATUS_SIGNATURE, s_status_signature, &value_len) != ESP_OK)
        s_status_signature[0] = 0;
    size_t font_len = sizeof(s_font);
    if (nvs_get_str(h, NVS_KEY_FONT, s_font, &font_len) != ESP_OK) {
        s_font[0] = '\0';
    }
    font_len = sizeof(s_system_font);
    if (nvs_get_str(h, NVS_KEY_SYS_FONT, s_system_font, &font_len) != ESP_OK)
        s_system_font[0] = 0;
    uint8_t system_size = 120;
    if (nvs_get_u8(h, NVS_KEY_SYS_SIZE, &system_size) == ESP_OK &&
        system_size >= 100 && system_size <= 200 && system_size % 10 == 0)
        s_system_size = system_size_preset(system_size);
    uint8_t lock_style = 0;
    if (nvs_get_u8(h, NVS_KEY_LOCK_STYLE, &lock_style) == ESP_OK && lock_style <= 1)
        s_lock_style = 1;
    size_t wallpaper_len = sizeof(s_wallpaper);
    if (nvs_get_str(h, NVS_KEY_WALLPAPER, s_wallpaper, &wallpaper_len) != ESP_OK ||
        !wallpaper_card_path_valid(s_wallpaper)) s_wallpaper[0] = 0;
    size_t last_len=sizeof(s_wallpaper_last);
    if(nvs_get_str(h,"wp_last",s_wallpaper_last,&last_len)!=ESP_OK || !wallpaper_card_path_valid(s_wallpaper_last)) s_wallpaper_last[0]=0;
    uint8_t wake = 0;
    if (nvs_get_u8(h, NVS_KEY_WAKE, &wake) == ESP_OK) s_last_wake = wake;
    uint8_t boot = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOT, &boot) == ESP_OK) s_last_boot = boot;
    uint8_t pickup = 0;
    if (nvs_get_u8(h, NVS_KEY_PICKUP, &pickup) == ESP_OK) s_pickup_wake = pickup != 0;
    uint8_t book_px = 48;
    if (nvs_get_u8(h, NVS_KEY_BOOK_PX, &book_px) == ESP_OK) s_book_px = valid_book_px(book_px);
    uint8_t fullscreen=0;
    if(nvs_get_u8(h,NVS_KEY_FULLSCREEN,&fullscreen)==ESP_OK&&fullscreen<=1)s_reader_fullscreen=fullscreen!=0;
    uint8_t reader_full_pages = 15;
    if (nvs_get_u8(h, NVS_KEY_READER_FULL, &reader_full_pages) == ESP_OK &&
        (reader_full_pages == 0 || reader_full_pages == 5 || reader_full_pages == 10 ||
         reader_full_pages == 15 || reader_full_pages == 30))
        s_reader_full_pages = reader_full_pages;
    uint8_t reader_turn_effect = 0;
    if (nvs_get_u8(h, NVS_KEY_READER_TURN, &reader_turn_effect) == ESP_OK && reader_turn_effect <= 1)
        s_reader_turn_effect = reader_turn_effect;
    uint8_t power_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_POWER_TURN, &power_turn) == ESP_OK) s_reader_power_turn = power_turn == 1;
    uint8_t immersive = 0;
    if (nvs_get_u8(h, NVS_KEY_IMMERSIVE, &immersive) == ESP_OK) s_reader_immersive = immersive == 1;
    uint8_t hold_refresh = 0;
    if (nvs_get_u8(h, NVS_KEY_HOLD_REFRESH, &hold_refresh) == ESP_OK) s_reader_hold_refresh = hold_refresh == 1;
    uint8_t vertical_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_VERTICAL_TURN, &vertical_turn) == ESP_OK) s_reader_vertical_turn = vertical_turn == 1;
    uint8_t hide_images = 0, recent_sort = 0;
    if (nvs_get_u8(h, NVS_KEY_HIDE_IMAGES, &hide_images) == ESP_OK) s_reader_hide_images = hide_images == 1;
    if (nvs_get_u8(h, NVS_KEY_SHELF_RECENT, &recent_sort) == ESP_OK) s_shelf_recent_sort = recent_sort == 1;
    uint8_t tracking = 2, reading_line = 0, rule_offset = 4, indent = 2;
    if (nvs_get_u8(h, NVS_KEY_BOOK_TRACK, &tracking) == ESP_OK && tracking <= 4)
        s_book_tracking = tracking;
    if (nvs_get_u8(h, NVS_KEY_BOOK_RULE, &reading_line) == ESP_OK && reading_line <= 2)
        s_book_reading_line = reading_line;
    if (nvs_get_u8(h, NVS_KEY_BOOK_RULE_OFFSET, &rule_offset) == ESP_OK && rule_offset <= 8)
        s_book_rule_offset = rule_offset;
    if (nvs_get_u8(h, NVS_KEY_BOOK_INDENT, &indent) == ESP_OK && indent <= 3)
        s_book_indent = indent;
    uint8_t line = 130, para = 50, margin = 36;
    if (nvs_get_u8(h, NVS_KEY_BOOK_LINE, &line) == ESP_OK) {
        if (line >= 110 && line <= 150) s_book_line = line;
        else if (line == 180) s_book_line = 150;
    }
    if (nvs_get_u8(h, NVS_KEY_BOOK_PARA, &para) == ESP_OK && para <= 75) s_book_para = para;
    if (nvs_get_u8(h, NVS_KEY_BOOK_MARGIN, &margin) == ESP_OK && margin >= 8 && margin <= 60)
        s_book_margin = margin;
    // 标准目录固定，忽略旧版自定义路径但不移动任何文件。/ Ignore legacy custom roots without moving files.
    strlcpy(s_books_dir,"/sdcard/books",sizeof(s_books_dir));
    strlcpy(s_fonts_dir,"/sdcard/fonts",sizeof(s_fonts_dir));
    nvs_close(h);
    ESP_LOGI(
        TAG, "sleep mode %s, font %s",
        app_sleep_mode_name(s_sleep),
        s_font[0] != '\0' ? s_font : "(builtin)"
    );
}

app_sleep_mode_t app_settings_sleep_mode(void) {
    return s_sleep;
}

const char* app_sleep_mode_name(app_sleep_mode_t mode) {
    switch (mode) {
        case APP_SLEEP_LIGHT: return "light";
        case APP_SLEEP_DEEP: return "deep";
        case APP_SLEEP_OFF: return "off";
        default: return "?";
    }
}

void app_settings_set_sleep_mode(app_sleep_mode_t mode) {
    if (mode > APP_SLEEP_OFF) mode = APP_SLEEP_DEEP;
    s_sleep = mode;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY_SLEEP, (uint8_t)mode);
    nvs_commit(h);
    nvs_close(h);
}

bool app_settings_staged_shutdown(void) { return s_staged_shutdown; }
void app_settings_set_staged_shutdown(bool staged) {
    if (staged == s_staged_shutdown) return;
    s_staged_shutdown = staged;
    nvs_put_u8(NVS_KEY_SHUTDOWN_MODE, staged ? 1 : 0);
}

bool app_settings_home_full_refresh(void) { return s_home_full_refresh; }
void app_settings_set_home_full_refresh(bool enabled) {
    if (enabled == s_home_full_refresh) return;
    s_home_full_refresh = enabled;
    nvs_put_u8(NVS_KEY_HOME_FULL, enabled ? 1 : 0);
}

static bool nvs_put_str(const char *key, const char *value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "save %s open: %s", key, esp_err_to_name(err)); return false; }
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save %s: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

const char *app_settings_device_name(void) { return s_device_name; }
void app_settings_set_device_name(const char *name) {
    if (!name || !name[0] || strnlen(name, sizeof(s_device_name)) >= sizeof(s_device_name)) return;
    if (nvs_put_str(NVS_KEY_DEVICE_NAME, name)) strlcpy(s_device_name, name, sizeof(s_device_name));
}
const char *app_settings_avatar_path(void) { return s_avatar; }
void app_settings_set_avatar_path(const char *path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/", 8)) ||
        strnlen(path, sizeof(s_avatar)) >= sizeof(s_avatar)) return;
    if (nvs_put_str(NVS_KEY_AVATAR, path)) strlcpy(s_avatar, path, sizeof(s_avatar));
}
const char *app_settings_status_signature(void) { return s_status_signature; }
void app_settings_set_status_signature(const char *signature) {
    if (!signature || strnlen(signature, sizeof(s_status_signature)) >= sizeof(s_status_signature)) return;
    if (nvs_put_str(NVS_KEY_STATUS_SIGNATURE, signature))
        strlcpy(s_status_signature, signature, sizeof(s_status_signature));
}

uint8_t app_settings_idle_sleep_minutes(void){return s_idle_sleep_minutes;}
void app_settings_set_idle_sleep_minutes(uint8_t minutes){
    if(!idle_sleep_valid(minutes))return;
    s_idle_sleep_minutes=minutes;nvs_put_u8(NVS_KEY_IDLE_SLEEP,minutes);
}

bool app_settings_status_show_date(void){return !(s_status_date&0x80);}
void app_settings_set_status_show_date(bool show){
    s_status_date=(s_status_date&7)|(show?0:0x80);nvs_put_u8(NVS_KEY_STATUS_DATE,s_status_date);
}
app_status_date_format_t app_settings_status_date_format(void){return (app_status_date_format_t)(s_status_date&7);}
void app_settings_set_status_date_format(app_status_date_format_t format){
    if((unsigned)format>=APP_STATUS_DATE_FORMAT_COUNT)return;
    s_status_date=(s_status_date&0x80)|(uint8_t)format;nvs_put_u8(NVS_KEY_STATUS_DATE,s_status_date);
}
void app_settings_format_status_clock(time_t now,char *out,size_t capacity){
    if(!out||!capacity)return;
    snprintf(out,capacity,"--:--");
    if(now<1704067200)return;
    time_t china=now+8*3600;struct tm date;
    if(!gmtime_r(&china,&date))return;
    if(!app_settings_status_show_date()){strftime(out,capacity,"%H:%M",&date);return;}
    switch(app_settings_status_date_format()){
        case APP_STATUS_DATE_SLASH:strftime(out,capacity,"%Y/%m/%d %H:%M",&date);break;
        case APP_STATUS_DATE_DOT:strftime(out,capacity,"%Y.%m.%d %H:%M",&date);break;
        case APP_STATUS_DATE_MONTH_DAY:strftime(out,capacity,"%m-%d %H:%M",&date);break;
        case APP_STATUS_DATE_MONTH_DAY_SLASH:strftime(out,capacity,"%m/%d %H:%M",&date);break;
        default:strftime(out,capacity,"%Y-%m-%d %H:%M",&date);break;
    }
}

const char* app_settings_font_path(void) {
    return s_font;
}

const char* app_settings_system_font_path(void) { return s_system_font; }
void app_settings_set_system_font_path(const char* path) {
    if (!path) path = "";
    if (strnlen(path, sizeof(s_system_font)) >= sizeof(s_system_font)) return;
    strlcpy(s_system_font, path, sizeof(s_system_font));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SYS_FONT, s_system_font);
    nvs_commit(h);
    nvs_close(h);
}
uint8_t app_settings_system_font_size(void) { return s_system_size; }
void app_settings_set_system_font_size(uint8_t percent) {
    if (percent < 100 || percent > 200 || percent % 10 || percent == s_system_size) return;
    percent = system_size_preset(percent);
    s_system_size = percent;
    nvs_put_u8(NVS_KEY_SYS_SIZE, percent);
}
uint8_t app_settings_lock_style(void) { return s_lock_style; }
void app_settings_set_lock_style(uint8_t style) {
    (void)style;
    s_lock_style = 1;
}
const char* app_settings_wallpaper_path(void) { return s_wallpaper; }
// 目录以末尾斜线存入既有字段，保持备份格式不变。/ A trailing slash encodes a directory in the existing backup field.
bool app_settings_wallpaper_rotating(void) { size_t n=strlen(s_wallpaper);return n && s_wallpaper[n-1]=='/'; }
static esp_err_t wallpaper_store(const char *path) {
    if(!path || (path[0] && !wallpaper_card_path_valid(path)) || strlen(path)>=sizeof(s_wallpaper))return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;esp_err_t err=nvs_open(NVS_NS,NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    err=nvs_set_str(h,NVS_KEY_WALLPAPER,path);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);
    if(err==ESP_OK)strlcpy(s_wallpaper,path,sizeof(s_wallpaper));
    return err;
}
void app_settings_set_wallpaper_path(const char* path) { (void)wallpaper_store(path); }
esp_err_t app_settings_set_wallpaper_directory(const char *path) {
    if(!wallpaper_card_path_valid(path))return ESP_ERR_INVALID_ARG;
    char directory[288];size_t n=strlen(path);
    if(snprintf(directory,sizeof(directory),"%s%s",path,n && path[n-1]=='/'?"":"/") >= (int)sizeof(directory))return ESP_ERR_INVALID_ARG;
    return wallpaper_store(directory);
}
const char *app_settings_wallpaper_last(void){return s_wallpaper_last;}
void app_settings_note_wallpaper(const char *path){
    if(!wallpaper_card_path_valid(path)||!strcmp(path,s_wallpaper_last))return;
    strlcpy(s_wallpaper_last,path,sizeof(s_wallpaper_last));
    nvs_handle_t h;if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return;
    if(nvs_set_str(h,"wp_last",path)==ESP_OK)(void)nvs_commit(h);
    nvs_close(h);
}

static void nvs_put_u8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_last_wake(void) {
    return s_last_wake;
}

void app_settings_set_last_wake(uint8_t src) {
    if (src == s_last_wake) return;
    s_last_wake = src;
    nvs_put_u8(NVS_KEY_WAKE, src);
}

uint8_t app_settings_last_boot(void) {
    return s_last_boot;
}

void app_settings_set_last_boot(uint8_t reason) {
    if (reason == 0 || reason == s_last_boot) return;
    s_last_boot = reason;
    nvs_put_u8(NVS_KEY_BOOT, reason);
}

bool app_settings_pickup_wake(void) {
    return s_pickup_wake;
}

void app_settings_set_pickup_wake(bool on) {
    if (s_pickup_wake == on) return;
    s_pickup_wake = on;
    nvs_put_u8(NVS_KEY_PICKUP, on ? 1 : 0);
}

void app_settings_set_font_path(const char* path) {
    if (path == NULL) path = "";
    strlcpy(s_font, path, sizeof(s_font));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_FONT, s_font);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_book_px(void) {
    return s_book_px;
}

void app_settings_set_book_px(uint8_t px) {
    px = valid_book_px(px);
    if (s_book_px == px) return;
    s_book_px = px;
    nvs_put_u8(NVS_KEY_BOOK_PX, px);
}

uint8_t app_settings_reader_full_pages(void) { return s_reader_full_pages; }
void app_settings_set_reader_full_pages(uint8_t pages) {
    if ((pages != 0 && pages != 5 && pages != 10 && pages != 15 && pages != 30) ||
        pages == s_reader_full_pages) return;
    s_reader_full_pages = pages;
    nvs_put_u8(NVS_KEY_READER_FULL, pages);
}
uint8_t app_settings_reader_turn_effect(void) { return s_reader_turn_effect; }
void app_settings_set_reader_turn_effect(uint8_t effect) {
    if (effect > 1 || effect == s_reader_turn_effect) return;
    s_reader_turn_effect = effect;
    nvs_put_u8(NVS_KEY_READER_TURN, effect);
}
bool app_settings_reader_power_turn(void) { return s_reader_power_turn; }
void app_settings_set_reader_power_turn(bool on) {
    if (s_reader_power_turn == on) return;
    s_reader_power_turn = on;
    nvs_put_u8(NVS_KEY_POWER_TURN, on ? 1 : 0);
}
bool app_settings_reader_fullscreen(void){return s_reader_fullscreen;}
void app_settings_set_reader_fullscreen(bool on){if(s_reader_fullscreen==on)return;s_reader_fullscreen=on;nvs_put_u8(NVS_KEY_FULLSCREEN,on?1:0);}
bool app_settings_reader_immersive(void) { return s_reader_immersive; }
void app_settings_set_reader_immersive(bool on) {
    if (s_reader_immersive == on) return;
    s_reader_immersive = on;
    nvs_put_u8(NVS_KEY_IMMERSIVE, on ? 1 : 0);
}
bool app_settings_reader_hide_images(void) { return s_reader_hide_images; }
void app_settings_set_reader_hide_images(bool on) {
    if (s_reader_hide_images == on) return;
    s_reader_hide_images = on;
    nvs_put_u8(NVS_KEY_HIDE_IMAGES, on ? 1 : 0);
}
bool app_settings_reader_hold_refresh(void) { return s_reader_hold_refresh; }
bool app_settings_reader_vertical_turn(void) { return s_reader_vertical_turn; }
void app_settings_set_reader_vertical_turn(bool on) {
    if (s_reader_vertical_turn == on) return;
    s_reader_vertical_turn = on;
    nvs_put_u8(NVS_KEY_VERTICAL_TURN, on ? 1 : 0);
}
void app_settings_set_reader_hold_refresh(bool on) {
    if (s_reader_hold_refresh == on) return;
    s_reader_hold_refresh = on;
    nvs_put_u8(NVS_KEY_HOLD_REFRESH, on ? 1 : 0);
}

bool app_settings_shelf_recent_sort(void) { return s_shelf_recent_sort; }
void app_settings_set_shelf_recent_sort(bool on) {
    if (s_shelf_recent_sort == on) return;
    s_shelf_recent_sort = on;
    nvs_put_u8(NVS_KEY_SHELF_RECENT, on ? 1 : 0);
}
uint8_t app_settings_book_tracking(void) { return s_book_tracking; }
void app_settings_set_book_tracking(uint8_t index) {
    if (index > 4 || index == s_book_tracking) return;
    s_book_tracking = index;
    nvs_put_u8(NVS_KEY_BOOK_TRACK, index);
}
uint8_t app_settings_book_indent(void) { return s_book_indent; }
void app_settings_set_book_indent(uint8_t em) {
    if (em > 3 || em == s_book_indent) return;
    s_book_indent = em;
    nvs_put_u8(NVS_KEY_BOOK_INDENT, em);
}
uint8_t app_settings_book_reading_line(void) { return s_book_reading_line; }
void app_settings_set_book_reading_line(uint8_t style) {
    if (style > 2 || style == s_book_reading_line) return;
    s_book_reading_line = style;
    nvs_put_u8(NVS_KEY_BOOK_RULE, style);
}
int8_t app_settings_book_reading_line_offset(void) { return (int8_t)((int)s_book_rule_offset - 4) * 2; }
void app_settings_set_book_reading_line_offset(int8_t offset_px) {
    if (offset_px < -8 || offset_px > 8 || offset_px % 2) return;
    uint8_t index = (uint8_t)(offset_px / 2 + 4);
    if (index == s_book_rule_offset) return;
    s_book_rule_offset = index;
    nvs_put_u8(NVS_KEY_BOOK_RULE_OFFSET, index);
}
uint8_t app_settings_book_line_spacing(void) { return s_book_line; }
void app_settings_set_book_line_spacing(uint8_t percent) {
    if (percent < 110 || percent > 150) return;
    if (s_book_line == percent) return;
    s_book_line = percent;
    nvs_put_u8(NVS_KEY_BOOK_LINE, percent);
}
uint8_t app_settings_book_margin(void) { return s_book_margin; }
void app_settings_set_book_margin(uint8_t px) {
    if (px < 8 || px > 60 || s_book_margin == px) return;
    s_book_margin = px;
    nvs_put_u8(NVS_KEY_BOOK_MARGIN, px);
}
uint8_t app_settings_book_paragraph_spacing(void) { return s_book_para; }
void app_settings_set_book_paragraph_spacing(uint8_t percent) {
    if (percent > 75) return;
    if (s_book_para == percent) return;
    s_book_para = percent;
    nvs_put_u8(NVS_KEY_BOOK_PARA, percent);
}

const char* app_settings_books_dir(void) { return s_books_dir; }
const char* app_settings_fonts_dir(void) { return s_fonts_dir; }
bool app_settings_set_books_dir(const char* path) {
    return path && !strcmp(path,"/sdcard/books");
}
bool app_settings_set_fonts_dir(const char* path) {
    return path && !strcmp(path,"/sdcard/fonts");
}

#ifndef APP_SETTINGS_BACKUP_ROOT
#define APP_SETTINGS_BACKUP_ROOT "/sdcard"
#endif
#define BACKUP_FILE APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup"
#define BACKUP_TEMP APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup.tmp"
#define BACKUP_PREVIOUS APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup.previous"
#define RESTORE_BEFORE APP_SETTINGS_BACKUP_ROOT "/.Pico-restore.before"
#define RESTORE_TEMP APP_SETTINGS_BACKUP_ROOT "/.Pico-restore.before.tmp"
#define RESTORE_PREVIOUS APP_SETTINGS_BACKUP_ROOT "/.Pico-restore.before.previous"
#define RESTORE_DONE APP_SETTINGS_BACKUP_ROOT "/.Pico-restore.done"

// All fields are bytes, so the v1 disk layout is independent of structure padding.
// 所有字段均为字节，v1 磁盘格式不依赖编译器的结构体填充。
typedef struct {
    char magic[8];
    uint8_t flags[17];
    char font[FONT_PATH_MAX];
    char system_font[FONT_PATH_MAX];
    char wallpaper[288];
    char books_dir[MEDIA_DIR_MAX];
    char fonts_dir[MEDIA_DIR_MAX];
    uint8_t checksum[4];
} settings_backup_v1_t;

typedef struct {
    char device_name[64];
    char avatar[288];
    char status_signature[96];
    uint8_t home_full_refresh;
    uint8_t checksum[4];
} settings_backup_profile_t;

typedef struct {
    read_pico_transfer_wifi_backup_t credentials;
    uint8_t checksum[4];
} settings_backup_wifi_t;

typedef struct {
    cloud_config_t credentials;
    uint8_t checksum[4];
} settings_backup_cloud_t;

static void backup_erase_secret(void *ptr, size_t size) {
    volatile uint8_t *bytes = ptr;
    while (size--) *bytes++ = 0;
}

static uint32_t backup_wifi_checksum(const settings_backup_wifi_t *wifi) {
    const uint8_t *bytes = (const uint8_t *)wifi;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_wifi_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void backup_wifi_seal(settings_backup_wifi_t *wifi) {
    uint32_t hash = backup_wifi_checksum(wifi);
    for (int i = 0; i < 4; ++i) wifi->checksum[i] = (uint8_t)(hash >> (i * 8));
}

static bool backup_wifi_valid(const settings_backup_wifi_t *wifi) {
    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= (uint32_t)wifi->checksum[i] << (i * 8);
    return stored == backup_wifi_checksum(wifi) &&
           read_pico_transfer_wifi_backup_valid(&wifi->credentials);
}

// IF v8追加坚果云配置；旧版恢复时保留现有坚果云配置。/ IF v8 appends Nutstore; restoring older backups leaves it unchanged.
static uint32_t backup_cloud_checksum(const settings_backup_cloud_t *cloud) {
    const uint8_t *bytes = (const uint8_t *)cloud;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_cloud_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
static void backup_cloud_seal(settings_backup_cloud_t *cloud) {
    uint32_t hash = backup_cloud_checksum(cloud);
    for (int i = 0; i < 4; ++i) cloud->checksum[i] = (uint8_t)(hash >> (i * 8));
}
static bool backup_cloud_valid(const settings_backup_cloud_t *cloud) {
    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= (uint32_t)cloud->checksum[i] << (i * 8);
    return stored == backup_cloud_checksum(cloud) && cloud_config_valid(&cloud->credentials, false);
}

enum {
    BK_SLEEP, BK_PICKUP, BK_SYS_SIZE, BK_SYS_CONTRAST, BK_LOCK,
    BK_BOOK_PX, BK_SHAKE, BK_FULL_PAGES, BK_TURN, BK_POWER_TURN,
    BK_IMMERSIVE, BK_TRACKING, BK_READING_LINE, BK_LINE_SPACING,
    BK_MARGIN, BK_PARAGRAPH, BK_SHELF,
};

static uint32_t backup_checksum(const settings_backup_v1_t *backup) {
    const uint8_t *data = (const uint8_t *)backup;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_v1_t, checksum); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static uint32_t backup_indent_checksum(const settings_backup_v1_t *backup, uint8_t indent) {
    const uint8_t *data = (const uint8_t *)backup;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(*backup); ++i) hash = (hash ^ data[i]) * 16777619u;
    return (hash ^ indent) * 16777619u;
}
static uint32_t backup_rule_offset_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                            uint8_t rule_offset) {
    return (backup_indent_checksum(backup, indent) ^ rule_offset) * 16777619u;
}
static uint32_t backup_shutdown_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                         uint8_t rule_offset, uint8_t staged_shutdown) {
    return (backup_rule_offset_checksum(backup, indent, rule_offset) ^ staged_shutdown) * 16777619u;
}
static uint32_t backup_profile_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                        uint8_t rule_offset, uint8_t staged_shutdown,
                                        const settings_backup_profile_t *profile) {
    uint32_t hash = backup_shutdown_checksum(backup, indent, rule_offset, staged_shutdown);
    const uint8_t *data = (const uint8_t *)profile;
    for (size_t i = 0; i < offsetof(settings_backup_profile_t, checksum); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static uint32_t backup_date_checksum(const settings_backup_v1_t *backup,uint8_t value){
    return (backup_checksum(backup)^value)*16777619u;
}

static void backup_seal(settings_backup_v1_t *backup) {
    uint32_t value = backup_checksum(backup);
    for (int i = 0; i < 4; ++i) backup->checksum[i] = (uint8_t)(value >> (i * 8));
}

static bool backup_card_ready(void) {
    read_pico_sd_info_t info = {0};
    return read_pico_sd_get_info(&info) == ESP_OK && info.mounted;
}

static uint32_t backup_reader_checksum(const settings_backup_v1_t *,const reader_input_preferences_t *);

static esp_err_t backup_save_to(const char *target, const char *temporary, const char *previous) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    settings_backup_v1_t backup = {0};
    memcpy(backup.magic, "PICOSETB", sizeof(backup.magic));
    uint8_t *f = backup.flags;
    f[BK_SLEEP] = s_sleep;
    f[BK_PICKUP] = s_pickup_wake;
    f[BK_SYS_SIZE] = s_system_size;
    f[BK_SYS_CONTRAST] = 100; // 退役字段保持旧备份格式兼容。/ Retired field preserves the legacy backup format.
    f[BK_LOCK] = s_lock_style;
    f[BK_BOOK_PX] = s_book_px;
    f[BK_SHAKE] = 0; // 旧备份占位，不再启用晃动翻页。/ Retired backup slot never enables shake turning.
    f[BK_FULL_PAGES] = s_reader_full_pages;
    f[BK_TURN] = s_reader_turn_effect;
    f[BK_POWER_TURN] = s_reader_power_turn;
    f[BK_IMMERSIVE] = s_reader_immersive;
    f[BK_TRACKING] = s_book_tracking;
    f[BK_READING_LINE] = s_book_reading_line;
    f[BK_LINE_SPACING] = s_book_line;
    f[BK_MARGIN] = s_book_margin;
    f[BK_PARAGRAPH] = s_book_para;
    // 旧备份的外观槽保留固定有效值，不再控制显示。/ Keep a valid legacy style slot for backup compatibility only.
    f[BK_SHELF]=2;
    strlcpy(backup.font, !strcmp(s_font, "builtin") ? "" : s_font, sizeof(backup.font));
    strlcpy(backup.system_font, s_system_font, sizeof(backup.system_font));
    strlcpy(backup.wallpaper, s_wallpaper, sizeof(backup.wallpaper));
    strlcpy(backup.books_dir, s_books_dir, sizeof(backup.books_dir));
    strlcpy(backup.fonts_dir, s_fonts_dir, sizeof(backup.fonts_dir));
    backup_seal(&backup);
    uint8_t extension[7] = {s_book_indent, s_book_rule_offset, s_staged_shutdown ? 1 : 0};
    uint32_t extension_hash = backup_shutdown_checksum(&backup, s_book_indent,
                                                        s_book_rule_offset, extension[2]);
    for (int i = 0; i < 4; ++i) extension[i + 3] = (uint8_t)(extension_hash >> (i * 8));
    settings_backup_profile_t profile = {0};
    strlcpy(profile.device_name, s_device_name, sizeof(profile.device_name));
    strlcpy(profile.avatar, s_avatar, sizeof(profile.avatar));
    strlcpy(profile.status_signature, s_status_signature, sizeof(profile.status_signature));
    // v5 及之后复用此字段的预留位；v7 在其后追加 WiFi 与阅读资料。
    // V5+ use reserved bits here; v7 appends WiFi and reading records afterward.
    profile.home_full_refresh = (s_home_full_refresh ? 1 : 0) |
                                (s_reader_hide_images ? 2 : 0) |
                                (s_shelf_recent_sort ? 4 : 0) |
                                (s_reader_hold_refresh ? 16 : 0) |
                                (s_reader_vertical_turn ? 32 : 0) |
                                (s_reader_fullscreen ? 64 : 0);
    uint32_t profile_hash = backup_profile_checksum(&backup, extension[0], extension[1], extension[2], &profile);
    for (int i = 0; i < 4; ++i) profile.checksum[i] = (uint8_t)(profile_hash >> (i * 8));

    settings_backup_wifi_t wifi = {0};
    esp_err_t wifi_err = read_pico_transfer_export_wifi_backup(&wifi.credentials);
    if (wifi_err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return wifi_err; }
    backup_wifi_seal(&wifi);

    settings_backup_cloud_t cloud = {0};
    esp_err_t cloud_err = cloud_config_backup_export(&cloud.credentials);
    if (cloud_err != ESP_OK) {
        backup_erase_secret(&cloud, sizeof(cloud));
        backup_erase_secret(&wifi, sizeof(wifi));
        return cloud_err;
    }
    backup_cloud_seal(&cloud);

    FILE *file = fopen(temporary, "wb");
    if (!file) { backup_erase_secret(&cloud, sizeof(cloud)); backup_erase_secret(&wifi, sizeof(wifi)); return ESP_FAIL; }
    bool ok = fwrite(&backup, 1, sizeof(backup), file) == sizeof(backup);
    if (ok) ok = fwrite(extension, 1, sizeof(extension), file) == sizeof(extension);
    if (ok) ok = fwrite(&profile, 1, sizeof(profile), file) == sizeof(profile);
    if (ok) ok = fwrite(&wifi, 1, sizeof(wifi), file) == sizeof(wifi);
    if (ok) ok = fwrite(&cloud, 1, sizeof(cloud), file) == sizeof(cloud);
    uint8_t display[6]={s_status_date,s_idle_sleep_minutes};
    uint32_t display_hash=(backup_date_checksum(&backup,s_status_date)^s_idle_sleep_minutes)*16777619u;
    for(int i=0;i<4;++i)display[i+2]=(uint8_t)(display_hash>>(i*8));
    if(ok)ok=fwrite(display,1,sizeof(display),file)==sizeof(display);
    reader_input_preferences_t reader_prefs;reader_input_export(&reader_prefs);
    uint32_t reader_hash=backup_reader_checksum(&backup,&reader_prefs);uint8_t reader_seal[4];
    for(int i=0;i<4;++i)reader_seal[i]=(uint8_t)(reader_hash>>(8*i));
    if(ok)ok=fwrite(&reader_prefs,1,sizeof(reader_prefs),file)==sizeof(reader_prefs)&&fwrite(reader_seal,1,4,file)==4;
    backup_erase_secret(&cloud, sizeof(cloud));
    backup_erase_secret(&wifi, sizeof(wifi));
    if (ok) ok = book_history_backup_write(file) == ESP_OK;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    bool rotated = false;
    if (ok && remove(previous) != 0 && errno != ENOENT) ok = false;
    if (ok && rename(target, previous) == 0) rotated = true;
    else if (ok && errno != ENOENT) ok = false;
    if (ok && rename(temporary, target) != 0) ok = false;
    if (!ok && rotated) (void)rename(previous, target);
    if (ok && rotated) (void)remove(previous);
    if (!ok) { (void)remove(temporary); return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t app_settings_backup_save(void) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    struct stat pending;
    if (stat(RESTORE_BEFORE, &pending) == 0) return ESP_ERR_INVALID_STATE;
    if (errno != ENOENT) return ESP_FAIL;
    return backup_save_to(BACKUP_FILE, BACKUP_TEMP, BACKUP_PREVIOUS);
}

static bool backup_path_valid(const char *path, size_t capacity) {
    size_t len = strnlen(path, capacity);
    if (len == capacity) return false;
    if (!len) return true;
    if (strncmp(path, "/sdcard/", 8) || !path[8]) return false;
    for (size_t i = 8; i < len; ++i)
        if ((unsigned char)path[i] < 32 || path[i] == '\\' || path[i] == ':') return false;
    for (const char *part = path + 8; *part;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
        if (!end) break;
        part = end + 1;
    }
    return true;
}

static bool backup_valid(const settings_backup_v1_t *backup) {
    const uint8_t *f = backup->flags;
    uint32_t checksum = 0;
    for (int i = 0; i < 4; ++i) checksum |= (uint32_t)backup->checksum[i] << (i * 8);
    if ((memcmp(backup->magic, "PICOSET1", 8) && memcmp(backup->magic, "PICOSET2", 8) &&
         memcmp(backup->magic, "PICOSET3", 8) && memcmp(backup->magic, "PICOSET4", 8) &&
         memcmp(backup->magic, "PICOSET5", 8) && memcmp(backup->magic, "PICOSET6", 8) &&
         memcmp(backup->magic, "PICOSET7", 8) && memcmp(backup->magic, "PICOSET8", 8) && memcmp(backup->magic,"PICOSET9",8) && memcmp(backup->magic,"PICOSETA",8) && memcmp(backup->magic,"PICOSETB",8)) ||
        checksum != backup_checksum(backup)) return false;
    if (f[BK_SLEEP] > APP_SLEEP_OFF || f[BK_PICKUP] > 1 ||
        f[BK_SYS_SIZE] < 100 || f[BK_SYS_SIZE] > 200 || f[BK_SYS_SIZE] % 10 ||
        f[BK_SYS_CONTRAST] < 100 || f[BK_SYS_CONTRAST] > 140 || f[BK_SYS_CONTRAST] % 10 ||
        f[BK_LOCK] > 1 || f[BK_BOOK_PX] < 36 || f[BK_BOOK_PX] > 72 ||
        f[BK_SHAKE] > 1 || (f[BK_FULL_PAGES] != 0 && f[BK_FULL_PAGES] != 5 &&
                            f[BK_FULL_PAGES] != 10 && f[BK_FULL_PAGES] != 15 &&
                            f[BK_FULL_PAGES] != 30) ||
        f[BK_TURN] > 1 || f[BK_POWER_TURN] > 1 || f[BK_IMMERSIVE] > 1 ||
        f[BK_TRACKING] > 4 || f[BK_READING_LINE] > 2 ||
        f[BK_LINE_SPACING] < 110 || f[BK_LINE_SPACING] > 150 ||
        f[BK_MARGIN] < 8 || f[BK_MARGIN] > 60 ||
        f[BK_PARAGRAPH] > 75 ||
        f[BK_SHELF] < 1 || f[BK_SHELF] > 4) return false;
    if (strnlen(backup->books_dir, sizeof(backup->books_dir)) == sizeof(backup->books_dir) ||
        strnlen(backup->fonts_dir, sizeof(backup->fonts_dir)) == sizeof(backup->fonts_dir)) return false;
    return backup_path_valid(backup->font, sizeof(backup->font)) &&
           backup_path_valid(backup->system_font, sizeof(backup->system_font)) &&
           (!backup->wallpaper[0] || wallpaper_card_path_valid(backup->wallpaper)) &&
           valid_media_dir(backup->books_dir) && valid_media_dir(backup->fonts_dir);
}

static bool backup_file_exists(const char *path, bool directory) {
    struct stat st;
    return stat(path, &st) == 0 && (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

static uint32_t backup_reader_checksum(const settings_backup_v1_t *backup,const reader_input_preferences_t *prefs){
    uint32_t hash=backup_checksum(backup);const uint8_t *bytes=(const uint8_t *)prefs;
    for(size_t i=0;i<sizeof(*prefs);++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}

static esp_err_t backup_restore_from(const char *source, bool *mutated) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    *mutated = false;
    FILE *file = fopen(source, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    settings_backup_v1_t backup;
    bool ok = fread(&backup, 1, sizeof(backup), file) == sizeof(backup);
    uint8_t indent = 2, rule_offset = 4, staged_shutdown = 0;
    settings_backup_profile_t profile = {.device_name = "Pico"};
    settings_backup_wifi_t wifi = {0};
    settings_backup_cloud_t cloud = {0};
    bool has_wifi = false;
    if (ok && !memcmp(backup.magic, "PICOSET2", 8)) {
        uint8_t extension[5];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 1] << (i * 8);
            ok = indent <= 3 && stored == backup_indent_checksum(&backup, indent);
        }
    } else if (ok && !memcmp(backup.magic, "PICOSET3", 8)) {
        uint8_t extension[6];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0]; rule_offset = extension[1];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 2] << (i * 8);
            ok = indent <= 3 && rule_offset <= 8 &&
                 stored == backup_rule_offset_checksum(&backup, indent, rule_offset);
        }
    } else if (ok && (!memcmp(backup.magic, "PICOSET4", 8) || !memcmp(backup.magic, "PICOSET5", 8) ||
                      !memcmp(backup.magic, "PICOSET6", 8) || !memcmp(backup.magic, "PICOSET7", 8) ||
                      (!memcmp(backup.magic,"PICOSET8",8)||(!memcmp(backup.magic,"PICOSET9",8)||(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8)))))) {
        uint8_t extension[7];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0]; rule_offset = extension[1]; staged_shutdown = extension[2];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 3] << (i * 8);
            ok = indent <= 3 && rule_offset <= 8 && staged_shutdown <= 1 &&
                 stored == backup_shutdown_checksum(&backup, indent, rule_offset, staged_shutdown);
        }
        if (ok && ( !memcmp(backup.magic, "PICOSET5", 8) || !memcmp(backup.magic, "PICOSET6", 8) ||
                    !memcmp(backup.magic, "PICOSET7", 8) || (!memcmp(backup.magic,"PICOSET8",8)||(!memcmp(backup.magic,"PICOSET9",8)||(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8)))))) {
            ok = fread(&profile, 1, sizeof(profile), file) == sizeof(profile);
            if (ok) {
                uint32_t stored = 0;
                for (int i = 0; i < 4; ++i) stored |= (uint32_t)profile.checksum[i] << (i * 8);
                ok = stored == backup_profile_checksum(&backup, indent, rule_offset,
                                                        staged_shutdown, &profile) &&
                     strnlen(profile.device_name, sizeof(profile.device_name)) < sizeof(profile.device_name) &&
                     profile.device_name[0] &&
                     strnlen(profile.status_signature, sizeof(profile.status_signature)) < sizeof(profile.status_signature) &&
                     profile.home_full_refresh <= 127 &&
                     backup_path_valid(profile.avatar, sizeof(profile.avatar));
            }
        }
    }
    uint8_t status_date=s_status_date,idle_minutes=s_idle_sleep_minutes;
    reader_input_preferences_t reader_prefs;reader_input_export(&reader_prefs);
    long history_position = -1;
    has_wifi = ok && (!memcmp(backup.magic, "PICOSET7", 8) || (!memcmp(backup.magic,"PICOSET8",8)||(!memcmp(backup.magic,"PICOSET9",8)||(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8)))));
    if (has_wifi) ok = fread(&wifi, 1, sizeof(wifi), file) == sizeof(wifi) && backup_wifi_valid(&wifi);
    bool has_cloud = ok && (!memcmp(backup.magic,"PICOSET8",8)||(!memcmp(backup.magic,"PICOSET9",8)||(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8))));
    if (has_cloud) ok = fread(&cloud, 1, sizeof(cloud), file) == sizeof(cloud) && backup_cloud_valid(&cloud);
    if(ok && (!memcmp(backup.magic,"PICOSET9",8)||(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8)))){
        uint8_t display[6]={0};bool with_idle=(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8));
        size_t size=with_idle?6:5,offset=with_idle?2:1;
        ok=fread(display,1,size,file)==size;
        if(ok){uint32_t hash=0;for(int i=0;i<4;++i)hash|=(uint32_t)display[i+offset]<<(i*8);
            uint32_t expected=backup_date_checksum(&backup,display[0]);
            if(with_idle)expected=(expected^display[1])*16777619u;
            ok=status_date_valid(display[0])&&(!with_idle||idle_sleep_valid(display[1]))&&hash==expected;
            if(ok){status_date=display[0];if(with_idle)idle_minutes=display[1];}
        }
    }
    bool has_reader=ok&&!memcmp(backup.magic,"PICOSETB",8);
    if(has_reader){
        uint8_t seal[4];ok=fread(&reader_prefs,1,sizeof(reader_prefs),file)==sizeof(reader_prefs)&&fread(seal,1,4,file)==4;
        if(ok){uint32_t hash=0;for(int i=0;i<4;++i)hash|=(uint32_t)seal[i]<<(8*i);ok=reader_input_preferences_valid(&reader_prefs)&&hash==backup_reader_checksum(&backup,&reader_prefs);}
    }
    bool has_history = ok && (!memcmp(backup.magic, "PICOSET6", 8) ||
                              !memcmp(backup.magic, "PICOSET7", 8) || (!memcmp(backup.magic,"PICOSET8",8)||(!memcmp(backup.magic,"PICOSET9",8)||(!memcmp(backup.magic,"PICOSETA",8)||!memcmp(backup.magic,"PICOSETB",8)))));
    if (has_history) {
        history_position = ftell(file);
        ok = history_position >= 0 && book_history_backup_validate(file);
    } else if (ok) ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok || !backup_valid(&backup)) {
        backup_erase_secret(&cloud, sizeof(cloud));
        backup_erase_secret(&wifi, sizeof(wifi));
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (has_cloud) {
        cloud_config_t current;
        esp_err_t cloud_err = cloud_config_backup_export(&current);
        backup_erase_secret(&current, sizeof(current));
        if (cloud_err != ESP_OK) {
            backup_erase_secret(&cloud, sizeof(cloud));
            backup_erase_secret(&wifi, sizeof(wifi));
            return cloud_err;
        }
    }

    // The backup contains paths, not the corresponding font or image bytes.
    // 备份只含资源路径；资源已被删除时回退到安全的内建选项。
    if (backup.font[0] && !backup_file_exists(backup.font, false)) backup.font[0] = 0;
    if (backup.system_font[0] && !backup_file_exists(backup.system_font, false)) backup.system_font[0] = 0;
    if (backup.wallpaper[0] && !backup_file_exists(backup.wallpaper, backup.wallpaper[strlen(backup.wallpaper)-1]=='/')) {
        backup.wallpaper[0] = 0;
        backup.flags[BK_LOCK] = 0;
    }
    backup.flags[BK_LOCK] = 1;
    backup.flags[BK_SYS_SIZE] = system_size_preset(backup.flags[BK_SYS_SIZE]);
    if (profile.avatar[0] && !backup_file_exists(profile.avatar, false)) profile.avatar[0] = 0;
    strlcpy(backup.books_dir, "/sdcard/books", sizeof(backup.books_dir));
    strlcpy(backup.fonts_dir, "/sdcard/fonts", sizeof(backup.fonts_dir));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { backup_erase_secret(&cloud, sizeof(cloud)); backup_erase_secret(&wifi, sizeof(wifi)); return err; }
    *mutated = true;
#define BACKUP_SET_U8(key, index) do { if (err == ESP_OK) err = nvs_set_u8(h, key, backup.flags[index]); } while (0)
#define BACKUP_SET_STR(key, value) do { if (err == ESP_OK) err = nvs_set_str(h, key, value); } while (0)
    BACKUP_SET_U8(NVS_KEY_SLEEP, BK_SLEEP);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHUTDOWN_MODE, staged_shutdown);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_REFRESH, (profile.home_full_refresh & 16) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_VERTICAL_TURN, (profile.home_full_refresh & 32) != 0);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_FULLSCREEN,(profile.home_full_refresh&64)!=0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_FULL, profile.home_full_refresh & 1);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HIDE_IMAGES, (profile.home_full_refresh & 2) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHELF_RECENT, (profile.home_full_refresh & 4) != 0);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_DEVICE_NAME, profile.device_name);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_AVATAR, profile.avatar);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_STATUS_SIGNATURE, profile.status_signature);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_STATUS_DATE,status_date);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_IDLE_SLEEP,idle_minutes);
    BACKUP_SET_U8(NVS_KEY_PICKUP, BK_PICKUP);
    BACKUP_SET_U8(NVS_KEY_SYS_SIZE, BK_SYS_SIZE);
    BACKUP_SET_U8(NVS_KEY_LOCK_STYLE, BK_LOCK);
    BACKUP_SET_U8(NVS_KEY_BOOK_PX, BK_BOOK_PX);
    BACKUP_SET_U8(NVS_KEY_READER_FULL, BK_FULL_PAGES);
    BACKUP_SET_U8(NVS_KEY_READER_TURN, BK_TURN);
    BACKUP_SET_U8(NVS_KEY_POWER_TURN, BK_POWER_TURN);
    BACKUP_SET_U8(NVS_KEY_IMMERSIVE, BK_IMMERSIVE);
    BACKUP_SET_U8(NVS_KEY_BOOK_TRACK, BK_TRACKING);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_INDENT, indent);
    BACKUP_SET_U8(NVS_KEY_BOOK_RULE, BK_READING_LINE);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_RULE_OFFSET, rule_offset);
    BACKUP_SET_U8(NVS_KEY_BOOK_LINE, BK_LINE_SPACING);
    BACKUP_SET_U8(NVS_KEY_BOOK_MARGIN, BK_MARGIN);
    BACKUP_SET_U8(NVS_KEY_BOOK_PARA, BK_PARAGRAPH);
    BACKUP_SET_STR(NVS_KEY_FONT, backup.font);
    BACKUP_SET_STR(NVS_KEY_SYS_FONT, backup.system_font);
    BACKUP_SET_STR(NVS_KEY_WALLPAPER, backup.wallpaper);
    BACKUP_SET_STR(NVS_KEY_BOOKS_DIR, backup.books_dir);
    BACKUP_SET_STR(NVS_KEY_FONTS_DIR, backup.fonts_dir);
#undef BACKUP_SET_U8
#undef BACKUP_SET_STR
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { backup_erase_secret(&cloud, sizeof(cloud)); backup_erase_secret(&wifi, sizeof(wifi)); return err; }

    if(has_reader){esp_err_t input_err=reader_input_import(&reader_prefs);if(input_err!=ESP_OK){backup_erase_secret(&cloud,sizeof(cloud));backup_erase_secret(&wifi,sizeof(wifi));return input_err;}}

    if (has_history) {
        file = fopen(source, "rb");
        if (!file) { backup_erase_secret(&cloud, sizeof(cloud)); backup_erase_secret(&wifi, sizeof(wifi)); return ESP_FAIL; }
        esp_err_t history_err = fseek(file, history_position, SEEK_SET) == 0
            ? book_history_backup_restore(file) : ESP_FAIL;
        if (fclose(file) != 0 && history_err == ESP_OK) history_err = ESP_FAIL;
        if (history_err != ESP_OK) { backup_erase_secret(&cloud, sizeof(cloud)); backup_erase_secret(&wifi, sizeof(wifi)); return history_err; }
    }
    if (has_wifi) {
        esp_err_t wifi_err = read_pico_transfer_import_wifi_backup(&wifi.credentials);
        backup_erase_secret(&wifi, sizeof(wifi));
        if (wifi_err != ESP_OK) { backup_erase_secret(&cloud, sizeof(cloud)); return wifi_err; }
    }
    esp_err_t cloud_err = has_cloud ? cloud_config_backup_restore(&cloud.credentials) : ESP_OK;
    backup_erase_secret(&cloud, sizeof(cloud));
    if (cloud_err != ESP_OK) return cloud_err;
    const uint8_t *f = backup.flags;
    s_sleep = (app_sleep_mode_t)f[BK_SLEEP];
    s_staged_shutdown = staged_shutdown != 0;
    s_reader_hold_refresh = (profile.home_full_refresh & 16) != 0;
    s_reader_vertical_turn = (profile.home_full_refresh & 32) != 0;
    s_reader_fullscreen = (profile.home_full_refresh & 64) != 0;
    s_home_full_refresh = (profile.home_full_refresh & 1) != 0;
    s_reader_hide_images = (profile.home_full_refresh & 2) != 0;
    s_shelf_recent_sort = (profile.home_full_refresh & 4) != 0;
    strlcpy(s_device_name, profile.device_name, sizeof(s_device_name));
    strlcpy(s_avatar, profile.avatar, sizeof(s_avatar));
    strlcpy(s_status_signature, profile.status_signature, sizeof(s_status_signature));
    s_status_date=status_date;
    s_idle_sleep_minutes=idle_minutes;
    s_pickup_wake = f[BK_PICKUP];
    s_system_size = system_size_preset(f[BK_SYS_SIZE]);
    s_lock_style = 1;
    s_book_px = f[BK_BOOK_PX];
    s_reader_full_pages = f[BK_FULL_PAGES];
    s_reader_turn_effect = f[BK_TURN];
    s_reader_power_turn = f[BK_POWER_TURN];
    s_reader_immersive = f[BK_IMMERSIVE];
    s_book_tracking = f[BK_TRACKING];
    s_book_indent = indent;
    s_book_reading_line = f[BK_READING_LINE];
    s_book_rule_offset = rule_offset;
    s_book_line = f[BK_LINE_SPACING];
    s_book_margin = f[BK_MARGIN];
    s_book_para = f[BK_PARAGRAPH];
    strlcpy(s_font, backup.font, sizeof(s_font));
    strlcpy(s_system_font, backup.system_font, sizeof(s_system_font));
    strlcpy(s_wallpaper, backup.wallpaper, sizeof(s_wallpaper));
    strlcpy(s_books_dir, backup.books_dir, sizeof(s_books_dir));
    strlcpy(s_fonts_dir, backup.fonts_dir, sizeof(s_fonts_dir));
    return ESP_OK;
}

// 用户授权修复：修改任何设置前保留整组旧状态，普通写入失败时回退。
// Authorized review fix: retain the whole previous state before writes and roll back ordinary failures.
esp_err_t app_settings_backup_restore(void) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    bool mutated = false;
    struct stat st;
    if (remove(RESTORE_DONE) != 0 && errno != ENOENT) return ESP_FAIL;
    // 保留的恢复材料优先处理，不允许新操作覆盖唯一旧状态。
    // Recover retained material first; a new restore must never overwrite the only old snapshot.
    if (stat(RESTORE_BEFORE, &st) == 0) {
        esp_err_t recovery = backup_restore_from(RESTORE_BEFORE, &mutated);
        if (recovery != ESP_OK) return recovery;
        if (remove(RESTORE_BEFORE) != 0) return ESP_FAIL;
    } else if (errno != ENOENT) return ESP_FAIL;
    const char *source = BACKUP_FILE;
    if (stat(source, &st) != 0) {
        if (errno != ENOENT) return ESP_FAIL;
        source = BACKUP_PREVIOUS;
        if (stat(source, &st) != 0) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    esp_err_t err = backup_save_to(RESTORE_BEFORE, RESTORE_TEMP, RESTORE_PREVIOUS);
    if (err != ESP_OK) return err;
    err = backup_restore_from(source, &mutated);
    if (err != ESP_OK && mutated) {
        bool rollback_mutated = false;
        esp_err_t rollback = backup_restore_from(RESTORE_BEFORE, &rollback_mutated);
        if (rollback != ESP_OK) return err; // 保留快照供重试。/ Retain the snapshot for retry.
    }
    if (err == ESP_OK) {
        // 成功快照先改名，避免清理失败导致下一次把已成功的恢复撤回。
        // Rename completed material first so a cleanup failure cannot roll back a successful restore later.
        if (rename(RESTORE_BEFORE, RESTORE_DONE) != 0) return ESP_FAIL;
        (void)remove(RESTORE_DONE);
        return ESP_OK;
    }
    (void)remove(RESTORE_BEFORE);
    return err;
}
