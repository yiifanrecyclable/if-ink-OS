/* SPDX-License-Identifier: Apache-2.0
 * 中文：Banner草稿控件与TF图片浏览器；只有保存写入配置。
 * English: Draft banner controls and TF image browsing; only Save persists configuration.
 */
#ifndef PICO_HOST_SIMULATOR
// TF卡FatFS不支持符号链接；宿主检查仍拒绝符号链接。/ FatFS has no symlinks; host checks still reject links.
#define banner_image_stat stat
#else
#define banner_image_stat lstat
#endif
#define BANNER_PICK_MAX_ROWS 10
typedef struct {char path[CONTENT_BANNER_IMAGE_BYTES],name[128];bool folder;} banner_pick_item_t;
static banner_pick_item_t s_banner_items[BANNER_PICK_MAX_ROWS];
static char s_banner_directory[CONTENT_BANNER_IMAGE_BYTES];
static int s_banner_image_page,s_banner_image_total,s_banner_image_count;
static int banner_image_note_top(void){return UI_NAV_PAGER_TOP-3*ui_text_line_height(UI_TEXT_CAPTION)-10;}
static int banner_image_rows(void){int rows=ui_list_visible_rows(366,banner_image_note_top()-40,font_list_step());return rows>BANNER_PICK_MAX_ROWS?BANNER_PICK_MAX_ROWS:rows;}
static void banner_image_scan(void){
    s_banner_image_count=s_banner_image_total=0;int rows=banner_image_rows();
    // 文件夹优先，逐页枚举；不把整张卡的目录存入内存。/ Folders first; enumerate one page without retaining the whole card listing.
    for(int pass=0;pass<2;++pass){
        DIR *dir=opendir(s_banner_directory);if(!dir)break;struct dirent *entry;
        while((entry=readdir(dir))){
            if(entry->d_name[0]=='.')continue;
            char path[CONTENT_BANNER_IMAGE_BYTES];int len=ui_snprintf(path,sizeof(path),"%s/%s",s_banner_directory,entry->d_name);
            if(len<0||len>=(int)sizeof(path))continue;
            struct stat st;if(banner_image_stat(path,&st))continue;bool folder=S_ISDIR(st.st_mode);
            if(folder!=(pass==0))continue;
            if(!folder&&(!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>2*1024*1024||!content_banner_image_valid(path)))continue;
            int index=s_banner_image_total++;
            if(index<s_banner_image_page*rows||s_banner_image_count>=rows)continue;
            banner_pick_item_t *item=&s_banner_items[s_banner_image_count++];
            ui_snprintf(item->path,sizeof(item->path),"%s",path);ui_snprintf(item->name,sizeof(item->name),"%.*s",(int)sizeof(item->name)-1,entry->d_name);item->folder=folder;
        }
        closedir(dir);
    }
}
static void banner_picker_open(void){
    ui_snprintf(s_banner_directory,sizeof(s_banner_directory),"/sdcard/banner pic");
    struct stat st;if(stat(s_banner_directory,&st)||!S_ISDIR(st.st_mode))ui_snprintf(s_banner_directory,sizeof(s_banner_directory),"/sdcard");
    s_banner_image_page=0;banner_image_scan();s_notice[0]=0;s_page=SETTINGS_BANNER_IMAGE;
}
static void banner_heading(uint8_t *fb,int y,const char *text){ui_text_vc(fb,36,y,UI_TEXT_TITLE,text,EPD_DRAW_ALIGN_LEFT,false);}
static void banner_slider_draw(uint8_t *fb,int y,int value,int minimum,int maximum,bool enabled){
    int knob=72+(value-minimum)*540/(maximum-minimum);uint8_t color=enabled?0x44:0xbb;
    epd_fill_rect((EpdRect){72,y-2,540,4},0xcc,fb);epd_fill_rect((EpdRect){72,y-2,knob-72,4},color,fb);epd_fill_circle(knob,y,12,color,fb);
}
static void banner_toggle_draw(uint8_t *fb,int y,bool on){
    ui_text_vc(fb,648,y,UI_TEXT_BODY,on?UI_MSG("开启"):UI_MSG("关闭"),EPD_DRAW_ALIGN_RIGHT,false);
}
static EpdRect banner_arrow_rect(int down,int y){return (EpdRect){480+88*down,y-28,72,56};}
// 沿线段盖圆点，保证竖直及斜向笔画具有相同粗度和圆头。/ Stamp circles along segments for uniform round-capped vertical and diagonal strokes.
static void banner_round_stroke(uint8_t *fb,int x,int y,int end_x,int end_y){
    int dx=abs(end_x-x),sx=x<end_x?1:-1,dy=-abs(end_y-y),sy=y<end_y?1:-1,error=dx+dy;
    for(;;){
        epd_fill_circle(x,y,3,UI_GRAY_BLACK,fb);if(x==end_x&&y==end_y)break;
        int twice=2*error;if(twice>=dy){error+=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}
    }
}
static void banner_arrow_draw(uint8_t *fb,int down,int y){
    EpdRect box=banner_arrow_rect(down,y);int cx=box.x+36,dir=down?1:-1;
    epd_draw_circle(cx,y,26,UI_GRAY_BLACK,fb);epd_draw_circle(cx,y,25,UI_GRAY_BLACK,fb);
    banner_round_stroke(fb,cx,y-14,cx,y+14);
    banner_round_stroke(fb,cx,y+dir*14,cx-11,y+dir*3);
    banner_round_stroke(fb,cx,y+dir*14,cx+11,y+dir*3);
}
static void banner_caption(uint8_t *fb,int y,const char *value){
    char shown[CONTENT_HOME_QUOTE_BYTES];strlcpy(shown,value,sizeof(shown));int px=ui_text_effective_px(UI_TEXT_CAPTION);
    for(char *c=shown;*c;++c)if(*c=='\n')*c=' ';
    while(shown[0]&&ui_text_fixed_width_px(px,shown)>340){
        size_t n=strlen(shown)-1;while(n&&((unsigned char)shown[n]&0xc0)==0x80)--n;shown[n]=0;
    }
    ui_text_vc(fb,648,y,UI_TEXT_CAPTION,shown,EPD_DRAW_ALIGN_RIGHT,false);
}
static void banner_footer_draw(uint8_t *fb,int i){
    EpdRect box=banner_footer(i);ui_fill_round_rect(fb,box,28,UI_GRAY_WHITE);ui_draw_round_rect(fb,box,28,UI_GRAY_BLACK);
    if(app_settings_language()==APP_LANGUAGE_EN){ui_text_fixed_vc(fb,box.x+box.width/2,box.y+box.height/2,ui_text_effective_px(UI_TEXT_BODY),i?UI_MSG("保存"):UI_MSG("取消"),EPD_DRAW_ALIGN_CENTER,false);return;}
    const char *first=i?UI_MSG("保"):UI_MSG("取"),*last=i?UI_MSG("存"):UI_MSG("消");int px=ui_text_effective_px(UI_TEXT_BODY);
    int advance=ui_text_fixed_width_px(px,first),width=advance+BANNER_FOOTER_TRACKING+ui_text_fixed_width_px(px,last);
    int x=box.x+(box.width-width)/2,y=box.y+box.height/2;
    ui_text_fixed_vc(fb,x,y,px,first,EPD_DRAW_ALIGN_LEFT,false);
    ui_text_fixed_vc(fb,x+advance+BANNER_FOOTER_TRACKING,y,px,last,EPD_DRAW_ALIGN_LEFT,false);
}
static void banner_editor_draw(uint8_t *fb){
    if(s_quote_preview)ui_bitmap_draw(fb,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300},s_quote_preview);
    else ui_text_vc(fb,342,206,UI_TEXT_BODY,UI_MSG("预览暂不可用"),EPD_DRAW_ALIGN_CENTER,false);
    epd_fill_rect((EpdRect){s_banner_draft.mode?0:36,356,s_banner_draft.mode?UI_LOCK_WIDTH:612,2},UI_GRAY_SECTION_LINE,fb);
    banner_heading(fb,404,UI_MSG("Banner区域设置"));
    banner_heading(fb,476,UI_MSG("背景"));
    const char *name=strrchr(s_banner_draft.image,'/');name=name?name+1:s_banner_draft.image;
    char shown[sizeof(s_banner_draft.image)+8];ui_snprintf(shown,sizeof(shown),"%s  ›",s_banner_draft.mode?name:UI_MSG("无背景图"));fit_value(shown,360);
    ui_text_vc(fb,648,476,UI_TEXT_BODY,shown,EPD_DRAW_ALIGN_RIGHT,false);
    epd_fill_rect((EpdRect){36,520,612,1},UI_GRAY_ITEM_LINE,fb);
    banner_heading(fb,568,UI_MSG("日期"));banner_toggle_draw(fb,568,s_banner_draft.show_date);
    if(s_banner_draft.show_date){
        ui_text_vc(fb,36,640,UI_TEXT_BODY,UI_MSG("高度调节"),EPD_DRAW_ALIGN_LEFT,false);
        banner_arrow_draw(fb,0,640);banner_arrow_draw(fb,1,640);
    }
    epd_fill_rect((EpdRect){36,688,612,1},UI_GRAY_ITEM_LINE,fb);
    banner_heading(fb,736,UI_MSG("文字"));banner_toggle_draw(fb,736,s_banner_draft.show_text);
    if(s_banner_draft.show_text){
        static const char *const labels[]={UI_MSG("编辑内容"),UI_MSG("选择字体"),UI_MSG("对齐方式"),UI_MSG("高度调节")};
        for(int i=0;i<4;++i){
            ui_text_vc(fb,36,banner_text_row_center(i),UI_TEXT_BODY,labels[i],EPD_DRAW_ALIGN_LEFT,false);
            if(i<3)epd_fill_rect((EpdRect){36,BANNER_TEXT_ROW_TOP+(i+1)*BANNER_TEXT_ROW_H,612,1},UI_GRAY_ITEM_LINE,fb);
        }
        banner_caption(fb,banner_text_row_center(0),s_banner_draft.text);
        banner_caption(fb,banner_text_row_center(1),s_banner_draft.font[0]?system_font_label(s_banner_draft.font):UI_MSG("跟随系统字体"));
        for(int i=0;i<4;++i)quote_alignment_draw(fb,i,s_banner_draft.align==i);
        banner_arrow_draw(fb,0,banner_text_row_center(3));banner_arrow_draw(fb,1,banner_text_row_center(3));
    }
    if(s_notice[0])ui_text_fixed_vc(fb,36,1060,16,s_notice,EPD_DRAW_ALIGN_LEFT,false);
    for(int i=0;i<2;++i)banner_footer_draw(fb,i);
    ui_nav_draw(fb,0);
}
static void banner_picker_draw(uint8_t *fb){
    back_header(fb,UI_MSG("选择背景图片"));
    EpdRect none={36,200,612,80};settings_option(fb,none,!s_banner_draft.mode);
    ui_text_vc(fb,48,240,UI_TEXT_BODY,UI_MSG("无背景图"),EPD_DRAW_ALIGN_LEFT,false);
    if(!s_banner_draft.mode)epd_fill_circle(609,240,6,UI_GRAY_BLACK,fb);
    char name[sizeof(s_banner_directory)];ui_snprintf(name,sizeof(name),"%s",s_banner_directory+7);if(!name[0])ui_snprintf(name,sizeof(name),"/");fit_value(name,410);
    ui_text_vc(fb,36,316,UI_TEXT_BODY,name,EPD_DRAW_ALIGN_LEFT,false);
    ui_text_vc(fb,648,316,UI_TEXT_BODY,UI_MSG("上一级"),EPD_DRAW_ALIGN_RIGHT,false);
    for(int i=0;i<s_banner_image_count;++i){
        EpdRect r={36,366+i*font_list_step(),612,font_list_step()-12};
        char shown[160];ui_snprintf(shown,sizeof(shown),"%s%s",s_banner_items[i].folder?"▸ ":"",s_banner_items[i].name);fit_value(shown,580);
        ui_text_vc(fb,48,r.y+r.height/2,UI_TEXT_BODY,shown,EPD_DRAW_ALIGN_LEFT,false);epd_fill_rect((EpdRect){36,r.y+r.height,612,1},UI_GRAY_ITEM_LINE,fb);
    }
    if(!s_banner_image_total)ui_text_vc(fb,342,500,UI_TEXT_BODY,UI_MSG("此文件夹内没有可用图片"),EPD_DRAW_ALIGN_CENTER,false);
    if(s_notice[0])ui_text_fixed_vc(fb,36,banner_image_note_top()-20,20,s_notice,EPD_DRAW_ALIGN_LEFT,false);
    static const char *const notes[]={UI_MSG("支持 PNG、JPG/JPEG 格式。"),UI_MSG("推荐尺寸：684×300 像素。"),UI_MSG("其他尺寸将等比例缩放、居中裁切。")};
    int step=ui_text_line_height(UI_TEXT_CAPTION),px=ui_text_effective_px(UI_TEXT_CAPTION);
    for(int i=0;i<3;++i)ui_text_fixed_vc_gray(fb,36,banner_image_note_top()+i*step+step/2,px,notes[i],EPD_DRAW_ALIGN_LEFT,8,UI_INK_WHITE);
    int rows=banner_image_rows(),pages=s_banner_image_total?(s_banner_image_total+rows-1)/rows:1;
    ui_nav_draw_pager(fb,s_banner_image_total,UI_MSG("项"),s_banner_image_page,pages);ui_nav_draw(fb,0);
}
static app_redraw_t banner_picker_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int nav=ui_nav_hit(ev->x0,ev->y0);if(nav>=0){ui_nav_request(ctx,nav);return APP_REDRAW_NONE;}
    if(ev->y0<180){s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;}
    if(ev->y0>=200&&ev->y0<280){content_banner_config_t next=s_banner_draft;next.mode=0;if(banner_preview_apply(&next,false))s_page=SETTINGS_HOME_QUOTE;return APP_REDRAW_PAGE;}
    if(ev->y0>=286&&ev->y0<348&&ev->x0>=470){
        char *slash=strrchr(s_banner_directory,'/');if(slash&&slash>s_banner_directory+7)*slash=0;else ui_snprintf(s_banner_directory,sizeof(s_banner_directory),"/sdcard");
        s_banner_image_page=0;banner_image_scan();return APP_REDRAW_PAGE;
    }
    for(int direction=-1;direction<=1;direction+=2)if(ui_rect_hit(ui_nav_pager_rect(direction),ev->x0,ev->y0)){
        int pages=(s_banner_image_total+banner_image_rows()-1)/banner_image_rows();
        if(s_banner_image_page+direction<0||s_banner_image_page+direction>=pages)return APP_REDRAW_NONE;
        s_banner_image_page+=direction;banner_image_scan();return APP_REDRAW_PAGE;
    }
    for(int i=0;i<s_banner_image_count;++i)if(ui_rect_hit((EpdRect){36,366+i*font_list_step(),612,font_list_step()-12},ev->x0,ev->y0)){
        if(s_banner_items[i].folder){ui_snprintf(s_banner_directory,sizeof(s_banner_directory),"%s",s_banner_items[i].path);s_banner_image_page=0;banner_image_scan();}
        else {content_banner_config_t next=s_banner_draft;next.mode=1;ui_snprintf(next.image,sizeof(next.image),"%s",s_banner_items[i].path);if(banner_preview_apply(&next,true))s_page=SETTINGS_HOME_QUOTE;}
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t banner_editor_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int nav=ui_nav_hit(ev->x0,ev->y0);if(nav>=0){ui_nav_request(ctx,nav);return APP_REDRAW_NONE;}
    if(ui_rect_hit(banner_footer(0),ev->x0,ev->y0)){ui_nav_request(ctx,0);return APP_REDRAW_NONE;}
    if(ui_rect_hit(banner_footer(1),ev->x0,ev->y0)){
        if(!banner_preview_apply(&s_banner_draft,true))return APP_REDRAW_PAGE;
        if(content_banner_save(&s_banner_draft)!=ESP_OK){strlcpy(s_notice,UI_MSG("保存失败，请重试"),sizeof(s_notice));return APP_REDRAW_PAGE;}
        s_quote_committed=true;ui_nav_request(ctx,0);return APP_REDRAW_NONE;
    }
    if(ev->y0>=438&&ev->y0<520){banner_picker_open();return APP_REDRAW_PAGE;}
    if(ev->x0>=536&&((ev->y0>=542&&ev->y0<594)||(ev->y0>=710&&ev->y0<762))){
        content_banner_config_t next=s_banner_draft;
        if(ev->y0<650)next.show_date=!next.show_date;else next.show_text=!next.show_text;
        (void)banner_preview_apply(&next,false);return APP_REDRAW_PAGE;
    }
    if(s_banner_draft.show_date)for(int i=0;i<2;++i)if(ui_rect_hit(banner_arrow_rect(i,640),ev->x0,ev->y0)){
        content_banner_config_t next=s_banner_draft;int y=banner_date_y()+(i?4:-4);
        if(y<CONTENT_BANNER_DATE_MIN)y=CONTENT_BANNER_DATE_MIN;
        if(y>CONTENT_BANNER_DATE_MAX)y=CONTENT_BANNER_DATE_MAX;
        next.date_y=y;(void)banner_preview_apply(&next,false);return APP_REDRAW_PAGE;
    }
    if(!s_banner_draft.show_text)return APP_REDRAW_NONE;
    if(ev->y0>=BANNER_TEXT_ROW_TOP&&ev->y0<BANNER_TEXT_ROW_TOP+BANNER_TEXT_ROW_H){home_quote_editor_open();return APP_REDRAW_PAGE;}
    if(ev->y0>=BANNER_TEXT_ROW_TOP+BANNER_TEXT_ROW_H&&ev->y0<BANNER_TEXT_ROW_TOP+2*BANNER_TEXT_ROW_H){ttf_font_scan();s_font_page=0;s_page=SETTINGS_QUOTE_FONT;s_notice[0]=0;return APP_REDRAW_PAGE;}
    for(int i=0;i<4;++i)if(ui_rect_hit(quote_alignment_rect(i),ev->x0,ev->y0)){
        content_banner_config_t next=s_banner_draft;next.align=i;
        (void)banner_preview_apply(&next,true);return APP_REDRAW_PAGE;
    }
    for(int i=0;i<2;++i)if(ui_rect_hit(banner_arrow_rect(i,banner_text_row_center(3)),ev->x0,ev->y0)){
        content_banner_config_t next=s_banner_draft;int y=ui_banner_text_y(&next)+(i?4:-4);
        if(y<0)y=0;
        if(y>CONTENT_BANNER_TEXT_MAX)y=CONTENT_BANNER_TEXT_MAX;
        next.text_y=y;(void)banner_preview_apply(&next,true);return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
