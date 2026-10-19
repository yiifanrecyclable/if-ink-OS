/* SPDX-License-Identifier: Apache-2.0
 * 阅读控制层的纯绘制与共同几何；仅由app_book.c包含。/ Pure reader-control drawing and shared geometry, private to app_book.c.
 */
#pragma once
static char s_reader_face_label[96];
static int reader_px(int role){return (role*app_settings_system_font_size()+50)/100;}
static int reader_row_height(void){if(s_reader_panel==READER_PANEL_SHORTCUTS)return 790/READER_SHORTCUT_COUNT;if(s_reader_panel==READER_PANEL_BINDING)return 790/READER_ACTION_COUNT;int h=reader_px(UI_TEXT_BODY)*2+48;return h<112?112:h;}
static int reader_control_rows(void){if(s_reader_panel==READER_PANEL_SHORTCUTS)return READER_SHORTCUT_COUNT;if(s_reader_panel==READER_PANEL_BINDING)return READER_ACTION_COUNT;int n=(1010-UI_PAGE_Y(220))/reader_row_height();return n<1?1:n;}
static EpdRect reader_control_row(int visible){return (EpdRect){36,UI_PAGE_Y(220)+visible*reader_row_height(),612,reader_row_height()};}
static int reader_control_top(void){int h=reader_px(UI_TEXT_TITLE)+reader_px(UI_TEXT_BODY)*2+reader_px(UI_TEXT_CAPTION)*2+168;return 1096-h;}
static int reader_progress_y(void){return reader_control_top()+reader_px(UI_TEXT_TITLE)+reader_px(UI_TEXT_CAPTION)+70;}
static int reader_actions_y(void){return reader_progress_y()+reader_px(UI_TEXT_CAPTION)+48;}
static int reader_actions_height(void){return reader_px(UI_TEXT_BODY)*2+16;}
static EpdRect reader_jump_back_rect(void){int width=reader_px(UI_TEXT_CAPTION)*5+24;return (EpdRect){668-width,reader_progress_y()+16,width,reader_actions_y()-reader_progress_y()-16};}
static int reader_progress_from_x(int x){int p=(x-36)*100/612;return p<0?0:p>100?100:p;}
static int reader_binding_owner(int action){for(int i=0;action&&i<READER_SHORTCUT_COUNT;++i)if(reader_input_action(i)==action)return i;return -1;}
static int reader_panel_tab(void){
    if(s_reader_panel==READER_PANEL_BOOKMARKS)return 1;
    if(s_reader_panel==READER_PANEL_FONT_SETTINGS||s_reader_panel==READER_PANEL_FONT_PICKER||s_reader_panel==READER_PANEL_MARKDOWN_SIZES)return 2;
    if(s_reader_panel==READER_PANEL_LAYOUT_SETTINGS||s_reader_panel==READER_PANEL_RULE_SETTINGS)return 3;
    return s_reader_panel==READER_PANEL_TOOLS?-1:4;
}
static void reader_control_text(uint8_t *fb,int x,int y,int role,const char *text,int align,bool inverse){ui_text_fixed_vc(fb,x,y,reader_px(role),text,align,inverse);}
static void reader_control_label(uint8_t *fb,EpdRect r,const char *text,const char *value){
    epd_fill_rect((EpdRect){r.x,r.y+r.height-1,r.width,1},UI_GRAY_ITEM_LINE,fb);
    char label[192];copy_text(label,sizeof(label),text);int width=value&&*value?320:570;
    fit_fixed_text(label,reader_px(UI_TEXT_BODY),width);
    reader_control_text(fb,r.x+12,r.y+r.height/2,UI_TEXT_BODY,label,EPD_DRAW_ALIGN_LEFT,false);
    if(value&&*value){char detail[128];copy_text(detail,sizeof(detail),value);fit_fixed_text(detail,reader_px(UI_TEXT_CAPTION),238);reader_control_text(fb,r.x+r.width-12,r.y+r.height/2,UI_TEXT_CAPTION,detail,EPD_DRAW_ALIGN_RIGHT,false);}
}
static void reader_control_footer(uint8_t *fb){
    static const char *const labels[]={"目录","书签","文字","版式","更多"};
    static const ui_icon_t icons[]={UI_ICON_LIST,UI_ICON_BOOKMARK,UI_ICON_TYPE,UI_ICON_SLIDERS_HORIZONTAL,UI_ICON_SETTINGS};
    int selected=s_view==TOC?0:reader_panel_tab();
    epd_fill_rect((EpdRect){0,1096,684,120},UI_GRAY_WHITE,fb);ui_hairline(fb,1096,0,684,UI_GRAY_SECTION_LINE);
    for(int i=0;i<5;++i){EpdRect r=tool_rect(i);bool on=i==selected;
        if(on)epd_fill_rect((EpdRect){r.x+r.width/2-22,1104,44,5},0x44,fb);
        ui_draw_icon(fb,r.x+r.width/2,1131,36,icons[i],UI_GRAY_BLACK);
        reader_control_text(fb,r.x+r.width/2,1183,UI_TEXT_BODY,labels[i],EPD_DRAW_ALIGN_CENTER,false);
    }
}
static void reader_control_header(uint8_t *fb,const char *title){
    epd_fill_rect((EpdRect){0,UI_STATUS_HEIGHT,684,1096-UI_STATUS_HEIGHT},UI_GRAY_WHITE,fb);
    ui_draw_icon(fb,62,UI_PAGE_Y(126),34,UI_ICON_CHEVRON_LEFT,UI_GRAY_BLACK);
    reader_control_text(fb,342,UI_PAGE_Y(126),UI_TEXT_TITLE,title,EPD_DRAW_ALIGN_CENTER,false);
    reader_control_text(fb,626,UI_PAGE_Y(126),UI_TEXT_BODY,"×",EPD_DRAW_ALIGN_CENTER,false);
    ui_hairline(fb,UI_PAGE_Y(180),36,612,UI_GRAY_SECTION_LINE);
}
static void reader_control_pager(uint8_t *fb,int count,int page){
    int pages=count?1+(count-1)/reader_control_rows():1;
    if(pages<=1)return;
    char text[32];snprintf(text,sizeof(text),"%d / %d",page+1,pages);
    ui_draw_icon(fb,64,1050,30,UI_ICON_CHEVRON_LEFT,page?0:0x99);
    ui_draw_icon(fb,620,1050,30,UI_ICON_CHEVRON_RIGHT,page+1<pages?0:0x99);
    reader_control_text(fb,342,1050,UI_TEXT_CAPTION,text,EPD_DRAW_ALIGN_CENTER,false);
}
static int reader_control_count(void){
    switch(s_reader_panel){
    case READER_PANEL_FONT_PICKER:return ttf_font_count()+1;
    case READER_PANEL_BOOKMARKS:return (int)bookmark_count()+2;
    case READER_PANEL_LAYOUT_SETTINGS:return 6;
    case READER_PANEL_SHORTCUTS:return READER_SHORTCUT_COUNT;
    case READER_PANEL_BINDING:return READER_ACTION_COUNT;
    case READER_PANEL_REFRESH_SETTINGS:return 6;
    case READER_PANEL_RULE_SETTINGS:return 5;
    default:return 0;
    }
}
static void draw_pill_slider(uint8_t *fb,EpdRect r,const char *left,const char *right,const char *value,int index,int count,int lp,int rp){
    (void)left;(void)right;(void)lp;(void)rp;
    int cy=r.y+r.height/2,x0=r.x+64,x1=r.x+r.width-64;
    reader_control_text(fb,r.x+24,cy,UI_TEXT_TITLE,"−",EPD_DRAW_ALIGN_CENTER,false);
    reader_control_text(fb,r.x+r.width-24,cy,UI_TEXT_TITLE,"+",EPD_DRAW_ALIGN_CENTER,false);
    epd_fill_rect((EpdRect){x0,cy-2,x1-x0,4},0xbb,fb);
    int cx=count>1?x0+(x1-x0)*index/(count-1):x0;
    epd_fill_rect((EpdRect){x0,cy-2,cx-x0,4},0x44,fb);epd_fill_circle(cx,cy,9,0x44,fb);
    reader_control_text(fb,r.x+r.width-75,r.y-22,UI_TEXT_CAPTION,value,EPD_DRAW_ALIGN_RIGHT,false);
}
static void reader_control_slider(uint8_t *fb,int slider,const char *label,const char *value,int index,int count){
    EpdRect r=reader_slider_rect(slider);if(r.width<=0)return;
    reader_control_text(fb,48,r.y-22,UI_TEXT_BODY,label,EPD_DRAW_ALIGN_LEFT,false);
    draw_pill_slider(fb,r,"","",value,index,count,0,0);
    ui_hairline(fb,r.y+r.height+8,36,612,UI_GRAY_ITEM_LINE);
}
static void reader_control_progress(uint8_t *fb){
    int caption=reader_px(UI_TEXT_CAPTION);unsigned progress=s_progress_drag?(unsigned)s_progress_percent:percent(s_page);
    int y=reader_progress_y(),thumb=36+612*(int)progress/100;
    epd_fill_rect((EpdRect){36,y-3,612,6},0xcc,fb);epd_fill_rect((EpdRect){36,y-3,thumb-36,6},0x44,fb);epd_fill_circle(thumb,y,12,0x44,fb);
    char page[80];if(s_progress_drag)snprintf(page,sizeof(page),"松手跳转至 %u%%",progress);else snprintf(page,sizeof(page),"本章 %u/%u 页 · %u/%u 节",(unsigned)s_page+1,(unsigned)book_layout_page_count(),(unsigned)s_chapter+1,(unsigned)book_chapter_count());
    int page_width=612-(s_jump_back_valid?reader_jump_back_rect().width:0);
    fit_fixed_text(page,caption,page_width);
    reader_control_text(fb,36,y+caption/2+20,UI_TEXT_CAPTION,page,EPD_DRAW_ALIGN_LEFT,false);
    if(s_jump_back_valid)reader_control_text(fb,648,y+caption/2+20,UI_TEXT_CAPTION,"返回跳转前",EPD_DRAW_ALIGN_RIGHT,false);
}
static void draw_reader_tools(uint8_t *fb){
    int top=reader_control_top(),title=reader_px(UI_TEXT_TITLE),caption=reader_px(UI_TEXT_CAPTION);
    for(int d=0;d<UI_NAV_SHADOW_DEPTH;++d)epd_fill_rect((EpdRect){0,top-UI_NAV_SHADOW_DEPTH+d,684,1},(uint8_t)(0xff-(d+1)*0x66/UI_NAV_SHADOW_DEPTH),fb);
    epd_fill_rect((EpdRect){0,top,684,1096-top},UI_GRAY_WHITE,fb);
    ui_hairline(fb,top,0,684,UI_GRAY_SECTION_LINE);
    int values_y=top+28+title/2;
    unsigned progress=s_progress_drag?(unsigned)s_progress_percent:percent(s_page);
    char values[3][40];snprintf(values[0],40,"%u%%",progress);snprintf(values[1],40,"%lu 分钟",(unsigned long)(s_session_read_ms/60000));snprintf(values[2],40,"%lu 次",(unsigned long)s_session_turns);
    const char *labels[]={"阅读进度","本次阅读","翻页"};
    for(int i=0;i<3;++i){int cx=114+i*228;reader_control_text(fb,cx,values_y,UI_TEXT_TITLE,values[i],EPD_DRAW_ALIGN_CENTER,false);reader_control_text(fb,cx,values_y+title/2+caption/2+14,UI_TEXT_CAPTION,labels[i],EPD_DRAW_ALIGN_CENTER,false);}
    reader_control_progress(fb);
    int ay=reader_actions_y(),h=reader_actions_height(),body=reader_px(UI_TEXT_BODY);
    const char *first[]={"退出",!s_reader_fullscreen?"常规":s_reader_immersive?"沉浸":"普通",bookmark_is_current()?"取消":"添加","立刻"};
    const char *second[]={"书籍",!s_reader_fullscreen?"布局":"全屏","书签","刷新"};
    int above=0,below=0;ui_text_fixed_measure_px(body,"退出全屏阅读书签列表立刻刷新",&above,&below);int text_step=(body+6+above+below+1)/2;
    for(int i=0;i<4;++i){int cx=(2*i+1)*684/8;if(i)epd_fill_rect((EpdRect){i*684/4,ay+12,1,h-24},UI_GRAY_ITEM_LINE,fb);reader_control_text(fb,cx,ay+h/2-text_step/2,UI_TEXT_BODY,first[i],EPD_DRAW_ALIGN_CENTER,false);reader_control_text(fb,cx,ay+h/2+(text_step+1)/2,UI_TEXT_BODY,second[i],EPD_DRAW_ALIGN_CENTER,false);}
}
// 标签白描边，淡化底图保持真实阅读区域可见。/ White halos keep labels legible over the faded reading page.
static void reader_help_label_aligned(uint8_t *fb,int x,int y,int px,const char *text,int align){
    for(int dx=-3;dx<=3;dx+=3)for(int dy=-3;dy<=3;dy+=3)ui_text_fixed_vc_gray(fb,x+dx,y+dy,px,text,align,UI_INK_WHITE,UI_INK_WHITE);
    ui_text_fixed_vc_gray(fb,x,y,px,text,align,UI_INK_BLACK,UI_INK_WHITE);
}
static void reader_help_label(uint8_t *fb,int x,int y,int px,const char *text){reader_help_label_aligned(fb,x,y,px,text,EPD_DRAW_ALIGN_CENTER);}
// 操作、缩略图和示例共用相同分区比例。/ Actions, thumbnails and guidance share one region model.
static EpdRect reader_tap_rect(EpdRect area,reader_tap_layout_t layout,unsigned zone){
    reader_tap_region_t r=reader_input_tap_region(layout,zone,area.width,area.height);
    return (EpdRect){area.x+r.x,area.y+r.y,r.width,r.height};
}
static EpdRect reader_basic_option(int index){return (EpdRect){36+324*index,238,288,652};}
static void draw_reader_basic(uint8_t *fb){
    static const char *const titles[]={"左／中／右","上／中／下"};
    static const char *const labels[]={"上一页","面板","下一页"};
    for(int mode=0;mode<2;++mode){EpdRect card=reader_basic_option(mode),paper={card.x+8,320,card.width-16,450};
        reader_control_text(fb,card.x+card.width/2,268,UI_TEXT_BODY,titles[mode],EPD_DRAW_ALIGN_CENTER,false);
        for(int z=0;z<3;++z){EpdRect r=reader_tap_rect(paper,(reader_tap_layout_t)mode,z);epd_draw_rect(r,0x88,fb);
            int px=ui_text_fit_px(UI_TEXT_CAPTION,labels[z],r.width-8,40);
            ui_text_fixed_vc_gray(fb,r.x+r.width/2,r.y+r.height/2,px,labels[z],EPD_DRAW_ALIGN_CENTER,0,15);}
        bool selected=reader_input_tap_layout()==mode;
        // 外框与示意图之间保留白隙，选中状态不依赖说明文字。/ Leave a white gap between the selection frame and the diagram; no selection caption is needed.
        if(selected)for(int inset=0;inset<3;++inset)epd_draw_rect((EpdRect){paper.x-8+inset,paper.y-8+inset,paper.width+16-2*inset,paper.height+16-2*inset},0x44,fb);
    }
    reader_control_text(fb,342,1020,UI_TEXT_CAPTION,"图书与文档共用，实体按键不变",EPD_DRAW_ALIGN_CENTER,false);
}
static void draw_reader_help(uint8_t *fb){
    epd_fill_rect((EpdRect){0,UI_STATUS_HEIGHT,684,1216-UI_STATUS_HEIGHT},UI_GRAY_WHITE,fb);
    // 朱自清《荷塘月色》开篇，公有领域；出处见阅读操作说明。/ Public-domain opening by Zhu Ziqing; source in reader documentation.
    static const char *const paragraphs[]={
        "这几天心里颇不宁静。今晚在院子里坐着乘凉，忽然想起日日走过的荷塘，在这满月的光里，总该另有一番样子吧。月亮渐渐地升高了，墙外马路上孩子们的欢笑，已经听不见了；妻在屋里拍着闰儿，迷迷糊糊地哼着眠歌。我悄悄地披了大衫，带上门出去。",
        "沿着荷塘，是一条曲折的小煤屑路。这是一条幽僻的路；白天也少人走，夜晚更加寂寞。荷塘四面，长着许多树，蓊蓊郁郁的。路的一旁，是些杨柳，和一些不知道名字的树。没有月光的晚上，这路上阴森森的，有些怕人。今晚却很好，虽然月光也还是淡淡的。",
        "路上只我一个人，背着手踱着。这一片天地好像是我的；我也像超出了平常的自己，到了另一世界里。我爱热闹，也爱冷静；爱群居，也爱独处。像今晚上，一个人在这苍茫的月下，什么都可以想，什么都可以不想，便觉是个自由的人。白天里一定要做的事，一定要说的话，现在都可不理。这是独处的妙处，我且受用这无边的荷香月色好了。"};
    // 按实际字体量宽换行，底图跨越三个点击区；不修改当前书籍的分页状态。/ Measure the active face across all tap zones without changing book pagination.
    int text_y=196;
    for(unsigned i=0;i<sizeof(paragraphs)/sizeof(paragraphs[0])&&text_y<=1149;++i){
        const char *at=paragraphs[i];bool first=true;
        while(*at&&text_y<=1149){
            char line[256]="";size_t used=0;
            if(first){copy_text(line,sizeof(line),"　　");used=strlen(line);first=false;}
            while(*at){
                const char *next=at+1;while((*next&0xc0)==0x80)++next;
                size_t bytes=(size_t)(next-at);if(used+bytes>=sizeof(line))break;
                memcpy(line+used,at,bytes);line[used+bytes]=0;
                if(ui_text_fixed_width_px(42,line)>612){line[used]=0;break;}
                used+=bytes;at=next;
            }
            ui_text_fixed_vc_gray(fb,36,text_y,42,line,EPD_DRAW_ALIGN_LEFT,12,15);text_y+=47;
        }
        text_y+=10;
    }
    EpdRect area=reader_area();
    const char *labels[]={"上一页","打开面板","下一页"};
    reader_tap_layout_t layout=reader_input_tap_layout();
    for(int i=0;i<3;++i){EpdRect r=reader_tap_rect(area,layout,(unsigned)i);int top=r.y<UI_PAGE_Y(160)?UI_PAGE_Y(160):r.y,end=r.y+r.height;if(end>1179)end=1179;
        if(i){if(layout==READER_TAP_ROWS)epd_fill_rect((EpdRect){0,r.y,684,2},UI_GRAY_BLACK,fb);else epd_fill_rect((EpdRect){r.x,UI_PAGE_Y(160),2,1180-UI_PAGE_Y(160)},UI_GRAY_BLACK,fb);}
        int x=r.x+r.width/2,cy=(top+end)/2;reader_help_label(fb,x,cy-25,36,"单击");reader_help_label(fb,x,cy+25,36,labels[i]);}
    epd_fill_rect((EpdRect){0,UI_STATUS_HEIGHT,684,80},UI_GRAY_WHITE,fb);
    for(int inset=0;inset<2;++inset)epd_draw_rect((EpdRect){28+inset,UI_PAGE_Y(109)+inset,28-2*inset,28-2*inset},UI_GRAY_BLACK,fb);
    if(!s_help_hide)epd_fill_rect((EpdRect){35,UI_PAGE_Y(116),14,14},UI_GRAY_BLACK,fb);
    reader_help_label_aligned(fb,70,UI_PAGE_Y(123),28,"在每本书开始时显示",EPD_DRAW_ALIGN_LEFT);
    reader_help_label_aligned(fb,625,UI_PAGE_Y(123),30,"关闭",EPD_DRAW_ALIGN_RIGHT);
    // 箭头向下指向实体键并避开装配遮挡区。/ Arrows point to the physical keys above the bezel occlusion zone.
    const char *keys[]={"上一页","短按：面板","下一页"};
    for(int i=0;i<3;++i){
        int x=114+i*228;reader_help_label(fb,x,1088,36,keys[i]);if(i==1)reader_help_label(fb,x,1128,36,"长按：首页");
        for(int y=i==1?1154:1118;y<1169;y+=12)epd_fill_rect((EpdRect){x,y,2,7},UI_GRAY_BLACK,fb);
        for(int d=0;d<2;++d){epd_draw_line(x-7+d,1164,x+d,1174,UI_GRAY_BLACK,fb);epd_draw_line(x+7+d,1164,x+d,1174,UI_GRAY_BLACK,fb);}
    }
}
static EpdRect reader_rule_offset_rect(int direction){EpdRect row=reader_control_row(3-s_control_page*reader_control_rows());return (EpdRect){direction<0?332:540,row.y+12,96,row.height-24};}
static void reader_rule_offset(uint8_t *fb,EpdRect r){
    ui_hairline(fb,r.y+r.height-1,r.x,r.width,UI_GRAY_ITEM_LINE);
    reader_control_text(fb,48,r.y+r.height/2,UI_TEXT_BODY,"上下位置",EPD_DRAW_ALIGN_LEFT,false);
    for(int dir=-1;dir<=1;dir+=2){EpdRect b=reader_rule_offset_rect(dir);epd_draw_rect(b,UI_GRAY_ITEM_LINE,fb);reader_control_text(fb,b.x+b.width/2,b.y+b.height/2,UI_TEXT_TITLE,dir<0?"−":"+",EPD_DRAW_ALIGN_CENTER,false);}
    char value[24];snprintf(value,sizeof(value),"%+d px",app_settings_book_reading_line_offset());reader_control_text(fb,484,r.y+r.height/2,UI_TEXT_CAPTION,value,EPD_DRAW_ALIGN_CENTER,false);
}
static void reader_rule_preview(uint8_t *fb,EpdRect r,int style){
    bool selected=(int)app_settings_book_reading_line()==style;
    const char *names[]={"无","虚线","点线"};
    reader_control_label(fb,r,names[style],selected?"已选择":"");
    int x=166,y=r.y+r.height/2,w=280;
    ui_text_fixed_vc_gray(fb,x,y-12,reader_px(UI_TEXT_CAPTION),"阅读文字 Aa",EPD_DRAW_ALIGN_LEFT,7,15);
    if(style){int dash=style==1?19:2,period=style==1?31:13;for(int dx=0;dx<w;dx+=period)epd_fill_rect((EpdRect){x+dx,y+18,dx+dash>w?w-dx:dash,2},0x50,fb);}
}
static EpdRect reader_font_sample_rect(void){return reader_markdown_font_panel()?(EpdRect){24,936,636,100}:(EpdRect){24,reader_control_row(3).y+50,636,250};}
static void reader_markdown_label(uint8_t *fb,int index,int px){
    static const char *const names[]={"正文","一级标题","二级标题","三级标题"};EpdRect row=reader_markdown_row(index);
    if(!index)return;
    int above=0,below=0;ttf_measure_line_px(px,names[index],&above,&below);
    ttf_draw_text_px(fb,48,row.y+48+(above-below)/2,px,names[index],EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
}
static void reader_markdown_slider(uint8_t *fb,int index,const uint8_t sizes[4]){
    int slider=5+index,px=sizes[index],min=index?24:BOOK_PX_MIN,max=index?96:BOOK_PX_MAX;char value[16];snprintf(value,sizeof(value),"%d",px);
    EpdRect r=reader_slider_rect(slider);if(!index)reader_control_text(fb,48,r.y-22,UI_TEXT_BODY,"正文",EPD_DRAW_ALIGN_LEFT,false);
    draw_pill_slider(fb,r,"","",value,px-min,max-min+1,0,0);EpdRect row=reader_markdown_row(index);ui_hairline(fb,row.y+row.height-1,36,612,UI_GRAY_ITEM_LINE);
}
static void reader_control_font_sample(uint8_t *fb){
        int px=reader_markdown_font_panel()?(s_reader_slider==5?s_md_draft[0]:s_md_sizes[0]):s_reader_slider==0?s_reader_preview_px:s_px;
        if(reader_markdown_font_panel()){
            char line[128]="清风明月，阅读此刻。Aa 0123";size_t n=strlen(line);
            while(n&&ttf_text_width_px(px,line)>600){do{--n;}while(n&&((unsigned char)line[n]&0xc0)==0x80);line[n]=0;}
            int above=0,below=0;ttf_measure_line_px(px,line,&above,&below);ttf_draw_text_px(fb,36,986+(above-below)/2,px,line,EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);return;
        }
        char sample[160];size_t offset=reader_page_offset(s_page),n=0;
        const char *at=s_text&&offset<s_text_len?s_text+offset:"阅读 Aa 0123";
        while(*at&&n<sizeof(sample)-5){if(*at=='\n'||*at=='\r'||*at=='\t'){sample[n++]=' ';++at;continue;}const char *end=at+1;while((*end&0xc0)==0x80)++end;size_t bytes=(size_t)(end-at);memcpy(sample+n,at,bytes);n+=bytes;at=end;}
        sample[n]=0;
        while(n&&ttf_text_width_px(px,sample)>600){do{--n;}while(n&&((unsigned char)sample[n]&0xc0)==0x80);sample[n]=0;}
        int above=0,below=0;ttf_measure_line_px(px,sample,&above,&below);
        int cy=reader_control_row(3).y+110;
        ttf_draw_text_px(fb,36,cy+(above-below)/2,px,sample,EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
        ttf_measure_line_px(px,"阅读 Aa 0123",&above,&below);
        ttf_draw_text_px(fb,36,cy+px+24+(above-below)/2,px,"阅读 Aa 0123",EPD_DRAW_ALIGN_LEFT,UI_INK_BLACK,UI_INK_WHITE);
}
static void reader_control_slider_value(uint8_t *fb,int slider){
    char value[32];
    if(slider>=5&&reader_markdown_font_panel()){reader_markdown_slider(fb,slider-5,s_md_draft);return;}
    if(slider>=5){int i=slider-5;const char *names[]={"正文","一级标题","二级标题","三级标题"};int px=s_md_draft[i],min=i?24:BOOK_PX_MIN,max=i?96:BOOK_PX_MAX;snprintf(value,sizeof(value),"%d",px);reader_control_slider(fb,slider,names[i],value,px-min,max-min+1);}
    else if(slider==0){snprintf(value,sizeof(value),"%d",s_reader_preview_px);reader_control_slider(fb,0,"正文字号",value,s_reader_preview_px-BOOK_PX_MIN,BOOK_PX_MAX-BOOK_PX_MIN+1);}
    else if(slider==1){snprintf(value,sizeof(value),"%d px",s_reader_preview_margin);reader_control_slider(fb,1,"页边距",value,s_reader_preview_margin-BOOK_MARGIN_MIN,BOOK_MARGIN_MAX-BOOK_MARGIN_MIN+1);}
    else if(slider==2){snprintf(value,sizeof(value),"%d%%",s_reader_preview_line);reader_control_slider(fb,2,"行距",value,s_reader_preview_line-110,41);}
    else if(slider==3){snprintf(value,sizeof(value),"%d%%",s_reader_preview_para);reader_control_slider(fb,3,"段距",value,s_reader_preview_para,76);}
    else{snprintf(value,sizeof(value),"%+d px",(s_reader_preview_tracking-2)*2);reader_control_slider(fb,4,"字间距",value,s_reader_preview_tracking,5);}
}
// 仅擦除动态控件，在现有帧缓冲上保留正文和不变的菜单内容。/ Preserve prose and static controls in the existing framebuffer.
static EpdRect reader_control_preview(uint8_t *fb,int slider){
    bool custom=app_settings_system_font_path()[0]!=0;if(custom)app_font_activate_system();ui_text_set_system_scale(true);
    EpdRect dirty;
    if(slider<0){
        int top=reader_control_top(),title=reader_px(UI_TEXT_TITLE),caption=reader_px(UI_TEXT_CAPTION),y=reader_progress_y();
        epd_fill_rect((EpdRect){0,top+1,228,title+40},UI_GRAY_WHITE,fb);
        char value[24];snprintf(value,sizeof(value),"%d%%",s_progress_percent);reader_control_text(fb,114,top+28+title/2,UI_TEXT_TITLE,value,EPD_DRAW_ALIGN_CENTER,false);
        // 清除百分比值时覆盖到的同列静态标签。/ Restore the static label in the same statistics column.
        reader_control_text(fb,114,top+28+title+caption/2+14,UI_TEXT_CAPTION,"阅读进度",EPD_DRAW_ALIGN_CENTER,false);
        epd_fill_rect((EpdRect){24,y-18,636,reader_actions_y()-y+18},UI_GRAY_WHITE,fb);reader_control_progress(fb);
        dirty=(EpdRect){0,top+1,684,reader_actions_y()-top-1};
    }else{
        int row=slider>=5?slider-5:slider==0?0:slider-1-s_control_page*reader_control_rows();
        dirty=slider>=5&&reader_markdown_font_panel()?reader_markdown_row(slider-5):reader_control_row(row);epd_fill_rect(dirty,UI_GRAY_WHITE,fb);reader_control_slider_value(fb,slider);
    }
    if(custom)app_font_activate_reading();
    ui_text_set_system_scale(false);
    if(slider>=6&&reader_markdown_font_panel())reader_markdown_label(fb,slider-5,s_md_draft[slider-5]);
    if(slider==0||(slider==5&&reader_markdown_font_panel())){EpdRect sample=reader_font_sample_rect();epd_fill_rect(sample,UI_GRAY_WHITE,fb);reader_control_font_sample(fb);dirty=ui_rect_union(dirty,sample);}
    return dirty;
}
static void draw_reader_panel(uint8_t *fb){
    if(s_reader_panel==READER_PANEL_NONE)return;
    copy_text(s_reader_face_label,sizeof(s_reader_face_label),ttf_font_localized_name(ttf_font_display_name()));
    bool custom=app_settings_system_font_path()[0]!=0;
    if(custom)app_font_activate_system();
    ui_text_set_system_scale(true);
    if(s_reader_panel==READER_PANEL_TOOLS)draw_reader_tools(fb);
    else if(s_reader_panel==READER_PANEL_HELP)draw_reader_help(fb);
    else{
        const char *title="阅读设置";
        switch(s_reader_panel){case READER_PANEL_FONT_SETTINGS:title="文字";break;case READER_PANEL_LAYOUT_SETTINGS:title="版式";break;case READER_PANEL_MARKDOWN_SIZES:title="文档字号";break;case READER_PANEL_FONT_PICKER:title="阅读字体";break;case READER_PANEL_BOOKMARKS:title="书签";break;case READER_PANEL_RULE_SETTINGS:title="阅读线";break;case READER_PANEL_SHORTCUTS:title="快捷手势";break;case READER_PANEL_BINDING:title=reader_shortcut_name(s_shortcut_slot);break;case READER_PANEL_BASIC:title="基础操作";break;default:break;}
        reader_control_header(fb,title);
        int rows=reader_control_rows(),first=s_control_page*rows;
        if(s_reader_panel==READER_PANEL_BASIC){draw_reader_basic(fb);}
        else if(reader_markdown_font_panel()){
            const uint8_t *sizes=s_reader_slider>=5?s_md_draft:s_md_sizes;
            reader_control_label(fb,(EpdRect){36,UI_PAGE_Y(210),612,80},"阅读字体",s_reader_face_label);
            for(int i=1;i<4;++i)reader_markdown_slider(fb,i,sizes);
            reader_markdown_slider(fb,0,sizes);
            reader_control_text(fb,36,920,UI_TEXT_CAPTION,"正文效果",EPD_DRAW_ALIGN_LEFT,false);
            reader_control_text(fb,36,1072,UI_TEXT_CAPTION,"本页设置仅对文档阅读生效",EPD_DRAW_ALIGN_LEFT,false);
            if(s_md_notice[0])reader_control_text(fb,36,1040,UI_TEXT_CAPTION,s_md_notice,EPD_DRAW_ALIGN_LEFT,false);
        }else if(s_reader_panel==READER_PANEL_FONT_SETTINGS){
            int px=s_reader_slider>=0?s_reader_preview_px:s_px;char value[32];snprintf(value,sizeof(value),"%d",px);
            reader_control_slider(fb,0,"正文字号",value,px-BOOK_PX_MIN,BOOK_PX_MAX-BOOK_PX_MIN+1);
            reader_control_label(fb,reader_control_row(1),"阅读字体",s_reader_face_label);
            if(book_kind()==BOOK_KIND_MARKDOWN)reader_control_label(fb,reader_control_row(2),"标题与正文字号","设置 ›");
            reader_control_text(fb,36,reader_control_row(3).y+30,UI_TEXT_CAPTION,"正文效果",EPD_DRAW_ALIGN_LEFT,false);
            reader_control_text(fb,36,1000,UI_TEXT_CAPTION,"调整后保留当前阅读位置",EPD_DRAW_ALIGN_LEFT,false);
        }else if(s_reader_panel==READER_PANEL_MARKDOWN_SIZES){
            const uint8_t *sizes=s_reader_slider>=5?s_md_draft:s_md_sizes;const char *names[]={"正文","一级标题","二级标题","三级标题"};
            for(int i=0;i<4;++i){char value[16];snprintf(value,sizeof(value),"%u",sizes[i]);int min=i?24:BOOK_PX_MIN,max=i?96:BOOK_PX_MAX;reader_control_slider(fb,5+i,names[i],value,sizes[i]-min,max-min+1);}
            if(s_md_notice[0])reader_control_text(fb,36,990,UI_TEXT_CAPTION,s_md_notice,EPD_DRAW_ALIGN_LEFT,false);
        }else if(s_reader_panel==READER_PANEL_LAYOUT_SETTINGS){
            int margin=s_reader_slider>=0?s_reader_preview_margin:s_margin,line=s_reader_slider>=0?s_reader_preview_line:app_settings_book_line_spacing(),para=s_reader_slider>=0?s_reader_preview_para:app_settings_book_paragraph_spacing(),tracking=s_reader_slider>=0?s_reader_preview_tracking:app_settings_book_tracking();
            const char *names[]={"页边距","行距","段距","字间距","首行缩进","阅读线"};
            for(int row=0;row<rows&&first+row<6;++row){int i=first+row;char value[32];
                if(i==0){snprintf(value,sizeof(value),"%d px",margin);reader_control_slider(fb,1,names[i],value,margin-BOOK_MARGIN_MIN,BOOK_MARGIN_MAX-BOOK_MARGIN_MIN+1);}
                else if(i==1){snprintf(value,sizeof(value),"%d%%",line);reader_control_slider(fb,2,names[i],value,line-110,41);}
                else if(i==2){snprintf(value,sizeof(value),"%d%%",para);reader_control_slider(fb,3,names[i],value,para,76);}
                else if(i==3){snprintf(value,sizeof(value),"%+d px",(tracking-2)*2);reader_control_slider(fb,4,names[i],value,tracking,5);}
                else{snprintf(value,sizeof(value),i==4?"%u 字 · 点按切换":"设置 ›",app_settings_book_indent());reader_control_label(fb,reader_control_row(row),names[i],value);}
            }
        }else if(s_reader_panel==READER_PANEL_RULE_SETTINGS){
            for(int row=0;row<rows&&first+row<5;++row){int i=first+row;if(i<3)reader_rule_preview(fb,reader_control_row(row),i);else if(i==3)reader_rule_offset(fb,reader_control_row(row));else reader_control_label(fb,reader_control_row(row),"恢复居中","");}
        }else if(s_reader_panel==READER_PANEL_BOOKMARKS){
            reader_bookmarks_t marks={0};bool valid=bookmark_load(&marks);int count=valid?marks.count:0;
            for(int row=0;row<rows&&first+row<count+2;++row){int i=first+row;EpdRect r=reader_control_row(row);
                if(i==0)reader_control_label(fb,r,bookmark_is_current()?"移除当前位置书签":"添加当前位置书签","");
                else if(i==1)reader_control_label(fb,r,s_bookmark_edit?"结束删除书签":"删除书签模式",s_bookmark_edit?"开启":"关闭");
                else{reader_bookmark_entry_t *mark=&marks.entries[count-(i-2)-1];char label[160],value[48];if(book_chapter_title(mark->chapter,label,sizeof(label))!=ESP_OK)snprintf(label,sizeof(label),"章节 %u",mark->chapter+1);snprintf(value,sizeof(value),s_bookmark_edit?"点按删除":"%u 节 ›",mark->chapter+1);reader_control_label(fb,r,label,value);}
            }
            if(s_bookmark_delete_confirm){epd_fill_rect((EpdRect){36,460,612,250},UI_GRAY_WHITE,fb);epd_draw_rect((EpdRect){36,460,612,250},0x88,fb);reader_control_text(fb,342,515,UI_TEXT_BODY,s_bookmark_delete_error?"删除失败，请重试":"删除这条书签？",EPD_DRAW_ALIGN_CENTER,false);reader_control_text(fb,185,640,UI_TEXT_BODY,"取消",EPD_DRAW_ALIGN_CENTER,false);reader_control_text(fb,500,640,UI_TEXT_BODY,"删除",EPD_DRAW_ALIGN_CENTER,false);}
        }else if(s_reader_panel==READER_PANEL_FONT_PICKER){
            int count=ttf_font_count();for(int row=0;row<rows&&first+row<count+1;++row){int i=first+row;const ttf_font_item_t *item=i?ttf_font_item(i-1):NULL;const char *name=item?ttf_font_localized_name(item->name):"默认字体";bool on=item?!strcmp(s_font_path,item->path):ttf_font_is_builtin();reader_control_label(fb,reader_control_row(row),name,on?"已选择":"");}
        }else if(s_reader_panel==READER_PANEL_SHORTCUTS||s_reader_panel==READER_PANEL_BINDING){
            int count=reader_control_count();for(int row=0;row<rows&&first+row<count;++row){int i=first+row;EpdRect r=reader_control_row(row);
                const char *name=s_reader_panel==READER_PANEL_SHORTCUTS?reader_shortcut_name(i):reader_action_name(i);
                int owner=s_reader_panel==READER_PANEL_BINDING?reader_binding_owner(i):-1;
                const char *value=s_reader_panel==READER_PANEL_SHORTCUTS?reader_action_name(reader_input_action(i)):owner>=0?reader_shortcut_name(owner):reader_input_action(s_shortcut_slot)==i?"已选择":"";
                ui_hairline(fb,r.y+r.height-1,r.x,r.width,UI_GRAY_ITEM_LINE);
                int detail_width=*value?ui_text_fixed_width_px(reader_px(UI_TEXT_CAPTION),value):0;
                char label[128];copy_text(label,sizeof(label),name);fit_fixed_text(label,reader_px(UI_TEXT_BODY),r.width-24-(detail_width?detail_width+20:0));
                reader_control_text(fb,48,r.y+r.height/2,UI_TEXT_BODY,label,EPD_DRAW_ALIGN_LEFT,false);
                if(*value)reader_control_text(fb,636,r.y+r.height/2,UI_TEXT_CAPTION,value,EPD_DRAW_ALIGN_RIGHT,false);
            }
        }else{
            const char *names[]={"基础操作","快捷手势","操作示例","定期全刷","翻页效果","显示插图"};
            for(int row=0;row<rows&&first+row<6;++row){int i=first+row;char value[48]="›";if(i==3){unsigned p=app_settings_reader_full_pages();if(p)snprintf(value,sizeof(value),"每 %u 页",p);else strcpy(value,"关闭");}else if(i==4)strcpy(value,app_settings_reader_turn_effect()?"水波纹":"默认");else if(i==5)strcpy(value,app_settings_reader_hide_images()?"关闭":"开启");reader_control_label(fb,reader_control_row(row),names[i],value);}
        }
        reader_control_pager(fb,reader_control_count(),s_control_page);
    }
    if(s_reader_panel!=READER_PANEL_HELP)reader_control_footer(fb);
    if(s_reader_notice[0]){char notice[96];copy_text(notice,sizeof(notice),s_reader_notice);fit_fixed_text(notice,reader_px(UI_TEXT_CAPTION),600);reader_control_text(fb,342,s_reader_panel==READER_PANEL_TOOLS?reader_control_top()-24:198,UI_TEXT_CAPTION,notice,EPD_DRAW_ALIGN_CENTER,false);}
    if(custom)app_font_activate_reading();
    ui_text_set_system_scale(false);
    if(s_reader_panel==READER_PANEL_FONT_SETTINGS||reader_markdown_font_panel())reader_control_font_sample(fb);
    if(reader_markdown_font_panel()){const uint8_t *sizes=s_reader_slider>=5?s_md_draft:s_md_sizes;for(int i=1;i<4;++i)reader_markdown_label(fb,i,sizes[i]);}
}
