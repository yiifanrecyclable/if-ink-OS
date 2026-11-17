/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// All coordinates are native landscape pixels. Detail hit rectangles are pane-local.
enum { WIDE_FONT_LIST_TOP = 76, WIDE_LAYOUT_MODES_TOP = 24 };

static int wide_detail_rows(void) {
    if (s_reader_panel == READER_PANEL_FONT_PICKER)
        return (WIDE_PAGER_Y - WIDE_FONT_LIST_TOP) / reader_font_row_height();
    return s_reader_panel == READER_PANEL_BOOKMARKS ? 9 : 6;
}
static int wide_detail_pages(void) {
    if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS) return 2;
    int count = reader_control_count();
    return count ? 1 + (count - 1) / wide_detail_rows() : 1;
}
static EpdRect wide_detail_row(int row) {
    int top = 24, step = WIDE_LIST_STEP;
    switch (s_reader_panel) {
    case READER_PANEL_FONT_PICKER: top = WIDE_FONT_LIST_TOP; step = reader_font_row_height(); break;
    case READER_PANEL_SHORTCUTS:
    case READER_PANEL_BINDING: top = 68; step = 88; break;
    case READER_PANEL_LAYOUT_SETTINGS: top = WIDE_LAYOUT_MODES_TOP + READER_LAYOUT_MODES_HEIGHT + 16; step = 96; break;
    case READER_PANEL_RULE_SETTINGS: top = 74; step = 94; break;
    case READER_PANEL_FONT_SETTINGS: top = 155; step = 78; break;
    case READER_PANEL_REFRESH_SETTINGS: step = 96; break;
    default: break;
    }
    return (EpdRect){24,top + row * step,WIDE_PANE_WIDTH-48,step};
}
static EpdRect wide_slider_rect(int slider) {
    if (s_reader_panel == READER_PANEL_FONT_SETTINGS && slider == 0)
        return (EpdRect){24,78,WIDE_PANE_WIDTH-48,58};
    if (s_reader_panel == READER_PANEL_LAYOUT_SETTINGS && s_control_page == 1 && slider >= 1 && slider <= 4)
        return (EpdRect){24,78+(slider-1)*142,WIDE_PANE_WIDTH-48,62};
    return (EpdRect){0};
}
static EpdRect wide_at(EpdRect r,int origin) { r.x += origin; return r; }
static EpdRect wide_layout_modes(void) { return (EpdRect){24,WIDE_LAYOUT_MODES_TOP,WIDE_PANE_WIDTH-48,READER_LAYOUT_MODES_HEIGHT}; }
static EpdRect wide_close(void) { return (EpdRect){16,8,56,52}; }
static EpdRect wide_tap_zone(EpdRect area, int zone) {
    static const int edges[] = {0,30,55,100};
    const int width = area.width / 3;
    const int top = area.height * edges[zone] / 100;
    const int bottom = area.height * edges[zone+1] / 100;
    const int x = s_reader_landscape_reverse ? area.x : area.x + area.width - width;
    return (EpdRect){x,area.y+top,width,bottom-top};
}

static EpdRect wide_tap_rect(int zone) {
    EpdRect area=landscape_reader_area();
    area.height=UI_LOCK_WIDTH-area.y;
    return wide_tap_zone(area,zone);
}
static EpdRect wide_rule_adjust(int direction) {
    return (EpdRect){direction<0?302:475,wide_detail_row(3).y,80,wide_detail_row(3).height};
}
static EpdRect wide_toc_jump_slider(void) { return (EpdRect){28,285,WIDE_PANE_WIDTH-56,64}; }
static void wide_text(uint8_t *fb,int x,int y,int width,int px,const char *value,int align) {
    char line[192];
    copy_text(line,sizeof(line),ui_message(value));
    fit_fixed_text(line,px,width);
    ui_text_fixed_vc(fb,x,y,px,line,align,false);
}
static void wide_label(uint8_t *fb,EpdRect r,const char *name,const char *value) {
    int label_px=reader_px(UI_TEXT_BODY), value_px=reader_px(UI_TEXT_CAPTION);
    int value_width = value[0] ? r.width * 2 / 5 : 0;
    wide_text(fb,r.x+10,r.y+r.height/2,r.width-value_width-24,label_px,name,EPD_DRAW_ALIGN_LEFT);
    if(value_width)wide_text(fb,r.x+r.width-10,r.y+r.height/2,value_width,value_px,value,EPD_DRAW_ALIGN_RIGHT);
    ui_hairline(fb,r.y+r.height-1,r.x,r.width,UI_GRAY_ITEM_LINE);
}
static const char *wide_detail_title(void) {
    switch(s_reader_panel) {
    case READER_PANEL_FONT_PICKER: return UI_MSG("阅读字体");
    case READER_PANEL_RULE_SETTINGS: return UI_MSG("阅读线");
    case READER_PANEL_BASIC: return UI_MSG("基础操作");
    case READER_PANEL_SHORTCUTS: return UI_MSG("快捷手势");
    case READER_PANEL_BINDING: return reader_shortcut_name(s_shortcut_slot);
    default: return NULL;
    }
}
static void wide_pager(uint8_t *fb,int x,int page,int pages,bool jump) {
    int y=(WIDE_PAGER_Y+UI_LOCK_WIDTH)/2;
    ui_hairline(fb,WIDE_PAGER_Y,x+20,WIDE_PANE_WIDTH-40,UI_GRAY_SECTION_LINE);
    int inset=jump?WIDE_PANE_WIDTH*3/10:68;
    if(page>0)ui_draw_icon(fb,x+inset,y,30,UI_ICON_CHEVRON_LEFT,UI_GRAY_BLACK);
    if(page+1<pages)ui_draw_icon(fb,x+WIDE_PANE_WIDTH-inset,y,30,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    if(jump&&page>0)
        reader_control_text(fb,x+WIDE_PANE_WIDTH/10,y,UI_TEXT_CAPTION,"−10",EPD_DRAW_ALIGN_CENTER,false);
    if(jump&&page+1<pages)
        reader_control_text(fb,x+WIDE_PANE_WIDTH*9/10,y,UI_TEXT_CAPTION,"+10",EPD_DRAW_ALIGN_CENTER,false);
    char text[32]; ui_snprintf(text,sizeof(text),"%d / %d",page+1,pages);
    reader_control_text(fb,x+WIDE_PANE_WIDTH/2,y,UI_TEXT_CAPTION,text,EPD_DRAW_ALIGN_CENTER,false);
    if(jump&&pages>1)epd_fill_rect((EpdRect){x+WIDE_PANE_WIDTH/2-24,y+19,48,2},UI_GRAY_ITEM_LINE,fb);
}
static void draw_wide_tools(uint8_t *fb) {
    int x=wide_panel_x(true), width=WIDE_PANE_WIDTH;
    epd_fill_rect((EpdRect){x,0,width,UI_LOCK_WIDTH},UI_GRAY_WHITE,fb);
    int progress=s_progress_drag?s_progress_percent:(int)percent(s_page);
    char values[3][40];
    ui_snprintf(values[0],40,"%d%%",progress);
    ui_snprintf(values[1],40,UI_MSG("%lu 分钟"),(unsigned long)(s_session_read_ms/60000));
    ui_snprintf(values[2],40,UI_MSG("%lu 次"),(unsigned long)s_session_turns);
    const char *stats[]={UI_MSG("阅读进度"),UI_MSG("本次阅读"),UI_MSG("翻页")};
    for(int i=0;i<3;++i){
        int cx=x+(2*i+1)*width/6;
        wide_text(fb,cx,78,width/3-12,reader_px(UI_TEXT_TITLE),values[i],EPD_DRAW_ALIGN_CENTER);
        wide_text(fb,cx,132,width/3-12,reader_px(UI_TEXT_CAPTION),stats[i],EPD_DRAW_ALIGN_CENTER);
    }
    int length=width-72, fill=length*progress/100;
    epd_fill_rect((EpdRect){x+36,WIDE_PROGRESS_Y-3,length,6},0xcc,fb);
    epd_fill_rect((EpdRect){x+36,WIDE_PROGRESS_Y-3,fill,6},0x44,fb);
    epd_fill_circle(x+36+fill,WIDE_PROGRESS_Y,12,0x44,fb);
    char status[96];
    if (s_progress_drag) ui_snprintf(status,sizeof(status),UI_MSG("松手跳转至 %u%%"),(unsigned)progress);
    else ui_snprintf(status,sizeof(status),UI_MSG("本章 %u/%u 页 · %u/%u 节"),(unsigned)s_page+1,(unsigned)book_layout_page_count(),(unsigned)s_chapter+1,(unsigned)book_chapter_count());
    wide_text(fb,x+36,273,s_jump_back_valid?width-250:width-72,reader_px(UI_TEXT_CAPTION),status,EPD_DRAW_ALIGN_LEFT);
    if(s_jump_back_valid)
        wide_text(fb,x+width-36,273,166,reader_px(UI_TEXT_CAPTION),UI_MSG("返回跳转前"),EPD_DRAW_ALIGN_RIGHT);
    bool marked=bookmark_is_current();
    const char *first[]={UI_MSG("退出"),!s_reader_fullscreen?UI_MSG("常规"):s_reader_immersive?UI_MSG("沉浸"):UI_MSG("普通"),marked?UI_MSG("取消"):UI_MSG("添加"),UI_MSG("立刻")};
    const char *second[]={UI_MSG("书籍"),!s_reader_fullscreen?UI_MSG("布局"):UI_MSG("全屏"),UI_MSG("书签"),UI_MSG("刷新")};
    if(ui_i18n_english()){first[2]=marked?"Remove":"Add";second[2]="bookmark";}
    int spacing=reader_px(UI_TEXT_BODY)+10;
    for(int i=0;i<4;++i){
        int cx=x+(2*i+1)*width/8;
        if(i)epd_fill_rect((EpdRect){x+i*width/4,WIDE_ACTIONS_Y+16,1,78},UI_GRAY_ITEM_LINE,fb);
        wide_text(fb,cx,WIDE_ACTIONS_Y+54-spacing/2,width/4-12,reader_px(UI_TEXT_BODY),first[i],EPD_DRAW_ALIGN_CENTER);
        wide_text(fb,cx,WIDE_ACTIONS_Y+54+spacing/2,width/4-12,reader_px(UI_TEXT_BODY),second[i],EPD_DRAW_ALIGN_CENTER);
    }
    const char *tabs[]={UI_MSG("目录"),UI_MSG("书签"),UI_MSG("文字"),UI_MSG("版式"),UI_MSG("更多")};
    const ui_icon_t icons[]={UI_ICON_LIST,UI_ICON_BOOKMARK,UI_ICON_TYPE,UI_ICON_SLIDERS_HORIZONTAL,UI_ICON_SETTINGS};
    int selected=s_view==TOC?0:reader_panel_tab(), px=reader_px(UI_TEXT_BODY);
    if(ui_i18n_english())for(int i=0;i<5;++i){
        int fit=ui_text_fit_px(UI_TEXT_BODY,tabs[i],width/5-10,px);
        if(fit<px)px=fit;
    }
    ui_hairline(fb,WIDE_TABS_Y,x,width,UI_GRAY_SECTION_LINE);
    for(int i=0;i<5;++i){
        int cx=x+(2*i+1)*width/10;
        if(i==selected)epd_fill_rect((EpdRect){cx-22,WIDE_TABS_Y+10,44,5},0x44,fb);
        ui_draw_icon(fb,cx,WIDE_TABS_Y+51,36,icons[i],UI_GRAY_BLACK);
        wide_text(fb,cx,WIDE_TABS_Y+111,width/5-10,px,tabs[i],EPD_DRAW_ALIGN_CENTER);
    }
}
static void wide_slider(uint8_t *fb,int x,int slider,const char *label,const char *value,int index,int count) {
    EpdRect r=wide_at(wide_slider_rect(slider),x);
    reader_control_text(fb,r.x+12,r.y-22,UI_TEXT_BODY,label,EPD_DRAW_ALIGN_LEFT,false);
    draw_pill_slider(fb,r,"","",value,index,count,0,0);
}
static void draw_wide_font_sample(uint8_t *fb) {
    int x=wide_panel_x(false)+36,px=s_reader_slider==0?s_reader_preview_px:s_px;
    char text[160];
    size_t offset=reader_page_offset(s_page);
    copy_text(text,sizeof(text),s_text&&offset<s_text_len?s_text+offset:"阅读 Aa 0123");
    for(char *p=text;*p;++p)if(*p=='\n'||*p=='\r'||*p=='\t')*p=' ';
    int above=0, below=0;
    fit_fixed_text(text,px,WIDE_PANE_WIDTH-72);
    ttf_measure_line_px(px,text,&above,&below);
    ttf_draw_text_px(fb,x,370+(above-below)/2,px,text,EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
    ttf_measure_line_px(px,"Aa 0123",&above,&below);
    ttf_draw_text_px(fb,x,508+(above-below)/2,px,"Aa 0123",EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
}
static void draw_wide_toc(app_ctx_t *ctx,uint8_t *fb,int x) {
    size_t count=book_navigation_count(),current=current_toc_position();
    if(s_toc_jump_open){
        reader_control_text(fb,x+WIDE_PANE_WIDTH/2,140,UI_TEXT_TITLE,UI_MSG("跳到目录位置"),EPD_DRAW_ALIGN_CENTER,false);
        char value[40];ui_snprintf(value,sizeof(value),"%d%%",s_toc_jump_percent);
        draw_pill_slider(fb,wide_at(wide_toc_jump_slider(),x),"","",value,s_toc_jump_percent,101,0,0);
        reader_control_text(fb,x+WIDE_PANE_WIDTH/4,525,UI_TEXT_BODY,UI_MSG("取消"),EPD_DRAW_ALIGN_CENTER,false);
        reader_control_text(fb,x+WIDE_PANE_WIDTH*3/4,525,UI_TEXT_BODY,UI_MSG("跳转"),EPD_DRAW_ALIGN_CENTER,false);
        return;
    }
    if(!count || s_message[0])wide_text(fb,x+WIDE_PANE_WIDTH/2,310,WIDE_PANE_WIDTH-48,reader_px(UI_TEXT_BODY),
                         s_message[0]?s_message:UI_MSG("这本书没有可用目录"),EPD_DRAW_ALIGN_CENTER);
    for(int row=0;!s_message[0]&&row<WIDE_TOC_ROWS;++row){
        size_t at=(size_t)ctx->leaf*WIDE_TOC_ROWS+row;
        if(at>=count)break;
        EpdRect r={x+24,24+row*WIDE_LIST_STEP,WIDE_PANE_WIDTH-48,WIDE_LIST_STEP};
        if(at==current)epd_fill_rect(r,0xee,fb);
        char number[16],title[160];
        ui_snprintf(number,sizeof(number),"%02u",(unsigned)at+1);
        if(book_navigation_title(at,title,sizeof(title))!=ESP_OK||!title[0])
            ui_snprintf(title,sizeof(title),UI_MSG("第 %u 节"),(unsigned)at+1);
        for(char *p=title;*p;++p)if((unsigned char)*p<32)*p=' ';
        reader_control_text(fb,r.x+14,r.y+r.height/2,UI_TEXT_CAPTION,number,EPD_DRAW_ALIGN_LEFT,false);
        int reserve=at==current?98:0;
        wide_text(fb,r.x+98,r.y+r.height/2,r.width-110-reserve,reader_px(UI_TEXT_BODY),title,EPD_DRAW_ALIGN_LEFT);
        if(reserve)reader_control_text(fb,r.x+r.width-10,r.y+r.height/2,UI_TEXT_CAPTION,UI_MSG("当前"),EPD_DRAW_ALIGN_RIGHT,false);
        ui_hairline(fb,r.y+r.height-1,r.x,r.width,UI_GRAY_ITEM_LINE);
    }
    wide_pager(fb,x,ctx->leaf,leaves(),true);
}
static void draw_wide_basic(uint8_t *fb,int x) {
    const char *labels[]={UI_MSG("上一页"),UI_MSG("打开面板"),UI_MSG("下一页")};
    EpdRect screen={x+62,160,478,269};
    epd_draw_rect(screen,0x44,fb);
    int key_x=s_reader_landscape_reverse?screen.x-20:screen.x+screen.width+19;
    for(int zone=0;zone<3;++zone){
        EpdRect r=wide_tap_zone(screen,zone);
        epd_draw_rect(r,0x88,fb);
        int px=ui_text_fit_px(UI_TEXT_CAPTION,labels[zone],r.width-10,reader_px(UI_TEXT_CAPTION));
        wide_text(fb,r.x+r.width/2,r.y+r.height/2,r.width-10,px,labels[zone],EPD_DRAW_ALIGN_CENTER);
        int key_y=screen.y+screen.height*(zone+2)/6;
        epd_draw_circle(key_x,key_y,zone==1?7:5,0x44,fb);
    }
}
static EpdRect wide_font_name_rect(int row,bool selected) {
    EpdRect r=wide_at(wide_detail_row(row),wide_panel_x(false));
    return (EpdRect){r.x+12,r.y,r.width-24-(selected?100:0),r.height-1};
}
static void draw_wide_font_names(uint8_t *fb) {
    s_reader_names_shown=0;
    s_reader_names_settled=false;
    int rows=wide_detail_rows(),first=s_control_page*rows;
    int count=ttf_font_count(),origin=wide_panel_x(false);
    if(!count)reader_control_text(fb,origin+WIDE_PANE_WIDTH/2,320,UI_TEXT_BODY,UI_MSG("TF 卡没有可选字体"),EPD_DRAW_ALIGN_CENTER,false);
    for(int row=0;row<rows&&first+row<count;++row){
        const ttf_font_item_t *item=ttf_font_item(first+row);
        bool selected=!strcmp(s_font_path,item->path);
        EpdRect r=wide_at(wide_detail_row(row),origin), area=wide_font_name_rect(row,selected);
        const char *name=ttf_font_localized_name(item->name);
        if(ttf_name_preview_draw(fb,item,name,reader_font_name_px(),area,UI_GRAY_WHITE,false))s_reader_names_shown|=1u<<row;
        else wide_text(fb,area.x,area.y+area.height/2,area.width,reader_font_name_px(),name,EPD_DRAW_ALIGN_LEFT);
        if(selected)wide_text(fb,r.x+r.width-10,r.y+r.height/2,90,reader_px(UI_TEXT_CAPTION),UI_MSG("已选择"),EPD_DRAW_ALIGN_RIGHT);
        ui_hairline(fb,r.y+r.height-1,r.x,r.width,UI_GRAY_ITEM_LINE);
    }
}
static app_redraw_t wide_font_preview_tick(app_ctx_t *ctx) {
    if(s_reader_names_settled || !ttf_name_preview_idle(ctx->now_ms,ctx->consumed||ctx->pressed||ctx->released||
                              (ctx->touch&&ctx->touch->touched))) return APP_REDRAW_NONE;
    int rows=wide_detail_rows(),first=s_control_page*rows;
    for(int row=0;row<rows;++row){
        const ttf_font_item_t *item=ttf_font_item(first+row);
        if(!item)continue;
        EpdRect r=wide_font_name_rect(row,!strcmp(s_font_path,item->path));
        if(!ttf_name_preview_request(item,ttf_font_localized_name(item->name),reader_font_name_px(),r.width))return APP_REDRAW_NONE;
    }
    s_reader_names_settled=true;
    enum EpdRotation previous=epd_get_rotation();
    reader_set_landscape_rotation();
    EpdRect dirty={0};
    for(int row=0;row<rows;++row){
        const ttf_font_item_t *item=ttf_font_item(first+row);
        if(!item || (s_reader_names_shown&(1u<<row)))continue;
        EpdRect r=wide_font_name_rect(row,!strcmp(s_font_path,item->path));
        if(ttf_name_preview_draw(ctx->fb,item,ttf_font_localized_name(item->name),reader_font_name_px(),r,UI_GRAY_WHITE,true)){
            dirty=ui_rect_union(dirty,r);
            s_reader_names_shown|=1u<<row;
        }
    }
    EpdRect physical=dirty.width?ui_rotate_rect_to_fb(dirty):(EpdRect){0};
    epd_set_rotation(previous);
    if(!dirty.width)return APP_REDRAW_NONE;
#ifdef PICO_HOST_SIMULATOR
    s_area=physical;
#else
    s_area=(EpdRect){epd_height()-physical.y-physical.height,physical.x,physical.height,physical.width};
#endif
    return APP_REDRAW_AREA;
}
static void draw_wide_detail(app_ctx_t *ctx,uint8_t *fb,const char *face) {
    int x=wide_panel_x(false);
    epd_fill_rect((EpdRect){x,0,WIDE_PANE_WIDTH,UI_LOCK_WIDTH},UI_GRAY_WHITE,fb);
    if(s_view==TOC){draw_wide_toc(ctx,fb,x);return;}
    const char *title=wide_detail_title();
    if(title){
        ui_nav_close_in(fb,wide_at(wide_close(),x));
        wide_text(fb,x+WIDE_PANE_WIDTH/2,34,WIDE_PANE_WIDTH-160,reader_px(UI_TEXT_TITLE),title,EPD_DRAW_ALIGN_CENTER);
        ui_hairline(fb,61,x,WIDE_PANE_WIDTH,UI_GRAY_SECTION_LINE);
    }
    char value[48];
    int pages=wide_detail_pages();
    if(s_reader_panel==READER_PANEL_FONT_SETTINGS){
        int px=s_reader_slider==0?s_reader_preview_px:s_px;
        ui_snprintf(value,sizeof(value),"%d",px);
        wide_slider(fb,x,0,UI_MSG("正文字号"),value,px-BOOK_PX_MIN,BOOK_PX_MAX-BOOK_PX_MIN+1);
        wide_label(fb,wide_at(wide_detail_row(0),x),UI_MSG("阅读字体"),face);
        reader_control_text(fb,x+36,286,UI_TEXT_CAPTION,UI_MSG("正文效果"),EPD_DRAW_ALIGN_LEFT,false);
    }else if(s_reader_panel==READER_PANEL_FONT_PICKER)draw_wide_font_names(fb);
    else if(s_reader_panel==READER_PANEL_BASIC)draw_wide_basic(fb,x);
    else if(s_reader_panel==READER_PANEL_LAYOUT_SETTINGS){
        if(!s_control_page){
            draw_reader_layout_modes(fb,wide_at(wide_layout_modes(),x));
            ui_snprintf(value,sizeof(value),UI_MSG("%u 字 · 点按切换"),app_settings_book_indent());
            wide_label(fb,wide_at(wide_detail_row(0),x),UI_MSG("首行缩进"),value);
            wide_label(fb,wide_at(wide_detail_row(1),x),UI_MSG("阅读线"),"›");
        }else{
            const int values[]={s_reader_slider>=0?s_reader_preview_margin:s_margin,
                s_reader_slider>=0?s_reader_preview_line:app_settings_book_line_spacing(),
                s_reader_slider>=0?s_reader_preview_para:app_settings_book_paragraph_spacing(),
                s_reader_slider>=0?s_reader_preview_tracking:app_settings_book_tracking()};
            const char *names[]={UI_MSG("页边距"),UI_MSG("行距"),UI_MSG("段距"),UI_MSG("字间距")};
            const int min[]={BOOK_MARGIN_MIN,110,0,0},count[]={BOOK_MARGIN_CHOICES,41,76,5};
            for(int i=0;i<4;++i){
                ui_snprintf(value,sizeof(value),i==0?"%d px":i==3?"%+d px":"%d%%",i==3?(values[i]-2)*2:values[i]);
                wide_slider(fb,x,i+1,names[i],value,values[i]-min[i],count[i]);
            }
        }
    }else if(s_reader_panel==READER_PANEL_RULE_SETTINGS){
        const char *names[]={UI_MSG("无"),UI_MSG("虚线"),UI_MSG("点线"),UI_MSG("上下位置"),UI_MSG("恢复居中")};
        for(int i=0;i<5;++i){
            EpdRect r=wide_at(wide_detail_row(i),x);
            wide_label(fb,r,names[i],i<3&&(int)app_settings_book_reading_line()==i?UI_MSG("已选择"):"");
            if(i==1||i==2)for(int dx=0;dx<130;dx+=i==1?24:10)
                epd_fill_rect((EpdRect){r.x+190+dx,r.y+r.height/2,i==1?12:2,2},0x50,fb);
            if(i==3){
                ui_snprintf(value,sizeof(value),"%+d px",app_settings_book_reading_line_offset());
                EpdRect minus=wide_at(wide_rule_adjust(-1),x),plus=wide_at(wide_rule_adjust(1),x);
                reader_control_text(fb,minus.x+minus.width/2,r.y+r.height/2,UI_TEXT_TITLE,"−",EPD_DRAW_ALIGN_CENTER,false);
                reader_control_text(fb,x+425,r.y+r.height/2,UI_TEXT_CAPTION,value,EPD_DRAW_ALIGN_CENTER,false);
                reader_control_text(fb,plus.x+plus.width/2,r.y+r.height/2,UI_TEXT_TITLE,"+",EPD_DRAW_ALIGN_CENTER,false);
            }
        }
    }else if(s_reader_panel==READER_PANEL_BOOKMARKS){
        reader_bookmarks_t marks={0};
        int count=bookmark_load(&marks)?marks.count:0, rows=wide_detail_rows(), first=s_control_page*rows;
        for(int row=0;row<rows&&first+row<count+2;++row){
            int i=first+row;
            EpdRect r=wide_at(wide_detail_row(row),x);
            if(i==0)wide_label(fb,r,bookmark_is_current()?UI_MSG("移除当前位置书签"):UI_MSG("添加当前位置书签"),"");
            else if(i==1)wide_label(fb,r,s_bookmark_edit?UI_MSG("结束删除书签"):UI_MSG("删除书签模式"),s_bookmark_edit?UI_MSG("开启"):UI_MSG("关闭"));
            else{
                reader_bookmark_entry_t *mark=&marks.entries[count-(i-2)-1];
                char label[160];
                if(book_chapter_title(mark->chapter,label,sizeof(label))!=ESP_OK)ui_snprintf(label,sizeof(label),UI_MSG("章节 %u"),mark->chapter+1);
                ui_snprintf(value,sizeof(value),s_bookmark_edit?UI_MSG("点按删除"):UI_MSG("%u 节 ›"),mark->chapter+1);
                wide_label(fb,r,label,value);
            }
        }
        if(s_bookmark_delete_confirm){
            EpdRect dialog={x+24,215,WIDE_PANE_WIDTH-48,245};
            epd_fill_rect(dialog,UI_GRAY_WHITE,fb);epd_draw_rect(dialog,0x88,fb);
            wide_text(fb,x+WIDE_PANE_WIDTH/2,279,dialog.width-20,reader_px(UI_TEXT_BODY),s_bookmark_delete_error?UI_MSG("删除失败，请重试"):UI_MSG("删除这条书签？"),EPD_DRAW_ALIGN_CENTER);
            reader_control_text(fb,x+WIDE_PANE_WIDTH/4,393,UI_TEXT_BODY,UI_MSG("取消"),EPD_DRAW_ALIGN_CENTER,false);
            reader_control_text(fb,x+WIDE_PANE_WIDTH*3/4,393,UI_TEXT_BODY,UI_MSG("删除"),EPD_DRAW_ALIGN_CENTER,false);
        }
    }else if(s_reader_panel==READER_PANEL_SHORTCUTS||s_reader_panel==READER_PANEL_BINDING){
        int rows=wide_detail_rows(),first=s_control_page*rows,count=reader_control_count();
        for(int i=first;i<first+rows&&i<count;++i){
            bool binding=s_reader_panel==READER_PANEL_BINDING;
            int owner=binding?reader_binding_owner(i):-1;
            const char *label=binding?reader_action_name(i):reader_shortcut_name(i);
            const char *status=!binding?reader_action_name(reader_input_action(i)):
                owner>=0?reader_shortcut_name(owner):reader_input_action(s_shortcut_slot)==i?UI_MSG("已选择"):"";
            wide_label(fb,wide_at(wide_detail_row(i-first),x),label,status);
        }
    }else{
        const char *names[]={UI_MSG("基础操作"),UI_MSG("快捷手势"),UI_MSG("操作示例"),UI_MSG("定期全刷"),UI_MSG("翻页效果"),UI_MSG("显示插图")};
        for(int i=0;i<6;++i){
            const char *status="›";
            if(i==3){
                unsigned p=app_settings_reader_full_pages();
                if(p){ui_snprintf(value,sizeof(value),UI_MSG("每 %u 页"),p);status=value;}else status=UI_MSG("关闭");
            }else if(i==4)status=app_settings_reader_turn_effect()?UI_MSG("水波纹"):UI_MSG("默认");
            else if(i==5)status=app_settings_reader_hide_images()?UI_MSG("关闭"):UI_MSG("开启");
            wide_label(fb,wide_at(wide_detail_row(i),x),names[i],status);
        }
    }
    if(pages>1||s_reader_panel==READER_PANEL_BOOKMARKS)wide_pager(fb,x,s_control_page,pages,false);
    if(s_reader_notice[0])
        wide_text(fb,x+WIDE_PANE_WIDTH/2,603,WIDE_PANE_WIDTH-48,reader_px(UI_TEXT_CAPTION),s_reader_notice,EPD_DRAW_ALIGN_CENTER);
}
// Lay out the example independently of the open book's pagination and progress.
static const char *wide_help_text(uint8_t *fb,EpdRect box,const char *at,int px,bool vertical) {
    int spacing=px*app_settings_book_line_spacing()/100;
    int tracking=((int)app_settings_book_tracking()-2)*2;
    int rows=(vertical?box.width:box.height)/spacing;
    for(int row=0;row<rows&&*at;++row){
        if(vertical){
            char line[256];
            int length=0,used=0;
            while(at[length]){
                int next=length+1;
                while(((unsigned char)at[next]&0xc0)==0x80)++next;
                // All non-ASCII sample glyphs use the full em in vertical text.
                int advance=next-length==1?ttf_vertical_advance_px(px,(unsigned char)at[length]):px;
                if(next>=(int)sizeof(line)||used+advance>box.height)break;
                used+=advance+tracking;
                length=next;
            }
            if(!length)break;
            memcpy(line,at,length);
            line[length]=0;
            ttf_draw_vertical_px(fb,box.x+box.width-spacing/2-row*spacing,box.y,box.height,px,line,tracking);
            at+=length;
        }else{
            char line[256];
            copy_text(line,sizeof(line),at);
            fit_ttf_text(line,px,box.width-16);
            if(!line[0])break;
            int above=0,below=0;
            ttf_measure_line_px(px,line,&above,&below);
            int baseline=box.y+spacing/2+row*spacing+(above-below)/2;
            ttf_draw_text_px(fb,box.x+8,baseline,px,line,EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
            at+=strlen(line);
        }
    }
    return at;
}
static void draw_wide_help(uint8_t *fb,size_t page) {
    draw_landscape_body(fb,page,true);
    enum EpdRotation previous=epd_get_rotation();
    reader_set_landscape_rotation();
    EpdRect body=body_rect();
    const int footer_center=LANDSCAPE_REGULAR_PROGRESS_Y+6;
    const int footer_top=reader_help_exit_rect(footer_center).y;
    if(body.y+body.height>footer_top)body.height=footer_top-body.y;
    char sample[1536];
    ui_snprintf(sample,sizeof(sample),"%s　　%s　　%s",reader_help_paragraphs[0],reader_help_paragraphs[1],reader_help_paragraphs[2]);
    bool vertical=reader_book_vertical();
    const char *rest=wide_help_text(fb,book_layout_column_rect(body,0),sample,s_px,vertical);
    if(s_reader_screen==APP_BOOK_LANDSCAPE_TWO_COLUMNS)
        wide_help_text(fb,book_layout_column_rect(body,1),rest,s_px,vertical);
    ui_fade_region(fb,body,0xcc);

    const char *labels[]={UI_MSG("上一页"),UI_MSG("打开面板"),UI_MSG("下一页")};
    EpdRect strip=wide_tap_rect(0);
    const int divider=s_reader_landscape_reverse?strip.x+strip.width:strip.x;
    epd_fill_rect((EpdRect){divider,strip.y,2,footer_top-strip.y},UI_GRAY_SECTION_LINE,fb);
    for(int zone=0;zone<3;++zone){
        EpdRect r=wide_tap_rect(zone);
        if(zone)ui_hairline(fb,r.y,r.x,r.width,UI_GRAY_SECTION_LINE);
        int bottom=r.y+r.height<footer_top?r.y+r.height:footer_top;
        reader_help_label(fb,r.x+r.width/2,(r.y+bottom)/2,36,labels[zone]);
    }
    int idle_x=s_reader_landscape_reverse?divider:0;
    int idle_width=UI_LOCK_HEIGHT-strip.width;
    reader_help_label(fb,idle_x+idle_width/2,(strip.y+footer_top)/2,36,UI_MSG("无点击操作"));
    reader_help_controls(fb,footer_center);
    if(s_reader_notice[0]){
        epd_fill_rect((EpdRect){338,UI_STATUS_HEIGHT+8,540,40},UI_GRAY_WHITE,fb);
        wide_text(fb,608,UI_STATUS_HEIGHT+28,508,reader_px(UI_TEXT_CAPTION),s_reader_notice,EPD_DRAW_ALIGN_CENTER);
    }
    epd_set_rotation(previous);
}
