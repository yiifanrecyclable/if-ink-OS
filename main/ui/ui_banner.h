/* SPDX-License-Identifier: Apache-2.0
 * 中文：首页大字格言的四行限制与可调高度。 / English: Four-line large home text with adjustable height.
 */
#pragma once
#include "content_library.h"
#include <stdbool.h>
#define UI_BANNER_QUOTE_PX 96
#define UI_BANNER_QUOTE_LINES 4
#define UI_BANNER_QUOTE_INSET 18
#define UI_BANNER_GAP 14
#define UI_BANNER_BOTTOM 354
typedef struct {char lines[UI_BANNER_QUOTE_LINES][CONTENT_HOME_QUOTE_BYTES];int count;bool overflow;} ui_banner_quote_t;
typedef struct {int date_px,date_top,quote_top,quote_height,quote_px;content_quote_align_t quote_align;content_quote_valign_t quote_valign;bool free_y;} ui_banner_positions_t;
/// 在当前系统字库中计算默认锚点，格言换行不移动日期。/ Compute default system-face anchors without moving the date on quote wrapping.
ui_banner_positions_t ui_banner_positions(const char *date);
ui_banner_positions_t ui_banner_positions_size(const char *date,int px);
bool ui_banner_quote_fits_size(const char *text,const char *path,int px);
/// 返回逻辑竖屏坐标的300行4bpp缓存；用ui_bitmap_draw绘制，不能直接复制进物理屏幕缓冲。
/// Return 300 logical portrait rows; draw with ui_bitmap_draw, never memcpy into the physical framebuffer.
bool ui_banner_preview(const char *text,const char *path,int px,content_quote_align_t align,content_quote_valign_t valign,uint32_t day,bool valid,uint8_t **out,bool *fits);
/// 用完整草稿渲染缓存，失败不保存设置。/ Render a complete draft without persisting; false on image/alloc failure.
bool ui_banner_preview_config(const content_banner_config_t *config,uint32_t day,bool valid,uint8_t **out,bool *fits);
uint16_t ui_banner_default_date_y(int quote_px);
/// 当前文字实际高度位置（相对Banner顶部，按字形高度限制边界）。/ Resolved text offset, constrained by actual ink height.
uint16_t ui_banner_text_y(const content_banner_config_t *config);
void ui_banner_date_line(uint32_t day,bool valid,char *line,size_t cap);
bool ui_banner_quote_layout(const char *text,bool custom,ui_banner_quote_t *out);
bool ui_banner_quote_fits_font(const char *text,const char *path);
void ui_banner_quote_draw(uint8_t *fb,const char *text,bool custom,ui_banner_positions_t positions);
