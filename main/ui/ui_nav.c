/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：绘制与命中紧凑四栏导航；Lucide 图标表示首页、内容、文件和设置。
 * English: Draw and hit-test compact four-tab navigation with Lucide home, contents, files and settings icons.
 */
#include "ui_nav.h"
#include "ui_nav_icons.h"
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
#define UI_STATUS_TIME_PX UI_STATUS_PERCENT_PX
#define UI_STATUS_PERCENT_PX 28
#define UI_STATUS_SIGNATURE_PX UI_STATUS_PERCENT_PX

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
        static const ui_icon_t tabs[]={UI_ICON_HOUSE,UI_ICON_LIBRARY_BIG,UI_ICON_FOLDER_OPEN,UI_ICON_SLIDERS_HORIZONTAL};
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
    char label[80];snprintf(label,sizeof(label),"%d%s · %02d/%02d",count,unit,page+1,pages);
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

void ui_nav_status(uint8_t *fb) {
    time_t now = time(NULL);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (now < 1704067200 && pmu && pmu->time_synced && pmu->unix_sec >= 1704067200)
        now = pmu->unix_sec;
    char clock[64];app_settings_format_status_clock(now,clock,sizeof(clock));
    const int status_center_y = 40;
    // 固定状态栏字号，让系统字体缩放时文字仍与图标共用中心线。
    // Keep status type fixed so text and glyphs share one centerline at every system font scale.
    ui_text_fixed_vc(fb, 36, status_center_y, UI_STATUS_TIME_PX, clock, EPD_DRAW_ALIGN_LEFT, false);
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    const bool wifi_visible = network.network_ready && network.mode == READ_PICO_TRANSFER_MODE_STA;
    char percent[8] = "--%";
    int gauge_percent = pmu_battery_percent(pmu);
    if (gauge_percent >= 0) snprintf(percent, sizeof(percent), "%u%%", (unsigned)gauge_percent);
    // 按实际百分比宽度排列网络图标，留 10 像素间隔；墨色与电池相同。
    // Place the network marks 10 px before the measured percentage, using the battery ink.
    const int percent_left = 595 - ui_text_fixed_width_px(UI_STATUS_PERCENT_PX, percent);
    const int network_right = percent_left - 10;
    const int wifi_center = network_right - UI_NAV_NETWORK_ICON_PX / 2;
    int signature_width = 280;
    const int clock_right = 36 + ui_text_fixed_width_px(UI_STATUS_TIME_PX, clock);
    int available = 2 * (UI_LOCK_WIDTH / 2 - clock_right - 12);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    int group_left = percent_left;
    if (wifi_visible) {
        group_left = wifi_center - UI_NAV_NETWORK_ICON_PX / 2;
    }
    available = 2 * (group_left - 12 - UI_LOCK_WIDTH / 2);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    char signature[96];
    snprintf(signature, sizeof(signature), "%s", app_settings_status_signature());
    while (signature[0] && ui_text_fixed_width_px(UI_STATUS_SIGNATURE_PX, signature) > signature_width) {
        size_t n = strlen(signature) - 1;
        while (n && ((unsigned char)signature[n] & 0xc0) == 0x80) --n;
        signature[n] = 0;
    }
    if (signature[0]) ui_text_fixed_vc(fb, UI_LOCK_WIDTH / 2, status_center_y, UI_STATUS_SIGNATURE_PX,
                                      signature, EPD_DRAW_ALIGN_CENTER, false);
    if (wifi_visible) ui_nav_wifi_icon(fb, wifi_center, status_center_y, UI_NAV_NETWORK_ICON_PX, UI_GRAY_BLACK);
    EpdRect battery = {606, status_center_y - 11, 40, 22};
    ui_fill_round_rect(fb,battery,6,UI_GRAY_WHITE);
    ui_draw_round_rect(fb, battery, 6, UI_GRAY_BLACK);
    ui_fill_round_rect(fb, (EpdRect){648, status_center_y - 4, 5, 8}, 2, UI_GRAY_BLACK);
    bool charging = pmu_battery_charging(pmu);
    if (!charging && gauge_percent >= 0) {
        int width = (31 * gauge_percent + 50) / 100;
        if (width > 0) ui_fill_round_rect(fb, (EpdRect){610, status_center_y - 7, width, 14}, 4, UI_GRAY_BLACK);
    }
    if (charging) ui_nav_battery_bolt(fb,627,status_center_y);
    ui_text_fixed_vc(fb, 595, status_center_y, UI_STATUS_PERCENT_PX, percent, EPD_DRAW_ALIGN_RIGHT, false);
}

int ui_nav_hit(uint16_t x, uint16_t y) {
    if (x >= UI_LOCK_WIDTH || y < UI_NAV_TOP || y >= UI_LOCK_HEIGHT) return -1;
    int tab = x * 4 / UI_LOCK_WIDTH;
    return tab < 4 ? tab : -1;
}

void ui_nav_request(app_ctx_t *ctx, int index) {
    if (index >= 0 && index < 4) ctx->request_app = app_at(index);
}
