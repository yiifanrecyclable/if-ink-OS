/* SPDX-License-Identifier: Apache-2.0
 * 中文：复用 rc58 的真实书源、书架、阅读绘制及手势，仅操作虚拟存储。
 * English: rc58 book sources, shelf, rendering and gestures over virtual storage.
 */
#include "settings.h"
#define app_book simulator_descriptor_book
#include "../../main/apps/app_book.c"
#include "ui_gesture.h"
static ui_gesture_t s_sim_reader_gesture;
extern unsigned ttf_sim_font_loads;
static unsigned s_sim_event_draws,s_sim_event_loads,s_sim_area_updates,s_sim_area_violations;
static void simulator_reader_present(app_ctx_t *ctx,app_redraw_t redraw){
    // 与真实主循环一致：整页刷新会取消未完成的手势。/ Mirror main-loop cancellation before page redraws.
    if(redraw==APP_REDRAW_PAGE||redraw==APP_REDRAW_FULL){
        if(s_sim_reader_gesture.active){
            ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL,.x0=s_sim_reader_gesture.x0,.y0=s_sim_reader_gesture.y0,.x=s_sim_reader_gesture.x,.y=s_sim_reader_gesture.y};
            gesture_event(ctx,&cancel);
        }
        ui_gesture_reset(&s_sim_reader_gesture);render(ctx,ctx->fb);s_presented_view=s_view;
    }
}
static app_redraw_t simulator_reader_event(app_ctx_t *ctx,const ui_gesture_event_t *event){
    // 可选宿主检查：AREA不得改动声明区域之外的像素。/ Optional host check: AREA may not modify pixels outside its hint.
    uint8_t *before=getenv("PICO_SIM_VERIFY_AREAS")?malloc(684*1216/2):NULL;
    if(before)memcpy(before,ctx->fb,684*1216/2);
    app_redraw_t redraw=gesture_event(ctx,event);
    if(redraw==APP_REDRAW_AREA){
        ++s_sim_area_updates;
        if(before)for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
            bool inside=x>=s_area.x&&x<s_area.x+s_area.width&&y>=s_area.y&&y<s_area.y+s_area.height;
            if(!inside&&epd_get_pixel(x,y,684,1216,before)!=epd_get_pixel(x,y,684,1216,ctx->fb))++s_sim_area_violations;
        }
    }
    free(before);simulator_reader_present(ctx,redraw);return redraw;
}
static void simulator_reader_raw(app_ctx_t *ctx,char type,unsigned x,unsigned y){
    cst836u_touch_t touch={.touched=type!='e',.count=type=='e'?0:type=='n'?2:1,.x=x,.y=y};
    touch.points[0].x=x;touch.points[0].y=y;touch.points[1].x=x+60;touch.points[1].y=y;
    ctx->touch=&touch;ctx->pressed=type=='b';ctx->released=type=='e';
    ui_gesture_event_t event;if(ui_gesture_feed(&s_sim_reader_gesture,ctx,&event))simulator_reader_event(ctx,&event);
    simulator_reader_present(ctx,on_tick(ctx));ctx->touch=NULL;ctx->pressed=ctx->released=false;
}
#define SIM_REPLAY_RAW(ctx,type,x,y) simulator_reader_raw(ctx,type,x,y)
#define SIM_REPLAY_SHORTCUT(ctx,slot) reader_execute_shortcut(ctx,slot)
#include "replay.h"
extern int simulator_route,simulator_book_view,simulator_reader_panel,simulator_reader_page,simulator_reader_px;
extern bool simulator_reader_fullscreen,simulator_toc_jump;
unsigned simulator_reader_progress(void){return percent(s_page);}
bool simulator_document_image_ready(void){
    if(book_kind()!=BOOK_KIND_MARKDOWN)return false;
    for(int i=0;i<s_page_image_count;++i)if(s_page_images[i].gray)return true;
    return s_inline_gray&&book_layout_page_image(s_page)==s_inline_index;
}
bool simulator_reader_image_slot(unsigned i,int *x,int *y,int *w,int *h){
    if(!reader_image_slot(s_page,(int)i,NULL,y,w,h))return false;
    EpdRect body=body_rect();*x=body.x+(body.width-*w)/2;*y+=body.y;return true;
}
bool simulator_document_zoom(void){return false;}
unsigned simulator_document_image_top(void){return 0;}
size_t simulator_document_count(void){return s_document_count;}
bool simulator_document_starred_only(void){return s_document_starred_only;}
bool simulator_document_menu(void){return s_document_menu;}
bool simulator_document_delete_confirm(void){return s_document_delete_confirm;}
const char *simulator_document_first(void){return s_document_count?s_documents[0].path:"";}
const char *simulator_document_item(unsigned i){return i<s_document_count?s_documents[i].path:"";}
const char *simulator_shelf_item(unsigned i){return i<(unsigned)s_visible_count?s_shelf[i].path:"";}
int simulator_list_progress(bool documents,unsigned i){return documents?(i<s_document_count&&s_documents[i].has_progress?s_documents[i].pct:-1):(i<(unsigned)s_visible_count&&s_shelf[i].has_progress?s_shelf[i].pct:-1);}
bool simulator_content_sort_open(void){return s_sort_open;}
unsigned simulator_document_selected_count(void){return document_selected_count();}
bool simulator_document_batch_confirm(void){return s_document_batch_confirm;}
unsigned simulator_content_sort_mode(bool documents){return documents?s_document_options.mode:s_book_options.mode;}
const char *simulator_book_path(void){return s_requested_open[0]?s_requested_open:s_path;}
unsigned simulator_reader_bookmark_count(void){return bookmark_count();}
unsigned simulator_reader_binding(unsigned slot){return reader_input_action(slot);}
int simulator_reader_margin(void){return body_rect().x;}
size_t simulator_reader_offset(void){return reader_page_offset(s_page);}
int simulator_reader_progress_geometry(unsigned index){return index==0?reader_progress_y():index==1?reader_actions_y():reader_actions_height();}
int simulator_reader_control_geometry(unsigned index){return index==0?reader_control_top():index==1?reader_row_height():reader_control_rows();}
int simulator_reader_layout(void){return s_reader_fullscreen?(s_reader_immersive?2:1):0;}
int simulator_reader_tap_layout(void){return reader_input_tap_layout();}
int simulator_markdown_slider_y(unsigned index){EpdRect r=reader_markdown_row((int)index);return r.y+(index?98:54)+26;}
unsigned simulator_reader_perf(unsigned i){return i==0?s_sim_event_draws:i==1?s_sim_event_loads:i==2?s_sim_area_updates:s_sim_area_violations;}
int simulator_toc_percent(void){return s_toc_jump_percent;}
bool simulator_toc_drag(void){return s_toc_jump_drag;}
// 宿主每帧完成有限个空闲封面任务；硬件仍一次只处理一张。
// Settle a bounded number of idle cover jobs per host frame; hardware keeps one job per idle tick.
static void simulator_settle_covers(app_ctx_t *ctx){
 if(s_view!=SHELF&&s_view!=MANAGE)return;
 book_view_t view=s_view;s_view=SHELF;prepare_covers(ctx);
 for(int i=0;i<BOOK_ROWS&&s_cover_pending_mask;++i)(void)on_tick(ctx);
 s_view=view;
}
size_t simulator_book(uint8_t *fb,int view,int panel,int page,int px,const char *path,const char *events){
 if(panel==6||panel==8)panel=READER_PANEL_TOOLS;
 if(panel==10)panel=READER_PANEL_SHORTCUTS;
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);scan_shelf(&ctx);scan_documents(&ctx);s_scan_pending=false;
 ctx.leaf=(view!=READING&&view!=TOC)?page:0;
 if(view==READING||view==TOC){
   if(!open_book(&ctx,path))return 0;
   // -1是跨页面打开的续读请求；按原文字节位置重排，不用界面的默认第0页覆盖它。
   // -1 requests resume across app routes; reflow the saved text offset instead of overriding it with UI page zero.
   size_t resume_offset=reader_page_offset(s_page);
   s_reader_fullscreen=panel==100||app_settings_reader_fullscreen();s_reader_immersive=app_settings_reader_immersive();if(book_kind()!=BOOK_KIND_MARKDOWN)s_px=px;
   if(!load_chapter(&ctx,s_chapter,page<0?resume_offset:0,false))return 0;
   if(page>=0)s_page=(size_t)page;
   if(s_page>=book_layout_page_count())s_page=book_layout_page_count()-1;prepare_inline_image();
 }
 if(panel==READER_PANEL_FONT_PICKER)ttf_font_scan();
 if(view==TOC)app_font_activate_system();
 s_view=(book_view_t)view;s_reader_panel=view==READING?(reader_panel_t)panel:READER_PANEL_NONE;
 if(view==DOCUMENTS||view==DOCUMENT_BULK)s_document_mode=true;
 s_reader_fullscreen=panel==100||app_settings_reader_fullscreen();s_reader_immersive=app_settings_reader_immersive();if(panel==100)s_reader_panel=READER_PANEL_NONE;
 if(view==TOC){s_toc_jump_open=panel==1;s_toc_jump_percent=45;}
 if(s_count){s_managed=s_shelf[0];snprintf(s_editor_title,sizeof(s_editor_title),"%s",s_managed.name);}
 if(view==EDIT)editor_start();
 if(view==SEARCH){s_search_parent=BULK;ui_text_input_open(&s_search_input,s_search_draft,sizeof(s_search_draft),true,false,UI_INPUT_ENGLISH);}
 simulator_settle_covers(&ctx);render(&ctx,fb);s_presented_view=s_view;
 unsigned draws=s_sim_reader_draws,loads=ttf_sim_font_loads;
 simulator_replay(&ctx,simulator_reader_event,on_key,events);
 s_sim_event_draws=s_sim_reader_draws-draws;s_sim_event_loads=ttf_sim_font_loads-loads;
 // 宿主每帧是独立进程；退出前保存内存中的当前位置，模拟真机离页的保存边界。
 // Each host frame is a separate process; persist its current position to model the hardware leave-page save boundary.
 if(s_text&&(s_view==READING||s_view==TOC))save_progress();
 simulator_settle_covers(&ctx);render(&ctx,fb);
 if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
 simulator_toc_jump=s_toc_jump_open;simulator_book_view=s_view;simulator_reader_panel=s_reader_panel;simulator_reader_fullscreen=s_reader_fullscreen;simulator_reader_page=s_view==READING?s_page:ctx.leaf;simulator_reader_px=s_px;
 size_t pages=s_view==READING?book_layout_page_count():(size_t)leaves();
 /* 退出进程时由 OS 回收；保留路径供 render 输出，不模拟实际关书。
  * OS reclaims this one-frame process; preserve source path for renderer metadata. */
 return pages?pages:1;
}

bool simulator_markdown_committed(void){return s_md_committed;}
unsigned simulator_markdown_size(unsigned index){return index<4?(s_reader_slider>=5?s_md_draft[index]:s_md_sizes[index]):0;}

int simulator_document_rows(void){return document_rows();}
int simulator_document_row_height(void){return document_row_step();}
int simulator_management_rows(void){return management_rows();}

const char *simulator_document_page_first(void){return simulator_document_item((unsigned)(simulator_reader_page*document_rows()));}

int simulator_shelf_geometry(unsigned row,unsigned part){
 if(row>=(unsigned)shelf_rows())return 0;EpdRect card=row_rect((int)row),image=shelf_image_rect(card,(int)row),track=shelf_progress_rect(card);
 int fields[]={card.x,card.y,card.width,card.height,image.x,image.y,image.width,image.height,(int)s_covers[row].width,(int)s_covers[row].height,track.x,track.y,track.width,track.height};
 return part<14?fields[part]:0;
}
