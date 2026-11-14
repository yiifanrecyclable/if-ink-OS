/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：绘制与命中紧凑四栏导航；Lucide 图标表示首页、内容、应用和设置。
 * English: Draw and hit-test compact four-tab navigation with Lucide home, contents, applications and settings icons.
 */
#include "ui_nav.h"
// Modified 2026-10-06: compact icon-only Lucide tabs and full local date/time in the status bar.
// 中文：状态栏左侧统一显示完整本地日期与时间，签名按可用宽度避让。
// 中文：选中导航与主体白底连接，阴影只落在较低的灰色未选中区。
// English: Active white navigation joins the body; shadows fall only on the recessed gray tabs.
// 中文：底栏改用Lucide官方图标，入口名称按用户要求调整。
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_registry.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "ui_kit.h"
#include "settings.h"
#define UI_NAV_NETWORK_ICON_PX 28

EpdRect ui_nav_rect(void) { return (EpdRect){0,UI_NAV_TOP,UI_LOCK_WIDTH,UI_NAV_HEIGHT}; }

int ui_nav_action_height(void) { return ui_text_line_height(UI_TEXT_BODY) + 48; }

ui_action_card_t ui_nav_action_card(int extra_height) {
    int header = ui_text_line_height(UI_TEXT_BODY) + ui_text_line_height(UI_TEXT_CAPTION) + 40;
    int path = ui_text_line_height(UI_TEXT_CAPTION) + 24;
    int top = UI_NAV_TOP - header - path - ui_nav_action_height() - extra_height - 12;
    return (ui_action_card_t){top, header, path, {592, top+12, 76, 76}};
}

EpdRect ui_nav_action_button(int index, int count) {
    int inset = count == 4 ? 0 : 24;
    int width = (UI_LOCK_WIDTH - 2*inset) / count;
    int height = ui_nav_action_height();
    return (EpdRect){inset + index*width, UI_NAV_TOP-12-height, width, height};
}

void ui_nav_draw_action_card(uint8_t *fb, ui_action_card_t card) {
    int top = card.top;
    ui_fade_region(fb, (EpdRect){0, UI_STATUS_HEIGHT, UI_LOCK_WIDTH, top-UI_STATUS_HEIGHT}, 0x99);
    for (int d = 0; d < UI_NAV_SHADOW_DEPTH; ++d)
        epd_fill_rect((EpdRect){0, top-UI_NAV_SHADOW_DEPTH+d, UI_LOCK_WIDTH, 1},
                      (uint8_t)(0xff-(d+1)*0x66/UI_NAV_SHADOW_DEPTH), fb);
    epd_fill_rect((EpdRect){0, top, UI_LOCK_WIDTH, UI_NAV_TOP-top}, UI_GRAY_WHITE, fb);
    ui_hairline(fb, top, 0, UI_LOCK_WIDTH, UI_GRAY_SECTION_LINE);
}

void ui_nav_draw_action_row(uint8_t *fb, const char *const *labels, int count) {
    EpdRect first = ui_nav_action_button(0, count);
    ui_hairline(fb, first.y, 36, 612, UI_GRAY_ITEM_LINE);
    for (int i = 0; i < count; ++i) {
        EpdRect r = ui_nav_action_button(i, count);
        if (i) epd_fill_rect((EpdRect){r.x, r.y+24, 1, r.height-48}, UI_GRAY_ITEM_LINE, fb);
        int px = ui_text_fit_px(UI_TEXT_BODY, labels[i], r.width-16, r.height-16);
        ui_text_fixed_vc(fb, r.x+r.width/2, r.y+r.height/2, px, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
}

EpdRect ui_nav_close_rect(void){return(EpdRect){24,UI_PAGE_Y(73),68,68};}
static void header_stroke(uint8_t *fb,int x0,int y0,int x1,int y1,uint8_t gray){
    int vx=x1-x0,vy=y1-y0,len=vx*vx+vy*vy;
    int left=(x0<x1?x0:x1)-2,right=(x0>x1?x0:x1)+2;
    int top=(y0<y1?y0:y1)-2,bottom=(y0>y1?y0:y1)+2;
    for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++){
        float t=len?(float)((x-x0)*vx+(y-y0)*vy)/len:0;
        if(t<0)t=0;
        if(t>1)t=1;
        float dx=x-x0-t*vx,dy=y-y0-t*vy;
        if(dx*dx+dy*dy<=2.25f)epd_draw_pixel(x,y,gray,fb);
    }
}
void ui_nav_close_in(uint8_t *fb, EpdRect r){
    int x=r.x+r.width/2,y=r.y+r.height/2;
    header_stroke(fb,x-10,y-10,x+10,y+10,UI_GRAY_BLACK);
    header_stroke(fb,x-10,y+10,x+10,y-10,UI_GRAY_BLACK);
}
void ui_nav_close(uint8_t *fb){ ui_nav_close_in(fb,ui_nav_close_rect()); }
void ui_nav_parent(uint8_t *fb,EpdRect r,bool enabled){
    int x=r.x+r.width/2,y=r.y+r.height/2;uint8_t gray=enabled?UI_GRAY_BLACK:0xb0;
    header_stroke(fb,x-13,y,x+13,y,gray);
    header_stroke(fb,x-13,y,x-3,y-10,gray);
    header_stroke(fb,x-13,y,x-3,y+10,gray);
}

void ui_nav_back(uint8_t *fb, int x, int y) {
    // 参考系统题头缩小返回键，中心仍保持在 y=113。/ Match the smaller reference back control while retaining the title centerline.
    ui_fill_round_rect(fb, (EpdRect){x, y + 13, 44, 44}, 22, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, (EpdRect){x, y + 13, 44, 44}, 22, UI_NAV_BACK_BORDER_GRAY);
    ui_draw_round_rect(fb, (EpdRect){x + 1, y + 14, 42, 42}, 21, UI_NAV_BACK_BORDER_GRAY);
    ui_draw_icon(fb, x + 22, y + 35, 38, UI_ICON_CHEVRON_LEFT, UI_GRAY_BLACK);
}

void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray) {
    if (fb && size >= 16 && size <= 40) ui_draw_icon(fb, cx, cy, size, UI_ICON_WIFI, gray);
}

// 用五档原生灰阶从白边渐淡至按钮底色；转角不叠加加深。
// Fade five native gray levels from the white edge into the button background without double-darkening corners.
static void nav_recess_shadow(uint8_t *fb, int left, int width, bool white_left, bool white_right) {
    const int reach = UI_NAV_SHADOW_DEPTH;
    for (int d = 0; d < reach; ++d) {
        int shade = 4 - 4 * d / (reach - 1);
        uint8_t gray = UI_NAV_INACTIVE_GRAY - shade * 0x11;
        int start = left + (white_left ? d : 0);
        int end = left + width - (white_right ? d : 0);
        epd_fill_rect((EpdRect){start, UI_NAV_TOP + d, end - start, 1}, gray, fb);
        if (white_left)
            epd_fill_rect((EpdRect){left + d, UI_NAV_TOP + d + 1, 1, UI_NAV_HEIGHT - d - 1}, gray, fb);
        if (white_right)
            epd_fill_rect((EpdRect){left + width - d - 1, UI_NAV_TOP + d + 1, 1, UI_NAV_HEIGHT - d - 1}, gray, fb);
    }
}

void ui_nav_draw(uint8_t *fb, int active) {
    // 白色主体及选中区在上，灰色按钮在下；图标最后绘制以保持清晰。
    // Raise the white body and selected tab above gray buttons; draw icons last for clarity.
    for(int i=0;i<4;++i) {
        int left=i*UI_LOCK_WIDTH/4,width=UI_LOCK_WIDTH/4;
        bool selected=i==active;
        epd_fill_rect((EpdRect){left,UI_NAV_TOP,width,UI_NAV_HEIGHT},selected?UI_GRAY_WHITE:UI_NAV_INACTIVE_GRAY,fb);
        if(!selected)nav_recess_shadow(fb,left,width,i>0&&i-1==active,i<3&&i+1==active);
        static const ui_icon_t tabs[]={UI_ICON_HOUSE,UI_ICON_LIBRARY_BIG,UI_ICON_LAYOUT_GRID,UI_ICON_SLIDERS_HORIZONTAL};
        ui_draw_icon(fb,left+width/2,UI_NAV_TOP+UI_NAV_HEIGHT/2,44,tabs[i],selected?UI_GRAY_BLACK:UI_GRAY_WHITE);
    }
    for(int i=1;i<4;++i) {
        if(i==active || i-1==active)continue;
        int x=i*UI_LOCK_WIDTH/4;
        epd_fill_rect((EpdRect){x,UI_NAV_TOP,1,UI_NAV_HEIGHT},UI_GRAY_SECTION_LINE,fb);
    }
}

EpdRect ui_nav_pager_rect(int direction) {
    return (EpdRect){direction<0?16:568,UI_NAV_PAGER_TOP,100,64};
}
void ui_nav_draw_pager(uint8_t *fb,int count,const char *unit,int page,int pages) {
    char label[80];ui_snprintf(label,sizeof(label),ui_i18n_english()?"%d %s · %02d/%02d":"%d%s · %02d/%02d",count,unit,page+1,pages);
    int y=UI_NAV_PAGER_TOP+38,px=ui_text_fit_px(UI_TEXT_CAPTION,label,444,44);
    ui_text_fixed_vc(fb,342,y,px,label,EPD_DRAW_ALIGN_CENTER,false);
    ui_draw_icon(fb,58,y,28,UI_ICON_CHEVRON_LEFT,page>0?0:0x90);
    ui_draw_icon(fb,626,y,28,UI_ICON_CHEVRON_RIGHT,page+1<pages?0:0x90);
}

// 空心电池内使用固定黑色实心闪电，低电量时仍清楚可见。
// A fixed solid black bolt remains visible inside the empty battery even at low charge.
static void ui_nav_battery_bolt(uint8_t *fb,int cx,int cy){
    epd_fill_triangle(cx+2,cy-9,cx-8,cy+1,cx-1,cy+1,UI_GRAY_BLACK,fb);
    epd_fill_triangle(cx+2,cy-9,cx-1,cy+1,cx+1,cy-2,UI_GRAY_BLACK,fb);
    epd_fill_triangle(cx-1,cy+1,cx-3,cy+9,cx+8,cy-2,UI_GRAY_BLACK,fb);
    epd_fill_triangle(cx-1,cy+1,cx+8,cy-2,cx+1,cy-2,UI_GRAY_BLACK,fb);
}

static char s_status_notice[192];
static int64_t s_status_notice_until;
void ui_nav_notice_show(const char *text,int64_t now_ms){
    ui_snprintf(s_status_notice,sizeof(s_status_notice),"%s",text?text:"");
    s_status_notice_until=now_ms+4000;
}
void ui_nav_notice_clear(void){s_status_notice[0]=0;s_status_notice_until=0;}
const char *ui_nav_notice_text(void){return s_status_notice;}
bool ui_nav_notice_tick(int64_t now_ms){
    if(!s_status_notice[0]||now_ms<s_status_notice_until)return false;
    ui_nav_notice_clear();return true;
}

static void status_text(uint8_t *fb, const char *source, int x, int width, size_t capacity,
                        enum EpdFontFlags align) {
    if (!source || width <= 0) return;
    char label[192];
    size_t len = strlen(source);
    if (len >= capacity) {
        len = capacity - 1;
        while (len && ((unsigned char)source[len] & 0xc0) == 0x80) --len;
    }
    for (size_t i = 0; i < len; ++i)
        label[i] = (unsigned char)source[i] < 0x20 ? ' ' : source[i];
    label[len] = 0;
    while (len && ui_text_fixed_width_px(UI_STATUS_SIGNATURE_PX, label) > width) {
        do { --len; } while (len && ((unsigned char)label[len] & 0xc0) == 0x80);
        label[len] = 0;
    }
    if (len) ui_text_fixed_vc(fb, x, UI_STATUS_CENTER_Y, UI_STATUS_SIGNATURE_PX, label, align, false);
}

static void draw_status(uint8_t *fb, const char *title, bool landscape, bool reading) {
    int width = landscape ? UI_LOCK_HEIGHT : UI_LOCK_WIDTH;
    int right_offset = width - UI_LOCK_WIDTH;
    // A notice owns the status strip until it expires. In the reader it shares the status text size.
    if(s_status_notice[0]){
        int px=reading?UI_STATUS_SIGNATURE_PX:ui_text_effective_px(UI_TEXT_TITLE);char label[192];
        snprintf(label,sizeof(label),"%s",s_status_notice);
        bool shortened=false;
        while(label[0]&&ui_text_fixed_width_px(px,label)>width-48){
            size_t n=strlen(label)-1;while(n&&((unsigned char)label[n]&0xc0)==0x80)--n;
            label[n]=0;shortened=true;
        }
        if(shortened){
            while(label[0]&&ui_text_fixed_width_px(px,label)+ui_text_fixed_width_px(px,"…")>width-48){
                size_t n=strlen(label)-1;while(n&&((unsigned char)label[n]&0xc0)==0x80)--n;label[n]=0;
            }
            size_t n=strlen(label);if(n+4<sizeof(label))memcpy(label+n,"…",4);
        }
        epd_fill_rect((EpdRect){0,0,width,UI_STATUS_HEIGHT},UI_GRAY_WHITE,fb);
        ui_text_fixed_vc(fb,width/2,UI_STATUS_CENTER_Y,px,label,EPD_DRAW_ALIGN_CENTER,false);
        return;
    }
    time_t now = time(NULL);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (now < 1704067200 && pmu && pmu->time_synced && pmu->unix_sec >= 1704067200)
        now = pmu->unix_sec;
    char clock[64];
    if (landscape) app_settings_format_status_clock_with_date(now,clock,sizeof(clock));
    else app_settings_format_status_clock(now,clock,sizeof(clock));
    const int status_center_y = UI_STATUS_CENTER_Y;
    // 固定状态栏字号，让系统字体缩放时文字仍与图标共用中心线。
    // Keep status type fixed so text and glyphs share one centerline at every system font scale.
    ui_text_fixed_vc(fb, 36, status_center_y, UI_STATUS_SIGNATURE_PX, clock, EPD_DRAW_ALIGN_LEFT, false);
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    const bool wifi_visible = network.network_ready && network.mode == READ_PICO_TRANSFER_MODE_STA;
    char percent[8] = "--%";
    int gauge_percent = pmu_battery_percent(pmu);
    if (gauge_percent >= 0) snprintf(percent, sizeof(percent), "%u%%", (unsigned)gauge_percent);
    // 按实际百分比宽度排列网络图标，留 10 像素间隔；墨色与电池相同。
    // Place the network marks 10 px before the measured percentage, using the battery ink.
    const int percent_left = 595 + right_offset - ui_text_fixed_width_px(UI_STATUS_SIGNATURE_PX, percent);
    const int network_right = percent_left - 10;
    const int wifi_center = network_right - UI_NAV_NETWORK_ICON_PX / 2;
    int clock_right = 36 + ui_text_fixed_width_px(UI_STATUS_SIGNATURE_PX, clock);
    int group_left = wifi_visible ? wifi_center - UI_NAV_NETWORK_ICON_PX / 2 : percent_left;
    int center = width / 2;
    const char *signature = app_settings_status_signature();
    if (landscape && signature[0]) {
        const int gap = 14, rule_height = 18;
        epd_fill_rect((EpdRect){center, status_center_y - rule_height / 2, 1, rule_height},
                      UI_GRAY_SECTION_LINE, fb);
        status_text(fb, signature, center - gap, center - gap - clock_right - 12, 96, EPD_DRAW_ALIGN_RIGHT);
        status_text(fb, title, center + gap, group_left - 12 - center - gap, 192, EPD_DRAW_ALIGN_LEFT);
    } else {
        int available = 2 * (center - clock_right - 12);
        int right = 2 * (group_left - 12 - center);
        if (right < available) available = right;
        if (!landscape && available > 280) available = 280;
        status_text(fb, title ? title : signature, center, available, title ? 192 : 96, EPD_DRAW_ALIGN_CENTER);
    }
    if (wifi_visible) ui_nav_wifi_icon(fb, wifi_center, status_center_y, UI_NAV_NETWORK_ICON_PX, UI_GRAY_BLACK);
    EpdRect battery = {606 + right_offset, status_center_y - 11, 40, 22};
    ui_fill_round_rect(fb,battery,6,UI_GRAY_WHITE);
    ui_draw_round_rect(fb, battery, 6, UI_GRAY_BLACK);
    ui_fill_round_rect(fb, (EpdRect){648 + right_offset, status_center_y - 4, 5, 8}, 2, UI_GRAY_BLACK);
    bool charging = pmu_battery_charging(pmu);
    if (!charging && gauge_percent >= 0) {
        int width = (31 * gauge_percent + 50) / 100;
        if (width > 0) ui_fill_round_rect(fb, (EpdRect){610 + right_offset, status_center_y - 7, width, 14}, 4, UI_GRAY_BLACK);
    }
    if (charging) ui_nav_battery_bolt(fb,627 + right_offset,status_center_y);
    ui_text_fixed_vc(fb, 595 + right_offset, status_center_y, UI_STATUS_SIGNATURE_PX, percent, EPD_DRAW_ALIGN_RIGHT, false);
}

void ui_nav_reader_status(uint8_t *fb, const char *title, bool landscape) {
    draw_status(fb, title, landscape, true);
}

void ui_nav_status(uint8_t *fb) { draw_status(fb, NULL, false, false); }

int ui_nav_hit(uint16_t x, uint16_t y) {
    if (x >= UI_LOCK_WIDTH || y < UI_NAV_TOP || y >= UI_LOCK_HEIGHT) return -1;
    int tab = x * 4 / UI_LOCK_WIDTH;
    return tab < 4 ? tab : -1;
}

void ui_nav_request(app_ctx_t *ctx, int index) {
    if (index >= 0 && index < 4) {
        ctx->request_app = app_at(index);
    }
}
