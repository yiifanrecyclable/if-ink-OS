/* SPDX-License-Identifier: Apache-2.0
 * 中文：数字拼豆，17x27点阵、六灰度、32步撤回和TF卡PNG保存。
 * English: Cyber Beads with a 17x27 grid, six gray shades, 32-step undo and SD PNG export.
 */
#include "app.h"
#include "cyber_beads.h"
#include "beads_png.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "read_pico_sd.h"
#include "usb_storage.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include <stdlib.h>
#include <string.h>
static cyber_beads_t *s_board;
static int s_dialog;
static EpdRect s_area;
static bool s_toolbar_dirty, s_board_save_pending;
static char s_notice[160];
static const uint8_t shades[]={0xdd,0xbb,0x88,0x55,0x22,0x00};
static EpdRect toolbar(void) { return (EpdRect){0,1100,684,116}; }
#define GRID_PITCH 39
#define GRID_LEFT 11
#define GRID_TOP 15
static EpdRect cell_rect(int i) { return (EpdRect){GRID_LEFT+(i%BEADS_COLS)*GRID_PITCH,GRID_TOP+(i/BEADS_COLS)*GRID_PITCH,GRID_PITCH,GRID_PITCH}; }
static bool any_beads(void) {
    if(s_board)for(int i=0;i<BEADS_COUNT;++i)if(s_board->cells[i]>=0)return true;
    return false;
}
static void draw_cell(uint8_t *fb,int i) {
    EpdRect r=cell_rect(i);int cx=r.x+GRID_PITCH/2,cy=r.y+GRID_PITCH/2;
    epd_fill_rect(r,UI_GRAY_WHITE,fb);
    int color=s_board?s_board->cells[i]:-1;
    if(color<0)epd_fill_circle(cx,cy,2,0x99,fb);
    else { epd_fill_circle(cx,cy,18,shades[color],fb);epd_fill_circle(cx,cy,7,UI_GRAY_WHITE,fb); }
}
static void draw_tools(uint8_t *fb) {
    epd_fill_rect(toolbar(),UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){0,1100,684,2},UI_GRAY_ITEM_LINE,fb);
    const int cy=1144;
    // 所有工具图标与候选豆共用中心线。/ Align every tool icon and candidate bead on one centerline.
    const int icon_size=42;
    ui_draw_icon(fb,36,cy,icon_size,UI_ICON_ARROW_LEFT,UI_GRAY_BLACK);
    ui_draw_icon(fb,108,cy,icon_size,UI_ICON_UNDO_2,s_board&&s_board->steps?0:0x99);
    for(int i=0;i<6;++i) {
        int cx=177+i*66;epd_fill_circle(cx,cy,20,shades[i],fb);epd_fill_circle(cx,cy,8,UI_GRAY_WHITE,fb);
        if(s_board&&i==(int)s_board->selected)ui_fill_round_rect(fb,(EpdRect){cx-14,1170,29,6},3,UI_GRAY_BLACK);
    }
    ui_draw_icon(fb,576,cy,icon_size,UI_ICON_BRUSH_CLEANING,any_beads()?0:0x99);
    ui_draw_icon(fb,648,cy,icon_size,UI_ICON_SAVE,s_board&&(cyber_beads_can_save(s_board)||s_board_save_pending)?0:0x99);
}
static EpdRect dialog_button(int index) { return (EpdRect){62,560+index*92,560,76}; }
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;ui_clear_page(fb);
    for(int i=0;i<BEADS_COUNT;++i)draw_cell(fb,i);
    draw_tools(fb);
    if(!s_board)ui_text_vc(fb,342,550,UI_TEXT_BODY,UI_MSG("内存不足"),EPD_DRAW_ALIGN_CENTER,false);
    if(s_notice[0]) {
        epd_fill_rect((EpdRect){30,1012,624,72},UI_GRAY_WHITE,fb);
        ui_text_vc(fb,342,1048,UI_TEXT_CAPTION,s_notice,EPD_DRAW_ALIGN_CENTER,false);
    }
    if(s_dialog) {
        EpdRect r={36,424,612,s_dialog==1?438:346};
        epd_fill_rect(r,UI_GRAY_WHITE,fb);epd_draw_rect(r,UI_GRAY_BLACK,fb);
        ui_text_vc(fb,342,480,UI_TEXT_TITLE,s_dialog==1?UI_MSG("画布尚未保存"):UI_MSG("清空画布？"),EPD_DRAW_ALIGN_CENTER,false);
        if(s_dialog==2)ui_text_vc(fb,342,524,UI_TEXT_CAPTION,UI_MSG("清空后可用撤回恢复"),EPD_DRAW_ALIGN_CENTER,false);
        const char *exit_labels[]={UI_MSG("保存并退出"),UI_MSG("不保存退出"),UI_MSG("继续编辑")};
        const char *clear_labels[]={UI_MSG("清空"),UI_MSG("取消")};
        for(int i=0;i<(s_dialog==1?3:2);++i)ui_draw_button(fb,dialog_button(i),s_dialog==1?exit_labels[i]:clear_labels[i],false);
    }
}
static void leave(app_ctx_t *ctx) { extern const app_desc_t app_files;ctx->request_app=&app_files; }
static app_redraw_t request_exit(app_ctx_t *ctx) {
    if(s_board&&(cyber_beads_dirty(s_board)||s_board_save_pending)){s_dialog=1;return APP_REDRAW_PAGE;}
    leave(ctx);return APP_REDRAW_NONE;
}
static bool save(app_ctx_t *ctx) {
    if(!s_board||(!cyber_beads_can_save(s_board)&&!s_board_save_pending))return true;
    if(usb_storage_active()){ui_snprintf(s_notice,sizeof(s_notice),UI_MSG("请先结束同步或USB连接"));return false;}
    read_pico_sd_info_t sd;read_pico_sd_get_info(&sd);
    if(!sd.mounted){ui_snprintf(s_notice,sizeof(s_notice),UI_MSG("请检查 TF 卡"));return false;}
    // 仅在内存画面暂时替换工具栏，导出后立即恢复；不把导出过程推屏。
    // Replace tools only in the framebuffer for export and restore immediately, without presenting it.
    int previous_dialog=s_dialog;s_dialog=0;s_notice[0]=0;render(ctx,ctx->fb);
    epd_fill_rect(toolbar(),UI_GRAY_WHITE,ctx->fb);
    epd_fill_rect((EpdRect){0,1012,684,88},UI_GRAY_WHITE,ctx->fb);
    for(int i=0;i<BEADS_COUNT;++i)if(cell_rect(i).y+GRID_PITCH>1012)draw_cell(ctx->fb,i);
    ui_text_fixed_vc_gray(ctx->fb,342,1158,24,"if-ink OS @ Pico",EPD_DRAW_ALIGN_CENTER,6,15);
    char path[128];bool ok=true;
    if(cyber_beads_can_save(s_board)) {
        ok=beads_png_save(ctx->fb,path,sizeof(path));
        if(ok){cyber_beads_saved(s_board);s_board_save_pending=true;}
    }
    if(ok) {
        ok=beads_board_save(s_board);
        if(ok){s_board_save_pending=false;ui_snprintf(s_notice,sizeof(s_notice),UI_MSG("已保存到 pictures/pindou"));}
        else ui_snprintf(s_notice,sizeof(s_notice),UI_MSG("图片已保存，残局保存失败，请重试"));
    }
    else ui_snprintf(s_notice,sizeof(s_notice),UI_MSG("保存失败，请检查 TF 卡"));
    s_dialog=previous_dialog;render(ctx,ctx->fb);return ok;
}
static void on_enter(app_ctx_t *ctx) {
    (void)ctx;if(!s_board)s_board=heap_caps_malloc(sizeof(*s_board),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(s_board) {
        cyber_beads_reset(s_board);
        if(!usb_storage_active())(void)beads_board_load(s_board);
    }
    s_board_save_pending=false;
    s_notice[0]=0;s_dialog=0;s_area=(EpdRect){0};s_toolbar_dirty=false;
}
static void beads_on_exit(app_ctx_t *ctx) { (void)ctx;free(s_board);s_board=NULL; }
static app_redraw_t on_key(app_ctx_t *ctx,int key) { (void)key;return request_exit(ctx); }
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    if(s_dialog) {
        int count=s_dialog==1?3:2;
        for(int i=0;i<count;++i)if(ui_rect_hit(dialog_button(i),ev->x0,ev->y0)) {
            if(s_dialog==1) {
                if(i==0){if(save(ctx))leave(ctx);else s_dialog=0;}
                else if(i==1)leave(ctx);
                else s_dialog=0;
            } else {if(i==0&&s_board)cyber_beads_clear(s_board);s_dialog=0;}
            return ctx->request_app?APP_REDRAW_NONE:APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(ev->y0>=1100) {
        if(ev->x0<72)return request_exit(ctx);
        if(!s_board)return APP_REDRAW_NONE;
        bool had_notice=s_notice[0];s_notice[0]=0;
        if(ev->x0<144){if(!cyber_beads_undo(s_board))return APP_REDRAW_NONE;return APP_REDRAW_PAGE;}
        if(ev->x0<540){s_board->selected=(ev->x0-144)/66;s_area=toolbar();s_toolbar_dirty=false;draw_tools(ctx->fb);return had_notice?APP_REDRAW_PAGE:APP_REDRAW_AREA;}
        if(ev->x0<612){if(any_beads()){s_dialog=2;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
        if(!cyber_beads_can_save(s_board)&&!s_board_save_pending)return APP_REDRAW_NONE;
        save(ctx);return APP_REDRAW_PAGE;
    }
    if(!s_board||ev->x0<GRID_LEFT||ev->y0<GRID_TOP||ev->x0>=GRID_LEFT+BEADS_COLS*GRID_PITCH||ev->y0>=GRID_TOP+BEADS_ROWS*GRID_PITCH)return APP_REDRAW_NONE;
    int index=(ev->y0-GRID_TOP)/GRID_PITCH*BEADS_COLS+(ev->x0-GRID_LEFT)/GRID_PITCH;
    bool tools=!s_board->steps||!any_beads()||!cyber_beads_can_save(s_board);
    bool notice=s_notice[0];s_notice[0]=0;
    cyber_beads_place(s_board,index);tools=tools||!any_beads()||!cyber_beads_can_save(s_board);
    s_area=cell_rect(index);s_toolbar_dirty=tools;
    draw_cell(ctx->fb,index);if(tools)draw_tools(ctx->fb);
    return notice?APP_REDRAW_PAGE:APP_REDRAW_AREA;
}
static EpdRect area_hint(app_ctx_t *ctx) { (void)ctx;return s_area; }
static bool present(app_ctx_t *ctx,app_redraw_t redraw) {
    if(redraw!=APP_REDRAW_AREA)return false;
    guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,s_area));
    if(s_toolbar_dirty)guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,toolbar()));
    return true;
}
static bool no_menu(app_ctx_t *ctx) { (void)ctx;return false; }
const app_desc_t app_pindou={
    .title=UI_MSG("数字拼豆"),.detail="Cyber Beads",.enter_full=true,.owns_keys=true,
    .on_enter=on_enter,.on_exit=beads_on_exit,.render=render,.on_gesture=on_gesture,.on_key=on_key,
    .area_hint=area_hint,.present=present,.menu_handle_enabled=no_menu,
};
