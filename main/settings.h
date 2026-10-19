/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 里的用户设置：睡眠档、字体路径、上次唤醒/开机原因、拿起唤醒开关。
 *
 * User settings in NVS: sleep mode, font path, last wake/boot reason,
 * pickup-wake switch.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "esp_err.h"

/// 默认深睡。浅睡：按键回原页；拿起唤醒默认关。软睡：SOFT_SLEEP 拉低 EN，再短按开机。关机：EN=0，长按开机。
/// Default is deep. Light: key returns to the page; pickup-wake defaults off. Soft sleep: SOFT_SLEEP drops EN, then a short press boots. Off: EN=0, long-press to boot.
typedef enum {
    APP_SLEEP_LIGHT = 0,
    APP_SLEEP_DEEP = 1,
    APP_SLEEP_OFF = 2,
} app_sleep_mode_t;

void app_settings_init(void);
app_sleep_mode_t app_settings_sleep_mode(void);
void app_settings_set_sleep_mode(app_sleep_mode_t mode);
const char* app_sleep_mode_name(app_sleep_mode_t mode);
/// 电源菜单“关机”：false 为彻底断电，true 为先浅睡 10 分钟再由 PMU 软睡断电。
/// Power-menu shutdown: false powers fully off; true light-sleeps for 10 minutes, then PMU soft-sleeps.
bool app_settings_staged_shutdown(void);
void app_settings_set_staged_shutdown(bool staged);
/// 无操作自动锁屏休眠：0关闭，1/3/5/10/30分钟，默认5分钟。/ Idle lock: 0 disables, otherwise 1/3/5/10/30 minutes; default 5.
uint8_t app_settings_idle_sleep_minutes(void);
void app_settings_set_idle_sleep_minutes(uint8_t minutes);
/// 首页进入时强制全刷；默认关闭。/ Full refresh when entering Home; off by default.
bool app_settings_home_full_refresh(void);
void app_settings_set_home_full_refresh(bool enabled);
const char *app_settings_device_name(void);
void app_settings_set_device_name(const char *name);
const char *app_settings_avatar_path(void);
void app_settings_set_avatar_path(const char *path);
const char *app_settings_status_signature(void);
void app_settings_set_status_signature(const char *signature);
/// 状态栏日期可隐藏，时间始终显示；五种数字日期格式。/ Hide the status date independently of the always-visible time.
typedef enum {
    APP_STATUS_DATE_ISO, APP_STATUS_DATE_SLASH, APP_STATUS_DATE_DOT,
    APP_STATUS_DATE_MONTH_DAY, APP_STATUS_DATE_MONTH_DAY_SLASH,
    APP_STATUS_DATE_FORMAT_COUNT
} app_status_date_format_t;
bool app_settings_status_show_date(void);
void app_settings_set_status_show_date(bool show);
app_status_date_format_t app_settings_status_date_format(void);
void app_settings_set_status_date_format(app_status_date_format_t format);
void app_settings_format_status_clock(time_t now,char *out,size_t capacity);
/// 空路径表示固件内建字体；非空为 SD 上的 TTF。/ Empty path is the built-in font; non-empty is a TTF on the SD card.
const char* app_settings_font_path(void);
void app_settings_set_font_path(const char* path);
/// 系统界面字体；空值使用内建思源黑体并从 TF 卡补字。/ UI font; empty uses built-in Source Han Sans with SD fallback.
const char* app_settings_system_font_path(void);
void app_settings_set_system_font_path(const char* path);
/// 小/标准/大/特大为100/120/140/160；旧值就近归档。/ Small/standard/large/extra-large: 100/120/140/160; migrate legacy values.
uint8_t app_settings_system_font_size(void);
void app_settings_set_system_font_size(uint8_t percent);
/// 仅壁纸锁屏；旧样式接口保留供备份兼容。/ Wallpaper-only lock; retain legacy style API for backup compatibility.
uint8_t app_settings_lock_style(void);
void app_settings_set_lock_style(uint8_t style);
const char* app_settings_wallpaper_path(void);
void app_settings_set_wallpaper_path(const char* path);
/// 轮播目录复用壁纸路径字段（末尾斜线），纳入配置备份。/ Store rotation directories with a trailing slash in the backed-up wallpaper path.
bool app_settings_wallpaper_rotating(void);
esp_err_t app_settings_set_wallpaper_directory(const char *path);
/// 上次成功显示的图片跨唤醒/重启保留，不属于用户配置。/ Retain the last successfully shown image across wakes/reboots, outside user configuration.
const char *app_settings_wallpaper_last(void);
void app_settings_note_wallpaper(const char *path);
/// 上次浅睡唤醒源（app_wake_source_t），掉电也保留。/ Last light-sleep wake source (app_wake_source_t); kept across power loss.
uint8_t app_settings_last_wake(void);
void app_settings_set_last_wake(uint8_t src);
/// 最近一次非 0 的 PMU wake_reason。STATUS 报 0 时用这个回显。/ Last non-zero PMU wake_reason. Used when STATUS reports 0.
uint8_t app_settings_last_boot(void);
void app_settings_set_last_boot(uint8_t reason);
/// 浅睡拿起唤醒。默认关；有加速度计也不会自动开。/ Light-sleep pickup wake. Defaults off; an accelerometer does not turn it on.
bool app_settings_pickup_wake(void);
void app_settings_set_pickup_wake(bool on);
/// 阅读默认字号，36..72，默认 48。/ Default reading size, 36..72, initially 48.
uint8_t app_settings_book_px(void);
/// 无效字号恢复 48。/ Invalid sizes fall back to 48.
void app_settings_set_book_px(uint8_t px);
/// 阅读时每 5/10/15/30 页全刷；0 关闭周期全刷，手动全刷仍可用。
/// Full-screen cleanup every 5/10/15/30 turns; 0 disables periodic cleanup only.
uint8_t app_settings_reader_full_pages(void);
void app_settings_set_reader_full_pages(uint8_t pages);
/// 阅读翻页效果：0 默认，1 水波纹；初始为默认。/ Reader turn effect: 0 default, 1 water ripple; initially default.
uint8_t app_settings_reader_turn_effect(void);
void app_settings_set_reader_turn_effect(uint8_t effect);
/// 阅读正文里短按电源键翻下一页；默认关闭。/ Short power press turns forward only in reader body; off by default.
bool app_settings_reader_power_turn(void);
void app_settings_set_reader_power_turn(bool on);
/// 沉浸全屏时隐藏阅读状态栏。/ Hide the reader status bar in immersive full screen.
/// 全屏阅读跨内容和重启记忆。/ Remember reader full screen across content and restarts.
bool app_settings_reader_fullscreen(void);
void app_settings_set_reader_fullscreen(bool on);
bool app_settings_reader_immersive(void);
void app_settings_set_reader_immersive(bool on);
/// 阅读时跳过书内插图页；原图仍保留在 EPUB 中。/ Skip inline illustrations while reading without changing the EPUB.
bool app_settings_reader_hide_images(void);
void app_settings_set_reader_hide_images(bool on);
/// 阅读时长按中间圆圈键全刷；关闭时返回首页，默认关闭。
/// Refresh the entire screen on a reader middle-key hold; off returns Home, default off.
bool app_settings_reader_hold_refresh(void);
void app_settings_set_reader_hold_refresh(bool on);
/// 上中下轻点分区；默认左右翻页。/ Top/middle/bottom tap regions; defaults to horizontal.
bool app_settings_reader_vertical_turn(void);
void app_settings_set_reader_vertical_turn(bool on);
/// 书架排序偏好重启后保持。/ Persist the shelf's recent-reading sort across restarts.
bool app_settings_shelf_recent_sort(void);
void app_settings_set_shelf_recent_sort(bool on);
/// 字间距档位 0..4，默认 2 居中；对应 -4/-2/0/+2/+4 像素。/ Tracking index 0..4, centered default 2; maps to -4/-2/0/+2/+4 pixels.
uint8_t app_settings_book_tracking(void);
void app_settings_set_book_tracking(uint8_t index);
/// 普通正文首行缩进 0..3 字，默认两字。/ First-line indent: 0..3 em, default 2.
uint8_t app_settings_book_indent(void);
void app_settings_set_book_indent(uint8_t em);
/// 正文阅读线：0 无，1 虚线，2 点线。/ Body reading guides: none, dashed or dotted.
uint8_t app_settings_book_reading_line(void);
void app_settings_set_book_reading_line(uint8_t style);
/// 阅读线相对默认位置 -8..+8 像素，2 像素一步，默认 0。/ Guide offset in 2 px steps.
int8_t app_settings_book_reading_line_offset(void);
void app_settings_set_book_reading_line_offset(int8_t offset_px);
/// 阅读行高百分比，110..150。/ Reader line-height percentage: 110..150.
uint8_t app_settings_book_line_spacing(void);
void app_settings_set_book_line_spacing(uint8_t percent);
/// 阅读页左右边距，8..60 像素。/ Reader horizontal margin: 8..60 pixels.
uint8_t app_settings_book_margin(void);
void app_settings_set_book_margin(uint8_t px);
/// 段后距离百分比，0/25/50/75。/ Paragraph-gap percentage: 0/25/50/75.
uint8_t app_settings_book_paragraph_spacing(void);
void app_settings_set_book_paragraph_spacing(uint8_t percent);
/// 固定 TF 卡书籍目录和字体目录；分别为 /sdcard/books、/sdcard/fonts。
const char* app_settings_books_dir(void);
const char* app_settings_fonts_dir(void);
/// 兼容旧调用方；只接受固定标准目录，不再改变路径。/ Legacy-call compatibility: accept only the fixed standard root, never reroute.
bool app_settings_set_books_dir(const char* path);
bool app_settings_set_fonts_dir(const char* path);

/// 将设置、阅读记录、WiFi及坚果云凭据保存到TF卡根目录；备份含明文密码，需妥善保管。
/// Save settings, reading records, WiFi and Nutstore credentials to TF root; keep the plaintext-secret backup private.
esp_err_t app_settings_backup_save(void);
/// 校验备份后恢复 NVS；缺失的外部字体或壁纸回退到内建资源。
/// Validate before restoring NVS; missing external assets fall back safely.
esp_err_t app_settings_backup_restore(void);
