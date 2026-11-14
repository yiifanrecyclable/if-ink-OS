/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

static app_redraw_t wide_detail_turn(int direction) {
    int next=s_control_page+direction;
    if(next<0||next>=wide_detail_pages())return APP_REDRAW_NONE;
    reader_set_control_page(next);
    return APP_REDRAW_PAGE;
}

static app_redraw_t wide_toc_action(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    int x=ev->x0,y=ev->y0;
    bool tap=ev->type==UI_GESTURE_TAP;
    if(s_toc_jump_open){
        EpdRect slider=wide_toc_jump_slider();
        if(tap && ui_rect_hit(slider,x,y)){
            int delta=x<slider.x+64?-1:x>=slider.x+slider.width-64?1:0;
            if(delta){
                int value=s_toc_jump_percent+delta;
                if(value<0||value>100)return APP_REDRAW_NONE;
                s_toc_jump_percent=value;
                return reader_wide_preview(ctx);
            }
        }
        int value=slider_index(slider,ev->x,101);
        if(ev->type==UI_GESTURE_PRESS && y>=slider.y && y<slider.y+slider.height &&
           x>=slider.x+64&&x<slider.x+slider.width-64){
            s_toc_jump_origin=s_toc_jump_percent;
            s_toc_jump_drag=true;
        }
        if(s_toc_jump_drag){
            if(ev->type==UI_GESTURE_LONG_PRESS)return APP_REDRAW_NONE;
            if(ev->type==UI_GESTURE_CANCEL&&!(ctx->released&&ctx->touch&&!ctx->consumed))value=s_toc_jump_origin;
            if(ev->type!=UI_GESTURE_PRESS&&ev->type!=UI_GESTURE_MOVE)s_toc_jump_drag=false;
            if(value==s_toc_jump_percent)return APP_REDRAW_NONE;
            s_toc_jump_percent=value;
            return reader_wide_preview(ctx);
        }
        if(tap&&y>=482&&y<565){
            if(x>=WIDE_PANE_WIDTH/2)ctx->leaf=book_toc_page_from_percent(s_toc_jump_percent,leaves());
            s_toc_jump_open=false;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(!tap)return APP_REDRAW_NONE;
    if(y>=WIDE_PAGER_Y){
        if(leaves()<=1)return APP_REDRAW_NONE;
        int button=x*5/WIDE_PANE_WIDTH;
        if(button==2){
            s_toc_jump_open=true;
            s_toc_jump_percent=(ctx->leaf+1)*100/leaves();
        }else{
            const int shift[]={-10,-1,0,1,10};
            int next=ctx->leaf+shift[button];
            if(next<0)next=0;
            if(next>=leaves())next=leaves()-1;
            if(next==ctx->leaf)return APP_REDRAW_NONE;
            ctx->leaf=next;
        }
        return APP_REDRAW_PAGE;
    }
    if(x<24||x>=WIDE_PANE_WIDTH-24||y<24||y>=24+WIDE_TOC_ROWS*WIDE_LIST_STEP)return APP_REDRAW_NONE;
    int target=ctx->leaf*WIDE_TOC_ROWS+(y-24)/WIDE_LIST_STEP;
    return (size_t)target<book_navigation_count()?reader_toc_target(ctx,target,x):APP_REDRAW_NONE;
}

static app_redraw_t wide_detail_action(app_ctx_t *ctx, uint16_t x,uint16_t y) {
    if(s_bookmark_delete_confirm&&s_reader_panel==READER_PANEL_BOOKMARKS){
        if(y<360||y>=440)return APP_REDRAW_NONE;
        if(x<WIDE_PANE_WIDTH/2)s_bookmark_delete_confirm=s_bookmark_delete_error=false;
        else if(bookmark_delete_selected()){
            s_bookmark_delete_confirm=s_bookmark_delete_error=false;
            int last=wide_detail_pages()-1;
            if(s_control_page>last)reader_set_control_page(last);
        }else s_bookmark_delete_error=true;
        return APP_REDRAW_PAGE;
    }
    if(wide_detail_title()&&ui_rect_hit(wide_close(),x,y)){
        if(s_reader_panel==READER_PANEL_BINDING)return reader_show_panel(READER_PANEL_SHORTCUTS);
        if(s_reader_panel==READER_PANEL_FONT_PICKER)return reader_show_panel(READER_PANEL_FONT_SETTINGS);
        if(s_reader_panel==READER_PANEL_RULE_SETTINGS)return reader_show_panel(READER_PANEL_LAYOUT_SETTINGS);
        return reader_show_panel(READER_PANEL_REFRESH_SETTINGS);
    }
    if(y>=WIDE_PAGER_Y&&wide_detail_pages()>1){
        if(x<WIDE_PANE_WIDTH/3)return wide_detail_turn(-1);
        if(x>=WIDE_PANE_WIDTH*2/3)return wide_detail_turn(1);
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel==READER_PANEL_BASIC)return APP_REDRAW_NONE;
    if(s_reader_panel==READER_PANEL_LAYOUT_SETTINGS){
        if(s_control_page)return APP_REDRAW_NONE;
        if(y>=26&&y<94){
            int button=x*3/WIDE_PANE_WIDTH;
            bool vertical=reader_book_vertical();
            if(button==0)return apply_book_presentation(!vertical,s_reader_screen);
            if(button==1)return apply_book_presentation(vertical,APP_BOOK_PORTRAIT);
            return apply_book_presentation(vertical,s_reader_screen==APP_BOOK_LANDSCAPE?
                      APP_BOOK_LANDSCAPE_TWO_COLUMNS:APP_BOOK_LANDSCAPE);
        }
        if(ui_rect_hit(wide_detail_row(0),x,y))return apply_reader_indent(ctx,(app_settings_book_indent()+1)%4);
        if(ui_rect_hit(wide_detail_row(1),x,y))return reader_show_panel(READER_PANEL_RULE_SETTINGS);
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel==READER_PANEL_FONT_SETTINGS){
        return ui_rect_hit(wide_detail_row(0),x,y)?reader_show_panel(READER_PANEL_FONT_PICKER):APP_REDRAW_NONE;
    }
    int rows=wide_detail_rows(),count=reader_control_count();
    for(int row=0;row<rows;++row){
        int index=s_control_page*rows+row;
        if(index>=count||!ui_rect_hit(wide_detail_row(row),x,y))continue;
        return reader_panel_item(ctx,index,x,y);
    }
    return APP_REDRAW_NONE;
}

static uint16_t wide_progress_x(int x) {
    if(x<36)x=36;
    if(x>WIDE_PANE_WIDTH-36)x=WIDE_PANE_WIDTH-36;
    return 36+(x-36)*(UI_LOCK_WIDTH-72)/(WIDE_PANE_WIDTH-72);
}
static app_redraw_t wide_gesture(app_ctx_t *ctx,const ui_gesture_event_t *event) {
    ui_gesture_event_t ev=*event;
    bool tap=ev.type==UI_GESTURE_TAP;
    if(s_view==READING&&s_reader_panel==READER_PANEL_HELP)
        return tap?reader_help_tap(ev.x0,ev.y0,LANDSCAPE_REGULAR_PROGRESS_Y+6):APP_REDRAW_NONE;
    if(ctx->touch&&ctx->touch->count>1){
        if(!s_progress_drag&&s_reader_slider<0&&!s_toc_jump_drag)return APP_REDRAW_NONE;
        s_progress_drag=false;
        s_reader_slider=-1;
        if(s_toc_jump_drag)s_toc_jump_percent=s_toc_jump_origin;
        s_toc_jump_drag=false;
        return APP_REDRAW_PAGE;
    }
    int origin=wide_panel_x(true);
    bool tools=ev.x0>=origin&&ev.x0<origin+WIDE_PANE_WIDTH;
    if(!tools)origin=wide_panel_x(false);
    int x=(int)ev.x0-origin,end=(int)ev.x-origin;
    if(x<0||x>=WIDE_PANE_WIDTH)return APP_REDRAW_NONE;
    if(end<0)end=0;
    if(end>=WIDE_PANE_WIDTH)end=WIDE_PANE_WIDTH-1;
    ev.x0=x;ev.x=end;
    if(tools){
        if(s_progress_drag||(ev.type==UI_GESTURE_PRESS&&abs((int)ev.y0-WIDE_PROGRESS_Y)<25&&x>=16&&x<WIDE_PANE_WIDTH-16)){
            ev.x0=wide_progress_x(ev.x0);
            ev.x=wide_progress_x(ev.x);
            return reader_progress_gesture(ctx,&ev);
        }
        if(!tap||event->x<origin||event->x>=origin+WIDE_PANE_WIDTH)return APP_REDRAW_NONE;
        if(ev.y0>=WIDE_TABS_Y)return reader_show_tab(ctx,x*5/WIDE_PANE_WIDTH);
        if(ev.y0>=WIDE_ACTIONS_Y&&ev.y0<WIDE_ACTIONS_Y+108)return reader_quick_action(ctx,x*4/WIDE_PANE_WIDTH);
        if(s_jump_back_valid&&ev.y0>=246&&ev.y0<303&&x>=WIDE_PANE_WIDTH-205){
            if(s_view==TOC){s_toc_jump_open=s_toc_jump_drag=false;set_reader_view(READING);}
            if(load_chapter(ctx,s_jump_back_chapter,s_jump_back_offset,false)){
                s_jump_back_valid=false;s_reader_panel=READER_PANEL_NONE;save_progress();
            }
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_view==TOC)return wide_toc_action(ctx,&ev);
    if(s_reader_panel==READER_PANEL_TOOLS)return tap&&event->x>=origin&&event->x<origin+WIDE_PANE_WIDTH?
        reader_show_panel(READER_PANEL_NONE):APP_REDRAW_NONE;
    if(s_reader_slider>=0)return reader_style_gesture(ctx,&ev);
    if(ev.type==UI_GESTURE_PRESS&&(s_reader_panel==READER_PANEL_FONT_SETTINGS||s_reader_panel==READER_PANEL_LAYOUT_SETTINGS)){
        int slider=reader_slider_at(ev.x0,ev.y0);
        if(slider>=0){reader_slider_begin(slider,ev.x0);return reader_wide_preview(ctx);}
    }
    if(!tap||event->x<origin||event->x>=origin+WIDE_PANE_WIDTH)return APP_REDRAW_NONE;
    return wide_detail_action(ctx,ev.x0,ev.y0);
}
