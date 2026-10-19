/* SPDX-License-Identifier: Apache-2.0
 * 中文：图书与文档独立的排序偏好，以及图书首次收录时间与置顶。
 * English: Independent content sort preferences first-added book dates and pins.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONTENT_HOME_QUOTE_BYTES 192
#define CONTENT_HOME_QUOTE_FONT_BYTES 160
#define CONTENT_MARKDOWN_FONT_BYTES 192
typedef enum { CONTENT_QUOTE_LEFT, CONTENT_QUOTE_RIGHT, CONTENT_QUOTE_CENTER, CONTENT_QUOTE_JUSTIFY } content_quote_align_t;
/// 格言水平排版，默认居中；与文字和字号一起备份。/ Quote alignment defaults to center and is backed up with text and size.
content_quote_align_t content_home_quote_alignment(void);
bool content_home_quote_alignment_valid(unsigned align);
esp_err_t content_home_quote_alignment_set(unsigned align);
// 格言仅在日期/品牌下方区域垂直排版；默认上对齐保持旧版位置。/ Vertically align only the quote below date/brand; top defaults to the previous position.
typedef enum { CONTENT_QUOTE_VTOP, CONTENT_QUOTE_VCENTER, CONTENT_QUOTE_VBOTTOM } content_quote_valign_t;
content_quote_valign_t content_home_quote_valign(void);
bool content_home_quote_valign_valid(unsigned align);
esp_err_t content_home_quote_valign_set(unsigned align);
#define CONTENT_QUOTE_SIZE_MIN 48
#define CONTENT_QUOTE_SIZE_MAX 120
uint8_t content_home_quote_size(void);
bool content_home_quote_size_valid(unsigned px);
esp_err_t content_home_quote_size_set(unsigned px);
/// 首页固定格言与输入/备份校验；内容设置命名空间持久保存。/ Persist the fixed home quote; validate editor and backup text.
const char *content_home_quote(void);
bool content_home_quote_valid(const char *text);
esp_err_t content_home_quote_set(const char *text);
/// 格言独立字体路径；空串跟随系统字体。/ Independent quote font path; empty follows the system font.
const char *content_home_quote_font(void);
bool content_home_quote_font_valid(const char *path);
esp_err_t content_home_quote_font_set(const char *path);
#define CONTENT_BANNER_IMAGE_BYTES 288
#define CONTENT_BANNER_DEFAULT_IMAGE "/sdcard/banner pic/默认背景.png"
#define CONTENT_BANNER_DATE_AUTO UINT16_MAX
#define CONTENT_BANNER_DATE_MIN 4
#define CONTENT_BANNER_DATE_MAX 260
#define CONTENT_BANNER_TEXT_AUTO UINT16_MAX
#define CONTENT_BANNER_TEXT_MAX 296
// 单个带版本快照实现预览/取消/保存，避免多个键部分写入。/ A versioned snapshot supports preview/cancel/save without partially updating multiple keys.
typedef struct {
    uint32_t version;
    uint16_t date_y;
    uint8_t mode,show_date,quote_px,align,valign,show_text; // v2: mode是背景开关，文字独立。/ v2: mode enables background independently of text.
    char text[CONTENT_HOME_QUOTE_BYTES],font[CONTENT_HOME_QUOTE_FONT_BYTES],image[CONTENT_BANNER_IMAGE_BYTES];
    uint16_t text_y; ///< 相对Banner顶部的文字高度，AUTO沿用旧位置。/ Text offset from banner top; AUTO retains legacy placement.
} content_banner_config_t;
bool content_banner_image_valid(const char *path);
bool content_banner_valid(const content_banner_config_t *config);
/// 解码旧快照并迁移，不写存储。/ Decode/migrate legacy snapshots without storage writes.
bool content_banner_decode(const void *data,size_t size,content_banner_config_t *config);
void content_banner_load(content_banner_config_t *config);
esp_err_t content_banner_save(const content_banner_config_t *config);

typedef enum { CONTENT_NAME, CONTENT_PROGRESS, CONTENT_ADDED, CONTENT_RECENT } content_sort_t;
// 旧标记字段只作备份兼容，读写强制为false。/ Legacy mark fields are compatibility-only and always load/save as false.
typedef struct { uint8_t mode; bool ascending, marked_first, marked_only; } content_options_t;
typedef struct { uint8_t magic[4], stamp[4]; char path[288]; } content_added_record_t;
/// 各子页独立加载；没有新记录的图书沿用旧排序偏好。/ Load per-tab options, falling back to legacy book sorting.
content_options_t content_options_load(bool documents, bool legacy_recent);
/// 保存后调用方才更新显示；偏好随设置的阅读资料备份。/ Update the UI only after save; preferences travel with settings history backup.
esp_err_t content_options_save(bool documents, content_options_t options);
/// 首次收录写入时间；无有效时钟、存储失败或碰撞时返回未知0。/ Record first addition; return unknown zero on invalid clock, storage errors or collision.
uint32_t content_first_added(const char *path);
/// 备份校验完整路径与散列键，不读写NVS。/ Validate path ownership without accessing NVS.
bool content_added_record_valid(const char *key, const content_added_record_t *record);
/// 名称不区分英文大小写，连续数字按自然顺序比较。/ Case-insensitive ASCII comparison with natural numeric ordering.
int content_name_compare(const char *a, const char *b);
/// 图书置顶绑定完整路径；重启及设置备份后保留。/ Book pins bind complete paths and survive reboot/settings backup.
bool content_book_pinned(const char *path);
/// 写入成功后才更新界面；碰撞或存储错误拒绝覆盖。/ Update UI after success; reject collisions and storage errors.
esp_err_t content_book_pin_set(const char *path, bool pinned);
/// 校验置顶备份的路径归属，不读写NVS。/ Validate pin backup ownership without accessing NVS.
bool content_book_pin_valid(const char *key, const char *path);

/// 所有文档共享正文/H1/H2/H3 字号；缺失或损坏时用48/72/64/56，图书设置独立。/ Document body/H1/H2/H3 sizes, defaults 48/72/64/56, independent of books.
void content_markdown_sizes(uint8_t sizes[4]);
bool content_markdown_sizes_valid(const uint8_t sizes[4]);
esp_err_t content_markdown_sizes_set(const uint8_t sizes[4]);
/// 空值表示继承旧阅读字体；选择后独立持久化。/ Empty inherits the legacy reader face until a document face is saved.
const char *content_markdown_font(void);
bool content_markdown_font_valid(const char *path);
esp_err_t content_markdown_font_set(const char *path);
