/* SPDX-License-Identifier: Apache-2.0
 * 阅读控制动作；所有快捷入口复用同一操作。/ Reader controls share their actions with all optional shortcuts.
 */
#pragma once
static app_redraw_t reader_show_panel(reader_panel_t panel){
    s_reader_panel=panel;s_control_page=0;s_reader_slider=-1;s_progress_drag=false;s_pressed_control=-1;
    s_reader_multi=(reader_multi_t){.blocked=true};invalidate_prep();return APP_REDRAW_PAGE;
}
static void reader_remember_jump(void){s_jump_back_valid=true;s_jump_back_chapter=s_chapter;s_jump_back_offset=reader_page_offset(s_page);}
static app_redraw_t reader_show_tab(app_ctx_t *ctx,int tab){
    s_control_page=0;s_reader_slider=-1;s_progress_drag=false;
    if(tab==0){save_progress();s_reader_panel=READER_PANEL_NONE;set_reader_view(TOC);s_message[0]=0;s_toc_jump_open=s_toc_jump_drag=false;size_t at=current_toc_position();ctx->leaf=at==SIZE_MAX?0:(int)(at/BOOK_TOC_ROWS);return APP_REDRAW_PAGE;}
    if(s_view==TOC)set_reader_view(READING);
    if(tab==1){s_bookmark_edit=s_bookmark_delete_confirm=s_bookmark_delete_error=false;s_bookmark_selected=0;return reader_show_panel(READER_PANEL_BOOKMARKS);}
    return reader_show_panel(tab==2?READER_PANEL_FONT_SETTINGS:tab==3?READER_PANEL_LAYOUT_SETTINGS:READER_PANEL_REFRESH_SETTINGS);
}
static app_redraw_t reader_change_size(app_ctx_t *ctx,int direction){
    int next=s_px+direction*BOOK_PX_STEP;if(next<BOOK_PX_MIN)next=BOOK_PX_MIN;if(next>BOOK_PX_MAX)next=BOOK_PX_MAX;
    if(next==s_px)return APP_REDRAW_NONE;
    if(book_kind()!=BOOK_KIND_MARKDOWN)return apply_reader_layout(ctx,next,s_margin,app_settings_book_line_spacing(),app_settings_book_paragraph_spacing());
    memcpy(s_md_draft,s_md_sizes,4);s_md_draft[0]=(uint8_t)next;
    for(int i=1;i<4;++i){int px=(s_md_sizes[i]*next+s_px/2)/s_px;s_md_draft[i]=px<24?24:px>96?96:px;}
    return apply_markdown_sizes(ctx);
}
static app_redraw_t reader_execute_shortcut(app_ctx_t *ctx,int slot){
    if(s_view!=READING||s_toolbar||s_clear_confirm||s_document_zoom||slot<0||slot>=READER_SHORTCUT_COUNT)return APP_REDRAW_NONE;
    switch(reader_input_action((unsigned)slot)){
    case READER_ACTION_PREV:return turn_page(ctx,-1);
    case READER_ACTION_NEXT:return turn_page(ctx,1);
    case READER_ACTION_TOC:return reader_show_tab(ctx,0);
    case READER_ACTION_BOOKMARKS:s_bookmark_edit=s_bookmark_delete_confirm=s_bookmark_delete_error=false;return reader_show_panel(READER_PANEL_BOOKMARKS);
    case READER_ACTION_PANEL:return reader_show_panel(READER_PANEL_TOOLS);
    case READER_ACTION_FULLSCREEN:return toggle_reader_fullscreen(ctx);
    case READER_ACTION_SMALLER:return reader_change_size(ctx,-1);
    case READER_ACTION_LARGER:return reader_change_size(ctx,1);
    case READER_ACTION_BOOKMARK:(void)bookmark_toggle();return APP_REDRAW_PAGE;
    case READER_ACTION_REFRESH:return reader_manual_refresh(ctx);
    default:return APP_REDRAW_NONE;
    }
}
static app_redraw_t reader_help_close(void){
    esp_err_t err=reader_input_ack_help(s_path,s_help_hide);
    if(err!=ESP_OK){copy_text(s_reader_notice,sizeof(s_reader_notice),"操作说明偏好保存失败，请重试");s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;}
    return reader_show_panel(READER_PANEL_NONE);
}
static app_redraw_t reader_panel_action(app_ctx_t *ctx,uint16_t x,uint16_t y){
    if(s_reader_panel==READER_PANEL_HELP){
        if(y>=80&&y<160){if(x>530)return reader_help_close();if(x<460){s_help_hide=!s_help_hide;return APP_REDRAW_PAGE;}}
        return APP_REDRAW_NONE;
    }
    if(s_bookmark_delete_confirm){
        if(y>=590&&y<700){if(x<342)s_bookmark_delete_confirm=s_bookmark_delete_error=false;
            else if(bookmark_delete_selected()){s_bookmark_delete_confirm=s_bookmark_delete_error=false;int pages=(bookmark_count()+1)/reader_control_rows()+1;if(s_control_page>=pages)s_control_page=pages-1;}
            else s_bookmark_delete_error=true;
            return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(y>=1096){int tab=x*5/684;return reader_show_tab(ctx,tab);}
    if(s_reader_panel==READER_PANEL_TOOLS){
        int top=reader_control_top();if(y<top)return reader_show_panel(READER_PANEL_NONE);
        int ay=reader_actions_y(),height=reader_actions_height();
        if(y>=ay&&y<ay+height){int button=x*4/684;
            if(button==0)return reader_return(ctx,false);
            if(button==1)return toggle_reader_fullscreen(ctx);
            if(button==2){(void)bookmark_toggle();return APP_REDRAW_PAGE;}
            return reader_manual_refresh(ctx);
        }
        if(s_jump_back_valid&&ui_rect_hit(reader_jump_back_rect(),x,y)){size_t chapter=s_jump_back_chapter,offset=s_jump_back_offset;if(load_chapter(ctx,chapter,offset,false)){s_jump_back_valid=false;s_reader_panel=READER_PANEL_NONE;save_progress();}return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(y<180){
        if(x>570)return reader_show_panel(READER_PANEL_NONE);
        if(x<110){
            if(s_reader_panel==READER_PANEL_BINDING)return reader_show_panel(READER_PANEL_SHORTCUTS);
            if(s_reader_panel==READER_PANEL_FONT_PICKER||s_reader_panel==READER_PANEL_MARKDOWN_SIZES)return reader_show_panel(READER_PANEL_FONT_SETTINGS);
            if(s_reader_panel==READER_PANEL_RULE_SETTINGS)return reader_show_panel(READER_PANEL_LAYOUT_SETTINGS);
            if(s_reader_panel==READER_PANEL_BASIC)return reader_show_panel(READER_PANEL_REFRESH_SETTINGS);
            return reader_show_panel(READER_PANEL_TOOLS);
        }
        return APP_REDRAW_NONE;
    }
    if(s_reader_panel==READER_PANEL_BASIC){
        for(int mode=0;mode<2;++mode)if(ui_rect_hit(reader_basic_option(mode),x,y)){
            if(reader_input_set_tap_layout((reader_tap_layout_t)mode)!=ESP_OK){copy_text(s_reader_notice,sizeof(s_reader_notice),"操作区域保存失败，请重试");s_reader_notice_until=esp_timer_get_time()/1000+2500;}
            return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(reader_markdown_font_panel()){
        if(ui_rect_hit((EpdRect){36,210,612,80},x,y)){ttf_font_scan();return reader_show_panel(READER_PANEL_FONT_PICKER);}
        return APP_REDRAW_NONE;
    }
    int count=reader_control_count(),rows=reader_control_rows();
    if(y>=1018&&y<1096&&count>rows){int next=s_control_page+(x<342?-1:1);if(next>=0&&next<1+(count-1)/rows)s_control_page=next;return APP_REDRAW_PAGE;}
    if(y<220||y>=220+rows*reader_row_height())return APP_REDRAW_NONE;
    int row=(y-220)/reader_row_height(),i=s_control_page*rows+row;
    if(s_reader_panel==READER_PANEL_FONT_SETTINGS){
        if(row==1){ttf_font_scan();return reader_show_panel(READER_PANEL_FONT_PICKER);}
        if(row==2&&book_kind()==BOOK_KIND_MARKDOWN)return reader_show_panel(READER_PANEL_MARKDOWN_SIZES);
    }else if(s_reader_panel==READER_PANEL_FONT_PICKER){
        if(i>=ttf_font_count()+1)return APP_REDRAW_NONE;
        ttf_font_item_t builtin={0};snprintf(builtin.path,sizeof(builtin.path),"%s",TTF_FONT_BUILTIN);
        app_redraw_t result=select_reading_font_item(ctx,i?ttf_font_item(i-1):&builtin);
        if(result!=APP_REDRAW_NONE)s_reader_panel=READER_PANEL_FONT_SETTINGS;
        return result;
    }else if(s_reader_panel==READER_PANEL_LAYOUT_SETTINGS){
        if(i==4)return apply_reader_indent(ctx,(app_settings_book_indent()+1)%4);
        if(i==5)return reader_show_panel(READER_PANEL_RULE_SETTINGS);
    }else if(s_reader_panel==READER_PANEL_RULE_SETTINGS){
        if(i<3){int style=i;app_settings_set_book_reading_line(style);book_layout_set_reading_line(style);}
        else if(i==3||i==4){int dir=ui_rect_hit(reader_rule_offset_rect(-1),x,y)?-1:ui_rect_hit(reader_rule_offset_rect(1),x,y)?1:0;if(i==3&&!dir)return APP_REDRAW_NONE;int offset=i==4?0:app_settings_book_reading_line_offset()+dir*2;if(offset< -8)offset=-8;if(offset>8)offset=8;app_settings_set_book_reading_line_offset(offset);book_layout_set_reading_line_offset(offset);}
        else return APP_REDRAW_NONE;
        invalidate_prep();return APP_REDRAW_PAGE;
    }else if(s_reader_panel==READER_PANEL_SHORTCUTS){
        if(i>=READER_SHORTCUT_COUNT)return APP_REDRAW_NONE;
        s_shortcut_slot=i;return reader_show_panel(READER_PANEL_BINDING);
    }else if(s_reader_panel==READER_PANEL_BINDING){
        if(i>=READER_ACTION_COUNT)return APP_REDRAW_NONE;
        int owner=reader_binding_owner(i);
        if(owner>=0&&owner!=s_shortcut_slot){snprintf(s_reader_notice,sizeof(s_reader_notice),"已绑定%s",reader_shortcut_name(owner));s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;}
        if(reader_input_assign(s_shortcut_slot,i)!=ESP_OK){copy_text(s_reader_notice,sizeof(s_reader_notice),"快捷操作保存失败，请重试");s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;}
        return reader_show_panel(READER_PANEL_SHORTCUTS);
    }else if(s_reader_panel==READER_PANEL_BOOKMARKS){
        if(i==0){(void)bookmark_toggle();return APP_REDRAW_PAGE;}
        if(i==1){s_bookmark_edit=!s_bookmark_edit;return APP_REDRAW_PAGE;}
        reader_bookmarks_t marks={0};if(!bookmark_load(&marks)||i-2>=marks.count)return APP_REDRAW_NONE;
        int at=marks.count-(i-2)-1;
        if(s_bookmark_edit){s_bookmark_selected=UINT32_C(1)<<at;s_bookmark_delete_confirm=true;return APP_REDRAW_PAGE;}
        reader_bookmark_entry_t mark=marks.entries[at];reader_remember_jump();
        if(load_chapter(ctx,mark.chapter,mark.byte_off,false)){s_reader_panel=READER_PANEL_NONE;save_progress();}return APP_REDRAW_PAGE;
    }else if(s_reader_panel==READER_PANEL_REFRESH_SETTINGS||s_reader_panel==READER_PANEL_TURN_SETTINGS||s_reader_panel==READER_PANEL_STATS||s_reader_panel==READER_PANEL_STATS_RECENT){
        if(i==0)return reader_show_panel(READER_PANEL_BASIC);
        if(i==1)return reader_show_panel(READER_PANEL_SHORTCUTS);
        if(i==2){reader_input_preferences_t prefs;reader_input_export(&prefs);s_help_hide=prefs.hide_help!=0;return reader_show_panel(READER_PANEL_HELP);}
        if(i==3){static const uint8_t opts[]={0,5,10,15,30};int at=0;while(at<5&&opts[at]!=app_settings_reader_full_pages())++at;app_settings_set_reader_full_pages(opts[(at+1)%5]);s_turns=0;return APP_REDRAW_PAGE;}
        if(i==4){app_settings_set_reader_turn_effect(!app_settings_reader_turn_effect());return APP_REDRAW_PAGE;}
        if(i==5)return apply_reader_option(ctx,2);
    }
    return APP_REDRAW_NONE;
}

// 使用与进度显示相同的源字节比例映射；只在松手时加载章节。/ Invert displayed source-byte progress, loading only on release.
static app_redraw_t reader_progress_jump(app_ctx_t *ctx,int progress){
    uint32_t total=book_total_bytes();size_t count=book_chapter_count();
    if(!total||!count)return APP_REDRAW_NONE;
    size_t before_chapter=s_chapter,before_offset=reader_page_offset(s_page);
    uint32_t target=(uint32_t)((uint64_t)total*(unsigned)progress/100);if(target>=total)target=total-1;
    size_t low=0,high=count;
    while(low+1<high){size_t mid=low+(high-low)/2;if(book_chapter_byte_offset(mid)<=target)low=mid;else high=mid;}
    if(!load_chapter(ctx,low,0,progress==100)){
        copy_text(s_reader_notice,sizeof(s_reader_notice),"跳转失败，请重试");s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;
    }
    if(progress!=100){uint32_t start=book_chapter_byte_offset(low),end=low+1<count?book_chapter_byte_offset(low+1):total;
        size_t offset=end>start?(size_t)((uint64_t)(target-start)*s_text_len/(end-start)):0;
        if(offset>=s_text_len)offset=s_text_len?s_text_len-1:0;
        while(offset&&offset<s_text_len&&((unsigned char)s_text[offset]&0xc0)==0x80)--offset;
        s_page=book_layout_page_for_offset(offset);s_jump_offset=offset;s_jump_page=s_page;
    }
    skip_hidden_image_pages(ctx,progress==100?-1:1);prepare_inline_image();
    s_jump_back_valid=true;s_jump_back_chapter=before_chapter;s_jump_back_offset=before_offset;
    s_reader_panel=READER_PANEL_NONE;invalidate_prep();save_progress();return APP_REDRAW_PAGE;
}
static app_redraw_t reader_progress_preview(app_ctx_t *ctx,int x){
    int next=reader_progress_from_x(x);if(next==s_progress_percent)return APP_REDRAW_NONE;
    s_progress_percent=next;lock_draw();s_area=reader_control_preview(ctx->fb,-1);unlock_draw();s_mode=MODE_GL16;return APP_REDRAW_AREA;
}
static app_redraw_t reader_style_preview(app_ctx_t *ctx,int x){
    if(s_reader_slider_endpoint){EpdRect r=reader_slider_rect(s_reader_slider);if(x<r.x+64||x>=r.x+r.width-64)return APP_REDRAW_NONE;}
    int old[]={s_reader_preview_px,s_reader_preview_margin,s_reader_preview_line,s_reader_preview_para,s_reader_preview_tracking,s_md_draft[0],s_md_draft[1],s_md_draft[2],s_md_draft[3]};
    reader_slider_move(x);
    int now[]={s_reader_preview_px,s_reader_preview_margin,s_reader_preview_line,s_reader_preview_para,s_reader_preview_tracking,s_md_draft[0],s_md_draft[1],s_md_draft[2],s_md_draft[3]};
    if(!memcmp(old,now,sizeof(old)))return APP_REDRAW_NONE;
    lock_draw();s_area=reader_control_preview(ctx->fb,s_reader_slider);unlock_draw();s_mode=MODE_GL16;return APP_REDRAW_AREA;
}
static app_redraw_t reader_toc_preview(app_ctx_t *ctx,int x,bool force){
    int next=book_toc_jump_percent((uint16_t)x);if(!force&&next==s_toc_jump_percent)return APP_REDRAW_NONE;
    s_toc_jump_percent=next;lock_draw();book_toc_render_jump_preview(ctx->fb,book_navigation_count(),next);unlock_draw();s_area=book_toc_jump_preview_area();s_mode=MODE_GL16;return APP_REDRAW_AREA;
}
static app_redraw_t reader_controls_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    if(ctx->touch&&ctx->touch->count>1){s_pressed_control=-1;if(s_progress_drag){s_progress_drag=false;return APP_REDRAW_PAGE;}if(s_reader_slider>=0){s_reader_slider=-1;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
    if(!s_toolbar&&(s_reader_multi.active||(s_reader_multi.blocked&&ctx->touch)))return APP_REDRAW_NONE;
    if(s_toolbar){
        if(s_reader_panel==READER_PANEL_TOOLS&&ev->type==UI_GESTURE_PRESS&&ev->x0>=16&&ev->x0<=668&&(int)ev->y0>=reader_progress_y()-28&&(int)ev->y0<reader_progress_y()+16){
            s_progress_drag=true;s_progress_percent=-1;return reader_progress_preview(ctx,ev->x0);
        }
        if(s_progress_drag){
            if(ev->type==UI_GESTURE_CANCEL){
                // 正常短拖/长按松手在通用识别器中也叫CANCEL；失焦和多点仍取消。/ Normal short/held releases share CANCEL with invalid sequences.
                s_progress_drag=false;
                if(ctx->released&&ctx->touch&&!ctx->consumed)return reader_progress_jump(ctx,reader_progress_from_x(ev->x));
                return APP_REDRAW_PAGE;
            }
            if(ev->type==UI_GESTURE_MOVE)return reader_progress_preview(ctx,ev->x);
            if(ev->type==UI_GESTURE_TAP||ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R||ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){s_progress_drag=false;return reader_progress_jump(ctx,reader_progress_from_x(ev->x));}
            return APP_REDRAW_NONE;
        }
        bool sliders=s_reader_panel==READER_PANEL_FONT_SETTINGS||s_reader_panel==READER_PANEL_LAYOUT_SETTINGS||s_reader_panel==READER_PANEL_MARKDOWN_SIZES;
        if(sliders&&ev->type==UI_GESTURE_PRESS){int slider=reader_slider_at(ev->x0,ev->y0);if(slider>=0){reader_slider_begin(slider,ev->x0);lock_draw();s_area=reader_control_preview(ctx->fb,slider);unlock_draw();s_mode=MODE_GL16;return APP_REDRAW_AREA;}}
        if(s_reader_slider>=0){
            if(ev->type==UI_GESTURE_MOVE)return reader_style_preview(ctx,ev->x);
            if(ev->type==UI_GESTURE_CANCEL){
                if(ctx->released&&ctx->touch&&!ctx->consumed){if(!s_reader_slider_endpoint)reader_slider_move(ev->x);return reader_slider_commit(ctx);}
                s_reader_slider=-1;return APP_REDRAW_PAGE;
            }
            if(ev->type==UI_GESTURE_TAP||ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R||ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){if(!s_reader_slider_endpoint)reader_slider_move(ev->x);return reader_slider_commit(ctx);}
            return APP_REDRAW_NONE;
        }
        if(ev->type==UI_GESTURE_TAP)return reader_panel_action(ctx,ev->x0,ev->y0);
        if(ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){int count=reader_control_count(),next=s_control_page+(ev->type==UI_GESTURE_SWIPE_U?1:-1);if(next>=0&&next*reader_control_rows()<count){s_control_page=next;return APP_REDRAW_PAGE;}}
        return APP_REDRAW_NONE;
    }
    if(ev->type==UI_GESTURE_TAP){
        if(!s_reader_fullscreen&&ui_rect_hit((EpdRect){32,75,64,64},ev->x0,ev->y0))return reader_return(ctx,false);
        if(book_kind()==BOOK_KIND_MARKDOWN&&s_inline_gray&&book_layout_page_image(s_page)==s_inline_index){EpdRect body=body_rect(),image={body.x+(body.width-(int)s_inline_w)/2,body.y+(body.height-(int)s_inline_h)/2,(int)s_inline_w,(int)s_inline_h};if(ui_rect_hit(image,ev->x0,ev->y0)&&prepare_document_zoom(0)){s_document_zoom=true;s_zoom_moved=false;invalidate_prep();return APP_REDRAW_PAGE;}}
        if(!ui_rect_hit(reader_area(),ev->x0,ev->y0))return APP_REDRAW_NONE;
        for(unsigned zone=0;zone<3;++zone)if(ui_rect_hit(reader_tap_rect(reader_area(),reader_input_tap_layout(),zone),ev->x0,ev->y0))
            return zone==1?reader_show_panel(READER_PANEL_TOOLS):turn_page(ctx,zone==0?-1:1);
        return APP_REDRAW_NONE;
    }
    int slot=ev->type==UI_GESTURE_LONG_PRESS?READER_HOLD:ev->type==UI_GESTURE_SWIPE_L?READER_SWIPE_LEFT:ev->type==UI_GESTURE_SWIPE_R?READER_SWIPE_RIGHT:ev->type==UI_GESTURE_SWIPE_U?READER_SWIPE_UP:ev->type==UI_GESTURE_SWIPE_D?READER_SWIPE_DOWN:-1;
    if(slot>=0&&ui_rect_hit(reader_area(),ev->x0,ev->y0))return reader_execute_shortcut(ctx,slot);
    return APP_REDRAW_NONE;
}
