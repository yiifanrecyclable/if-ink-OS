/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_standard_font.h"
#include "app_font_context.h"
#include "app_transfer_mode.h"
#include "book_index_cache.h"
#include "book_store.h"
#include "settings.h"
#include "standard_font_install.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"

extern const app_desc_t app_transfer;

static standard_font_owner_t s_owner;
static bool s_waiting_wifi, s_wifi_returned, s_cancelling;
static standard_font_file_t s_file;
static standard_font_status_t s_status;

bool ui_standard_font_visible(standard_font_owner_t owner) { return s_owner == owner; }

void ui_standard_font_open(standard_font_owner_t owner) {
    s_owner = owner;
    s_file = standard_font_file();
    s_status = (standard_font_status_t){0};
    s_waiting_wifi = s_wifi_returned = s_cancelling = false;
}

static void dismiss(void) {
    app_settings_dismiss_standard_font_prompt();
    s_owner = 0;
    s_waiting_wifi = s_wifi_returned = false;
}

static void start_download(void) {
    s_file = standard_font_file();
    if (s_file != STANDARD_FONT_MISSING) return;
    standard_font_start();
    s_status = (standard_font_status_t){.state = STANDARD_FONT_DOWNLOADING, .busy = true};
}

app_redraw_t ui_standard_font_back(void) {
    if (s_status.busy) {
        if (s_cancelling) return APP_REDRAW_NONE;
        s_cancelling = true;
        standard_font_cancel();
    } else dismiss();
    return APP_REDRAW_PAGE;
}

void ui_standard_font_leave(standard_font_owner_t owner) {
    if (!ui_standard_font_visible(owner) || s_waiting_wifi) return;
    if (s_status.busy) standard_font_stop();
    dismiss();
}

void ui_standard_font_interrupt(standard_font_owner_t owner) {
    if (ui_standard_font_visible(owner) && s_status.busy) standard_font_stop();
}

void ui_standard_font_media_changed(standard_font_owner_t owner, bool ready) {
    if (!ui_standard_font_visible(owner)) return;
    if (!ready) ui_standard_font_interrupt(owner);
    s_file = ready ? standard_font_file() : STANDARD_FONT_NO_CARD;
    if (!s_status.busy) s_status = (standard_font_status_t){0};
}

void ui_standard_font_wifi_finished(bool returned) {
    if (!s_waiting_wifi) return;
    s_waiting_wifi = false;
    if (returned) s_wifi_returned = true;
    else dismiss();
}

app_redraw_t ui_standard_font_tick(app_ctx_t *ctx) {
    if (ctx->pressed || ctx->released || ctx->consumed || (ctx->touch && ctx->touch->touched))
        return APP_REDRAW_NONE;
    if (s_wifi_returned) {
        s_wifi_returned = false;
        s_file = standard_font_file();
        if (standard_font_wifi_ready()) start_download();
        return APP_REDRAW_PAGE;
    }
    if (!s_status.busy) return APP_REDRAW_NONE;

    standard_font_status_t next = standard_font_status();
    next.percent = next.busy ? next.percent / 10 * 10 : next.percent;
    if (s_status.state == next.state && s_status.busy == next.busy && s_status.percent == next.percent)
        return APP_REDRAW_NONE;
    s_status = next;
    if (!next.busy) {
        s_cancelling = false;
        s_file = standard_font_file();
        if (next.state == STANDARD_FONT_INSTALLED) {
            app_settings_dismiss_standard_font_prompt();
            app_font_activate_system();
            ttf_font_picker_invalidate();
            if (s_owner == STANDARD_FONT_SETTINGS)
                ttf_font_picker_begin(book_store_revision(),book_index_cache_epoch());
        }
    }
    return APP_REDRAW_PAGE;
}

static const char *status_label(void) {
    if (s_cancelling) return UI_MSG("正在取消下载…");
    switch (s_status.state) {
    case STANDARD_FONT_DOWNLOADING: return UI_MSG("正在下载标准字库…");
    case STANDARD_FONT_INSTALLED: return UI_MSG("标准字库已安装");
    case STANDARD_FONT_CANCELLED: return UI_MSG("已取消");
    case STANDARD_FONT_NETWORK_ERROR: return UI_MSG("下载失败，请检查 WiFi 后重试");
    case STANDARD_FONT_INVALID: return UI_MSG("字体校验失败，请重试");
    case STANDARD_FONT_WRITE_ERROR: return UI_MSG("无法保存字体，请检查 TF 卡");
    default: break;
    }
    if (s_file == STANDARD_FONT_NO_CARD) return UI_MSG("请插入可写入的 TF 卡");
    if (s_file == STANDARD_FONT_CONFLICT) return UI_MSG("请检查 TF 卡中的同名字体");
    if (s_file == STANDARD_FONT_PRESENT) return UI_MSG("标准字库已安装");
    return UI_MSG("下载约 10.5 MB，保存到 TF 卡");
}

static bool ready_to_install(void) { return !s_status.busy && s_file == STANDARD_FONT_MISSING; }
static int action_count(void) { return ready_to_install() ? 2 : 1; }

void ui_standard_font_draw(uint8_t *fb) {
    ui_action_card_t card = ui_nav_action_card(s_status.busy ? 54 : 30);
    ui_nav_draw_action_card(fb,card);
    ui_text_vc(fb,36,card.top+45,UI_TEXT_HEADING,UI_MSG("安装标准字库"),EPD_DRAW_ALIGN_LEFT,false);
    const char *label = status_label();
    int px = ui_text_fit_px(UI_TEXT_BODY,label,612,54);
    ui_text_fixed_vc(fb,36,card.top+112,px,label,EPD_DRAW_ALIGN_LEFT,false);
    if (s_status.busy) {
        int y = ui_nav_action_button(0,1).y - 31;
        epd_fill_rect((EpdRect){36,y,612,6},0xcc,fb);
        if (s_status.percent) epd_fill_rect((EpdRect){36,y,(int)s_status.percent*612/100,6},UI_GRAY_BLACK,fb);
    }
    const char *labels[] = {
        s_status.busy ? UI_MSG("取消下载") :
            s_status.state == STANDARD_FONT_IDLE && s_file == STANDARD_FONT_MISSING ? UI_MSG("稍后") : UI_MSG("关闭"),
        standard_font_wifi_ready() ? UI_MSG("下载并安装") : UI_MSG("连接 WiFi"),
    };
    ui_nav_draw_action_row(fb,labels,action_count());
}

static bool tapped(EpdRect r, const ui_gesture_event_t *ev) {
    return ui_rect_hit(r,ev->x0,ev->y0) && ui_rect_hit(r,ev->x,ev->y);
}

app_redraw_t ui_standard_font_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    int count = action_count();
    if (tapped(ui_nav_action_button(0,count),ev)) return ui_standard_font_back();
    if (count != 2 || !tapped(ui_nav_action_button(1,count),ev)) return APP_REDRAW_NONE;
    if (standard_font_wifi_ready()) {
        start_download();
        return APP_REDRAW_PAGE;
    }
    s_waiting_wifi = true;
    app_transfer_request_standard_font_wifi();
    ctx->request_app = &app_transfer;
    return APP_REDRAW_NONE;
}
