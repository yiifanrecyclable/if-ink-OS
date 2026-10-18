#include <stdatomic.h>
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 自定义固件修改：图书/文档分别管理和排序；移除内容页导入入口，排序偏好独立保存。
 * English: Books/documents manage and sort independently; import remains on Files and sort preferences persist separately.
 * Modified derivative: user-requested document tabs, outline, tall images, pinning and stars.
 *
 * 图书书架、阅读、目录与页内进度；文件解析和排版由book模块负责。
 * Book shelf, reader, TOC and progress; book modules own parsing and pagination.
 *
 * 冻结：Phase4b统一手势入口并接管三键为上页/工具条/下页；工具条保留强刷。屏幕翻页在抬起提交，不画按下态。
 * 用户修订：移除晃动翻页及其加速度计生命周期；render只绘图。
 * 预渲染回调返回前收齐，避免菜单/锁屏绕过页内TTF锁。
 * 普通翻页只刷新正文与页脚；手动或周期清残影整屏全刷。
 * 用户2026-10-16修订：全屏全局记忆，像素边距和细段距独立调节，快捷手势限8项。
 * User revision: persist full screen, decouple pixel margins and retain eight shortcuts.
 * 用户2026-10-15修订：圆键或正文中央打开统一控制面板；左右轻点固定翻页，可选手势默认留空。
 * 长按圆键固定回首页，电源键锁屏；阅读控制采用系统文字和白底列表。
 * 用户修订：主面板滑块松手跳转、四个快捷按钮；书签入口切换当前书签，手势与功能一对一。
 * User revision: release-to-seek progress, four panel actions, bookmark toggle and unique gesture assignments.
 * 用户授权基础管理：长按书架先看完整详情，清进度与删文件分别确认；失败保留待重试记录，不自动回收其他书进度。
 * 用户修订：图书详情只在原书架上显示浮动卡片，置顶为单本操作并独立持久保存。
 * User revision: Book details are a modal over the original shelf; single-book pins persist independently.
 * 用户修订：单本管理为书架弹窗；管理页用于批量操作。分页和排序保留勾选，筛选/应用搜索及重扫清除勾选。
 * 用户修订：两种管理页共用布局和详情卡；文档取消星标，仅保留单篇长按置顶。
 * User revision: Management pages and detail cards share layout; documents retain single-item pins and remove stars.
 * 失败进度仅按变更路径失效；删除后的清理重试保留到本次开机结束，不随切页释放。
 * 卡失效时先保存进度并关闭阅读资源，再由主循环回退字体；禁止自动续读失效挂载。
 * 用户最新修订：图书固定三行三列封面，不绘制装饰；封面下依次显示无数字进度横条和书名，收录导入及读过的书。
 * User revision: Books use a plain 3x3 cover grid with number-free progress bars followed by names; imported/read books remain collected.
 * 移出仅隐藏，再读重新上架。/ Removing only hides a book; reading it again restores shelf membership.
 * 底栏保留首页/书架/文件/设置，设置直接进入设置页。
 * 用户修订：长按图书可用本机拼音输入编辑书名；阅读时长与翻页真实记录，供票根锁屏使用。
 * 用户最新修订：书名编辑可点选插入位置并用左右键微调，支持在文字中间插入和删除。
 * 用户修订：首页、书架、文件、设置四栏导航；切换界面采用 GL16，章节首页单独排标题。
 * 用户修订：EPUB 章节首页以书内目录标题为准，正文题头仅在相同时折叠，避免引言误判。
 * 用户修订：阅读进度条无外伸刻度并使用圆角；继续阅读区显示最近书籍封面。
 * 用户修订：书架只在封面下显示书名；首次打开时先显示书架，再逐本生成封面缓存。
 * 用户修订：字号和间距重排只保存最终进度，字号使用现有闲置灰阶整理。
 * 用户修订：阅读控制使用系统文字；滑杆松手重排；统计常驻主面板，不显示最近30天入口。
 * 用户修订：水波纹保持原实现；统一阅读控制面板，滑动与双指快捷操作默认不绑定。
 * 用户修订：目录由独立模块整页绘制与命中；目录标题清理换行并限制为单行，翻页不再沿用书架的局部刷新。
 * 用户修订：书架封面抽出与取消仅驱动变化像素，保持灰阶，不在点按时强制清屏。
 * Frozen: Phase4b uses the shared gesture entry and owns previous/tools/next keys; the toolbar keeps full refresh. Screen turns commit on release without pressed decoration.
 * User revision 2026-10-15: remove shake turning and its sensor lifecycle. Render only paints.
 * Join preparation before returning callbacks so menus/lock cannot race the page-local TTF lock.
 * Ordinary turns refresh only body and footer; manual and periodic ghost cleanup refresh the entire screen.
 * User revision: the circle key or center tap opens controls; side taps are fixed page turns. Optional gestures default unbound; the top-left control returns to the opening source.
 * A circle hold returns Home; power locks. Reader control lists use system typography, white surfaces and shared destinations.
 * User-authorized management shows full details before separate clear/delete confirmations; retain failed saves for retry without pruning other books.
 * User revision: single-book actions use a shelf dialog; full management is for batches. Paging/sorting preserve selection; filtering/applied search and rescanning clear it.
 * Invalidate failed progress only for changed paths; retain deletion cleanup retries across page exits for this boot.
 * Lost media saves progress and closes reader resources before global font fallback; never auto-resume an invalid mount.
 * Latest user revision: regular shelf pages hold nine imported or read books and spine pages hold thirteen; removal hides until reread.
 * The fourth tab opens Settings directly.
 * User revision: book details lead to an on-device Pinyin title editor; measured reading time and turns feed the ticket lock face.
 * Latest user revision: the title editor can place and move an insertion caret for edits in the middle of text.
 * User revision: home, shelf, files and settings have four-tab navigation; view changes use GL16 and chapter starts have a title lead.
 * User revision: EPUB chapter leads use navigation titles; body headings are folded only when matching, preventing front matter from being mislabeled.
 * User revision: the reader bar is rounded without protruding ticks; continue reading displays the latest cover.
 * User revision: the shelf shows titles without author rows; first visits paint the shelf before filling cached covers.
 * User revision: size and spacing reflow saves final progress only; size uses the existing idle grayscale settle.
 * User revision: controls use system type, sliders reflow on release and statistics stay on the main panel without a recent-30-days entry.
 * User revision: default turns keep their refresh policy; Refresh Settings may enable staggered GL16 water turns, while the font sheet stays compact.
 * User revision: a standalone module owns full-page TOC rendering and hit testing; normalized single-line titles cannot leak into another row.
 * 用户调整：书架点按不提起封面、不绘制按下反馈，松手直接打开图书。
 * User revision: shelf presses never lift or decorate covers; release opens the book directly.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "app_content_open.h"
#include "app_registry.h"
#include "book_layout.h"
#include "book_cover.h"
#include "book_epub.h"
#include "book_policy.h"
#include "book_progress.h"
#include "book_title.h"
#include "book_ticket.h"
#include "book_toc.h"
#include "app_font_context.h"
#include "book_source.h"
#include "book_markdown.h"
#include "document_store.h"
#include "content_library.h"
#include "document_image.h"
#include <math.h>
#include "book_store.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "read_pico_init.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "read_pico_search.h"
#include "settings.h"
#include "reader_input.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_image_dither.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "app_transfer_mode.h"

// 阅读工具栏图标盒边长；行内图标在 1096..1216 的工具条里以 1138 为中心。
// Reader toolbar icon box; the row centres on y=1138 inside the 1096..1216 bar.
#define READER_TOOL_ICON_PX 40
// 二级面板返回键里的箭头边长；圆底半径 24，原来的箭头是 20 像素高。
// Chevron box inside a secondary panel's back control; the old arrow was 20 px tall.
#define READER_SHEET_BACK_PX 34

#define BOOK_ROWS 13

#define BOOK_BULK_ROWS (management_rows())
static int management_rows(void);
static int document_rows(void);
#define BOOK_PX_MIN 36
#define BOOK_PX_MAX 72
#define BOOK_PX_STEP 4
#define BOOK_MARGIN_MIN 8
#define BOOK_MARGIN_MAX 60
#define BOOK_MARGIN_CHOICES (BOOK_MARGIN_MAX - BOOK_MARGIN_MIN + 1)
#define BOOK_TOOL_COUNT 5
#define BOOKMARK_MAX 24

typedef enum { SHELF, READING, TOC, MANAGE, BULK, IMPORT, SEARCH, EDIT, DOCUMENTS, DOCUMENT_BULK } book_view_t;
static book_view_t s_view;
typedef enum {
    READER_PANEL_NONE,
    READER_PANEL_TOOLS,
    READER_PANEL_FONT_SETTINGS,
    READER_PANEL_LAYOUT_SETTINGS,
    READER_PANEL_RULE_SETTINGS,
    READER_PANEL_FONT_PICKER,
    READER_PANEL_STATS,
    READER_PANEL_BOOKMARKS,
    READER_PANEL_STATS_RECENT,
    READER_PANEL_REFRESH_SETTINGS,
    READER_PANEL_TURN_SETTINGS,
    READER_PANEL_MARKDOWN_SIZES,
    READER_PANEL_SHORTCUTS, READER_PANEL_BINDING, READER_PANEL_HELP, READER_PANEL_BASIC,
} reader_panel_t;
typedef struct {
    char name[256];
    char author[128];
    char path[BOOK_STORE_PATH_MAX];
    uint32_t size;
    uint16_t chapter;
    bool is_flash;
    bool has_progress;
    uint8_t pct;
    uint32_t recent;
    uint32_t added;
    bool selected, removed, search_match, pinned;
} shelf_entry_t;

/* 用户授权新增 Markdown 文档页、原生阅读器和长图查看。 Modified derivative. */
static document_entry_t *s_documents;
static size_t s_document_count;
static bool s_document_mode, s_document_delete_confirm, s_document_clear_confirm, s_document_menu, s_document_starred_only, s_document_completed;
static char s_document_message[64], s_document_query[65];
static int s_document_filter;
static document_entry_t s_document_detail;
static void draw_document_details(uint8_t *fb);
static app_redraw_t document_details_action(app_ctx_t *ctx,uint16_t x,uint16_t y);
static content_options_t s_book_options, s_document_options, s_sort_draft;
static bool s_sort_open, s_sort_documents, s_document_batch_confirm;
static int s_document_batch_action;
static char s_sort_notice[80];
static void content_sort_open(bool documents);
static void draw_content_sort(uint8_t *fb);
static void draw_document_bulk(app_ctx_t *ctx,uint8_t *fb);
static app_redraw_t content_sort_action(app_ctx_t *ctx,uint16_t x,uint16_t y);
static app_redraw_t document_bulk_action(app_ctx_t *ctx,const ui_gesture_event_t *ev);
static document_image_t s_document_image;
static bool s_document_zoom, s_zoom_moved;
static uint32_t s_zoom_top, s_zoom_origin;
static uint8_t *s_zoom_gray;
static unsigned s_zoom_height;
static int compare_documents(const void *a,const void *b) {
    const document_entry_t *x=a,*y=b;
    if(x->pinned!=y->pinned)return x->pinned?-1:1;
    int order=0;
    if(s_document_options.mode==CONTENT_PROGRESS)order=(x->pct>y->pct)-(x->pct<y->pct);
    else if(s_document_options.mode==CONTENT_ADDED){
        bool x_known=x->date[0]>='0'&&x->date[0]<='9',y_known=y->date[0]>='0'&&y->date[0]<='9';
        if(x_known!=y_known)return x_known?-1:1;
        order=strcmp(x->date,y->date);
    }else if(s_document_options.mode==CONTENT_RECENT){
        if(!!x->recent!=!!y->recent)return x->recent?-1:1;
        order=(x->recent>y->recent)-(x->recent<y->recent);
        if(!order&&(!x->recent||!y->recent))order=strcmp(x->date,y->date);
    }else order=content_name_compare(x->name,y->name);
    if(order)return s_document_options.ascending?order:-order;
    order=content_name_compare(x->name,y->name);return order?order:strcmp(x->path,y->path);
}
static void clear_document_selection(void) {
    for(size_t i=0;i<s_document_count;++i)s_documents[i].selected=false;
}
static void scan_documents(app_ctx_t *ctx) {
    document_entry_t *next=NULL;size_t count=0;
    if(document_store_scan(&next,&count)!=ESP_OK){free(next);snprintf(s_document_message,sizeof(s_document_message),"文档读取失败，请重试");return;}
    size_t pending=0;
    for(size_t i=0;i<s_document_count;++i)if(s_documents[i].cleanup_pending)++pending;
    if(pending){
        if(count+pending>SIZE_MAX/sizeof(*next)){free(next);return;}
        document_entry_t *grown=realloc(next,(count+pending)*sizeof(*next));
        if(!grown){free(next);snprintf(s_document_message,sizeof(s_document_message),"内存不足，请稍后重试");return;}next=grown;
    }
    for(size_t i=0;i<count;++i){
        book_progress_t progress={0};
        if(book_progress_load(next[i].path,next[i].size,&progress)){next[i].pct=progress.pct;next[i].recent=progress.last_open_s;next[i].has_progress=true;}
        for(size_t j=0;j<s_document_count;++j)if(!strcmp(next[i].path,s_documents[j].path)){
            next[i].selected=s_documents[j].selected;next[i].cleanup_pending=s_documents[j].cleanup_pending;break;
        }
    }
    // 删除后清理失败的条目留在列表，供本次开机重试。/ Keep failed post-delete cleanup entries available for retry during this boot.
    for(size_t j=0;j<s_document_count;++j)if(s_documents[j].cleanup_pending){
        bool found=false;for(size_t i=0;i<count;++i)if(!strcmp(next[i].path,s_documents[j].path)){found=true;break;}
        if(!found)next[count++]=s_documents[j];
    }
    free(s_documents);s_documents=next;s_document_count=count;
    s_document_starred_only=false;
    if(s_document_query[0]){size_t n=0;for(size_t i=0;i<s_document_count;++i)if(read_pico_search_match(s_documents[i].name,s_document_query)||s_documents[i].cleanup_pending)s_documents[n++]=s_documents[i];s_document_count=n;}
    if(s_document_count>1)qsort(s_documents,s_document_count,sizeof(*s_documents),compare_documents);
    int rows=s_view==DOCUMENT_BULK?BOOK_BULK_ROWS:document_rows();
    if(ctx->leaf<0||(size_t)ctx->leaf*rows>=s_document_count)ctx->leaf=0;
}
// 一体式双段胶囊，副标题与点击区域共用；墨水屏直接切换，不做滑动动画。
// One two-segment capsule shares subtitle hit targets; switch directly without ink-display animation.
static EpdRect content_tab(int i){return (EpdRect){36+i*152,84,152,76};}
static void draw_content_tabs(uint8_t *fb,bool documents){
    EpdRect capsule={36,84,304,76};
    ui_fill_round_rect(fb,capsule,38,0xd0);
    ui_draw_round_rect(fb,capsule,38,0x90);
    for(int i=0;i<2;i++){
        EpdRect r=content_tab(i);bool active=(i==1)==documents;
        if(active)ui_fill_round_rect(fb,(EpdRect){r.x+4,r.y+4,r.width-8,r.height-8},34,UI_NAV_ACTIVE_GRAY);
        uint8_t fg=active?15:0,bg=active?(UI_NAV_ACTIVE_GRAY>>4):13;
        ui_text_fixed_vc_gray(fb,r.x+r.width/2,110,31,i?"文档":"图书",EPD_DRAW_ALIGN_CENTER,fg,bg);
        ui_text_fixed_vc_gray(fb,r.x+r.width/2,140,23,i?"Markdown":"EPUB/TXT",EPD_DRAW_ALIGN_CENTER,fg,bg);
    }
}
static int document_row_step(void) { return ui_list_row_height(false)+22; }
static int document_rows(void) { return ui_list_visible_rows(224,1042,document_row_step()); }
static EpdRect document_row(int row){return (EpdRect){36,224+row*document_row_step(),612,document_row_step()-10};}
static EpdRect content_action(int i){return (EpdRect){442+i*109,94,97,54};}
static void library_button(uint8_t *fb,EpdRect r,const char *label,bool active){
    ui_fill_round_rect(fb,r,16,active?UI_GRAY_BLACK:UI_GRAY_WHITE);
    ui_draw_round_rect(fb,r,16,0x70);
    ui_text_vc(fb,r.x+r.width/2,r.y+r.height/2,UI_TEXT_BODY,label,EPD_DRAW_ALIGN_CENTER,active);
}
static void draw_content_actions(uint8_t *fb) {
    for(int i=0;i<2;++i){EpdRect r=content_action(i);library_button(fb,r,i?"排序":"管理",false);}
}
static void draw_documents(app_ctx_t *ctx,uint8_t *fb){
    ui_clear_page(fb);epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,UI_NAV_TOP},UI_GRAY_WHITE,fb);ui_nav_status(fb);draw_content_tabs(fb,true);
    ui_hairline(fb,195,36,612,0x68);
    draw_content_actions(fb);
    if(!s_document_count)ui_text_vc(fb,342,408,27,s_document_query[0]?"没有匹配的文档":"还没有 Markdown 文档",EPD_DRAW_ALIGN_CENTER,false);
    for(int row=0;row<document_rows();row++){size_t i=(size_t)ctx->leaf*document_rows()+row;if(i>=s_document_count)break;
        document_entry_t *d=&s_documents[i];EpdRect r=document_row(row);epd_fill_rect(r,UI_GRAY_WHITE,fb);
        char title[256];snprintf(title,sizeof(title),"%s",d->name);char *ext=strrchr(title,'.');if(ext)*ext=0;
        // 按 UTF-8 边界截断，日期与进度区域固定保留。
        // Trim at UTF-8 boundaries and reserve fixed date/progress columns.
        int title_px=ui_text_effective_px(UI_TEXT_BODY);
        int date_px=ui_text_effective_px(UI_TEXT_CAPTION);
        const char *date_label=d->cleanup_pending?"清理待重试":d->date[0]?d->date:"未导入";
        int date_width=ui_text_fixed_width_px(date_px,date_label);
        int title_width=592-date_width-52-36;
        size_t n=strlen(title);while(n&&ui_text_fixed_width_px(title_px,title)>title_width){do{n--;}while(n&&((unsigned char)title[n]&0xc0)==0x80);title[n]=0;}
        ui_text_fixed_vc(fb,52,r.y+r.height/2,title_px,title,EPD_DRAW_ALIGN_LEFT,false);
        if(d->pinned){epd_fill_rect((EpdRect){592-date_width-23,r.y+r.height/2-9,10,4},0x58,fb);epd_fill_rect((EpdRect){592-date_width-19,r.y+r.height/2-5,2,12},0x58,fb);epd_fill_triangle(592-date_width-22,r.y+r.height/2-1,592-date_width-14,r.y+r.height/2-1,592-date_width-18,r.y+r.height/2-6,0x58,fb);}
        ui_text_fixed_vc(fb,592,r.y+r.height/2,date_px,date_label,EPD_DRAW_ALIGN_RIGHT,false);
        unsigned pct=d->pct;if(pct>100)pct=100;
        // 十二像素小圆环；灰底表示未读，深色弧表示阅读比例。
        // Small pale ring with a dark arc proportional to reading progress.
        for(int y=-12;y<=12;y++)for(int x=-12;x<=12;x++){int radius=x*x+y*y;if(radius<64||radius>144)continue;
            double angle=atan2((double)x,(double)-y);if(angle<0)angle+=6.283185307179586;
            epd_draw_pixel(622+x,r.y+r.height/2+y,angle<6.283185307179586*pct/100?0x00:0xc0,fb);}
    }
    int pages=s_document_count?(int)((s_document_count+document_rows()-1)/document_rows()):1;
    ui_nav_draw_pager(fb,(int)s_document_count,"篇文档",ctx->leaf,pages);ui_nav_draw(fb,1);
    if(s_document_menu)draw_document_details(fb);
}
static EpdRect content_sort_mode(int i){return (EpdRect){76+(i%2)*272,326+(i/2)*94,260,76};}
static EpdRect content_sort_control(void){return (EpdRect){76,530,532,66};}
static EpdRect content_sort_footer(int i){return (EpdRect){76+i*288,736,244,76};}
static void content_sort_open(bool documents){
    s_sort_documents=documents;s_sort_draft=documents?s_document_options:s_book_options;s_sort_notice[0]=0;s_sort_open=true;
}
static void draw_content_sort(uint8_t *fb){
    ui_clear_page(fb);ui_nav_status(fb);
    epd_fill_rect((EpdRect){44,302,596,2},UI_GRAY_SECTION_LINE,fb);
    epd_fill_rect((EpdRect){44,710,596,2},UI_GRAY_ITEM_LINE,fb);
    ui_text_vc(fb,342,270,30,s_sort_documents?"文档排序":"图书排序",EPD_DRAW_ALIGN_CENTER,false);
    const char *labels[]={s_sort_documents?"文档名":"书名","阅读进度","加入日期","最近阅读"};
    for(int i=0;i<4;++i)library_button(fb,content_sort_mode(i),labels[i],s_sort_draft.mode==i);
    const char *direction=s_sort_draft.mode==CONTENT_NAME?(s_sort_draft.ascending?"名称正序":"名称倒序"):
        s_sort_draft.mode==CONTENT_PROGRESS?(s_sort_draft.ascending?"进度从低到高":"进度从高到低"):
        (s_sort_draft.ascending?"时间从早到晚":"时间从晚到早");
    library_button(fb,content_sort_control(),direction,false);

    ui_text_vc(fb,342,635,20,s_sort_documents?"置顶文档始终排在最前":"置顶图书始终排在最前",EPD_DRAW_ALIGN_CENTER,false);
    if(s_sort_notice[0])ui_text_vc(fb,342,678,20,s_sort_notice,EPD_DRAW_ALIGN_CENTER,false);
    library_button(fb,content_sort_footer(0),"取消",false);
    library_button(fb,content_sort_footer(1),"完成",true);
    ui_nav_draw(fb,1);
}
// 两种管理页共用几何；底部三键不预留菜单空间。/ Shared management geometry; bottom controls span the full width.
static int management_rows(void) { int n=ui_list_visible_rows(308,936,ui_list_row_height(true)+UI_GAP);return n<6?n:6; }
static EpdRect management_row(int i){return (EpdRect){UI_MARGIN,308+i*(ui_list_row_height(true)+UI_GAP),ui_content_width(),ui_list_row_height(true)};}
static EpdRect bulk_filter_rect(int i){return ui_row_rect(i,3,216,70);}
static EpdRect batch_rect(int i){return ui_row_rect(i%3,3,i<3?952:1022,58);}
static EpdRect management_nav(int i){return ui_row_rect(i,3,UI_BAR_TOP,UI_BAR_H);}
static int management_nav_hit(uint16_t x,uint16_t y){for(int i=0;i<3;++i)if(ui_rect_hit(management_nav(i),x,y))return i;return -1;}
static unsigned document_selected_count(void){unsigned count=0;for(size_t i=0;i<s_document_count;++i)count+=s_documents[i].selected;return count;}
static bool prepare_document_zoom(uint32_t top){
    if(!s_document_image.width)return false;
    uint32_t span=(uint32_t)(((uint64_t)1216*s_document_image.width+683)/684);
    uint32_t maximum=s_document_image.height>span?s_document_image.height-span:0;
    if(top>maximum)top=maximum;
    s_zoom_top=top;
    uint64_t scaled_height=(uint64_t)s_document_image.height*684/s_document_image.width;s_zoom_height=scaled_height>1216?1216:(unsigned)scaled_height;if(!s_zoom_height)s_zoom_height=1;
    if(!s_zoom_gray)s_zoom_gray=malloc((size_t)684*1216);
    return s_zoom_gray&&document_image_sample(&s_document_image,top,span,684,s_zoom_height,s_zoom_gray);
}
static void draw_document_zoom(uint8_t *fb){
    ui_clear_page(fb);if(!s_zoom_gray)return;
    int top=(1216-(int)s_zoom_height)/2;
    for(unsigned y=0;y<s_zoom_height;y++)for(unsigned x=0;x<684;x++)epd_draw_pixel(x,top+y,(uint8_t)(s_zoom_gray[(size_t)y*684+x]&0xf0),fb);
}
static const char* TAG = "book";
static int s_presented_view = -1;
static bool s_reader_image_refresh_pending;
static char s_requested_open[BOOK_STORE_PATH_MAX];
static bool s_requested_open_home;
static bool s_reader_return_home;
static bool s_requested_manage;
static shelf_entry_t* s_shelf;
static struct { int index; uint8_t *gray; } s_covers[BOOK_ROWS];
static unsigned s_cover_pending_mask;
static size_t s_shelf_capacity;
static int s_count, s_visible_count, s_filter;
static bool s_recent_sort;
static shelf_entry_t s_managed;
static bool s_delete_confirm, s_file_removed;
static char s_shelf_warning[128], s_manage_message[128];
static char s_query[65], s_search_draft[65], s_batch_message[128];
static book_view_t s_search_parent;
// 批量操作需要二次确认的三类：删文件、清进度、从书架移除。前两类动数据，后一类只动书架。
// Three batch actions need confirmation: delete files, clear progress, remove from the shelf.
// The first two touch stored data; the last one only touches shelf membership.
typedef enum {
    BATCH_DELETE,
    BATCH_CLEAR,
    BATCH_UNSHELF,
} batch_kind_t;
static bool s_batch_confirm;
static batch_kind_t s_batch_kind;
typedef struct pending_progress {
    char path[BOOK_STORE_PATH_MAX];
    book_progress_t value;
    bool dirty, progress_saved;
    book_progress_watch_t* watch;
    struct pending_progress* next;
} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {
    shelf_entry_t entry;
    struct delete_retry* next;
} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[BOOK_STORE_PATH_MAX];
static char s_editor_title[121], s_editor_notice[96];
static ui_text_input_t s_title_input,s_search_input;
static uint8_t* s_editor_cover;
static bool s_save_failed;
static bool s_pending_invalidated;
static int64_t s_save_retry_ms;
static unsigned s_store_revision;
static bool s_shelf_cache_valid, s_cache_sd_present, s_cache_sd_mounted;
static bool s_scan_pending, s_clear_confirm;
static reader_panel_t s_reader_panel;
static bool s_reader_fullscreen;
static bool s_reader_immersive;
static char s_message[128], s_storage[128], s_path[BOOK_STORE_PATH_MAX], s_title[128];
static char s_book_title[128];
static char s_chapter_heading_title[128], s_chapter_heading_label[32];
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_height;
static char s_font_path[192];
static char* s_text;
bool app_book_reader_body_visible(void) {
    return s_view == READING && s_text && s_reader_panel == READER_PANEL_NONE && !s_clear_confirm;
}
static blk_t* s_blocks;
static text_style_t *s_text_styles;
static size_t s_text_style_count;
static markdown_cell_t *s_text_cells;
static size_t s_text_cell_count;
static bool s_md_committed;
static char s_md_notice[64];
static uint8_t s_md_sizes[4] = {48,72,64,56}, s_md_draft[4];
static char** s_images;
static size_t s_image_count;
typedef struct { uint8_t *gray; int width, height; } reader_image_t;
static reader_image_t *s_page_images;
static int s_page_image_count;
static size_t s_page_images_for = SIZE_MAX;
static uint32_t s_page_images_generation;
static uint8_t* s_inline_gray;
static unsigned s_inline_w, s_inline_h;
static int s_inline_index = -1;
static size_t s_block_count;
static size_t s_text_len, s_chapter, s_page;
static size_t s_selected_toc = SIZE_MAX;
static bool s_toc_jump_open, s_toc_jump_drag;
static int s_toc_jump_percent,s_toc_jump_origin;
static size_t s_jump_offset = SIZE_MAX, s_jump_page;
static uint32_t s_file_size;
static int s_px, s_margin, s_line_spacing, s_turns, s_unsaved;
static int64_t s_poll_ms, s_last_turn_ms;
static int64_t s_stats_last_ms, s_stats_activity_ms;
static uint32_t s_stats_pending_ms, s_stats_pending_turns;
static uint32_t s_session_read_ms, s_session_turns;
static int s_reader_slider = -1;
static bool s_reader_slider_endpoint;
static int s_reader_preview_px, s_reader_preview_margin, s_reader_preview_line, s_reader_preview_para;
static int s_reader_preview_tracking;
static char s_reader_notice[64];
static int64_t s_reader_notice_until;
static bool s_bookmark_edit, s_bookmark_delete_confirm, s_bookmark_delete_error;
static uint32_t s_bookmark_selected;
static reader_multi_t s_reader_multi;
static int s_control_page, s_shortcut_slot;
static bool s_help_hide;
static bool s_progress_drag;
static int s_progress_percent;
static bool s_jump_back_valid;
static bool prepare_document_zoom(uint32_t top);
static size_t s_jump_back_chapter, s_jump_back_offset;
static EpdRect s_area;
static enum EpdDrawMode s_mode = MODE_GL16;
static bool s_reader_cleanup;
static bool s_reader_footer_pending;
static bool s_water_turn_pending;
static e0470_turn_dir_t s_water_turn_dir;
static int s_pressed_control = -1;
static int64_t s_du_ms;
static unsigned s_du_count;
static EpdRect s_du_area;
static SemaphoreHandle_t s_draw_lock, s_prep_done;
static TaskHandle_t s_prep_task;
static uint8_t* s_next_fb;
static int s_next_page = -1, s_prep_page = -1;
static uint32_t s_next_generation;
static book_layout_cursor_t s_prep_cursor;
static atomic_bool s_prep_stop;

#define s_toolbar (s_reader_panel != READER_PANEL_NONE)

static void render(app_ctx_t* ctx, uint8_t* fb);
static void draw_reader_panel(uint8_t* fb);
static EpdRect reader_slider_rect(int slider);
static bool reader_markdown_font_panel(void);
static EpdRect reader_markdown_row(int index);
static void scan_shelf(app_ctx_t* ctx);
static void copy_text(char* dst, size_t cap, const char* src);
static void fit_text(char* text, int px, int width);
static void free_book(void);
static void save_progress(void);
static void invalidate_prep(void);
static void prepare_inline_image(void);
static void release_page_images(void);
static void sort_shelf(app_ctx_t* ctx);
static void prepare_covers(app_ctx_t* ctx);
bool app_book_request_open(const char* path) {
    if (document_store_sync_recover() != ESP_OK) return false;
    if (!path || !path[0] || strnlen(path, sizeof(s_requested_open)) >= sizeof(s_requested_open)) return false;
    copy_text(s_requested_open, sizeof(s_requested_open), path);
    s_requested_open_home = false;
    return true;
}
bool app_book_request_open_from_home(const char* path) {
    if (!app_book_request_open(path)) return false;
    s_requested_open_home = true;
    return true;
}
void app_book_request_manage(void) { s_requested_manage = true; }
static void draw_control(uint8_t* fb, EpdRect rect, const char* label, int id) {
    if (id == 112 && s_query[0]) {
        ui_fill_round_rect(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
        ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2,
                   UI_PX_BTN, label, EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
    if (s_pressed_control == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, label, false);
}
static void lock_draw(void) { if (s_draw_lock) xSemaphoreTake(s_draw_lock, portMAX_DELAY); }
static void unlock_draw(void) { if (s_draw_lock) xSemaphoreGive(s_draw_lock); }
static size_t fb_bytes(void) { return (size_t)epd_width() * epd_height() / 2; }
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
// 正文上方留出一致的呼吸空间，下沿靠近进度区；两种模式共用排版与刷新边界。
// Give the body a consistent top inset and extend it toward the progress strip in both modes.
static EpdRect reader_area(void) {
    const int top = s_reader_fullscreen ? (s_reader_immersive ? 0 : 80) : 160;
    // 全屏进度线单独差分，翻页时不让正文刷新反复驱动整条横线。
    // Keep the full-screen progress strip out of body updates so turns do not drive the whole bar.
    const int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP : UI_BAR_TOP + 8;
    return (EpdRect){0, top, UI_LOCK_WIDTH, bottom - top};
}
static EpdRect reader_fullscreen_progress_area(void) {
    return (EpdRect){0, READER_FULLSCREEN_PROGRESS_TOP,
                     UI_LOCK_WIDTH, UI_LOCK_HEIGHT - READER_FULLSCREEN_PROGRESS_TOP};
}
static EpdRect body_rect_for_tracking(int tracking_index) {
    int top = s_reader_fullscreen ? (s_reader_immersive ? 24 : 96) : 188;
    int margin = s_margin ? s_margin : 36;
    // Leave only a narrow strip for the full-book progress line in full screen.
    int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP - 2 : UI_BAR_TOP + 8;
    EpdRect outer = {margin, top, UI_LOCK_WIDTH - 2 * margin, bottom - top};
    (void)tracking_index;
    return outer;
}
static EpdRect body_rect(void) {
    return body_rect_for_tracking(app_settings_book_tracking());
}
static EpdRect progress_rect(void) {
    // 阅读页没有右侧菜单按钮；进度信息应和页眉分隔线一样铺满内容宽度。
    // The reader has no right-hand menu button; the footer spans the full content width.
    return (EpdRect){36, UI_BAR_TOP, UI_LOCK_WIDTH - 72, UI_BAR_H};
}
// 大/特大字级改用四个大封面；命中与缓存使用相同分页数量。/ Large type uses four covers; hits and cache share pagination.
static int shelf_grid_side(void){return app_settings_system_font_size()>=140?2:3;}
static int shelf_rows(void){int n=shelf_grid_side();return n*n;}
static int shelf_cell_height(void){return (UI_NAV_PAGER_TOP-224)/shelf_grid_side();}
static EpdRect row_rect(int row) {
    if (s_view != BULK) {
        int n=shelf_grid_side(),col=row%n,line=row/n;
        int cell=(ui_content_width()-(n-1)*UI_GAP)/n,h=shelf_cell_height();
        return (EpdRect){UI_MARGIN+col*(cell+UI_GAP),224+line*h,cell,h};
    }
    return management_row(row);
}
static void invalidate_covers(void) {
    s_cover_pending_mask = 0;
    for (int i = 0; i < BOOK_ROWS; ++i) {
        free(s_covers[i].gray);
        s_covers[i].gray = NULL;
        s_covers[i].index = -1;
    }
}
static uint8_t* load_cover_gray(const char* source, const char* title, const char* author,
                                bool decode, bool* pending) {
    *pending = false;
    uint8_t* gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray) return NULL;
    if (!book_cover_load_gray(source, title, author, gray, decode, pending)) {
        free(gray); return NULL;
    }
    return gray;
}
static void prepare_covers(app_ctx_t* ctx) {
    if (s_view != SHELF && s_view != MANAGE) return;
    int rows = shelf_rows();
    if (s_covers[0].index != ctx->leaf * rows) s_cover_pending_mask = 0;
    for (int row = 0; row < rows; ++row) {
        int index = ctx->leaf * rows + row;
        if (s_covers[row].index == index) continue;
        free(s_covers[row].gray);
        s_covers[row].gray = NULL;
        s_covers[row].index = index;
        if (index >= s_visible_count || index < 0 || s_shelf[index].removed) continue;
        bool pending = false;
        s_covers[row].gray = load_cover_gray(s_shelf[index].path, s_shelf[index].name,
                                              s_shelf[index].author, false, &pending);
        if (pending) s_cover_pending_mask |= 1u << row;
    }
}

static int shelf_cover_height(void) {
    int ideal=shelf_grid_side()==3?200:300;
    int available=shelf_cell_height()-2*ui_text_line_height(UI_TEXT_BODY)-16;
    return available<ideal?available:ideal;
}
static EpdRect shelf_image_rect(EpdRect card) {
    int height=shelf_cover_height(),width=height*176/240;
    return (EpdRect){card.x+(card.width-width)/2,card.y,width,height};
}
static void draw_shelf_cover(uint8_t* fb, EpdRect card, int row, const char* name) {
    EpdRect image=shelf_image_rect(card);
    epd_fill_rect(image, UI_GRAY_LIGHT, fb);
    if (s_covers[row].gray) {
        const uint8_t *gray = s_covers[row].gray;
        // 封面缓冲按当前格子比例裁剪，不拉伸。
        // Proportionally crop the 176x240 cover into the current grid without stretching.
        const unsigned frame_width = (unsigned)image.width, frame_height = (unsigned)image.height;
        const book_crop_t crop = book_cover_crop(BOOK_COVER_W, BOOK_COVER_H,
                                                 frame_width, frame_height);
        for (int y = 0; y < image.height; ++y) {
            const int sy = (int)(crop.y + (uint64_t)(unsigned)y * crop.height / frame_height);
            for (int x = 0; x < image.width; ++x) {
                const int sx = (int)(crop.x + (uint64_t)(unsigned)x * crop.width / frame_width);
                const uint8_t tone = ui_gray_level(gray[sy * BOOK_COVER_W + sx]);
                epd_draw_pixel(image.x + x, image.y + y,
                               ui_image_dither_gray(tone, image.x + x, image.y + y), fb);
            }
        }
    } else {
        char title[80]; copy_text(title, sizeof(title), name);
        char *dot = strrchr(title, '.'); if (dot) *dot = 0;
        fit_text(title, 26, image.width - 22);
        ui_text_vc(fb, image.x + image.width / 2, image.y + image.height / 2, 26,
                   title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, image, 0, UI_GRAY_BLACK);

}
static EpdRect tool_rect(int i) {
    int x0 = i * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    int x1 = (i + 1) * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    return (EpdRect){x0, 1096, x1 - x0, 120};
}
static int leaves(void) {
    if (s_view == DOCUMENTS || s_view == DOCUMENT_BULK) return s_document_count ? 1+(int)(s_document_count-1)/(s_view==DOCUMENT_BULK?BOOK_BULK_ROWS:document_rows()) : 1;
    if (s_view == TOC) return book_toc_pages(book_navigation_count());
    int count = s_visible_count;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    return count ? 1 + (count - 1) / rows : 1;
}
static EpdRect shelf_manage_rect(void) { return content_action(0); }
static EpdRect shelf_sort_rect(void) { return content_action(1); }
static EpdRect shelf_page_arrow_rect(int direction) {return ui_nav_pager_rect(direction);}
static void draw_shelf_pager(uint8_t *fb,int count,int page,int pages) {
    ui_nav_draw_pager(fb,count,"本书",page,pages);
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction);
static EpdRect import_rect(int index) { return (EpdRect){36, 230 + index * 164, 612, 136}; }
static void clean_filename(char *dst, size_t cap, const char *filename) {
    copy_text(dst, cap, filename);
    char *ext = strrchr(dst, '.');
    if (ext) *ext = 0;
    char *start = dst;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != dst) memmove(dst, start, strlen(start) + 1);
    size_t len = strlen(dst);
    while (len && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) dst[--len] = 0;
}

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void fit_text(char* text, int px, int width) {
    while (*text && ttf_text_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void fit_fixed_text(char* text, int px, int width) {
    while (*text && ui_text_fixed_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void reader_footer_strip_number(char *dst, size_t cap, const char *source) {
    char title[128];
    copy_text(title, sizeof(title), source ? source : "");
    char *name = title;
    while (*name == ' ' || *name == '\t') ++name;
    char *chapter_mark = !strncmp(name, "第", strlen("第")) ? strstr(name, "章") : NULL;
    if (chapter_mark && chapter_mark - name < 24) {
        char *after = chapter_mark + strlen("章");
        while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
               !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、")) ||
               !strncmp(after, "·", strlen("·"))) {
            if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
            else after += strlen("：");
        }
        name = after;
    }
    if (!*name || !strcmp(name, "本章") || !strcmp(name, "章节")) name = "未命名章节";
    copy_text(dst, cap, name);
}
static void reader_footer_chapter_name(char *dst, size_t cap) {
    char source[128] = {0};
    bool selected = s_selected_toc < book_navigation_count() &&
        book_navigation_chapter(s_selected_toc) == s_chapter &&
        book_navigation_title(s_selected_toc, source, sizeof(source)) == ESP_OK && source[0];
    if (!selected) {
        if (s_chapter_heading_title[0]) copy_text(source, sizeof(source), s_chapter_heading_title);
        else if (book_chapter_title(s_chapter, source, sizeof(source)) != ESP_OK) source[0] = 0;
    }
    reader_footer_strip_number(dst, cap, source);
}
// 标题最多两行，字号限制在格子可容纳的范围；保留UTF8边界。/ Captions use at most two lines with bounded font sizes and UTF-8-safe truncation.
static void draw_shelf_title(uint8_t *fb,EpdRect card,const char *full){
    int px=ui_text_effective_px(UI_TEXT_BODY);
    if(ui_text_fixed_width_px(px,full)<=card.width-8){ui_text_fixed_vc(fb,card.x+card.width/2,card.y+shelf_cover_height()+16+ui_text_line_height(UI_TEXT_BODY)/2,px,full,EPD_DRAW_ALIGN_CENTER,false);return;}
    px=ui_text_effective_px(UI_TEXT_BODY);
    char lines[2][256]={{0}};const char *next=full;
    for(int row=0;row<2;++row){size_t used=0;
        while(*next&&used+5<sizeof(lines[row])){
            unsigned char ch=(unsigned char)*next;size_t bytes=ch<0x80?1:ch<0xe0?2:ch<0xf0?3:4;
            if(strlen(next)<bytes)break;
            memcpy(lines[row]+used,next,bytes);lines[row][used+bytes]=0;
            if(ui_text_fixed_width_px(px,lines[row])>card.width-8){lines[row][used]=0;break;}
            used+=bytes;next+=bytes;
        }
    }
    if(*next){size_t n=strlen(lines[1]);
        while(n&&ui_text_fixed_width_px(px,lines[1])+ui_text_fixed_width_px(px,"…")>card.width-8){do{--n;}while(n&&((unsigned char)lines[1][n]&0xc0)==0x80);lines[1][n]=0;}
        strlcat(lines[1],"…",sizeof(lines[1]));
    }
    for(int i=0;i<2;++i)if(lines[i][0])ui_text_fixed_vc(fb,card.x+card.width/2,card.y+shelf_cover_height()+16+ui_text_line_height(UI_TEXT_BODY)/2+i*ui_text_line_height(UI_TEXT_BODY),px,lines[i],EPD_DRAW_ALIGN_CENTER,false);
}
static uint32_t chapter_end(void) {
    return s_chapter + 1 < book_chapter_count()
        ? book_chapter_byte_offset(s_chapter + 1) : book_total_bytes();
}
static size_t reader_page_offset(size_t page) {
    size_t off = book_layout_page_start_offset(page);
    if (s_jump_offset != SIZE_MAX && page == s_jump_page && s_jump_offset > off)
        off = s_jump_offset;
    return off;
}
static unsigned percent(size_t page) {
    uint32_t total = book_total_bytes();
    if (!total) return 0;
    if(book_kind()==BOOK_KIND_MARKDOWN&&book_layout_page_count()==1&&!s_document_completed)return 0;
    if (s_chapter + 1 == book_chapter_count() && page + 1 == book_layout_page_count()) return 100;
    uint32_t off = book_position_bytes(book_chapter_byte_offset(s_chapter), chapter_end(),
                                       reader_page_offset(page), s_text_len);
    return (unsigned)((uint64_t)off * 100 / total);
}
static pending_progress_t* pending_find(const char* path) {
    for (pending_progress_t* p = s_pending; p; p = p->next) if (!strcmp(p->path, path)) return p;
    return NULL;
}
static int layout_name(uint8_t* fb, const char* name, int y, bool draw);
static EpdRect details_panel(const char *name) {
    int height = layout_name(NULL,name,0,false)+495;
    return (EpdRect){UI_MARGIN - 16, 190 + (876 - height) / 2, ui_content_width() + 32, height};
}
static EpdRect manage_panel(void){return details_panel(s_managed.name);}
static EpdRect details_rect(const char *name,int index,int count){
    EpdRect panel=details_panel(name);
    return ui_row_rect(index, count, panel.y + panel.height - 94, 76);
}
static EpdRect manage_rect(int i,int count){return details_rect(s_managed.name,i,count);}
static EpdRect manage_aux_rect(int i){EpdRect panel=manage_panel();return ui_row_rect(i,2,panel.y+panel.height-190,68);}
static EpdRect manage_back_rect(void) { return (EpdRect){36, 92, 44, 44}; }

static size_t selected_count(void) {
    size_t selected = 0;
    for (int i = 0; i < s_count; ++i) if (s_shelf[i].selected) ++selected;
    return selected;
}
static void clear_selection(void) {
    for (int i = 0; i < s_count; ++i) s_shelf[i].selected = false;
}
static void toggle_selection(int index) {
    if (index >= 0 && index < s_visible_count) s_shelf[index].selected = !s_shelf[index].selected;
}
static void select_page(int page) {
    for (int i = page * BOOK_BULK_ROWS; i < s_visible_count && i < (page + 1) * BOOK_BULK_ROWS; ++i) s_shelf[i].selected = true;
}

static void search_begin(void) {
    s_search_parent = s_view;
    memcpy(s_search_draft,s_view==DOCUMENT_BULK?s_document_query:s_query,sizeof(s_search_draft));
    ui_text_input_open(&s_search_input,s_search_draft,sizeof(s_search_draft),true,false,UI_INPUT_ENGLISH);
    s_view = SEARCH;
}
static void refresh_search_matches(void) {
    for (int i = 0; i < s_count; ++i)
        s_shelf[i].search_match = read_pico_search_match(s_shelf[i].name, s_query);
}
static void search_finish(app_ctx_t* ctx, bool apply) {
    s_view = s_search_parent;
    if (apply) {
        if(s_view==DOCUMENT_BULK){
            memcpy(s_document_query,s_search_draft,sizeof(s_document_query));
            clear_document_selection();s_document_message[0]=0;ctx->leaf=0;scan_documents(ctx);
        }else{
            memcpy(s_query,s_search_draft,sizeof(s_query));refresh_search_matches();
            clear_selection();sort_shelf(ctx);s_batch_message[0]=0;
        }
    }
    memset(s_search_draft, 0, sizeof(s_search_draft));
}

static bool pending_reserve(const char* path) {
    if (pending_find(path)) return true;
    pending_progress_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    copy_text(p->path, sizeof(p->path), path);
    // 阅读页内注册；传书 HTTP 尚未启动，退出后也不注销失败项。
    // Register in reading before transfer HTTP starts; failed records survive page exit.
    p->watch = book_progress_watch_create(path);
    if (!p->watch) { free(p); return false; }
    pending_progress_t** tail = &s_pending;
    while (*tail) tail = &(*tail)->next;
    *tail = p;
    return true;
}
static bool pending_restore(const char* path, uint32_t size, book_progress_t* out) {
    pending_progress_t* pending = pending_find(path);
    if (!pending || !pending->dirty || pending->value.file_size != size) return false;
    *out = pending->value;
    return true;
}
static void pending_discard(const char* path) {
    pending_progress_t** p = &s_pending;
    while (*p) {
        if (!strcmp((*p)->path, path)) {
            pending_progress_t* old = *p;
            *p = old->next;
            book_progress_watch_destroy(old->watch);
            free(old);
            break;
        }
        p = &(*p)->next;
    }
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}
static void pending_mark_latest(const char* path) {
    pending_progress_t** item = &s_pending;
    while (*item && strcmp((*item)->path, path)) item = &(*item)->next;
    if (*item) {
        pending_progress_t* current = *item;
        *item = current->next;
        current->next = NULL;
        pending_progress_t** tail = &s_pending;
        while (*tail) tail = &(*tail)->next;
        *tail = current;
    }
    copy_text(s_latest_path, sizeof(s_latest_path), path);
}
static void pending_drop_invalidated(void) {
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        if (book_progress_watch_invalidated(p->watch)) {
            if (p->dirty) s_pending_invalidated = true;
            pending_discard(p->path);
        }
        p = next;
    }
}
static delete_retry_t* delete_retry_find(const char* path) {
    for (delete_retry_t* p = s_delete_retries; p; p = p->next)
        if (!strcmp(p->entry.path, path)) return p;
    return NULL;
}
static bool pending_flush(pending_progress_t* p) {
    if (!p->dirty) return true;
    if (!p->progress_saved) {
        if (book_progress_save(p->path, &p->value) != ESP_OK) return false;
        p->progress_saved = true;
    }
    if (!strcmp(p->path, s_latest_path) && book_progress_set_last_path(p->path) != ESP_OK) return false;
    p->dirty = false;
    return true;
}
static void retry_progress(void) {
    s_save_failed = false;
    bool had_dirty = false;
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        bool dirty = p->dirty;
        had_dirty |= dirty;
        if (s_text && !strcmp(p->path, s_path)) { p = next; continue; }
        if (!pending_flush(p)) s_save_failed = true;
        else if (strcmp(p->path, s_path)) pending_discard(p->path);
        else if (dirty) s_unsaved = 0;
        p = next;
    }
    if (s_text && (s_unsaved || had_dirty)) save_progress();
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}

/* ---- 管理详情 / Management details ---- */
static int layout_name(uint8_t* fb, const char* name, int y, bool draw) {
    const char* at = name;
    while (*at) {
        char line[256];
        size_t used = 0;
        while (at[used]) {
            unsigned char first = (unsigned char)at[used];
            size_t n = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            size_t remain = strlen(at + used);
            if (n > remain) n = 1;
            if (used + n >= sizeof(line)) break;
            memcpy(line + used, at + used, n);
            line[used + n] = 0;
            if (ui_text_fixed_width_px(ui_text_effective_px(UI_TEXT_BODY),line)>ui_content_width() && used) break;
            used += n;
        }
        if (!used) break;
        line[used] = 0;
        if (draw) ui_text(fb, UI_MARGIN, y, UI_TEXT_BODY, line, EPD_DRAW_ALIGN_LEFT, false);
        y += ui_text_line_height(UI_TEXT_BODY);
        at += used;
    }
    return y;
}
static int draw_wrapped_name(uint8_t* fb, const char* name, int y) {
    return layout_name(fb, name, y, true);
}
// 按钮标签按实际字宽适配卡片，兼容放大的系统字号。/ Fit labels to the modal controls at larger system font scales.
static void draw_manage_button(uint8_t *fb,EpdRect r,const char *label,bool active){
    int px=UI_PX_BTN;
    while(px>10&&(ui_text_fixed_width_px(ui_text_effective_px(px),label)>r.width-24||ui_text_effective_px(px)>r.height-16))--px;
    ui_fill_round_rect(fb,r,UI_BTN_RADIUS,active?UI_GRAY_BLACK:UI_GRAY_WHITE);
    ui_draw_round_rect(fb,r,UI_BTN_RADIUS,UI_GRAY_BLACK);
    ui_text_vc(fb,r.x+r.width/2,r.y+r.height/2,px,label,EPD_DRAW_ALIGN_CENTER,active);
}
// 图书和文档共用详情卡的尺寸、字级、边框与信息顺序。/ Books and documents share detail-card geometry, typography and information order.
static EpdRect draw_details_base(uint8_t *fb,const char *name,const char *title,const char *storage,const char *folder,
                                uint32_t size,bool has_progress,unsigned pct,const char *message){
    EpdRect panel=details_panel(name);
    ui_fill_round_rect(fb,panel,UI_BTN_RADIUS,UI_GRAY_WHITE);ui_draw_round_rect(fb,panel,UI_BTN_RADIUS,UI_GRAY_BLACK);
    ui_text(fb,UI_MARGIN,panel.y+18,UI_PX_BODY,title,EPD_DRAW_ALIGN_LEFT,false);
    int y=draw_wrapped_name(fb,name,panel.y+74)+12;char info[96];const char *ext=strrchr(name,'.');
    snprintf(info,sizeof(info),"%s · %s · %.2f MB",storage,ext?ext+1:"",size/1048576.0);
    ui_text(fb,UI_MARGIN,y,UI_TEXT_CAPTION,info,EPD_DRAW_ALIGN_LEFT,false);
    snprintf(info,sizeof(info),has_progress?"阅读进度 %u%%":"尚无阅读进度",pct);
    ui_text(fb,UI_MARGIN,y+44,UI_TEXT_CAPTION,info,EPD_DRAW_ALIGN_LEFT,false);
    ui_text(fb,UI_MARGIN,y+88,UI_TEXT_CAPTION,folder,EPD_DRAW_ALIGN_LEFT,false);
    if(message[0])ui_text(fb,UI_MARGIN,panel.y+panel.height-270,UI_TEXT_CAPTION,message,EPD_DRAW_ALIGN_LEFT,false);
    return panel;
}
static void draw_manage(uint8_t* fb) {
    EpdRect panel=draw_details_base(fb,s_managed.name,s_clear_confirm?(s_delete_confirm?"确认删除文件？":"确认清除进度？"):"图书详情",
        s_managed.is_flash?"内置存储":"TF 卡",s_managed.is_flash?"/flash/books":!strncmp(s_managed.path,"/sdcard/book/",13)?"/sdcard/book":app_settings_books_dir(),
        s_managed.size,s_managed.has_progress,s_managed.pct,s_manage_message);
    if (s_clear_confirm) {
        ui_text(fb, UI_MARGIN, panel.y + panel.height - 142, UI_PX_CAPTION, s_delete_confirm ? "删除后文件无法恢复" : "仅清阅读进度，保留图书文件", EPD_DRAW_ALIGN_LEFT, false);
        draw_manage_button(fb, manage_rect(0, 2), "取消", false);
        draw_manage_button(fb, manage_rect(1, 2), s_delete_confirm ? "确认删除" : "确认清除", false);
    } else if (s_file_removed) {
        draw_manage_button(fb, manage_rect(0, 2), "关闭", false);
        draw_manage_button(fb, manage_rect(1, 2), "重试清理", false);
    } else {
        draw_manage_button(fb,manage_aux_rect(0),s_managed.pinned?"取消置顶":"置顶",s_managed.pinned);
        draw_manage_button(fb,manage_aux_rect(1),"文件管理重命名",false);
        draw_manage_button(fb, manage_rect(0, 3), "关闭", false);
        draw_manage_button(fb, manage_rect(1, 3), "清进度", false);
        draw_manage_button(fb, manage_rect(2, 3), "删除文件", false);
    }
}
static void draw_document_details(uint8_t *fb){
    document_entry_t *d=&s_document_detail;bool confirm=s_document_delete_confirm||s_document_clear_confirm;
    EpdRect panel=draw_details_base(fb,d->name,confirm?(s_document_delete_confirm?"确认删除文件？":"确认清除进度？"):"文档详情",
        "TF 卡",DOCUMENT_ROOT,d->size,d->has_progress,d->pct,s_document_message);
    if(confirm){
        ui_text(fb,UI_MARGIN,panel.y+panel.height-142,UI_PX_CAPTION,s_document_delete_confirm?"关联插图与阅读进度也会删除":"仅清阅读进度，保留文档和图片",EPD_DRAW_ALIGN_LEFT,false);
        draw_manage_button(fb,details_rect(d->name,0,2),"取消",false);
        draw_manage_button(fb,details_rect(d->name,1,2),s_document_delete_confirm?"确认删除":"确认清除",false);
    }else if(d->cleanup_pending){
        draw_manage_button(fb,details_rect(d->name,0,2),"关闭",false);draw_manage_button(fb,details_rect(d->name,1,2),"重试清理",false);
    }else{
        draw_manage_button(fb,ui_row_rect(0,1,panel.y+panel.height-190,68),d->pinned?"取消置顶":"置顶",d->pinned);
        draw_manage_button(fb,details_rect(d->name,0,3),"关闭",false);
        draw_manage_button(fb,details_rect(d->name,1,3),"清进度",false);
        draw_manage_button(fb,details_rect(d->name,2,3),"删除文件",false);
    }
}
static void editor_start(void) {
    free(s_editor_cover);s_editor_cover=NULL;
    copy_text(s_editor_title,sizeof(s_editor_title),s_managed.name);s_editor_notice[0]=0;
    ui_text_input_open(&s_title_input,s_editor_title,sizeof(s_editor_title),true,false,UI_INPUT_CHINESE);
    s_view=EDIT;
}
static void draw_editor(uint8_t *fb){
    ui_text_input_draw(fb,&s_title_input,"编辑书籍","点选文字定位光标",NULL,"完成",s_editor_notice);
}
static void draw_search(uint8_t *fb){
    ui_text_input_draw(fb,&s_search_input,s_search_parent==DOCUMENT_BULK?"搜索文档":"搜索图书",
        "支持中文、拼音首字母、完整拼音或英文",NULL,"应用",NULL);
}
static void draw_import(uint8_t* fb) {
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "导入图书", EPD_DRAW_ALIGN_CENTER, false);
    ui_text(fb, 36, 166, 20, "选择导入方式", EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 207, 36, 612, UI_GRAY_LIGHT);
    static const char *titles[] = {"浏览 TF 卡", "WiFi 传书", "热点传书", "USB 读卡"};
    static const char *details[] = {
        "查看目录，打开或管理已有图书", "手机与 Pico 连接同一网络",
        "连接 Pico 热点后上传图书", "连接电脑，把图书放入 books 文件夹"
    };
    for (int i = 0; i < 4; ++i) {
        EpdRect r = import_rect(i);
        epd_fill_rect(r,UI_GRAY_WHITE,fb);
        epd_fill_rect((EpdRect){r.x,r.y+r.height,r.width,2},UI_GRAY_ITEM_LINE,fb);
        ui_text(fb, r.x + 24, r.y + 25, 27, titles[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, r.x + 24, r.y + 78, 19, details[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, r.x + r.width - 24, r.y + r.height / 2, 26, "›", EPD_DRAW_ALIGN_RIGHT, false);
    }
    ui_text(fb, 36, 925, 19, "图书放入 TF 卡的 books 文件夹后会自动出现在书架。",
            EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 1);
}
static void draw_batch_confirmation(uint8_t* fb) {
    if (!s_batch_confirm) return;
    EpdRect panel = {54, 406, 576, 330};
    ui_fill_round_rect(fb, panel, 25, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, panel, 25, 0x48);
    char title[96];
    const char* verb = s_batch_kind == BATCH_DELETE ? "删除" :
                       s_batch_kind == BATCH_CLEAR ? "清除进度：" : "从书架移除";
    const char* note = s_batch_kind == BATCH_DELETE ? "删除文件不可撤销，失败项可重试" :
                       s_batch_kind == BATCH_CLEAR ? "仅清阅读进度，所有文件保留" :
                       "只移出书架，文件与阅读进度都保留";
    snprintf(title, sizeof(title), "%s %u 本图书？", verb, (unsigned)selected_count());
    ui_text_vc(fb, 342, 477, 28, title, EPD_DRAW_ALIGN_CENTER, false);
    ui_text_vc(fb, 342, 541, 19, note, EPD_DRAW_ALIGN_CENTER, false);
    draw_control(fb, ui_row_rect(0, 2, 620, UI_BTN_H), "取消", 600);
    draw_control(fb, ui_row_rect(1, 2, 620, UI_BTN_H), "确认", 601);
}

static void draw_management_header(uint8_t *fb,bool documents,const char *summary,const char *message){
    ui_clear_page(fb);ui_nav_status(fb);ui_nav_back(fb,36,79);
    ui_text_vc(fb,342,107,34,documents?"管理文档":"管理图书",EPD_DRAW_ALIGN_CENTER,false);
    ui_text(fb,36,166,20,message[0]?message:summary,EPD_DRAW_ALIGN_LEFT,false);
    ui_hairline(fb,200,36,612,UI_GRAY_LIGHT);
    const char *source=documents?(s_document_filter?"TF 卡":"全部来源"):(s_filter==0?"全部来源":s_filter==1?"TF 卡":"内置");
    const char *query=documents?s_document_query:s_query;
    const char *filters[]={source,"排序",query[0]?"搜索中":"搜索"};
    for(int i=0;i<3;++i){EpdRect r=bulk_filter_rect(i);
        ui_fill_round_rect(fb,r,20,UI_GRAY_WHITE);ui_draw_round_rect(fb,r,20,0xb0);
        ui_text_vc(fb,r.x+r.width/2,r.y+r.height/2,UI_TEXT_BODY,filters[i],EPD_DRAW_ALIGN_CENTER,false);
    }
}
static void draw_management_row(uint8_t *fb,int row,const char *name,const char *detail,bool selected){
    EpdRect r=management_row(row);epd_fill_rect(r,selected?0xe0:UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){r.x,r.y+r.height,r.width,2},UI_GRAY_ITEM_LINE,fb);
    int title_px=ui_text_effective_px(UI_TEXT_BODY);
    int detail_px=ui_text_effective_px(UI_TEXT_CAPTION);
    char title[256];copy_text(title,sizeof(title),name);fit_fixed_text(title,title_px,495);
    ui_text_fixed_vc(fb,r.x+22,r.y+12+ui_text_line_height(UI_TEXT_BODY)/2,title_px,title,EPD_DRAW_ALIGN_LEFT,false);
    char subtitle[128];copy_text(subtitle,sizeof(subtitle),detail);fit_fixed_text(subtitle,detail_px,495);
    ui_text_fixed_vc(fb,r.x+22,r.y+12+ui_text_line_height(UI_TEXT_BODY)+ui_text_line_height(UI_TEXT_CAPTION)/2,detail_px,subtitle,EPD_DRAW_ALIGN_LEFT,false);
    epd_draw_circle(r.x+r.width-37,r.y+r.height/2,12,0x58,fb);
    if(selected)epd_fill_circle(r.x+r.width-37,r.y+r.height/2,7,0x38,fb);
}
static void draw_management_actions(uint8_t *fb,bool documents){
    const char *labels[]={"本页全选","清除勾选","重新扫描","删除所选","清进度","移出书架"};
    for(int i=0;i<(documents?5:6);++i){EpdRect r=batch_rect(i);
        ui_fill_round_rect(fb,r,18,UI_GRAY_WHITE);ui_draw_round_rect(fb,r,18,0xa0);
        ui_text_vc(fb,r.x+r.width/2,r.y+r.height/2,UI_TEXT_BODY,labels[i],EPD_DRAW_ALIGN_CENTER,false);
    }
    const char *nav[]={"上一页","完成","下一页"};
    for(int i=0;i<3;++i){EpdRect r=management_nav(i);ui_fill_round_rect(fb,r,20,UI_GRAY_WHITE);
        ui_text_vc(fb,r.x+r.width/2,r.y+r.height/2,UI_TEXT_BODY,nav[i],EPD_DRAW_ALIGN_CENTER,false);}
}
static void draw_bulk(uint8_t *fb,int leaf){
    char summary[96];snprintf(summary,sizeof(summary),"%d 本图书 · 已选 %u 本",s_visible_count,(unsigned)selected_count());
    draw_management_header(fb,false,summary,s_batch_message);
    for(int row=0;row<BOOK_BULK_ROWS;++row){int i=leaf*BOOK_BULK_ROWS+row;if(i>=s_visible_count)break;
        char detail[48];snprintf(detail,sizeof(detail),"%s · %s",s_shelf[i].is_flash?"内置":"TF 卡",s_shelf[i].has_progress?"已有阅读进度":"未开始阅读");
        draw_management_row(fb,row,s_shelf[i].name,detail,s_shelf[i].selected);
    }
    draw_management_actions(fb,false);draw_batch_confirmation(fb);
}
static void draw_document_bulk(app_ctx_t *ctx,uint8_t *fb){
    char summary[96];snprintf(summary,sizeof(summary),"%u 篇文档 · 已选 %u 篇",(unsigned)s_document_count,document_selected_count());
    draw_management_header(fb,true,summary,s_document_message);
    for(int row=0;row<BOOK_BULK_ROWS;++row){size_t i=(size_t)ctx->leaf*BOOK_BULK_ROWS+row;if(i>=s_document_count)break;
        document_entry_t *d=&s_documents[i];char detail[64];
        snprintf(detail,sizeof(detail),d->cleanup_pending?"清理待重试":"TF 卡 · %s · 已读 %u%%",d->date,(unsigned)d->pct);
        draw_management_row(fb,row,d->name,detail,d->selected);
    }
    draw_management_actions(fb,true);
    if(s_document_batch_confirm){
        ui_fill_round_rect(fb,(EpdRect){44,390,596,350},24,UI_GRAY_WHITE);ui_draw_round_rect(fb,(EpdRect){44,390,596,350},24,0x70);
        snprintf(summary,sizeof(summary),s_document_batch_action==3?"删除选中的 %u 篇文档？":"清除选中的 %u 篇阅读进度？",document_selected_count());
        ui_text_vc(fb,342,449,26,summary,EPD_DRAW_ALIGN_CENTER,false);
        ui_text_vc(fb,342,517,21,s_document_batch_action==3?"正文、关联插图与阅读记录将一起删除":"文档与插图保留，阅读进度归零",EPD_DRAW_ALIGN_CENTER,false);
        library_button(fb,(EpdRect){76,620,244,76},"取消",false);library_button(fb,(EpdRect){364,620,244,76},"确认",true);
    }
}
static void save_progress(void) {
    if (!s_text || !s_path[0] || !book_layout_page_count()) return;
    bool was_failed = s_save_failed;
    pending_progress_t* pending = pending_find(s_path);
    if (!pending) { s_save_failed = true; return; }
    book_progress_t p = {
        .file_size = s_file_size, .chapter = (uint16_t)s_chapter,
        .byte_off = (uint32_t)reader_page_offset(s_page),
        .px = (uint8_t)s_px, .pct = (uint8_t)percent(s_page),
        .last_open_s = 0,
    };
    if (!pending->dirty || pending->value.file_size != p.file_size || pending->value.chapter != p.chapter ||
        pending->value.byte_off != p.byte_off || pending->value.px != p.px || pending->value.pct != p.pct) {
        pending->value = p;
        pending->progress_saved = false;
    }
    pending->dirty = true;
    if (pending_flush(pending)) {
        s_unsaved = 0;
        s_save_failed = false;
        for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
    }
    else { if (!s_unsaved) s_unsaved = 1; s_save_failed = true; }
    if (was_failed != s_save_failed) invalidate_prep();
}
static void invalidate_prep(void) { s_next_page=s_prep_page=-1;s_next_generation=0;memset(&s_prep_cursor,0,sizeof(s_prep_cursor)); }

// 目录使用系统字体；切换前等待预渲染结束，回正文时恢复阅读字体与字形缓存。
// The directory uses the system face; join painting before switching and restore the reader face on return.
static void set_reader_view(book_view_t view) {
    lock_draw();
    invalidate_prep();
    if (view == READING) app_font_activate_reading();
    else app_font_activate_system();
    s_view = view;
    unlock_draw();
}

/* ---- 阅读工具层 / Reader overlays ---- */
typedef struct {
    uint32_t magic, file_size, byte_off;
    uint16_t chapter;
    char path[BOOK_STORE_PATH_MAX];
} legacy_reader_bookmark_t;

typedef struct {
    uint16_t chapter;
    uint16_t reserved;
    uint32_t byte_off;
    uint32_t saved_s;
} reader_bookmark_entry_t;

typedef struct {
    uint32_t magic, file_size;
    uint16_t count, reserved;
    char path[BOOK_STORE_PATH_MAX];
    reader_bookmark_entry_t entries[BOOKMARK_MAX];
} reader_bookmarks_t;

static void bookmark_key(const char* path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "m_%08lx", (unsigned long)hash);
}

// 从书架移除只隐藏条目，不动文件也不清进度；再次打开这本书会重新上书架。
// Removing from the shelf hides the entry only: no file or progress is touched, and opening the
// book again puts it back. 值为完整路径，命中的同键记录属于别的书时拒绝覆盖。
// The value is the full path, so a colliding key owned by a different book is never overwritten.
#define SHELF_HIDDEN_NS "rp_shelf"
static void shelf_hidden_key(const char *path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "h_%08lx", (unsigned long)hash);
}
static bool shelf_hidden_read_handle(nvs_handle_t h, const char *path) {
    char stored[BOOK_STORE_PATH_MAX];
    char key[11];
    shelf_hidden_key(path, key);
    size_t len = sizeof(stored);
    return nvs_get_str(h, key, stored, &len) == ESP_OK && strcmp(stored, path) == 0;
}
static bool shelf_hidden_load(const char *path) {
    nvs_handle_t h;
    if (nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool hidden = shelf_hidden_read_handle(h, path);
    nvs_close(h);
    return hidden;
}
static bool shelf_hidden_save(const char *path, bool hidden) {
    nvs_handle_t h;
    if (nvs_open(SHELF_HIDDEN_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11];
    shelf_hidden_key(path, key);
    char stored[BOOK_STORE_PATH_MAX];
    size_t len = sizeof(stored);
    esp_err_t existing = nvs_get_str(h, key, stored, &len);
    esp_err_t err = existing == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : existing;
    if (existing == ESP_OK && strcmp(stored, path) != 0) err = ESP_ERR_INVALID_STATE;
    // 恢复上架也校验路径，避免擦掉碰撞键属于另一书的隐藏记录。
    // Check ownership when unhiding too, so a collision cannot erase a different book removal.
    if (err == ESP_OK && hidden) err = nvs_set_str(h, key, path);
    else if (err == ESP_OK) {
        err = nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
static bool bookmark_read_handle(nvs_handle_t h, reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    memset(marks, 0, sizeof(*marks));
    char key[11]; bookmark_key(s_path, key);
    size_t size = 0;
    if (nvs_get_blob(h, key, NULL, &size) != ESP_OK) return false;
    if (size == sizeof(legacy_reader_bookmark_t)) {
        legacy_reader_bookmark_t old;
        size = sizeof(old);
        if (nvs_get_blob(h, key, &old, &size) != ESP_OK ||
            old.magic != UINT32_C(0x52504d31) || old.file_size != s_file_size ||
            strcmp(old.path, s_path)) return false;
        marks->magic = UINT32_C(0x52504d32);
        marks->file_size = old.file_size;
        marks->count = 1;
        copy_text(marks->path, sizeof(marks->path), old.path);
        marks->entries[0] = (reader_bookmark_entry_t){
            .chapter = old.chapter, .byte_off = old.byte_off,
        };
        return true;
    }
    if (size != sizeof(*marks) || nvs_get_blob(h, key, marks, &size) != ESP_OK) return false;
    return marks->magic == UINT32_C(0x52504d32) && marks->file_size == s_file_size &&
        marks->count <= BOOKMARK_MAX && !strcmp(marks->path, s_path);
}

static bool bookmark_load(reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = bookmark_read_handle(h, marks);
    nvs_close(h);
    return ok;
}
static bool bookmark_is_current(void) {
    reader_bookmarks_t marks;
    if (!bookmark_load(&marks)) return false;
    size_t offset = book_layout_page_start_offset(s_page);
    for (size_t i = 0; i < marks.count; ++i)
        if (marks.entries[i].chapter == s_chapter && marks.entries[i].byte_off == offset) return true;
    return false;
}
static size_t bookmark_count(void) {
    reader_bookmarks_t marks;
    return bookmark_load(&marks) ? marks.count : 0;
}

static void bookmark_compact(reader_bookmarks_t* marks, uint32_t selected) {
    size_t write = 0;
    for (size_t i = 0; i < marks->count; ++i)
        if (!(selected & (UINT32_C(1) << i))) marks->entries[write++] = marks->entries[i];
    marks->count = (uint16_t)write;
}
static bool bookmark_delete_selected(void) {
    if (!s_bookmark_selected || !s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    esp_err_t err = valid ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (valid) {
        bookmark_compact(&marks, s_bookmark_selected);
        char key[11]; bookmark_key(s_path, key);
        err = marks.count ? nvs_set_blob(h, key, &marks, sizeof(marks)) : nvs_erase_key(h, key);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) return false;
    s_bookmark_selected = 0;
    s_bookmark_edit = marks.count != 0;
    return true;
}
static bool bookmark_toggle(void) {
    if (!s_path[0]) return false;
    nvs_handle_t h;
    if (nvs_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11]; bookmark_key(s_path, key);
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    if (!valid) {
        memset(&marks, 0, sizeof(marks));
        marks.magic = UINT32_C(0x52504d32);
        marks.file_size = s_file_size;
        copy_text(marks.path, sizeof(marks.path), s_path);
    }
    uint32_t offset = (uint32_t)book_layout_page_start_offset(s_page);
    size_t found = marks.count;
    for (size_t i = 0; i < marks.count; ++i)
        if (marks.entries[i].chapter == s_chapter && marks.entries[i].byte_off == offset) { found = i; break; }
    bool remove = found < marks.count;
    if (remove) {
        memmove(&marks.entries[found], &marks.entries[found + 1],
                (marks.count - found - 1) * sizeof(marks.entries[0]));
        --marks.count;
    } else {
        if (marks.count == BOOKMARK_MAX) {
            memmove(&marks.entries[0], &marks.entries[1],
                    (BOOKMARK_MAX - 1) * sizeof(marks.entries[0]));
            --marks.count;
        }
        time_t now = time(NULL);
        marks.entries[marks.count++] = (reader_bookmark_entry_t){
            .chapter = (uint16_t)s_chapter, .byte_off = offset,
            .saved_s = now >= 1704067200 ? (uint32_t)now : 0,
        };
    }
    esp_err_t err = marks.count ? nvs_set_blob(h, key, &marks, sizeof(marks)) : nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && !marks.count) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        copy_text(s_reader_notice, sizeof(s_reader_notice), remove ? "书签已移除" : "书签已添加");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1600;
    } else {
        copy_text(s_reader_notice, sizeof(s_reader_notice), "书签保存失败");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 2200;
    }
    return err == ESP_OK;
}

#include "reader_controls_ui.h"

/* ---- 绘制与预渲染 / Drawing and preparation ---- */
static uint8_t inline_ink_gray(uint8_t gray) {
    // 解码器已提供灰阶；再次加深并叠加系统对比度会压死抖动前的中间色阶。
    // The decoder already supplies grayscale; extra darkening and UI contrast clip midtones.
    return gray;
}

static void draw_reader_header(uint8_t *fb) {
    ui_nav_back(fb, 36, 79);
    char header[128];
    copy_text(header, sizeof(header), s_book_title[0] ? s_book_title : s_title);
    fit_text(header, 30, 450);
    // 返回键与书名使用同一视觉中心线，字号切换不改变对齐。
    // Back and title share one visual centerline across font changes.
    ui_text_vc(fb, UI_LOCK_WIDTH / 2, 114, 30, header, EPD_DRAW_ALIGN_CENTER, false);
    ui_hairline(fb, 151, 36, 612, UI_GRAY_LIGHT);
}
static int reader_image_slots(size_t page) {
    int count = book_layout_page_image_count(page);
    return count ? count : book_layout_page_image(page) >= 0 ? 1 : 0;
}
static bool reader_image_slot(size_t page, int slot, int *index, int *y, int *width, int *height) {
    if (book_layout_page_image_count(page))
        return book_layout_page_image_at(page, slot, index, y, width, height);
    int image = book_layout_page_image(page);
    if (slot != 0 || image < 0) return false;
    EpdRect body = body_rect();
    if (index) *index = image;
    if (y) *y = 0;
    if (width) *width = body.width;
    if (height) *height = body.height;
    return true;
}
static void draw_reader_images(uint8_t *fb, size_t page, EpdRect body) {
    // 位图和布局代次必须匹配；预渲染另一页不能复用当前页插图。
    // Match page and layout generation; a prefetched page must never borrow current-page bitmaps.
    if (!app_settings_reader_hide_images() && page == s_page &&
        s_page_images_for == page && s_page_images_generation == book_layout_generation()) {
        for (int i = 0; i < reader_image_slots(page); ++i) {
            int y, width, height;
            if (!reader_image_slot(page, i, NULL, &y, &width, &height)) continue;
            const reader_image_t *image = s_page_images && i < s_page_image_count ? &s_page_images[i] : NULL;
            if (image && image->gray) {
                int left = body.x + (body.width - image->width) / 2;
                int top = body.y + y + (height - image->height) / 2;

                for (int iy = 0; iy < image->height; ++iy)
                    for (int ix = 0; ix < image->width; ++ix)
                        epd_draw_pixel(left + ix, top + iy,
                            ui_image_dither_gray(inline_ink_gray(image->gray[(size_t)iy * image->width + ix]),
                                                 left + ix, top + iy), fb);
            } else if (height >= UI_PX_CAPTION + 4 && width >= 70) {
                char notice[64]; copy_text(notice, sizeof(notice), "此插图暂无法显示");
                fit_text(notice, UI_PX_CAPTION, width - 8);
                ui_text_vc(fb, body.x + body.width / 2, body.y + y + height / 2,
                           UI_PX_CAPTION, notice, EPD_DRAW_ALIGN_CENTER, false);
            }
        }
    }
}
#ifdef PICO_HOST_SIMULATOR
static unsigned s_sim_reader_draws;
#endif
static void draw_reader_outer(uint8_t *fb,size_t page) {
    if (!(s_reader_fullscreen && s_reader_immersive)) ui_nav_status(fb);
    if (!s_reader_fullscreen) draw_reader_header(fb);
    EpdRect body = body_rect();
    if (page == 0 && s_chapter_lead_height && s_chapter_heading_title[0]) {
        char heading[128]; copy_text(heading, sizeof(heading), s_chapter_heading_title);
        fit_text(heading, 49, ui_content_width());
        ui_text(fb, UI_LOCK_WIDTH / 2, body.y + 15, 27, s_chapter_heading_label, EPD_DRAW_ALIGN_CENTER, false);
        ui_text(fb, UI_LOCK_WIDTH / 2, body.y + 87, 49, heading, EPD_DRAW_ALIGN_CENTER, false);
        ui_hairline(fb, body.y + 174, 210, 264, UI_GRAY_LIGHT);
    }
    if (book_kind() == BOOK_KIND_EPUB) draw_reader_images(fb, page, body);
    else if (!app_settings_reader_hide_images() && book_layout_page_image(page) == s_inline_index && s_inline_gray) {
        int left = body.x + (body.width - (int)s_inline_w) / 2;
        int top = body.y + (body.height - (int)s_inline_h) / 2;
        for (unsigned y = 0; y < s_inline_h; ++y)
            for (unsigned x = 0; x < s_inline_w; ++x)
                epd_draw_pixel(left + x, top + y,
                    ui_image_dither_gray(inline_ink_gray(s_inline_gray[y * s_inline_w + x]),
                                         left + (int)x, top + (int)y), fb);
    } else if (!app_settings_reader_hide_images() && book_layout_page_image(page) >= 0) {
        ui_text_vc(fb, body.x + body.width / 2, body.y + body.height / 2,
                   UI_PX_CAPTION, "此插图暂无法显示", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_reader_fullscreen) {
        // Full-book progress uses the last few screen rows and no footer text.
        EpdRect track = {36, READER_FULLSCREEN_PROGRESS_TOP + 3, UI_LOCK_WIDTH - 72, 3};
        ui_fill_round_rect(fb, track, 1, 0x90);
        track.width = track.width * (int)percent(page) / 100;
        if (track.width) ui_fill_round_rect(fb, track, 1, UI_GRAY_BLACK);
    } else {
        EpdRect track = progress_rect();
        EpdRect bar = {track.x, track.y + 16, track.width, 12};
        ui_fill_round_rect(fb, bar, 5, UI_GRAY_LIGHT);
        bar.width = bar.width * (int)percent(page) / 100;
        if (bar.width) ui_fill_round_rect(fb, bar, 5, UI_GRAY_BLACK);
        char chapter_name[72] = {0};
        reader_footer_chapter_name(chapter_name, sizeof(chapter_name));
        const int footer_px = 24;
        char progress[48];
        snprintf(progress, sizeof(progress), "%u/%u · 全书 %u%%",
                 (unsigned)page + 1, (unsigned)book_layout_page_count(), percent(page));
        char chapter_line[192];
        snprintf(chapter_line, sizeof(chapter_line), "%s%s",
                 s_save_failed ? "未保存 · " : "", chapter_name);
        fit_fixed_text(chapter_line, footer_px,
                       track.width - ui_text_fixed_width_px(footer_px, progress) - 16);
        // 页码和全书进度靠右固定，长章节名仅截短左侧；字号不跟随正文设置。
        // Keep page and book progress right-aligned; trim only long chapter titles and ignore body font settings.
        ui_text_fixed(fb, track.x, track.y + 35, footer_px, chapter_line, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_fixed(fb, track.x + track.width, track.y + 35, footer_px,
                      progress, EPD_DRAW_ALIGN_RIGHT, false);
    }
    if (s_reader_notice[0]) {
        EpdRect notice = {218, 160, 248, 48};
        ui_fill_round_rect(fb, notice, 24, 0xd0);
        ui_text_vc(fb, 342, 184, 19, s_reader_notice, EPD_DRAW_ALIGN_CENTER, false);
    }
    if(s_reader_panel==READER_PANEL_TOOLS)ui_fade_region(fb,(EpdRect){0,0,684,reader_control_top()},0x99);
    draw_reader_panel(fb);
}
static void draw_reader(uint8_t *fb,size_t page) {
#ifdef PICO_HOST_SIMULATOR
    ++s_sim_reader_draws;
#endif
    ui_clear_page(fb);book_layout_draw_page(fb,page,body_rect(),s_px);draw_reader_outer(fb,page);
}
// 在行/表格块边界停止，屏幕推送结束即可交还字体锁；4ms是软空闲预算。/ Stop at row/table boundaries after output; 4ms is a soft idle budget.
#define BOOK_PREP_RESERVE (1024u*1024u)
static int reader_prefetch_step(void) {
    if(!s_next_fb||s_prep_page<0)return -1;
    if(!s_prep_cursor.started)memset(s_next_fb,255,fb_bytes());
    int result=book_layout_draw_slice(s_next_fb,(size_t)s_prep_page,body_rect(),s_px,&s_prep_cursor);
    if(result<0)invalidate_prep();
    else if(result>0){draw_reader_outer(s_next_fb,(size_t)s_prep_page);s_next_page=s_prep_page;s_next_generation=book_layout_generation();}
    return result;
}
static void prep_task(void *arg) {
    (void)arg;
    for(;;){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);lock_draw();
        while(!atomic_load_explicit(&s_prep_stop,memory_order_acquire))if(reader_prefetch_step()!=0)break;
        unlock_draw();xSemaphoreGive(s_prep_done);
    }
}
static void ensure_prep(void) {
    if(!s_draw_lock)s_draw_lock=xSemaphoreCreateMutex();
    if(!s_prep_done)s_prep_done=xSemaphoreCreateBinary();
    size_t need=fb_bytes();
    if(!s_next_fb&&heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>need+BOOK_PREP_RESERVE)
        s_next_fb=heap_caps_aligned_alloc(16,need,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_prep_task&&s_draw_lock&&s_prep_done&&s_next_fb&&xTaskCreatePinnedToCore(prep_task,"book_prep",12*1024,NULL,3,&s_prep_task,1)!=pdPASS)s_prep_task=NULL;
}
static bool kick_prep(void) {
    if(s_view!=READING||!s_text||s_toolbar||s_clear_confirm||s_page+1>=book_layout_page_count()||
       book_layout_page_image(s_page+1)>=0||book_layout_page_image_count(s_page+1)>0)return false;
    int target=(int)s_page+1;
    if(s_next_page==target&&s_next_generation==book_layout_generation())return false;
    if(!s_prep_task||!s_next_fb)ensure_prep();
    if(!s_prep_task||!s_next_fb)return false;
    if(s_prep_page!=target||s_prep_cursor.generation!=book_layout_generation()){
        invalidate_prep();s_prep_page=target;
    }
    xSemaphoreTake(s_prep_done,0);atomic_store_explicit(&s_prep_stop,false,memory_order_release);xTaskNotifyGive(s_prep_task);return true;
}
static void reader_prefetch_end(bool started) {
    if(!started)return;
    atomic_store_explicit(&s_prep_stop,true,memory_order_release);xSemaphoreTake(s_prep_done,portMAX_DELAY);
}
static bool reader_prefetch_resume(size_t page) {
    if(!s_next_fb||s_prep_page!=(int)page||!s_prep_cursor.started||s_prep_cursor.generation!=book_layout_generation())return false;
    while(reader_prefetch_step()==0){}
    return s_next_page==(int)page&&s_next_generation==book_layout_generation();
}
static void reader_prefetch_idle(app_ctx_t *ctx) {
    if(s_view!=READING||!s_text||s_toolbar||s_clear_confirm||ctx->consumed||ctx->request_app||ctx->request_menu||
       (ctx->touch&&ctx->touch->touched)||s_prep_page!=(int)s_page+1||s_next_page==s_prep_page)return;
    lock_draw();int64_t deadline=esp_timer_get_time()+4000;
    for(unsigned row=0;row<8;++row)if(reader_prefetch_step()!=0||esp_timer_get_time()>=deadline)break;
    unlock_draw();
}
static size_t current_toc_position(void) {
    size_t count = book_navigation_count();
    if(book_kind()==BOOK_KIND_MARKDOWN){size_t best=0,offset=book_layout_page_start_offset(s_page);for(size_t i=0;i<count;i++){if(book_navigation_source_offset(i)>offset)break;best=i;}return best;}
    if (s_selected_toc < count && book_navigation_chapter(s_selected_toc) == s_chapter)
        return s_selected_toc;
    size_t selected = SIZE_MAX;
    size_t best_chapter = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t chapter = book_navigation_chapter(i);
        if (chapter > s_chapter) break;
        if (selected == SIZE_MAX || chapter > best_chapter) { selected = i; best_chapter = chapter; }
    }
    return selected;
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    ui_text_set_system_scale(s_view != READING);
    lock_draw();
    if (s_document_zoom) {draw_document_zoom(fb);unlock_draw();return;}
    if (s_sort_open) {draw_content_sort(fb);unlock_draw();return;}
    if (s_view == DOCUMENT_BULK) {draw_document_bulk(ctx,fb);unlock_draw();return;}
    if (s_view == DOCUMENTS) {draw_documents(ctx,fb);unlock_draw();return;}
    if (s_view == EDIT) { draw_editor(fb); unlock_draw(); return; }
    if (s_view == SEARCH) { draw_search(fb); unlock_draw(); return; }
    if (s_view == IMPORT) { draw_import(fb); unlock_draw(); return; }
    if (s_view == BULK) { draw_bulk(fb, ctx->leaf); unlock_draw(); return; }
    if (s_view == TOC) {
        book_toc_render(fb, s_book_title[0] ? s_book_title : s_title,
                        book_navigation_count(), current_toc_position(), ctx->leaf, s_message);
        if (s_toc_jump_open)
            book_toc_render_jump(fb, book_navigation_count(), s_toc_jump_percent);
        else reader_control_footer(fb);
        unlock_draw();
        return;
    }
    if(s_view==READING&&s_text)draw_reader(fb,s_page);
    else{
        // 详情复用书架背景，不再绘制独立详情页的管理栏。/ Details reuse the shelf background without a separate management page.
        ui_clear_page(fb);epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,UI_NAV_TOP},UI_GRAY_WHITE,fb);
        ui_nav_status(fb);draw_content_tabs(fb,false);draw_content_actions(fb);
        ui_hairline(fb,195,36,612,0x68);
        if(s_message[0]||s_shelf_warning[0])ui_text(fb,UI_MARGIN,1023,17,s_message[0]?s_message:s_shelf_warning,EPD_DRAW_ALIGN_LEFT,false);
        if(!s_visible_count&&s_count)ui_text(fb,UI_MARGIN,308,UI_PX_CAPTION,"当前筛选没有图书",EPD_DRAW_ALIGN_LEFT,false);
        if(!s_message[0]||s_visible_count){
            int rows=shelf_rows();
            for(int row=0;row<rows;++row){int i=ctx->leaf*rows+row;if(i>=s_visible_count)break;
                EpdRect r=row_rect(row);
                char name[128];copy_text(name,sizeof(name),s_shelf[i].name);
                draw_shelf_cover(fb,r,row,name);
                draw_shelf_title(fb,r,name);
                EpdRect image=shelf_image_rect(r);EpdRect track={image.x,image.y+image.height+8,image.width,4};
                epd_fill_rect(track,0xc0,fb);unsigned pct=s_shelf[i].pct>100?100:s_shelf[i].pct;
                if(pct){EpdRect fill=track;fill.width=(track.width*(int)pct+99)/100;epd_fill_rect(fill,UI_GRAY_BLACK,fb);}
            }
        }
        draw_shelf_pager(fb,s_visible_count,ctx->leaf,leaves());ui_nav_draw(fb,1);
        if(s_view==MANAGE)draw_manage(fb);
    }
    unlock_draw();
}
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return true;
    if (s_presented_view != (int)s_view && redraw == APP_REDRAW_AREA) redraw = APP_REDRAW_PAGE;
    if((s_view==EDIT||s_view==SEARCH)&&redraw==APP_REDRAW_AREA){
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_text_input_area()));
        return true;
    }
    if (s_view == SHELF || s_view == MANAGE) prepare_covers(ctx);
    int64_t start = esp_timer_get_time();
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) render(ctx, ctx->fb);
    bool prep = kick_prep();
    int64_t drawn = esp_timer_get_time();
    enum EpdDrawError err;
    bool image_gray_refresh = s_reader_image_refresh_pending && s_view == READING &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm;
    if (image_gray_refresh && !s_reader_cleanup && s_presented_view == (int)READING) {
        // 灰阶只驱动正文，避免每张插图都把整屏先清白再闪回。
        // Drive the grayscale body from the current frame instead of clearing the whole screen to white.
        err = update_display_area_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16, reader_area());
        if (err == EPD_DRAW_SUCCESS && s_reader_footer_pending)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, progress_rect());
        if (err == EPD_DRAW_SUCCESS && s_reader_fullscreen)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
    }
    else if (image_gray_refresh) err = update_display_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16);
    else if (redraw == APP_REDRAW_FULL || s_reader_cleanup) err = update_display_full(ctx->hl);
    else if(s_view==READING&&s_toolbar)err=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,redraw==APP_REDRAW_AREA?s_area:(EpdRect){0,0,684,1216});
    else if (redraw == APP_REDRAW_AREA) {
        // 翻页动画与页脚刷新独立：全屏没有页脚，仍使用用户选择的水波纹。
        // The turn effect is independent of the footer: full-screen turns still use the selected water effect.
        err = s_water_turn_pending
            ? update_display_water_turn(ctx->hl, s_area, s_water_turn_dir)
            : s_reader_fullscreen && s_mode == MODE_GL16
                ? update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, s_mode, s_area)
                : update_display_area_with(ctx->hl, &E0470_WAVEFORM, s_mode, s_area);
        if (s_reader_fullscreen && err == EPD_DRAW_SUCCESS) {
            // 百分比不变时差分直接跳过；变化时只驱动最底部的少量像素。
            // Skip unchanged percentages; otherwise update only the small bottom strip.
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
        }
        if (s_reader_footer_pending) {
            // 普通翻页页脚只驱动变化像素；全刷由上方整屏分支一次完成。
            // Ordinary turns drive only changed footer pixels; the full-screen branch handles cleanup at once.
            err = (enum EpdDrawError)(err | update_display_area_with(ctx->hl, &E0470_FOLLOW_WAVEFORM,
                                                                     MODE_DU, progress_rect()));
            ESP_LOGI(TAG, "reader regions body_h=%d footer=diff pct=%u", s_area.height, percent(s_page));
        }
    }
    else err = s_view == READING
        ? update_display_mode_diff(ctx->hl, APP_PAGE_REFRESH_MODE)
        : update_display_fast_page(ctx->hl);
    int64_t displayed = esp_timer_get_time();
    reader_prefetch_end(prep);
    if (redraw == APP_REDRAW_AREA && s_mode == MODE_DU &&
        !s_reader_cleanup && !image_gray_refresh) {
        s_du_area = s_du_count ? ui_rect_union(s_du_area, s_area) : s_area;
        ++s_du_count;
        s_du_ms = esp_timer_get_time() / 1000;
    } else s_du_count = 0;
    ESP_LOGI(TAG, "present draw=%lld display=%lld join=%lld ms", (drawn - start) / 1000,
             (displayed - drawn) / 1000, (esp_timer_get_time() - displayed) / 1000);
    guard_draw_result(ctx->hl, err);
    s_reader_cleanup = false;
    s_reader_image_refresh_pending = false;
    s_presented_view = (int)s_view;
    s_reader_footer_pending = false;
    s_water_turn_pending = false;
    s_mode = MODE_GL16;
    return true;
}
static app_redraw_t paint_reading(app_ctx_t* ctx, enum EpdDrawMode mode) {
    s_water_turn_pending = false;
    int64_t started = esp_timer_get_time();
    prepare_inline_image();
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() && (book_kind()==BOOK_KIND_EPUB ?
        reader_image_slots(s_page)>0 : s_inline_gray && book_layout_page_image(s_page)==s_inline_index);
    lock_draw();
    bool cached = !s_toolbar && !s_clear_confirm && s_next_fb &&
        ((s_next_page==(int)s_page&&s_next_generation==book_layout_generation())||reader_prefetch_resume(s_page));
    if (cached) {
        memcpy(ctx->fb,s_next_fb,fb_bytes());
        if(!(s_reader_fullscreen&&s_reader_immersive))ui_nav_status(ctx->fb);
    }
    else draw_reader(ctx->fb, s_page);
    unlock_draw();
    ESP_LOGI(TAG, "paint cached=%d ms=%lld", cached, (esp_timer_get_time() - started) / 1000);
    s_area = reader_area();
    s_reader_footer_pending = !s_reader_fullscreen;
    s_mode = mode;
    return APP_REDRAW_AREA;
}

/* ---- 文件与进度 / Files and progress ---- */
typedef struct {
    size_t chapter;
    char** images;
    size_t count;
} reader_dims_ctx_t;
static reader_dims_ctx_t s_reader_dims_ctx;

// 按 PR9 通过完整资源解包测量，兼容 SVG 包装及较晚的 JPEG 尺寸头。/ Use PR9's full resource measurement for SVG wrappers and late JPEG headers.
static bool reader_image_dims(void* ctx, int image, int* width, int* height) {
    const reader_dims_ctx_t* c = (const reader_dims_ctx_t*)ctx;
    if (!c || image < 0 || (size_t)image >= c->count) return false;
    uint8_t* encoded = NULL;
    size_t size = 0;
    bool png = false;
    if (book_chapter_image(c->chapter, c->images[image], &encoded, &size, &png) != ESP_OK) return false;
    unsigned w = 0, h = 0;
    const bool ok = book_image_dimensions(encoded, size, png, &w, &h) && w && h;
    free(encoded);
    if (!ok) return false;
    if (width) *width = (int)w;
    if (height) *height = (int)h;
    return true;
}
static void flush_ticket_stats(void) {
    uint32_t seconds = s_stats_pending_ms / 1000;
    if (!seconds && !s_stats_pending_turns) return;
    if (book_ticket_record(seconds, s_stats_pending_turns) == ESP_OK) {
        s_stats_pending_ms -= seconds * 1000;
        s_stats_pending_turns = 0;
    }
}
static void track_ticket_stats(app_ctx_t* ctx) {
    if (s_view != READING || !s_text) { s_stats_last_ms = ctx->now_ms; return; }
    if (s_stats_last_ms > 0 && ctx->now_ms >= s_stats_last_ms &&
        ctx->now_ms - s_stats_last_ms <= 5000 &&
        ctx->now_ms - s_stats_activity_ms <= 300000 && !s_toolbar) {
        uint32_t elapsed = (uint32_t)(ctx->now_ms - s_stats_last_ms);
        s_stats_pending_ms += elapsed;
        s_session_read_ms += elapsed;
    }
    s_stats_last_ms = ctx->now_ms;
    if (s_stats_pending_ms >= 30000) flush_ticket_stats();
}
static void free_book(void) {
    s_document_zoom=false;free(s_zoom_gray);s_zoom_gray=NULL;memset(&s_document_image,0,sizeof(s_document_image));
    flush_ticket_stats();
    e0470_page_turn_release();
    s_water_turn_pending = false;
    pending_progress_t* pending = pending_find(s_path);
    if (pending && !pending->dirty) pending_discard(s_path);
    invalidate_prep();
    book_layout_set_image_dims(NULL, NULL);
    s_reader_dims_ctx = (reader_dims_ctx_t){0};
    release_page_images();
    book_layout_free();
    book_layout_set_chapter_lead(0, 0);
    s_chapter_lead_skip = s_chapter_lead_height = 0;
    s_chapter_heading_title[0] = s_chapter_heading_label[0] = 0;
    free(s_text);
    free(s_blocks);
    free(s_text_styles);
    free(s_text_cells); s_text_cells = NULL; s_text_cell_count = 0;
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = NULL; s_image_count = 0;
    free(s_inline_gray); s_inline_gray = NULL; s_inline_index = -1;
    s_reader_image_refresh_pending = false;
    s_blocks = NULL;
    s_text_styles = NULL; s_text_style_count = 0;
    book_layout_set_markdown(false,NULL,NULL,0);
    s_block_count = 0;
    s_text = NULL;
    s_text_len = 0;
    s_selected_toc = SIZE_MAX;
    s_jump_offset = SIZE_MAX;
    book_close();
    s_path[0] = 0;
    s_book_title[0] = 0;
    app_font_activate_system();
}
static void release_page_images(void) {
    for (int i = 0; i < s_page_image_count; ++i) free(s_page_images[i].gray);
    free(s_page_images); s_page_images = NULL; s_page_image_count = 0;
    s_page_images_for = SIZE_MAX;
}
static void prepare_epub_images(void) {
    if (app_settings_reader_hide_images()) { release_page_images(); return; }
    uint32_t generation = book_layout_generation();
    if (s_page_images_for == s_page && s_page_images_generation == generation) return;
    int count = reader_image_slots(s_page);
    // 纯文字页保留预渲染结果，只有插图页需重画。/ Preserve prefetched text frames; only image pages require repainting.
    if (count) {invalidate_prep();free(s_next_fb);s_next_fb=NULL;}
    release_page_images();
    s_page_images_for = s_page; s_page_images_generation = generation;
    if (!count || count > (int)HTML_TEXT_MAX_BLOCKS) return;
    s_page_images = heap_caps_calloc((size_t)count, sizeof(*s_page_images), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_page_images) return;
    s_page_image_count = count;
    // 逐张解码并立即释放压缩数据；全页灰阶位图的总面积不超过正文区域。
    // Decode serially and release each encoded buffer; the page's bitmap area stays within its body.
    size_t pixels_left = (size_t)body_rect().width * body_rect().height;
    for (int i = 0; i < count; ++i) {
        int index = -1, width = 0, height = 0;
        if (!reader_image_slot(s_page, i, &index, NULL, &width, &height) ||
            index < 0 || (size_t)index >= s_image_count || width <= 0 || height <= 0) continue;
        uint8_t *encoded = NULL; size_t size = 0; bool png = false;
        esp_err_t err = book_chapter_image(s_chapter, s_images[index], &encoded, &size, &png);
        if (err != ESP_OK) { free(encoded); continue; }
        unsigned source_w = 0, source_h = 0;
        if (book_image_dimensions(encoded, size, png, &source_w, &source_h) && source_w && source_h) {
            // 回退槽也使用真实宽高等比缩小，避免尺寸探测失败时拉伸图片。
            // Aspect-fit even fallback slots from decoded dimensions, avoiding stretched unknown-size images.
            uint64_t w = source_w, h = source_h;
            if (w > (unsigned)width) { h = h * width / w; w = width; }
            if (h > (unsigned)height) { w = w * height / h; h = height; }
            width = w ? (int)w : 1; height = h ? (int)h : 1;
            size_t pixels = (size_t)width * height;
            uint8_t *gray = pixels <= pixels_left ? heap_caps_malloc(pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
            if(!gray&&pixels<=pixels_left){lock_draw();ttf_font_cache_clear_all();unlock_draw();gray=heap_caps_malloc(pixels,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
            if (gray && book_image_grayscale(encoded, size, png, width, height, gray)) {
                s_page_images[i] = (reader_image_t){gray, width, height};
                pixels_left -= pixels;
            } else free(gray);
        }
        free(encoded);
        vTaskDelay(1);
    }
}
static void prepare_inline_image(void) {
    if (book_kind() == BOOK_KIND_EPUB) { prepare_epub_images(); return; }
    release_page_images();
    if (app_settings_reader_hide_images()) return;
    int index = book_layout_page_image(s_page);
    if (index < 0 || index == s_inline_index || (size_t)index >= s_image_count) return;
    invalidate_prep();
    free(s_inline_gray); s_inline_gray = NULL;
    s_inline_index = index;
    memset(&s_document_image,0,sizeof(s_document_image));
    if(book_kind()==BOOK_KIND_MARKDOWN){
        char path[512];if(!book_markdown_attachment(s_images[index],path,sizeof(path))||!document_image_open(path,&s_document_image))return;
        EpdRect body=body_rect();bool tall=(uint64_t)s_document_image.height*684>(uint64_t)s_document_image.width*1216;
        double scale_x=(double)(tall?64:body.width)/s_document_image.width,scale_y=(double)body.height/s_document_image.height;
        double scale=scale_x<scale_y?scale_x:scale_y;if(scale>1)scale=1;
        s_inline_w=(unsigned)(s_document_image.width*scale);s_inline_h=(unsigned)(s_document_image.height*scale);if(!s_inline_w)s_inline_w=1;if(!s_inline_h)s_inline_h=1;
        s_inline_gray=malloc((size_t)s_inline_w*s_inline_h);
        if(s_inline_gray&&!document_image_sample(&s_document_image,0,s_document_image.height,s_inline_w,s_inline_h,s_inline_gray)){free(s_inline_gray);s_inline_gray=NULL;}
        return;
    }
    uint8_t *encoded = NULL; size_t size = 0; bool png = false;
    esp_err_t image_err = book_chapter_image(s_chapter, s_images[index], &encoded, &size, &png);
    if (image_err != ESP_OK) {
        ESP_LOGW(TAG, "inline image load chapter=%u index=%d: %s", (unsigned)s_chapter,
                 index, esp_err_to_name(image_err));
        return;
    }
    unsigned width = 0, height = 0;
    EpdRect body = body_rect();
    if (book_image_dimensions(encoded, size, png, &width, &height)) {
        double scale_x = (double)body.width / width, scale_y = (double)body.height / height;
        double scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale > 1.0) scale = 1.0;
        unsigned out_w = (unsigned)(width * scale), out_h = (unsigned)(height * scale);
        if (!out_w) out_w = 1;
        if (!out_h) out_h = 1;
        uint8_t *gray = heap_caps_malloc((size_t)out_w * out_h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(!gray){lock_draw();ttf_font_cache_clear_all();unlock_draw();gray=heap_caps_malloc((size_t)out_w*out_h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
        if (gray && book_image_grayscale(encoded, size, png, out_w, out_h, gray)) {
            s_inline_gray = gray; s_inline_w = out_w; s_inline_h = out_h;
        } else { free(gray); ESP_LOGW(TAG, "inline image decode chapter=%u index=%d", (unsigned)s_chapter, index); }
    } else ESP_LOGW(TAG, "inline image dimensions chapter=%u index=%d", (unsigned)s_chapter, index);
    free(encoded);
}
static bool load_chapter_at(app_ctx_t* ctx, size_t chapter, size_t offset,
                            bool last_page, const char *anchor, size_t source_offset) {
    if (chapter != s_chapter || (!anchor && source_offset == SIZE_MAX)) s_selected_toc = SIZE_MAX;
    html_text_t loaded = {0};
    size_t anchor_offset = 0;
    esp_err_t err = book_chapter_load_blocks_target(chapter, anchor, source_offset,
                                                    &anchor_offset, &loaded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chapter load failed: %s", esp_err_to_name(err));
        copy_text(s_message, sizeof(s_message), "加载失败，请检查存储卡后重试");
        return false;
    }
    char heading[128] = {0}, label[32] = {0}, toc_title[128] = {0};
    size_t lead_skip = 0;
    unsigned lead_height = 0;
    if (book_kind() == BOOK_KIND_EPUB && loaded.len) {
        // 已识别的正文编号题头优先；没有时沿用书内导航或备用标题。
        // Prefer a recognized numbered body heading, then authored navigation or a fallback title.
        if (book_chapter_title(chapter, toc_title, sizeof(toc_title)) != ESP_OK)
            snprintf(toc_title, sizeof(toc_title), "第 %u 节", (unsigned)chapter + 1);
        copy_text(heading, sizeof(heading), toc_title);
        char *chapter_mark = strstr(heading, "章");
        if (chapter_mark && chapter_mark - heading < 24) {
            char *after = chapter_mark + strlen("章");
            while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
                   !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、"))) {
                if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
                else after += strlen("：");
            }
            if (*after && (size_t)(chapter_mark + strlen("章") - heading) < sizeof(label)) {
                size_t label_len = (size_t)(chapter_mark + strlen("章") - heading);
                memcpy(label, heading, label_len); label[label_len] = 0;
                memmove(heading, after, strlen(after) + 1);
            }
        }
        size_t skip_blocks = 0;
        while (skip_blocks < loaded.count && skip_blocks < 2 &&
               loaded.blocks[skip_blocks].heading && loaded.blocks[skip_blocks].image < 0) {
            char body_heading[128] = {0};
            size_t len = loaded.blocks[skip_blocks].len;
            if (len >= sizeof(body_heading)) break;
            memcpy(body_heading, loaded.utf8 + loaded.blocks[skip_blocks].offset, len);
            while (len && (body_heading[len - 1] == ' ' || body_heading[len - 1] == '\t')) body_heading[--len] = 0;
            const char *trimmed = body_heading;
            while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
            if (strcmp(trimmed, toc_title) && strcmp(trimmed, heading) &&
                (!label[0] || strcmp(trimmed, label))) break;
            ++skip_blocks;
        }
        if (skip_blocks) lead_skip = skip_blocks < loaded.count ? loaded.blocks[skip_blocks].offset : loaded.len;
        if (skip_blocks < loaded.count && loaded.blocks[skip_blocks].image >= 0) lead_skip = 0;
        else
            lead_height = body_rect().height > 214 ? 214 : 0;
    }
    size_t old_lead_skip = s_chapter_lead_skip;
    unsigned old_lead_height = s_chapter_lead_height;
    reader_dims_ctx_t old_dims_ctx = s_reader_dims_ctx;
    lock_draw();
    invalidate_prep();
    book_layout_set_chapter_lead(lead_skip, lead_height);
    s_reader_dims_ctx = (reader_dims_ctx_t){chapter, loaded.images, loaded.image_count};
    book_layout_set_image_dims(book_kind()==BOOK_KIND_EPUB ? reader_image_dims : NULL, &s_reader_dims_ctx);
    book_layout_set_markdown(book_kind()==BOOK_KIND_MARKDOWN,s_md_sizes+1,loaded.styles,loaded.style_count);
    book_layout_set_markdown_tables(loaded.cells, loaded.cell_count);
    bool ok = book_layout_build_blocks(loaded.utf8, loaded.len, loaded.blocks, loaded.count, body_rect(), s_px);
    if (!ok) {
        s_reader_dims_ctx = old_dims_ctx;
        book_layout_set_markdown(book_kind()==BOOK_KIND_MARKDOWN,s_md_sizes+1,s_text_styles,s_text_style_count);
        book_layout_set_markdown_tables(s_text_cells, s_text_cell_count);
        html_text_free(&loaded);
        book_layout_set_chapter_lead(old_lead_skip, old_lead_height);
        bool restored = s_text && book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            ctx->leaf = 0;
        }
        copy_text(s_message, sizeof(s_message), "排版失败：内存不足或章节过长");
        return false;
    }
    free(s_text);
    free(s_blocks);
    free(s_text_styles);
    s_text_styles = loaded.styles; s_text_style_count = loaded.style_count;
    free(s_text_cells);
    s_text_cells = loaded.cells; s_text_cell_count = loaded.cell_count;
    s_text = loaded.utf8;
    s_text_len = loaded.len;
    s_blocks = loaded.blocks;
    s_block_count = loaded.count;
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = loaded.images; s_image_count = loaded.image_count;
    free(s_inline_gray); s_inline_gray = NULL; s_inline_index = -1;
    s_chapter = chapter;
    s_chapter_lead_skip = lead_skip;
    s_chapter_lead_height = lead_height;
    copy_text(s_chapter_heading_title, sizeof(s_chapter_heading_title), heading);
    copy_text(s_chapter_heading_label, sizeof(s_chapter_heading_label), label);
    s_page = last_page ? book_layout_page_count() - 1 :
             book_layout_page_for_offset(anchor || source_offset != SIZE_MAX ? anchor_offset : offset);
    s_jump_offset = anchor || source_offset != SIZE_MAX ? anchor_offset : SIZE_MAX;
    s_jump_page = s_page;
    if (book_chapter_title(chapter, s_title, sizeof(s_title)) != ESP_OK)
        snprintf(s_title, sizeof(s_title), "第 %u 节", (unsigned)chapter + 1);
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    s_message[0] = 0;
    unlock_draw();
    prepare_inline_image();
    return true;
}
static bool load_chapter(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page) {
    return load_chapter_at(ctx, chapter, offset, last_page, NULL, SIZE_MAX);
}
// 关闭图片时跳过纯插图页，避免出现空白翻页。/ Skip illustration-only pages when images are disabled.
static void skip_hidden_image_pages(app_ctx_t *ctx, int direction) {
    if (!app_settings_reader_hide_images()) return;
    for (int guard = 0; guard < 128 && book_layout_page_image(s_page) >= 0; ++guard) {
        if (direction > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
        else if (direction < 0 && s_page > 0) --s_page;
        else if (direction > 0 && s_chapter + 1 < book_chapter_count()) {
            if (!load_chapter(ctx, s_chapter + 1, 0, false)) break;
        } else if (direction < 0 && s_chapter > 0) {
            if (!load_chapter(ctx, s_chapter - 1, 0, true)) break;
        } else break;
    }
}
static bool open_book(app_ctx_t* ctx, const char* path) {
    if (document_store_sync_recover() != ESP_OK) {
        snprintf(s_message, sizeof(s_message), "同步恢复尚未完成，请重新进入同步页");
        return false;
    }
    bool is_document=strrchr(path,'.')&&!strcasecmp(strrchr(path,'.'),".md");
    s_reader_return_home = false;
    if (!strncmp(path, "/sdcard/", 8)) {
        read_pico_sd_info_t sd = {0};
        read_pico_sd_get_info(&sd);
        if (!sd.present || !sd.mounted) {
            copy_text(s_message, sizeof(s_message), "TF 卡不可用，请重新挂载后打开");
            return false;
        }
    }
    // 自动续读也不能绕过同路径旧文件的清理重试。
    // Automatic resume must also finish cleanup of the old file at this path.
    if (delete_retry_find(path)) {
        copy_text(s_message, sizeof(s_message), "文件已删除，进度清理失败，请重试");
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        copy_text(s_message, sizeof(s_message), "文件不可读，请检查 TF 卡和书籍文件");
        return false;
    }
    if (strncmp(path, "/flash/", 7) == 0 && st.st_size > BOOK_STORE_FLASH_FILE_MAX) {
        copy_text(s_message, sizeof(s_message), "内置存储单本限 1 MB");
        return false;
    }
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    // 书架封面和上一本书的预渲染缓冲可以重建；先释放，再解析 EPUB。
    // Cover and previous-page caches are rebuildable; release them before EPUB parsing.
    invalidate_covers();
    lock_draw();
    invalidate_prep();
    free(s_next_fb);
    s_next_fb = NULL;
    unlock_draw();
    lock_draw();ttf_font_cache_clear_all();unlock_draw();
    ESP_LOGI(TAG, "open start wifi=%d internal=%u/%u psram=%u/%u", network.network_ready,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法保留待保存进度");
        return false;
    }
    save_progress();
    free_book();
    s_reader_fullscreen = app_settings_reader_fullscreen();
    s_reader_immersive = app_settings_reader_immersive();
    app_font_set_document_mode(is_document);
    if(is_document&&!content_markdown_font()[0]){const char *initial=app_settings_font_path();(void)content_markdown_font_set(initial[0]?initial:TTF_FONT_BUILTIN);}
    app_font_activate_reading();
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法打开图书");
        app_font_activate_system();
        return false;
    }
    if(is_document){bool changed=false;if(document_store_refresh(path,&changed)!=ESP_OK){copy_text(s_message,sizeof(s_message),"文档或插图未完整入库，请检查后重试");return false;}if(changed)pending_discard(path);}
    esp_err_t err = book_open(path);
    s_document_mode=is_document;
    if (err != ESP_OK) {
        pending_progress_t* pending = pending_find(path);
        if (pending && !pending->dirty) pending_discard(path);
        copy_text(s_message, sizeof(s_message),
                  err == ESP_ERR_NOT_SUPPORTED ? "文件格式或压缩方式暂不支持" :
                  err == ESP_ERR_NO_MEM ? "内存不足，请重启后重试" :
                  err == ESP_ERR_INVALID_SIZE ? "图书资源过大或章节过多" :
                  "无法打开图书，请检查文件");
        ESP_LOGW(TAG, "open failed: %s internal=%u/%u psram=%u/%u", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        app_font_activate_system();
        return false;
    }
    copy_text(s_path, sizeof(s_path), path);
    const char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    if (!book_title_from_path(path, s_book_title, sizeof(s_book_title)))
        clean_filename(s_book_title, sizeof(s_book_title), filename);
    s_file_size = (uint32_t)st.st_size;
    book_progress_t p = {0};
    bool resume = pending_restore(path, s_file_size, &p) || book_progress_load(path, s_file_size, &p);
    s_document_completed=is_document&&resume&&p.pct==100;
    s_md_notice[0]=0; s_md_committed=false;
    content_markdown_sizes(s_md_sizes);
    s_px = is_document ? s_md_sizes[0] : app_settings_book_px();
    if (s_px < BOOK_PX_MIN || s_px > BOOK_PX_MAX) s_px = 48;
    size_t chapter = resume && p.chapter < book_chapter_count() ? p.chapter : 0;
    if (!load_chapter(ctx, chapter, resume && p.chapter == chapter ? p.byte_off : 0, false)) {
        free_book();
        return false;
    }
    skip_hidden_image_pages(ctx, 1);
    if (!s_text) { free_book(); return false; }
    s_view = READING;
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() && (book_kind()==BOOK_KIND_EPUB ?
        reader_image_slots(s_page)>0 : s_inline_gray && book_layout_page_image(s_page)==s_inline_index);
    s_stats_last_ms = s_stats_activity_ms = ctx->now_ms;
    s_reader_panel = reader_input_needs_help(s_path) ? READER_PANEL_HELP : READER_PANEL_NONE;
    s_control_page=0;reader_input_preferences_t input_prefs;reader_input_export(&input_prefs);s_help_hide=input_prefs.hide_help!=0;s_jump_back_valid=false;s_progress_drag=false;s_reader_multi=(reader_multi_t){0};
    s_clear_confirm = s_batch_confirm = false;
    s_session_read_ms = s_session_turns = 0;
    s_reader_notice[0] = 0;
    s_turns = s_unsaved = 0;
    pending_mark_latest(s_path);
    save_progress();
    // 打开即重新上书架：移出书架只隐藏条目，重新读这本书就是它该在架上的信号。
    // Opening puts the book back on the shelf: removal only hides the entry, and reading it
    // again is the signal that it belongs there.
    if (shelf_hidden_load(s_path)) (void)shelf_hidden_save(s_path, false);
    ESP_LOGI(TAG, "opened kind=%d chapters=%u pages=%u px=%d", book_kind(), (unsigned)book_chapter_count(), (unsigned)book_layout_page_count(), s_px);
    return true;
}
static bool open_requested_book(app_ctx_t* ctx) {
    if (!s_requested_open[0]) return false;
    char path[BOOK_STORE_PATH_MAX];
    copy_text(path, sizeof(path), s_requested_open);
    bool from_home = s_requested_open_home;
    s_requested_open[0] = 0;
    s_requested_open_home = false;
    if (!open_book(ctx, path)) return false;
    s_reader_return_home = from_home;
    return true;
}
/* ---- 书架数据 / Shelf data ---- */
static bool shelf_matches(const shelf_entry_t* item) {
    return item->search_match && (s_filter == 0 || (s_filter == 1 && !item->is_flash) || (s_filter == 2 && item->is_flash));
}
static int compare_books(const void* a, const void* b) {
    const shelf_entry_t* x = a;
    const shelf_entry_t* y = b;
    if (shelf_matches(x) != shelf_matches(y)) return shelf_matches(x) ? -1 : 1;
    if(x->pinned!=y->pinned)return x->pinned?-1:1;
    int order=0;
    if(s_book_options.mode==CONTENT_PROGRESS)order=(x->pct>y->pct)-(x->pct<y->pct);
    else if(s_book_options.mode==CONTENT_ADDED){
        if(!!x->added!=!!y->added)return x->added?-1:1;
        order=(x->added>y->added)-(x->added<y->added);
    }else if(s_book_options.mode==CONTENT_RECENT){
        if(!!x->recent!=!!y->recent)return x->recent?-1:1;
        order=(x->recent>y->recent)-(x->recent<y->recent);
        if(!order&&(!x->recent||!y->recent))order=(x->added>y->added)-(x->added<y->added);
    }else order=content_name_compare(x->name,y->name);
    if(order)return s_book_options.ascending?order:-order;
    int name=content_name_compare(x->name,y->name);return name?name:strcmp(x->path,y->path);
}
static void sort_shelf(app_ctx_t* ctx) {
    invalidate_covers();
    if (s_count > 1) qsort(s_shelf, s_count, sizeof(*s_shelf), compare_books);
    s_visible_count = 0;
    while (s_visible_count < s_count && shelf_matches(&s_shelf[s_visible_count])) ++s_visible_count;
    ctx->leaf = 0;
}
static bool shelf_reserve(void) {
    if ((size_t)s_count < s_shelf_capacity) return true;
    if (s_count == INT_MAX) return false;
    size_t cap = s_shelf_capacity ? s_shelf_capacity * 2 : 32;
    if (cap > INT_MAX || cap > SIZE_MAX / sizeof(*s_shelf)) return false;
    shelf_entry_t* entries = heap_caps_realloc(s_shelf, cap * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return false;
    s_shelf = entries;
    s_shelf_capacity = cap;
    return true;
}

// 在删除文件前预留记录，避免删除成功后才遇到内存不足。
// Reserve before unlink so allocation failure never loses a completed deletion's retry.
static delete_retry_t* delete_retry_reserve(const shelf_entry_t* entry) {
    delete_retry_t* existing = delete_retry_find(entry->path);
    if (existing) return existing;
    delete_retry_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->entry = *entry;
    p->next = s_delete_retries;
    s_delete_retries = p;
    return p;
}
static void delete_retry_discard(const char* path) {
    delete_retry_t** p = &s_delete_retries;
    while (*p) {
        if (!strcmp((*p)->entry.path, path)) {
            delete_retry_t* old = *p;
            *p = old->next;
            free(old);
            return;
        }
        p = &(*p)->next;
    }
}
// 显式目录栈放在 PSRAM；主任务只有 8 KB 栈，递归进多级 books 会在封面出现前溢出。
// Keep directory frames in PSRAM: recursive books scanning can overflow the 8 KB main-task stack before covers appear.
#define BOOK_SCAN_DEPTH_LIMIT 12
typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} shelf_scan_frame_t;
static void scan_shelf_dir(const char *root, bool is_flash, nvs_handle_t hidden, bool *truncated, bool *unreadable,
                           bool *skipped) {
    if (*truncated) return;
    shelf_scan_frame_t *frames = heap_caps_calloc(BOOK_SCAN_DEPTH_LIMIT + 1, sizeof(*frames),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frames) { *truncated = true; return; }
    frames[0].dir = opendir(root);
    if (!frames[0].dir) { *unreadable = true; free(frames); return; }
    snprintf(frames[0].path, sizeof(frames[0].path), "%s", root);
    int depth = 0;
    while (depth >= 0 && !*truncated) {
        errno = 0;
        struct dirent *ent = readdir(frames[depth].dir);
        if (!ent) {
            if (errno) *unreadable = true;
            closedir(frames[depth].dir);
            frames[depth].dir = NULL;
            --depth;
            continue;
        }
        if (ent->d_name[0] == '.') continue;
        char path[BOOK_STORE_PATH_MAX];
        int len = snprintf(path, sizeof(path), "%s/%s", frames[depth].path, ent->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) { *skipped = true; continue; }
        struct stat st;
        if (stat(path, &st) != 0) { *unreadable = true; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (depth == BOOK_SCAN_DEPTH_LIMIT) { *skipped = true; continue; }
            DIR *child = opendir(path);
            if (!child) { *unreadable = true; continue; }
            ++depth;
            frames[depth].dir = child;
            memcpy(frames[depth].path, path, (size_t)len + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) continue;
        // 显式移出书架的书不再列入；文件仍在存储卡上，随时可以重新读回书架。
        // Books the user removed stay off the shelf; the file stays on the card and reading it
        // again puts it back.
        if (hidden && shelf_hidden_read_handle(hidden, path)) continue;
        if (strlen(ent->d_name) >= sizeof(((shelf_entry_t *)0)->name) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { *skipped = true; continue; }
        if (!shelf_reserve()) { *truncated = true; break; }
        shelf_entry_t candidate = {0};
        shelf_entry_t *item = &candidate;
        copy_text(item->path, sizeof(item->path), path);
        if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
            clean_filename(item->name, sizeof(item->name), ent->d_name);
        if (!strcasecmp(ext, ".epub")) {
            char title[sizeof(item->name)] = {0};
            char author[sizeof(item->author)] = {0};
            // Listing must stay cheap and bounded: never parse every EPUB before
            // the shelf is first painted. Opening a book populates this cache.
            // 首屏列书只读已有元数据缓存，不在封面出现前逐本解析 EPUB。
            if (book_epub_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
                copy_text(item->author, sizeof(item->author), author);
        }
        item->search_match = read_pico_search_match(item->name, s_query);
        item->size = st.st_size;
        item->is_flash = is_flash;
        book_progress_t p;
        item->has_progress = book_progress_load(item->path, item->size, &p);
        item->pct = item->has_progress ? p.pct : 0;
        item->chapter = item->has_progress ? p.chapter : 0;
        item->recent = item->has_progress ? p.last_open_s : 0;
        item->added=content_first_added(item->path);
        item->pinned=content_book_pinned(item->path);
        s_shelf[s_count++] = candidate;
    }
    for (int i = 0; i <= depth; ++i)
        if (frames[i].dir) closedir(frames[i].dir);
    free(frames);
}

// 阅读过的书即使不在书根目录内也要上书架：文件管理可以直接打开存储卡任意位置的 TXT/EPUB，
// 这些书以前读完就消失了。文件已删除或内容已被替换的记录不上架，与进度不可恢复的口径一致。
// A book that was read belongs on the shelf even outside the book roots, because the file
// manager can open a TXT or EPUB anywhere on the card. Records whose file is gone, or whose
// contents were replaced, stay off the shelf, matching the rule that changed sizes never resume.
typedef struct {
    nvs_handle_t hidden;
    bool truncated;
} shelf_backfill_t;

static bool shelf_backfill_visit(const char* path, const book_progress_t* progress, void* ctx) {
    // 文档的阅读记录不能补入图书书架。/ Markdown progress must never backfill the book shelf.
    const char *kind_ext=strrchr(path,'.');
    if(!kind_ext||(strcasecmp(kind_ext,".txt")&&strcasecmp(kind_ext,".epub")))return true;
    shelf_backfill_t* scan = ctx;
    if (scan->truncated) return false;
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_shelf[i].path, path)) return true;   // 书根目录已经收录
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX || (uint32_t)st.st_size != progress->file_size)
        return true;
    if (scan->hidden && shelf_hidden_read_handle(scan->hidden, path)) return true;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strlen(base) >= sizeof(((shelf_entry_t*)0)->name)) return true;
    if (!shelf_reserve()) { scan->truncated = true; return false; }
    shelf_entry_t candidate = {0};
    shelf_entry_t* item = &candidate;
    copy_text(item->path, sizeof(item->path), path);
    if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
        clean_filename(item->name, sizeof(item->name), base);
    if (!item->name[0]) return true;
    const char* ext = strrchr(base, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        char title[sizeof(item->name)] = {0};
        char author[sizeof(item->author)] = {0};
        if (book_epub_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
            copy_text(item->author, sizeof(item->author), author);
    }
    item->size = (uint32_t)st.st_size;
    // 内置书根固定在 /flash 下，与 book_store.c 和 open_book 的前缀判断一致。
    // The internal root lives under /flash, matching the prefix test in book_store.c and open_book.
    item->is_flash = !strncmp(path, "/flash/", 7);
    item->has_progress = true;
    item->pct = progress->pct;
    item->chapter = progress->chapter;
    item->recent = progress->last_open_s;
    item->search_match = read_pico_search_match(item->name, s_query);
    item->added=content_first_added(item->path);
        item->pinned=content_book_pinned(item->path);
        s_shelf[s_count++] = candidate;
    return true;
}

// 把书根目录之外读过的书补进书架，返回是否新增了条目。
// 阅读只写进度、不动存储版本，所以缓存复用的路径也必须调用它，否则刚读完的书要等手动重扫。
// Backfill books read outside the roots and report whether anything was added. Reading only
// writes progress and never bumps the store revision, so the cached path has to call this too
// or a freshly read book stays invisible until a manual rescan.
static bool shelf_backfill_read_books(bool* truncated) {
    nvs_handle_t hidden = 0;
    (void)nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    const int before = s_count;
    shelf_backfill_t backfill = {.hidden = hidden};
    (void)book_progress_list(shelf_backfill_visit, &backfill);
    if (hidden) nvs_close(hidden);
    if (truncated && backfill.truncated) *truncated = true;
    return s_count != before;
}

static void scan_shelf(app_ctx_t* ctx) {
    s_count = 0;
    s_visible_count = 0;
    s_message[0] = 0;
    s_shelf_warning[0] = 0;
    // 内置分区每次启动都必须重新挂载；已有文件不会因此格式化。
    // Flash mounts on each boot; existing files are not formatted by that mount.
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int n = 0;
    esp_err_t root_err = book_store_roots(roots, &n);
    bool truncated = false, unreadable = root_err != ESP_OK || book_store_roots_degraded(), skipped = false;
    nvs_handle_t hidden = 0;
    (void)nvs_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    s_storage[0] = 0;
    for (int i = 0; i < n; ++i) {
        if (i == 0 || roots[i].is_flash || roots[i - 1].is_flash) {
            char capacity[64];
            snprintf(capacity, sizeof(capacity), "%s%s %.1f MB", s_storage[0] ? " · " : "",
                     roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
            strncat(s_storage, capacity, sizeof(s_storage) - strlen(s_storage) - 1);
        }
        scan_shelf_dir(roots[i].path, roots[i].is_flash, hidden,
                       &truncated, &unreadable, &skipped);
    }
    // 同路径重新出现也先完成旧清理，避免新阅读进度被后续重试擦掉。
    // Finish old cleanup even if the path reappears, before new reading can create progress.
    for (delete_retry_t* p = s_delete_retries; p; p = p->next) {
        int i = 0;
        while (i < s_count && strcmp(s_shelf[i].path, p->entry.path)) ++i;
        if (i == s_count) {
            if (!shelf_reserve()) { truncated = true; continue; }
            ++s_count;
        }
        s_shelf[i] = p->entry;
        s_shelf[i].selected = false;
        s_shelf[i].search_match = read_pico_search_match(p->entry.name, s_query);
    }
    if (hidden) nvs_close(hidden);
    // 补齐书根目录之外读过的书；显式移出书架的除外。
    // Backfill books read outside the roots, minus the ones removed from the shelf.
    (void)shelf_backfill_read_books(&truncated);
    sort_shelf(ctx);
    s_latest_path[0] = 0;
    char last_path[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(last_path, sizeof(last_path))) {
        for (int i = 0; i < s_count; ++i) {
            if (!strcmp(s_shelf[i].path, last_path) && !s_shelf[i].removed) {
                copy_text(s_latest_path, sizeof(s_latest_path), last_path);
                break;
            }
        }
    }
    if (!n) copy_text(s_storage, sizeof(s_storage), "存储不可用，请检查 TF 卡");
    if (truncated) snprintf(s_shelf_warning, sizeof(s_shelf_warning), "内存不足，仅列 %d 本；释放后重扫", s_count);
    else if (unreadable) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分目录或文件不可读，请检查后重扫");
    else if (skipped) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分文件过大或名称过长，未列入");
    else if (s_pending_invalidated) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "图书已更新，旧待保存进度已作废");
    if (!s_count && !unreadable && !truncated) copy_text(s_message, sizeof(s_message), "暂无图书，请传入 TXT 或 EPUB");
    read_pico_sd_info_t cache_sd = {0};
    read_pico_sd_get_info(&cache_sd);
    s_cache_sd_present = cache_sd.present;
    s_cache_sd_mounted = cache_sd.mounted;
    s_store_revision = book_store_revision();
    s_shelf_cache_valid = !unreadable && !truncated;
    ESP_LOGI(TAG, "shelf books=%d roots=%d", s_count, n);
}

static void refresh_cached_progress(app_ctx_t* ctx) {
    // 目录没变也要补一次：阅读只写进度，不改存储版本，否则刚读完的书不会出现。
    // Backfill even when the listing is unchanged: reading writes progress and never bumps the
    // store revision, so a freshly read book would otherwise stay invisible until a manual rescan.
    const bool added = shelf_backfill_read_books(NULL);
    bool found = false;
    char latest[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(latest, sizeof(latest))) {
        for (int i = 0; i < s_count; ++i) {
            shelf_entry_t *item = &s_shelf[i];
            if (strcmp(item->path, latest) || item->removed) continue;
            book_progress_t progress;
            item->has_progress = book_progress_load(item->path, item->size, &progress);
            item->pct = item->has_progress ? progress.pct : 0;
            item->chapter = item->has_progress ? progress.chapter : 0;
            item->recent = item->has_progress ? progress.last_open_s : 0;
            copy_text(s_latest_path, sizeof(s_latest_path), latest);
            found = true;
            break;
        }
    }
    // 新增条目必须重排才能重算可见列表；只刷新进度时沿用原有顺序。
    // Added entries need a re-sort to recompute the visible list; a progress refresh alone
    // keeps the current order.
    if (added || found) sort_shelf(ctx);
}
static void return_to_cached_shelf(app_ctx_t* ctx) {
    if (s_shelf_cache_valid && s_store_revision == book_store_revision()) refresh_cached_progress(ctx);
    else scan_shelf(ctx);
}

static void refresh_capacity(void) {
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int count = 0;
    s_storage[0] = 0;
    book_store_roots(roots, &count);
    for (int i = 0; i < count; ++i) {
        if (i && !roots[i].is_flash && !roots[i - 1].is_flash) continue;
        char line[64];
        snprintf(line, sizeof(line), "%s%s %.1f MB", s_storage[0] ? " · " : "", roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
        strncat(s_storage, line, sizeof(s_storage) - strlen(s_storage) - 1);
    }
}
static void manage_apply(app_ctx_t* ctx) {
    if (!strcmp(s_path, s_managed.path)) free_book();
    esp_err_t err;
    if (s_delete_confirm && !s_file_removed) {
        delete_retry_t* retry = delete_retry_reserve(&s_managed);
        if (!retry) {
            copy_text(s_manage_message, sizeof(s_manage_message), "内存不足，无法保留删除重试记录");
            s_clear_confirm = false;
            return;
        }
        bool removed = false;
        err = book_store_delete(s_managed.path, &removed);
        if (removed) {
            retry->entry.removed = true;
            s_file_removed = true;
            pending_discard(s_managed.path);
            book_store_notify_changed();
            s_store_revision = book_store_revision();
            for (int i = 0; i < s_count; ++i) if (!strcmp(s_shelf[i].path, s_managed.path)) s_shelf[i].removed = true;
            refresh_capacity();
        } else delete_retry_discard(s_managed.path);
    } else {
        pending_discard(s_managed.path);
        err = book_progress_forget(s_managed.path);
    }
    s_clear_confirm = false;
    if (err != ESP_OK) {
        copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ?
                  "文件已删除，进度清理失败，请重试" : s_delete_confirm ?
                  "文件删除失败，请检查存储后重试" : "进度清理失败，请重新尝试");
        return;
    }
    delete_retry_discard(s_managed.path);
    s_view = SHELF;
    int write = 0, leaf = ctx->leaf;
    for (int i = 0; i < s_count; ++i) {
        if (!strcmp(s_shelf[i].path, s_managed.path)) {
            if (s_file_removed) continue;
            s_shelf[i].has_progress = false; s_shelf[i].pct = 0; s_shelf[i].chapter = 0; s_shelf[i].recent = 0;
        }
        s_shelf[write++] = s_shelf[i];
    }
    s_count = write;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
}

/* ---- 输入与生命周期 / Input and lifecycle ---- */
static app_redraw_t turn_page(app_ctx_t* ctx, int dir) {
    if (!s_text || s_clear_confirm) return APP_REDRAW_NONE;
    if(book_kind()==BOOK_KIND_MARKDOWN&&book_layout_page_count()==1&&dir>0&&!s_document_completed){s_document_completed=true;save_progress();return APP_REDRAW_PAGE;}
    bool changed = true;
    if (dir > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
    else if (dir < 0 && s_page) --s_page;
    else if (dir > 0 && s_chapter + 1 < book_chapter_count()) { save_progress(); changed = load_chapter(ctx, s_chapter + 1, 0, false); }
    else if (dir < 0 && s_chapter) { save_progress(); changed = load_chapter(ctx, s_chapter - 1, 0, true); }
    else return APP_REDRAW_NONE;
    if (!changed) { set_reader_view(s_text ? TOC : SHELF); ctx->leaf = s_text ? s_chapter / BOOK_TOC_ROWS : 0; return APP_REDRAW_PAGE; }
    skip_hidden_image_pages(ctx, dir);
    if (!s_text) { s_view = SHELF; return APP_REDRAW_PAGE; }
    s_jump_offset = SIZE_MAX;
    s_last_turn_ms = s_stats_activity_ms = ctx->now_ms;
    ++s_stats_pending_turns;
    ++s_session_turns;
    uint8_t full_pages = app_settings_reader_full_pages();
    if (full_pages) ++s_turns;
    else s_turns = 0;
    s_reader_cleanup = full_pages > 0 && s_turns >= full_pages;
    if (s_reader_cleanup) s_turns = 0;
    if (s_unsaved < 8) ++s_unsaved;
    if (s_unsaved >= 8 && !s_save_failed) save_progress();
    ESP_LOGI(TAG, "turn chapter=%u page=%u/%u pct=%u", (unsigned)s_chapter, (unsigned)s_page + 1, (unsigned)book_layout_page_count(), percent(s_page));
    // 预渲染未命中时也使用 GL16；到达设定页数后在本次翻页整屏全刷。
    // A cache miss still uses GL16; the selected turn count triggers a full-screen refresh on this turn.
    app_redraw_t redraw = paint_reading(ctx, MODE_GL16);
    s_water_turn_pending = redraw == APP_REDRAW_AREA && !s_reader_cleanup &&
                           app_settings_reader_turn_effect() == 1;
    s_water_turn_dir = dir > 0 ? E0470_TURN_RTL : E0470_TURN_LTR;
    return redraw;
}


static app_redraw_t apply_reader_layout(app_ctx_t* ctx, int next_px, int next_margin,
                                        uint8_t next_line, uint8_t next_para) {
    if (next_px < BOOK_PX_MIN || next_px > BOOK_PX_MAX ||
        next_margin < BOOK_MARGIN_MIN || next_margin > BOOK_MARGIN_MAX ||
        next_line < 110 || next_line > 150 ||
        next_para > 75) return APP_REDRAW_NONE;
    if (next_px == s_px && next_margin == s_margin && next_line == app_settings_book_line_spacing() &&
        next_para == app_settings_book_paragraph_spacing()) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    int old_px = s_px, old_margin = s_margin;
    uint8_t old_line = app_settings_book_line_spacing();
    uint8_t old_para = app_settings_book_paragraph_spacing();
    lock_draw();
    invalidate_prep();
    s_px = next_px;
    s_margin = next_margin;
    s_line_spacing = next_line;
    book_layout_set_spacing(next_line, next_para);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        s_px = old_px;
        s_margin = old_margin;
        s_line_spacing = old_line;
        book_layout_set_spacing(old_line, old_para);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    if (book_kind()!=BOOK_KIND_MARKDOWN) app_settings_set_book_px((uint8_t)s_px);
    app_settings_set_book_margin((uint8_t)s_margin);
    app_settings_set_book_line_spacing(next_line);
    app_settings_set_book_paragraph_spacing(next_para);
    prepare_inline_image();
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_typography(app_ctx_t* ctx, int tracking_index) {
    (void)ctx;
    if (!s_text || tracking_index < 0 || tracking_index > 4)
        return APP_REDRAW_NONE;
    int old_index = app_settings_book_tracking();
    if (tracking_index == old_index) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_typography((tracking_index - 2) * 2);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count,
                                       body_rect_for_tracking(tracking_index), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        book_layout_set_typography((old_index - 2) * 2);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_tracking((uint8_t)tracking_index);
    prepare_inline_image();
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_indent(app_ctx_t* ctx, int em) {
    (void)ctx;
    if (!s_text || em < 0 || em > 3 || em == app_settings_book_indent())
        return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    uint8_t old_em = app_settings_book_indent();
    lock_draw();
    invalidate_prep();
    book_layout_set_first_line_indent((unsigned)em);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        book_layout_set_first_line_indent(old_em);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    app_settings_set_book_indent((uint8_t)em);
    prepare_inline_image();
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_manual_refresh(app_ctx_t* ctx) {
    (void)ctx;
    if (!s_text) return APP_REDRAW_NONE;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_progress_drag = false;
    s_reader_footer_pending = s_reader_image_refresh_pending = false;
    s_water_turn_pending = s_reader_cleanup = false;
    s_du_count = s_turns = 0;
    invalidate_prep();
    return APP_REDRAW_FULL;
}

static app_redraw_t apply_reader_option(app_ctx_t* ctx, int kind) {
    (void)ctx;
    if (kind == 0) {
        app_settings_set_reader_power_turn(!app_settings_reader_power_turn());
        return APP_REDRAW_PAGE;
    }
    if (kind == 3) {
        app_settings_set_reader_hold_refresh(!app_settings_reader_hold_refresh());
        return APP_REDRAW_PAGE;
    }
    if (!s_text || (kind != 1 && kind != 2)) return APP_REDRAW_NONE;
    bool previous = kind == 1 ? s_reader_immersive : app_settings_reader_hide_images();
    lock_draw();
    size_t off = book_layout_page_start_offset(s_page);
    invalidate_prep();
    if (kind == 1) {s_reader_immersive=!previous;app_settings_set_reader_immersive(!previous);}
    else {
        app_settings_set_reader_hide_images(!previous);
        book_layout_set_images_visible(previous);
    }
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        if (kind == 1) {s_reader_immersive=previous;app_settings_set_reader_immersive(previous);}
        else {
            app_settings_set_reader_hide_images(previous);
            book_layout_set_images_visible(!previous);
        }
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        copy_text(s_reader_notice, sizeof(s_reader_notice), "排版失败，请重试");
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1800;
    }
    unlock_draw();
    if (ok) {
        free(s_inline_gray); s_inline_gray = NULL; s_inline_index = -1;
        prepare_inline_image();
        s_jump_offset = SIZE_MAX;
        save_progress();
    }
    s_reader_footer_pending = s_reader_image_refresh_pending = false;
    s_water_turn_pending = false;
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_return(app_ctx_t *ctx, bool home) {
    save_progress();
    if (home || s_reader_return_home) {
        ui_nav_request(ctx, 0);
        return APP_REDRAW_NONE;
    }
    free_book();
    s_view = s_document_mode ? DOCUMENTS : SHELF;
    s_reader_panel = READER_PANEL_NONE;
    if (s_document_mode) { ctx->leaf=0; scan_documents(ctx); }
    else return_to_cached_shelf(ctx);
    return APP_REDRAW_PAGE;
}

// 三态布局共用正文锚点，控制面板保持当前状态。/ Three layout states preserve the text anchor and the open panel.
static app_redraw_t toggle_reader_fullscreen(app_ctx_t* ctx) {
    if (!s_text) return APP_REDRAW_NONE;
    size_t off=book_layout_page_start_offset(s_page);
    bool old_full=s_reader_fullscreen,old_immersive=s_reader_immersive;
    int mode=old_full?(old_immersive?2:1):0,next=(mode+1)%3;
    lock_draw();invalidate_prep();s_reader_fullscreen=next!=0;s_reader_immersive=next==2;
    bool ok=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
    if(ok)s_page=book_layout_page_for_offset(off);
    else{s_reader_fullscreen=old_full;s_reader_immersive=old_immersive;(void)book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);}
    unlock_draw();
    if(!ok)return APP_REDRAW_NONE;
    free(s_inline_gray);s_inline_gray=NULL;s_inline_index=-1;prepare_inline_image();s_jump_offset=SIZE_MAX;
    app_settings_set_reader_fullscreen(s_reader_fullscreen);app_settings_set_reader_immersive(s_reader_immersive);
    save_progress();return APP_REDRAW_PAGE;
}

static app_redraw_t select_reading_font_item(app_ctx_t* ctx, const ttf_font_item_t* item) {
    if (!item) return APP_REDRAW_NONE;
    if(!strcmp(ttf_font_path(),item->path))return APP_REDRAW_NONE;
    char previous[TTF_FONT_PATH_MAX];
    copy_text(previous, sizeof(previous), ttf_font_path());
    if (ttf_font_open(item->path) != ESP_OK) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else if (previous[0]) {
        (void)ttf_font_open(previous);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    if(book_kind()==BOOK_KIND_MARKDOWN){
        if(content_markdown_font_set(item->path)!=ESP_OK){
            lock_draw();(void)ttf_font_open(previous);(void)book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);s_page=book_layout_page_for_offset(off);unlock_draw();
            copy_text(s_reader_notice,sizeof(s_reader_notice),"字体保存失败，请重试");s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;
        }
    }else app_settings_set_font_path(item->path);
    copy_text(s_font_path, sizeof(s_font_path), item->path);
    prepare_inline_image();
    save_progress();
    return APP_REDRAW_PAGE;
}

static int slider_index(EpdRect rect, int x, int count) {
    int span = rect.width - 128;
    int pos = x - rect.x - 64;
    int index = span > 0 && count > 1 ? (pos * (count - 1) + span / 2) / span : 0;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    return index;
}
static bool reader_markdown_font_panel(void){return book_kind()==BOOK_KIND_MARKDOWN&&(s_reader_panel==READER_PANEL_FONT_SETTINGS||s_reader_panel==READER_PANEL_MARKDOWN_SIZES);}
// 为96px标题预留完整行高；滑块位置不随拖动跳动。/ Reserve a full 96px heading so slider positions stay stable during dragging.
static EpdRect reader_markdown_row(int index){return index?(EpdRect){36,310+(index-1)*154,612,154}:(EpdRect){36,772,612,112};}
static EpdRect reader_slider_rect(int slider) {
    if(slider>=5&&reader_markdown_font_panel()){int i=slider-5;EpdRect r=reader_markdown_row(i);return (EpdRect){36,r.y+(i?98:54),612,52};}
    int row=slider>=5?slider-5:slider==0?0:slider-1;
    if(slider>=1&&slider<5)row-=s_control_page*reader_control_rows();
    if(row<0||row>=reader_control_rows())return (EpdRect){0};
    EpdRect r=reader_control_row(row);return (EpdRect){36,r.y+r.height-70,612,60};
}

static int reader_slider_at(uint16_t x, uint16_t y) {
    if (reader_markdown_font_panel()) {
        for (int i=5;i<9;++i) if(ui_rect_hit(reader_slider_rect(i),x,y))return i;
        return -1;
    }
    int first = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 0 : 1;
    int end = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 1 : 5;
    for (int i = first; i < end; ++i) if (ui_rect_hit(reader_slider_rect(i), x, y)) return i;
    return -1;
}

static void reader_slider_map(int slider, int x) {
    if (slider >= 5 && slider <= 8) {
        int i=slider-5, min=i?24:BOOK_PX_MIN,max=i?96:BOOK_PX_MAX;
        s_md_draft[i]=min+slider_index(reader_slider_rect(slider),x,max-min+1);
    } else if (slider == 0) s_reader_preview_px = BOOK_PX_MIN + slider_index(reader_slider_rect(0), x, 37);
    else if (slider == 1) s_reader_preview_margin = BOOK_MARGIN_MIN + slider_index(reader_slider_rect(1), x, BOOK_MARGIN_MAX-BOOK_MARGIN_MIN+1);
    else if (slider == 2) s_reader_preview_line = 110 + slider_index(reader_slider_rect(2), x, 41);
    else if (slider == 3) s_reader_preview_para = slider_index(reader_slider_rect(3), x, 76);
    else if (slider == 4) s_reader_preview_tracking = slider_index(reader_slider_rect(4), x, 5);
}

static void reader_slider_begin(int slider, int x) {
    s_reader_slider = slider;
    if (slider >= 5 && slider <= 8) {
        memcpy(s_md_draft,s_md_sizes,4); EpdRect rect=reader_slider_rect(slider);
        int i=slider-5,min=i?24:BOOK_PX_MIN,max=i?96:BOOK_PX_MAX,value=s_md_draft[i];
        s_reader_slider_endpoint=x<rect.x+64 || x>=rect.x+rect.width-64;
        if (x<rect.x+64) --value; else if(x>=rect.x+rect.width-64) ++value;
        else {reader_slider_map(slider,x);return;}
        s_md_draft[i]=value<min?min:value>max?max:value;return;
    }
    s_reader_preview_px = s_px;
    s_reader_preview_margin = s_margin;
    s_reader_preview_line = app_settings_book_line_spacing();
    s_reader_preview_para = app_settings_book_paragraph_spacing();
    s_reader_preview_tracking = app_settings_book_tracking();
    EpdRect rect = reader_slider_rect(slider);
    int *value = slider == 0 ? &s_reader_preview_px : slider == 1 ? &s_reader_preview_margin :
                 slider == 2 ? &s_reader_preview_line : slider == 3 ? &s_reader_preview_para :
                 &s_reader_preview_tracking;
    int min = slider == 0 ? BOOK_PX_MIN : slider == 1 ? BOOK_MARGIN_MIN : slider == 2 ? 110 : 0;
    int max = slider == 0 ? BOOK_PX_MAX : slider == 1 ? 60 : slider == 2 ? 150 : slider == 3 ? 75 : 4;
    s_reader_slider_endpoint = x < rect.x + 64 || x >= rect.x + rect.width - 64;
    int step = 1;
    if (x < rect.x + 64) *value -= step;
    else if (x >= rect.x + rect.width - 64) *value += step;
    else reader_slider_map(slider, x);
    if (*value < min) *value = min;
    if (*value > max) *value = max;
}

static void reader_slider_move(int x) {
    int slider = s_reader_slider;
    if (slider < 0) return;
    s_reader_slider_endpoint = false;
    reader_slider_map(slider, x);
}

static app_redraw_t apply_markdown_sizes(app_ctx_t *ctx) {
    (void)ctx;
    if (book_kind()!=BOOK_KIND_MARKDOWN || !content_markdown_sizes_valid(s_md_draft) || !memcmp(s_md_draft,s_md_sizes,4)) return APP_REDRAW_PAGE;
    uint8_t old[4]; memcpy(old,s_md_sizes,4);
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw(); invalidate_prep();
    memcpy(s_md_sizes,s_md_draft,4); s_px=s_md_sizes[0];
    book_layout_set_markdown(true,s_md_sizes+1,s_text_styles,s_text_style_count);
    bool ok=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
    if (ok) ok=content_markdown_sizes_set(s_md_sizes)==ESP_OK;
    if (!ok) {
        memcpy(s_md_sizes,old,4); s_px=old[0];
        book_layout_set_markdown(true,s_md_sizes+1,s_text_styles,s_text_style_count);
        bool restored=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
        unlock_draw();
        if(!restored){free_book();s_view=SHELF;}
        copy_text(restored?s_md_notice:s_message,restored?sizeof(s_md_notice):sizeof(s_message),"字号保存或排版失败，请重试");
        return APP_REDRAW_PAGE;
    }
    s_md_notice[0]=0; s_md_committed=true;
    s_page=book_layout_page_for_offset(off); unlock_draw(); save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_slider_commit(app_ctx_t* ctx) {
    if (s_reader_slider < 0) return APP_REDRAW_NONE;
    int slider = s_reader_slider;
    int px = s_reader_preview_px, margin = s_reader_preview_margin, line = s_reader_preview_line;
    int para = s_reader_preview_para;
    int tracking = s_reader_preview_tracking;
    s_reader_slider = -1;
    s_progress_drag = false;
    if (slider>=5) return apply_markdown_sizes(ctx);
    if(slider==0&&book_kind()==BOOK_KIND_MARKDOWN){memcpy(s_md_draft,s_md_sizes,4);s_md_draft[0]=(uint8_t)px;return apply_markdown_sizes(ctx);}
    if (slider == 4) return apply_reader_typography(ctx, tracking);
    return apply_reader_layout(ctx, px, margin, line, (uint8_t)para);
}

#include "reader_controls_actions.h"



static app_redraw_t manage_action(app_ctx_t* ctx,uint16_t x,uint16_t y){
    if(!ui_rect_hit(manage_panel(),x,y)){
        if(s_clear_confirm)s_clear_confirm=false;else s_view=SHELF;
        return APP_REDRAW_PAGE;
    }
    if(!s_clear_confirm&&!s_file_removed){
        if(ui_rect_hit(manage_aux_rect(0),x,y)){
            bool next=!s_managed.pinned;
            if(content_book_pin_set(s_managed.path,next)!=ESP_OK){copy_text(s_manage_message,sizeof(s_manage_message),"置顶保存失败，请重试");return APP_REDRAW_PAGE;}
            s_managed.pinned=next;s_manage_message[0]=0;
            for(int i=0;i<s_count;++i)if(!strcmp(s_shelf[i].path,s_managed.path))s_shelf[i].pinned=next;
            sort_shelf(ctx);return APP_REDRAW_PAGE;
        }
        if(ui_rect_hit(manage_aux_rect(1),x,y)){
            extern void app_files_request_folder(int folder);app_files_request_folder(0);ui_nav_request(ctx,2);return APP_REDRAW_NONE;
        }
    }
    int count = s_clear_confirm || s_file_removed ? 2 : 3;
    for (int i = 0; i < count; ++i) if (ui_rect_hit(manage_rect(i, count), x, y)) {
        if (s_clear_confirm) { if (!i) s_clear_confirm = false; else manage_apply(ctx); }
        else if (!i) s_view = SHELF;
        else if (s_file_removed) manage_apply(ctx);
        else { s_clear_confirm = true; s_delete_confirm = i == 2; s_manage_message[0] = 0; }
        break;
    }
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_save(app_ctx_t* ctx) {
    if (s_title_input.pinyin[0]) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "请先选择候选字");
        return APP_REDRAW_PAGE;
    }
    char *start = s_editor_title;
    while (*start == ' ') ++start;
    if (start != s_editor_title) memmove(s_editor_title, start, strlen(start) + 1);
    size_t len = strlen(s_editor_title);
    while (len && s_editor_title[len - 1] == ' ') s_editor_title[--len] = 0;
    s_title_input.cursor=strlen(s_editor_title);
    if (!s_editor_title[0]) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "书名不能为空");
        return APP_REDRAW_PAGE;
    }
    esp_err_t err = book_title_set(s_managed.path, s_editor_title);
    if (err != ESP_OK) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), "保存失败，请检查书名或存储");
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_shelf[i].path, s_managed.path)) copy_text(s_shelf[i].name, sizeof(s_shelf[i].name), s_editor_title);
    free(s_editor_cover); s_editor_cover = NULL;
    s_view = SHELF;
    sort_shelf(ctx);
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_paint(app_ctx_t *ctx){
    render(ctx,ctx->fb);s_area=ui_text_input_area();s_mode=MODE_GL16;return APP_REDRAW_AREA;
}
static app_redraw_t editor_action(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    ui_input_result_t result=ui_text_input_gesture(&s_title_input,ev);
    if(result==UI_INPUT_CANCEL){ui_text_input_clear(&s_title_input);free(s_editor_cover);s_editor_cover=NULL;s_view=MANAGE;return APP_REDRAW_PAGE;}
    if(result==UI_INPUT_DONE)return editor_save(ctx);
    if(result==UI_INPUT_CHANGED){s_editor_notice[0]=0;return editor_paint(ctx);}
    return APP_REDRAW_NONE;
}
static app_redraw_t search_input_action(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    ui_input_result_t result=ui_text_input_gesture(&s_search_input,ev);
    if(result==UI_INPUT_CANCEL||result==UI_INPUT_DONE){search_finish(ctx,result==UI_INPUT_DONE);return APP_REDRAW_PAGE;}
    if(result==UI_INPUT_CHANGED)return editor_paint(ctx);
    return APP_REDRAW_NONE;
}
// 成功项退出选择，已删文件的失败项仅重试元数据。/ Deselect successes; removed-file failures retry metadata only.
static void batch_apply(app_ctx_t* ctx) {
    unsigned done = 0, failed = 0;
    int write = 0;
    const bool unshelf = s_batch_kind == BATCH_UNSHELF;
    for (int i = 0; i < s_count; ++i) {
        shelf_entry_t item = s_shelf[i];
        bool discard = false;
        if (item.selected) {
            esp_err_t err = ESP_OK;
            if (unshelf) {
                // 只隐藏条目：文件与阅读进度都留在原处，再次打开这本书会重新上书架。
                // Hide the entry only: file and progress stay, and reopening the book puts it back.
                if (shelf_hidden_save(item.path, true)) discard = true;
                else err = ESP_FAIL;
            } else {
                if (!strcmp(s_path, item.path)) free_book();
                if (s_batch_kind == BATCH_DELETE && !item.removed) {
                    delete_retry_t* retry = delete_retry_reserve(&item);
                    if (!retry) { ++failed; s_shelf[write++] = item; continue; }
                    bool removed = false;
                    err = book_store_delete(item.path, &removed);
                    if (removed) {
                        retry->entry.removed = true;
                        item.removed = true; pending_discard(item.path);
                        book_store_notify_changed(); s_store_revision = book_store_revision();
                    } else delete_retry_discard(item.path);
                } else { pending_discard(item.path); err = book_progress_forget(item.path); }
            }
            if (err == ESP_OK) {
                delete_retry_discard(item.path);
                ++done; item.selected = false;
                if (!unshelf) {
                    item.has_progress = false; item.pct = 0;
                    item.chapter = 0; item.recent = 0;
                }
                discard = discard || item.removed;
            } else ++failed;
        }
        if (!discard) s_shelf[write++] = item;
    }
    s_count = write;
    refresh_capacity();
    int leaf = ctx->leaf;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
    s_batch_confirm = false;
    if (unshelf)
        snprintf(s_batch_message, sizeof(s_batch_message), "已移出书架 %u 本，失败 %u 本%s",
                 done, failed, failed ? "；所选可重试" : "");
    else
        snprintf(s_batch_message, sizeof(s_batch_message), "成功 %u 本，失败 %u 本%s",
                 done, failed, failed ? "；所选可重试" : "");
}
static app_redraw_t batch_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_batch_confirm) {
        if (ui_rect_hit(ui_row_rect(0, 2, 620, UI_BTN_H), x, y)) s_batch_confirm = false;
        else if (ui_rect_hit(ui_row_rect(1, 2, 620, UI_BTN_H), x, y)) batch_apply(ctx);
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < 6; ++i) if (ui_rect_hit(batch_rect(i), x, y)) {
        s_batch_message[0] = 0;
        if (!i) select_page(ctx->leaf);
        else if (i == 1) clear_selection();
        else if (i == 2) { clear_selection(); s_batch_message[0] = 0; scan_shelf(ctx); }
        else if (selected_count()) {
            s_batch_kind = i == 3 ? BATCH_DELETE : i == 4 ? BATCH_CLEAR : BATCH_UNSHELF;
            s_batch_confirm = true;
        }
        else copy_text(s_batch_message, sizeof(s_batch_message), "请先选择图书");
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t content_sort_action(app_ctx_t *ctx,uint16_t x,uint16_t y){
    for(int i=0;i<4;++i)if(ui_rect_hit(content_sort_mode(i),x,y)){s_sort_draft.mode=i;s_sort_draft.ascending=i==CONTENT_NAME||i==CONTENT_PROGRESS;return APP_REDRAW_PAGE;}
    if(ui_rect_hit(content_sort_control(),x,y)){
        s_sort_draft.ascending=!s_sort_draft.ascending;return APP_REDRAW_PAGE;
    }
    if(ui_rect_hit(content_sort_footer(0),x,y)){s_sort_open=false;return APP_REDRAW_PAGE;}
    if(!ui_rect_hit(content_sort_footer(1),x,y))return APP_REDRAW_NONE;
    s_sort_draft.marked_first=s_sort_draft.marked_only=false;
    if(content_options_save(s_sort_documents,s_sort_draft)!=ESP_OK){snprintf(s_sort_notice,sizeof(s_sort_notice),"排序保存失败，请重试");return APP_REDRAW_PAGE;}
    if(s_sort_documents){
        s_document_options=s_sort_draft;ctx->leaf=0;scan_documents(ctx);
    }else{
        s_book_options=s_sort_draft;s_recent_sort=s_book_options.mode==CONTENT_RECENT;
        app_settings_set_shelf_recent_sort(s_recent_sort);sort_shelf(ctx);
    }
    s_sort_open=false;return APP_REDRAW_PAGE;
}
static void document_batch_apply(app_ctx_t *ctx){
    if(s_document_batch_action!=3&&s_document_batch_action!=4)return;
    unsigned done=0,failed=0;
    for(size_t i=0;i<s_document_count;++i){document_entry_t *d=&s_documents[i];if(!d->selected)continue;
        if(!strcmp(s_path,d->path))free_book();
        pending_discard(d->path);
        esp_err_t e;
        if(s_document_batch_action==3){e=document_store_delete(d->path);d->cleanup_pending=e!=ESP_OK;}
        else e=book_progress_forget(d->path);
        if(e==ESP_OK){++done;d->selected=false;}else ++failed;
    }
    s_document_batch_confirm=false;scan_documents(ctx);
    snprintf(s_document_message,sizeof(s_document_message),"成功 %u 篇，失败 %u 篇%s",done,failed,failed?"；勾选项可重试":"");
}
static void document_details_apply(app_ctx_t *ctx,bool deleting){
    document_entry_t *d=&s_document_detail;
    if(!strcmp(s_path,d->path))free_book();
    pending_discard(d->path);
    esp_err_t e=deleting?document_store_delete(d->path):book_progress_forget(d->path);
    s_document_delete_confirm=s_document_clear_confirm=false;
    if(e!=ESP_OK){
        if(deleting){struct stat st;d->cleanup_pending=stat(d->path,&st)!=0&&errno==ENOENT;
            for(size_t i=0;i<s_document_count;++i)if(!strcmp(s_documents[i].path,d->path))s_documents[i].cleanup_pending=d->cleanup_pending;
            scan_documents(ctx);
        }
        snprintf(s_document_message,sizeof(s_document_message),deleting?"文档删除或清理失败，请重试":"进度清理失败，请重试");return;
    }
    s_document_message[0]=0;
    if(deleting){
        s_document_menu=false;d->cleanup_pending=false;
        for(size_t i=0;i<s_document_count;++i)if(!strcmp(s_documents[i].path,d->path))s_documents[i].cleanup_pending=false;
    }else{d->has_progress=false;d->pct=0;d->recent=0;}
    scan_documents(ctx);
}
static app_redraw_t document_details_action(app_ctx_t *ctx,uint16_t x,uint16_t y){
    document_entry_t *d=&s_document_detail;EpdRect panel=details_panel(d->name);
    bool confirm=s_document_delete_confirm||s_document_clear_confirm;
    if(!ui_rect_hit(panel,x,y)){
        if(confirm)s_document_delete_confirm=s_document_clear_confirm=false;else s_document_menu=false;
        return APP_REDRAW_PAGE;
    }
    if(confirm){
        if(ui_rect_hit(details_rect(d->name,0,2),x,y))s_document_delete_confirm=s_document_clear_confirm=false;
        else if(ui_rect_hit(details_rect(d->name,1,2),x,y))document_details_apply(ctx,s_document_delete_confirm);
        return APP_REDRAW_PAGE;
    }
    if(d->cleanup_pending){
        if(ui_rect_hit(details_rect(d->name,0,2),x,y))s_document_menu=false;
        else if(ui_rect_hit(details_rect(d->name,1,2),x,y))document_details_apply(ctx,true);
        return APP_REDRAW_PAGE;
    }
    if(ui_rect_hit(ui_row_rect(0,1,panel.y+panel.height-190,68),x,y)){
        if(document_store_set_flags(d->path,!d->pinned,d->starred)!=ESP_OK){snprintf(s_document_message,sizeof(s_document_message),"置顶保存失败，请重试");return APP_REDRAW_PAGE;}
        d->pinned=!d->pinned;s_document_message[0]=0;ctx->leaf=0;scan_documents(ctx);return APP_REDRAW_PAGE;
    }
    for(int i=0;i<3;++i)if(ui_rect_hit(details_rect(d->name,i,3),x,y)){
        if(!i)s_document_menu=false;
        else{s_document_message[0]=0;s_document_clear_confirm=i==1;s_document_delete_confirm=i==2;}
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t document_bulk_action(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    if(s_document_batch_confirm){
        if(ev->type==UI_GESTURE_TAP){
            if(ui_rect_hit((EpdRect){76,620,244,76},ev->x0,ev->y0))s_document_batch_confirm=false;
            else if(ui_rect_hit((EpdRect){364,620,244,76},ev->x0,ev->y0))document_batch_apply(ctx);
            return APP_REDRAW_PAGE;
        }return APP_REDRAW_NONE;
    }
    if(ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){int next=ctx->leaf+(ev->type==UI_GESTURE_SWIPE_U?1:-1);if(next>=0&&next<leaves()){ctx->leaf=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    uint16_t x=ev->x0,y=ev->y0;
    if(ui_rect_hit((EpdRect){32,75,64,64},x,y)||ui_rect_hit(management_nav(1),x,y)){clear_document_selection();s_view=DOCUMENTS;ctx->leaf=0;return APP_REDRAW_PAGE;}
    if(ui_rect_hit(bulk_filter_rect(0),x,y)){s_document_filter=!s_document_filter;clear_document_selection();s_document_message[0]=0;ctx->leaf=0;scan_documents(ctx);return APP_REDRAW_PAGE;}
    if(ui_rect_hit(bulk_filter_rect(1),x,y)){content_sort_open(true);return APP_REDRAW_PAGE;}
    if(ui_rect_hit(bulk_filter_rect(2),x,y)){search_begin();return APP_REDRAW_PAGE;}
    for(int n=0;n<3;n+=2)if(ui_rect_hit(management_nav(n),x,y)){int next=ctx->leaf+(n==0?-1:1);if(next>=0&&next<leaves())ctx->leaf=next;return APP_REDRAW_PAGE;}
    for(int i=0;i<5;++i)if(ui_rect_hit(batch_rect(i),x,y)){
        s_document_message[0]=0;
        if(i==0){for(size_t j=(size_t)ctx->leaf*BOOK_BULK_ROWS;j<s_document_count&&j<(size_t)(ctx->leaf+1)*BOOK_BULK_ROWS;++j)s_documents[j].selected=true;}
        else if(i==1)clear_document_selection();
        else if(i==2){clear_document_selection();ctx->leaf=0;scan_documents(ctx);}
        else if(!document_selected_count())snprintf(s_document_message,sizeof(s_document_message),"请先选择文档");
        else{s_document_batch_action=i;s_document_batch_confirm=true;}
        return APP_REDRAW_PAGE;
    }
    for(int row=0;row<BOOK_BULK_ROWS;++row)if(ui_rect_hit(management_row(row),x,y)){size_t i=(size_t)ctx->leaf*BOOK_BULK_ROWS+row;if(i<s_document_count)s_documents[i].selected=!s_documents[i].selected;return APP_REDRAW_PAGE;}
    return APP_REDRAW_NONE;
}
// 正文高度三等分；页眉、页脚保留各自操作。/ Split the body into thirds, keeping header/footer actions separate.


static app_redraw_t action_at(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_view == IMPORT) {
        if (y < 160 && x < 120) { s_view = SHELF; return APP_REDRAW_PAGE; }
        int tab = ui_nav_hit(x, y);
        if (tab == 1) { s_view = SHELF; return APP_REDRAW_PAGE; }
        if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        for (int i = 0; i < 4; ++i) if (ui_rect_hit(import_rect(i), x, y)) {
            if (i == 0) {
                extern const app_desc_t app_files;
                ctx->request_app = &app_files;
            } else {
                extern const app_desc_t app_transfer;
                if (i == 1) app_transfer_request_wifi_upload();
                else if (i == 2) app_transfer_request_hotspot_start();
                else app_transfer_request_usb_start();
                ctx->request_app = &app_transfer;
            }
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == TOC) {
        if (s_toc_jump_open) {
            int choice = book_toc_jump_hit(x, y);
            if (choice == BOOK_TOC_JUMP_TRACK) {
                s_toc_jump_percent = book_toc_jump_percent(x);
                return APP_REDRAW_PAGE;
            }
            if (choice == BOOK_TOC_JUMP_CONFIRM)
                ctx->leaf = book_toc_page_from_percent(s_toc_jump_percent, leaves());
            s_toc_jump_open = s_toc_jump_drag = false;
            return APP_REDRAW_PAGE;
        }
        int target = book_toc_hit(x, y, ctx->leaf, book_navigation_count());
        if (target == BOOK_TOC_BACK) {
            s_message[0] = 0;
            set_reader_view(READING);
            s_reader_panel = READER_PANEL_NONE;
            return APP_REDRAW_PAGE;
        }
        if (target == BOOK_TOC_PREV || target == BOOK_TOC_NEXT ||
            target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT) {
            int step = target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT ? 10 : 1;
            int next = ctx->leaf + (target == BOOK_TOC_PREV || target == BOOK_TOC_SKIP_PREV ? -step : step);
            if (next < 0) next = 0;
            if (next >= leaves()) next = leaves() - 1;
            if (next != ctx->leaf) { ctx->leaf = next; return APP_REDRAW_PAGE; }
            return APP_REDRAW_NONE;
        }
        if (target == BOOK_TOC_JUMP && book_navigation_count()) {
            s_toc_jump_open = true;
            s_toc_jump_percent = (ctx->leaf + 1) * 100 / leaves();
            return APP_REDRAW_PAGE;
        }
        if(target>=0&&book_kind()==BOOK_KIND_MARKDOWN&&x<106+book_markdown_navigation_depth((size_t)target)*24){
            if(book_markdown_navigation_toggle((size_t)target)){s_selected_toc=SIZE_MAX;if(ctx->leaf>=leaves())ctx->leaf=leaves()-1;return APP_REDRAW_PAGE;}
        }
        if (target >= 0 && !s_message[0]) {
            size_t chapter = book_navigation_chapter((size_t)target);
            if (chapter >= book_chapter_count()) return APP_REDRAW_NONE;
            save_progress();
            reader_remember_jump();
            set_reader_view(READING);
            if (load_chapter_at(ctx, chapter, 0, false, book_navigation_anchor((size_t)target),
                                book_navigation_source_offset((size_t)target))) {
                s_selected_toc = (size_t)target;
                char entry_title[sizeof(s_title)];
                if (book_navigation_title((size_t)target, entry_title, sizeof(entry_title)) == ESP_OK)
                    copy_text(s_title, sizeof(s_title), entry_title);
                s_view = READING;
                s_reader_panel = READER_PANEL_NONE;
                save_progress();
            } else set_reader_view(s_text ? TOC : SHELF);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == MANAGE) return manage_action(ctx, x, y);

    if (s_view == BULK) {
        if (ui_rect_hit(manage_back_rect(), x, y)) {
            if (s_batch_confirm) s_batch_confirm = false;
            else { clear_selection(); s_batch_message[0] = 0; s_view = SHELF; ctx->leaf = 0; }
            return APP_REDRAW_PAGE;
        }
        app_redraw_t result = batch_action(ctx, x, y);
        if (result != APP_REDRAW_NONE) return result;
    }
    if (s_view == READING) {
        if (s_toolbar) return reader_panel_action(ctx, x, y);
        if (!s_reader_fullscreen && ui_rect_hit((EpdRect){32, 75, 64, 64}, x, y))
            return reader_return(ctx, false);

        if(!ui_rect_hit(reader_area(),x,y))return APP_REDRAW_NONE;
        if(x<UI_LOCK_WIDTH/3)return turn_page(ctx,-1);
        if(x>=UI_LOCK_WIDTH*2/3)return turn_page(ctx,1);
        return reader_show_panel(READER_PANEL_TOOLS);
    }
    if (s_view == SHELF || s_view == BULK) {
        if (s_view == SHELF) {
            int tab = ui_nav_hit(x, y);
            if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
            if (ui_rect_hit(shelf_page_arrow_rect(-1), x, y)) return shelf_turn_page(ctx, -1);
            if (ui_rect_hit(shelf_page_arrow_rect(1), x, y)) return shelf_turn_page(ctx, 1);
        }
        if (s_view == SHELF && ui_rect_hit(shelf_sort_rect(), x, y)) {
            content_sort_open(false);
            return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF && ui_rect_hit(shelf_manage_rect(), x, y)) {
            clear_selection();
            s_batch_confirm = false;
            s_batch_message[0] = 0;
            s_view = BULK;
            ctx->leaf = 0;
            return APP_REDRAW_PAGE;
        }
        if (s_view == BULK) {
        if (ui_rect_hit(bulk_filter_rect(0), x, y)) {
            s_filter = (s_filter + 1) % 3; clear_selection(); s_batch_message[0] = 0;
            sort_shelf(ctx);
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(bulk_filter_rect(1), x, y)) {
            content_sort_open(false);
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(bulk_filter_rect(2), x, y)) { search_begin(); return APP_REDRAW_PAGE; }
        }
        if (s_view == SHELF && s_save_failed && y >= UI_CONTENT_BOTTOM - UI_PX_CAPTION && y < UI_CONTENT_BOTTOM) {
            retry_progress();
            return APP_REDRAW_PAGE;
        }
    }
    int nav = s_view==BULK?management_nav_hit(x,y):ui_bar_hit(x,y,3);
    if (nav == 0 || nav == 2) {
        int next = ctx->leaf + (nav == 0 ? -1 : 1);
        if (next >= 0 && next < leaves()) ctx->leaf = next;
        return APP_REDRAW_PAGE;
    }
    if (nav == 1) {
        s_message[0] = 0;
        if (s_view == BULK) { clear_selection(); s_batch_message[0] = 0; }
        s_view = s_view == BULK ? SHELF : BULK; s_batch_confirm = false;
        return APP_REDRAW_PAGE;
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return APP_REDRAW_NONE;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    for (int row = 0; row < rows; ++row) if (ui_rect_hit(row_rect(row), x, y)) {
        int i = ctx->leaf * rows + row;
        if (s_view == BULK && i < s_visible_count) {
            toggle_selection(i);
            s_batch_message[0] = 0;
        }
        else if (s_view == SHELF && i < s_visible_count) {
            if (s_shelf[i].removed) {
                s_managed = s_shelf[i]; s_view = MANAGE; s_file_removed = s_delete_confirm = true; s_clear_confirm = false;
                copy_text(s_manage_message, sizeof(s_manage_message), "文件已删除，进度清理失败，请重试");
            } else open_book(ctx, s_shelf[i].path);
        }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static void on_enter(app_ctx_t* ctx) {
    // 设置页试按的旧事件不得在开书后翻页。/ Discard remote tests from earlier pages before opening a book.

    book_layout_set_spacing(app_settings_book_line_spacing(), app_settings_book_paragraph_spacing());
    book_layout_set_images_visible(!app_settings_reader_hide_images());
    book_layout_set_typography(((int)app_settings_book_tracking() - 2) * 2);
    book_layout_set_first_line_indent(app_settings_book_indent());
    book_layout_set_reading_line(app_settings_book_reading_line());
    book_layout_set_reading_line_offset(app_settings_book_reading_line_offset());
    s_reader_fullscreen = app_settings_reader_fullscreen();
    s_reader_immersive = app_settings_reader_immersive();
    s_bookmark_edit = s_bookmark_delete_confirm = s_bookmark_delete_error = false;
    s_bookmark_selected = 0;
    clear_selection();
    s_view = s_requested_manage ? BULK : SHELF;
    s_document_delete_confirm=s_document_clear_confirm=s_document_menu=false;s_document_message[0]=0;
    s_sort_open=s_document_batch_confirm=false;
    clear_document_selection();
    s_book_options=content_options_load(false,app_settings_shelf_recent_sort());
    s_document_options=content_options_load(true,false);
    s_document_starred_only=false;
    s_reader_return_home = false;
    s_requested_manage = false;
    s_presented_view = -1;
    ctx->leaf = 0;
    s_px = app_settings_book_px();
    s_recent_sort = s_book_options.mode==CONTENT_RECENT;
    s_margin = app_settings_book_margin();
    s_line_spacing = app_settings_book_line_spacing();
    // 传书页已停止并 join HTTP，安全注销且只丢弃对应路径的旧进度。
    // Transfer has stopped and joined HTTP; safely unregister only invalidated paths.
    pending_drop_invalidated();
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_progress_drag = false;
    s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_du_count = 0;
    s_reader_cleanup = false;
    s_poll_ms = 0;
    read_pico_sd_info_t sd = {0};
    esp_err_t media_err = read_pico_sd_get_info(&sd);
    bool reuse = s_shelf_cache_valid && media_err != ESP_ERR_NOT_FINISHED &&
                 sd.present == s_cache_sd_present && sd.mounted == s_cache_sd_mounted &&
                 s_store_revision == book_store_revision();
    s_scan_pending = !reuse;
    if (reuse) {
        refresh_cached_progress(ctx);
        sort_shelf(ctx);
        (void)open_requested_book(ctx);
    } else {
        s_count = s_visible_count = 0;
        s_message[0] = 0;
        s_storage[0] = 0;
        invalidate_covers();
        // 已指定文件可直接打开；书架目录留到真正进入书架时再扫描。
        // Open a requested file directly; scan the shelf only when it is actually shown.
        if (open_requested_book(ctx)) {
            s_scan_pending = false;
            return;
        }
        read_pico_sd_start_probe();
    }
}
static void book_on_exit(app_ctx_t* ctx) {
    s_presented_view = -1;
    free(s_editor_cover); s_editor_cover = NULL;
    s_pressed_control = -1;
    s_reader_slider = -1;
    s_progress_drag = false;
    save_progress();
    s_reader_return_home = false;
    free_book();
    if (s_prep_task) { vTaskDelete(s_prep_task); s_prep_task = NULL; }
    free(s_next_fb);
    s_next_fb = NULL;
    if (s_prep_done) { vSemaphoreDelete(s_prep_done); s_prep_done = NULL; }
    if (s_draw_lock) { vSemaphoreDelete(s_draw_lock); s_draw_lock = NULL; }
}
// 先停止使用旧卡句柄与字体预渲染，主循环随后切换内置字体。
// Stop old-card handles and font preparation before the loop switches to the builtin font.
static void book_on_media_lost(app_ctx_t* ctx) {
    lock_draw();
    save_progress();
    free_book();
    unlock_draw();
    free(s_editor_cover); s_editor_cover = NULL;
    s_view = SHELF;
    s_count = s_visible_count = 0;
    s_shelf_cache_valid = false;
    free(s_shelf); s_shelf = NULL; s_shelf_capacity = 0;
    invalidate_covers();
    ctx->leaf = 0;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_progress_drag = false;
    s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_scan_pending = true;
    s_du_count = 0;
    copy_text(s_storage, sizeof(s_storage), "TF 卡已移除");
    copy_text(s_message, sizeof(s_message), "TF 卡已移除");
}
static void book_on_media_ready(app_ctx_t* ctx) {
    (void)ctx;
    s_shelf_cache_valid = false;
    if (s_view == SHELF) s_scan_pending = true;
}
// 控件编号仅用于保持按下与抬起命中同一个目标。/ IDs pair a press with release on the same control.
static int control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
    if (s_view == BULK) {
        *rect = manage_back_rect();
        if (ui_rect_hit(*rect, x, y)) return 711;
    }
    if (s_view == IMPORT) {
        *rect = (EpdRect){36, 79, 58, 58};
        if (ui_rect_hit(*rect, x, y)) return 700;
        for (int i = 0; i < 4; ++i) {
            *rect = import_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 701 + i;
        }
        int tab = ui_nav_hit(x, y);
        if (tab >= 0) { *rect = (EpdRect){tab * 171, UI_NAV_TOP, 171, UI_NAV_HEIGHT}; return 705 + tab; }
        return -1;
    }

    if (s_view == BULK && s_batch_confirm) {
        for (int i = 0; i < 2; ++i) { *rect = ui_row_rect(i, 2, 620, UI_BTN_H); if (ui_rect_hit(*rect, x, y)) return 600 + i; }
        return -1;
    }
    if (s_view == BULK) for (int i = 0; i < 6; ++i) { *rect = batch_rect(i); if (ui_rect_hit(*rect, x, y)) return 610 + i; }
    if (s_view == MANAGE) {
        if(!s_clear_confirm&&!s_file_removed)for(int i=0;i<2;++i){*rect=manage_aux_rect(i);if(ui_rect_hit(*rect,x,y))return i?404:405;}
        int count = s_clear_confirm || s_file_removed ? 2 : 3;
        for (int i = 0; i < count; ++i) {
            *rect = manage_rect(i, count);
            if (ui_rect_hit(*rect, x, y)) return s_clear_confirm ? 300 + i : s_file_removed && i == 1 ? 403 : 400 + i;
        }
        return -1;
    }
    if (s_clear_confirm) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 610, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 300 + i;
        }
        return -1;
    }
    if (s_view == READING) {
        if (s_reader_panel == READER_PANEL_TOOLS) for (int i = 0; i < BOOK_TOOL_COUNT; ++i) {
            *rect = tool_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 200 + i;
        }
        return -1;
    }
    if (s_view != SHELF) for (int i = 0; i < 3; ++i) {
        *rect = s_view==BULK?management_nav(i):ui_bar_rect(i,3);
        if (ui_rect_hit(*rect, x, y)) return 100 + i;
    }
    if (s_view == SHELF && ui_rect_hit(shelf_manage_rect(), x, y)) { *rect = shelf_manage_rect(); return 114; }
    if (s_view == SHELF && ui_rect_hit(shelf_sort_rect(), x, y)) { *rect = shelf_sort_rect(); return 115; }
    if (s_view == SHELF) {
        for (int i = 0; i < 2; ++i) {
            *rect = shelf_page_arrow_rect(i ? 1 : -1);
            if (ui_rect_hit(*rect, x, y)) return 116 + i;
        }
    }
    if (s_view == BULK) {
        for (int i = 0; i < 3; ++i) {
            *rect = bulk_filter_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 110 + i;
        }
    }
    *rect = (EpdRect){UI_MARGIN, UI_CONTENT_BOTTOM - UI_PX_CAPTION, ui_content_width(), UI_PX_CAPTION};
    if (s_view == SHELF && s_save_failed && ui_rect_hit(*rect, x, y)) return 113;
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return -1;
    int rows = s_view == BULK ? BOOK_BULK_ROWS : shelf_rows();
    int count = s_visible_count;
    for (int row = 0; row < rows && ctx->leaf * rows + row < count; ++row) {
        *rect = row_rect(row);
        if (ui_rect_hit(*rect, x, y)) return row;
    }
    return -1;
}
static app_redraw_t paint_control(app_ctx_t* ctx, EpdRect rect) {
    render(ctx, ctx->fb);
    s_mode = MODE_DU;
    s_area = rect;
    return APP_REDRAW_AREA;
}
static app_redraw_t gesture_event(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if(s_sort_open)return ev->type==UI_GESTURE_TAP?content_sort_action(ctx,ev->x0,ev->y0):APP_REDRAW_NONE;
    if(s_view==DOCUMENT_BULK)return document_bulk_action(ctx,ev);
    if(s_view==MANAGE)return ev->type==UI_GESTURE_TAP?manage_action(ctx,ev->x0,ev->y0):APP_REDRAW_NONE;
    if(s_view==DOCUMENTS&&s_document_menu)return ev->type==UI_GESTURE_TAP?document_details_action(ctx,ev->x0,ev->y0):APP_REDRAW_NONE;
    if(s_document_zoom){
        if(ev->type==UI_GESTURE_PRESS){s_zoom_origin=s_zoom_top;s_zoom_moved=false;return APP_REDRAW_NONE;}
        if(ev->type==UI_GESTURE_MOVE||ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){
            int64_t next=(int64_t)s_zoom_origin+((int)ev->y0-(int)ev->y)*s_document_image.width/684;
            if(abs((int)ev->y-(int)ev->y0)>8)s_zoom_moved=true;
            if(next<0)next=0;
            if(next>UINT32_MAX)next=UINT32_MAX;
            if(!prepare_document_zoom((uint32_t)next))return APP_REDRAW_NONE;
            return APP_REDRAW_PAGE;
        }
        if(ev->type==UI_GESTURE_TAP&&!s_zoom_moved){s_document_zoom=false;free(s_zoom_gray);s_zoom_gray=NULL;return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(s_view==READING&&!s_clear_confirm){s_stats_activity_ms=ctx->now_ms;return reader_controls_gesture(ctx,ev);}
    if(s_view==TOC&&!s_toc_jump_open&&ev->type==UI_GESTURE_TAP&&ev->y0>=1096)return reader_show_tab(ctx,ev->x0*5/684);
    if((s_view==SHELF||s_view==DOCUMENTS)&&ev->type==UI_GESTURE_TAP){
        if(ui_rect_hit(content_tab(0),ev->x0,ev->y0)){s_view=SHELF;s_document_mode=false;ctx->leaf=0;prepare_covers(ctx);return APP_REDRAW_PAGE;}
        if(ui_rect_hit(content_tab(1),ev->x0,ev->y0)){s_view=DOCUMENTS;s_document_mode=true;s_document_delete_confirm=s_document_clear_confirm=s_document_menu=false;ctx->leaf=0;scan_documents(ctx);return APP_REDRAW_PAGE;}
    }
    if(s_view==DOCUMENTS){
        if(ev->type==UI_GESTURE_TAP){
            for(int direction=-1;direction<=1;direction+=2)if(ui_rect_hit(ui_nav_pager_rect(direction),ev->x0,ev->y0)){
                int next=ctx->leaf+direction;if(next>=0&&next<leaves()){ctx->leaf=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;
            }
            if(ui_rect_hit(content_action(0),ev->x0,ev->y0)){clear_document_selection();s_document_batch_confirm=false;s_document_message[0]=0;s_view=DOCUMENT_BULK;ctx->leaf=0;return APP_REDRAW_PAGE;}
            if(ui_rect_hit(content_action(1),ev->x0,ev->y0)){content_sort_open(true);return APP_REDRAW_PAGE;}
            int tab=ui_nav_hit(ev->x0,ev->y0);if(tab>=0){ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
        }
        if(ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){int next=ctx->leaf+(ev->type==UI_GESTURE_SWIPE_U?1:-1);if(next>=0&&next<leaves()){ctx->leaf=next;return APP_REDRAW_PAGE;}}
        if(ev->type==UI_GESTURE_TAP||ev->type==UI_GESTURE_LONG_PRESS){for(int row=0;row<document_rows();row++)if(ui_rect_hit(document_row(row),ev->x0,ev->y0)){size_t i=(size_t)ctx->leaf*document_rows()+row;if(i>=s_document_count)break;
            if(ev->type==UI_GESTURE_LONG_PRESS){s_document_detail=s_documents[i];s_document_menu=true;s_document_delete_confirm=s_document_clear_confirm=false;s_document_message[0]=0;return APP_REDRAW_PAGE;}
            char path[288];snprintf(path,sizeof(path),"%s",s_documents[i].path);if(open_book(ctx,path))return APP_REDRAW_PAGE;return APP_REDRAW_NONE;
        }}return APP_REDRAW_NONE;
    }

    if (s_view == TOC) {
        if (s_toc_jump_open) {
            if (ev->type == UI_GESTURE_PRESS &&
                book_toc_jump_hit(ev->x0, ev->y0) == BOOK_TOC_JUMP_TRACK) {
                s_toc_jump_origin=s_toc_jump_percent;s_toc_jump_drag=true;
                return reader_toc_preview(ctx,ev->x0,true);
            }
            if(s_toc_jump_drag){
                if(ev->type==UI_GESTURE_LONG_PRESS)return APP_REDRAW_NONE;
                if(ev->type==UI_GESTURE_MOVE)return reader_toc_preview(ctx,ev->x,false);
                s_toc_jump_drag=false;
                if(ev->type==UI_GESTURE_CANCEL&&!(ctx->released&&ctx->touch&&!ctx->consumed))return reader_toc_preview(ctx,92+s_toc_jump_origin*5,true);
                return reader_toc_preview(ctx,ev->x,true);
            }
            if (ev->type == UI_GESTURE_TAP) return action_at(ctx, ev->x0, ev->y0);
            return APP_REDRAW_NONE;
        }
        if (ev->type == UI_GESTURE_TAP) {
            int start = book_toc_hit(ev->x0, ev->y0, ctx->leaf, book_navigation_count());
            int end = book_toc_hit(ev->x, ev->y, ctx->leaf, book_navigation_count());
            if (start != -1 && start == end) return action_at(ctx, ev->x0, ev->y0);
        }
        if (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D) {
            int next = ctx->leaf + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
            if (next >= 0 && next < leaves()) { ctx->leaf = next; return APP_REDRAW_PAGE; }
        }
        return APP_REDRAW_NONE;
    }
    if(s_view==EDIT)return editor_action(ctx,ev);
    if(s_view==SEARCH)return search_input_action(ctx,ev);

    if (s_view == SHELF && ev->type == UI_GESTURE_TAP && ui_nav_hit(ev->x0, ev->y0) >= 0) {
        ui_nav_request(ctx, ui_nav_hit(ev->x0, ev->y0));
        return APP_REDRAW_NONE;
    }
    EpdRect start_rect = {0}, end_rect = {0};
    int start = control_at(ctx, ev->x0, ev->y0, &start_rect);
    int end = control_at(ctx, ev->x, ev->y, &end_rect);
    if (ev->type == UI_GESTURE_PRESS) {
        if (s_view == SHELF && start >= 0 && start < shelf_rows()) {
            s_pressed_control = -1;
            return APP_REDRAW_NONE;
        }
        s_pressed_control = start;
        return start >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
    }
    bool decorated = s_pressed_control >= 0;
    s_pressed_control = -1;
    if (ev->type == UI_GESTURE_LONG_PRESS && start == end) {
        if (s_view == SHELF && start >= 0 && start < shelf_rows() && !s_clear_confirm) {
            s_managed = s_shelf[ctx->leaf * shelf_rows() + start];
            s_view = MANAGE;
            s_clear_confirm = false;
            s_delete_confirm = s_file_removed = s_managed.removed;
            copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ? "文件已删除，进度清理失败，请重试" : "");
            return APP_REDRAW_PAGE;
        }

    }
    if (ev->type == UI_GESTURE_TAP && !s_scan_pending) {
        if (start >= 0 && start == end) {
            app_redraw_t result = action_at(ctx, ev->x0, ev->y0);
            if (ctx->request_app || ctx->request_menu || ctx->request_return) return result;
            return result != APP_REDRAW_NONE ? result : paint_control(ctx, start_rect);
        }
        if (start < 0 && end < 0 && s_view == READING && !s_clear_confirm)
            return action_at(ctx, ev->x0, ev->y0);
    }
    if (!s_clear_confirm && !s_scan_pending) {
        if (s_view == SHELF && (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R)) {
            app_redraw_t result = shelf_turn_page(ctx, ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
            if (result != APP_REDRAW_NONE) return result;
        } else if ((s_view == BULK && !s_batch_confirm) &&
                   (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
            int next = ctx->leaf + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
            if (next >= 0 && next < leaves()) {
                ctx->leaf = next;
                return APP_REDRAW_PAGE;
            }
        }
    }
    return decorated ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if(s_sort_open){s_sort_open=false;return APP_REDRAW_PAGE;}
    if(s_view==DOCUMENT_BULK){
        if(s_document_batch_confirm){s_document_batch_confirm=false;return APP_REDRAW_PAGE;}
        if(key!=UI_KEY_2&&leaves()>1){int next=ctx->leaf+(key==UI_KEY_1?-1:1);if(next<0||next>=leaves())return APP_REDRAW_NONE;ctx->leaf=next;return APP_REDRAW_PAGE;}
        clear_document_selection();
        if(key==UI_KEY_2){ui_nav_request(ctx,0);return APP_REDRAW_NONE;}
        s_view=DOCUMENTS;ctx->leaf=0;return APP_REDRAW_PAGE;
    }
    if(s_document_zoom){if(key==UI_KEY_2){s_document_zoom=false;free(s_zoom_gray);s_zoom_gray=NULL;}else {uint32_t span=(uint32_t)((uint64_t)1216*s_document_image.width/684);uint32_t next=key==UI_KEY_1?(s_zoom_top>span?s_zoom_top-span:0):s_zoom_top+span;(void)prepare_document_zoom(next);}return APP_REDRAW_PAGE;}
    if(s_view==DOCUMENTS){if(s_document_menu){if(s_document_delete_confirm||s_document_clear_confirm)s_document_delete_confirm=s_document_clear_confirm=false;else s_document_menu=false;return APP_REDRAW_PAGE;}if(key==UI_KEY_2){ui_nav_request(ctx,0);return APP_REDRAW_NONE;}int next=ctx->leaf+(key==UI_KEY_1?-1:1);if(next>=0&&next<leaves()){ctx->leaf=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}

    if(s_view==READING&&s_toolbar){
        s_reader_multi=(reader_multi_t){.blocked=true};
        if(key==UI_KEY_2)return s_reader_panel==READER_PANEL_HELP?reader_help_close():reader_show_panel(READER_PANEL_NONE);
        int next=s_control_page+(key==UI_KEY_1?-1:1),count=reader_control_count();
        if(next>=0&&next*reader_control_rows()<count){s_control_page=next;return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    s_pressed_control = -1;
    if (s_view == TOC && s_toc_jump_open) {
        if (key == UI_KEY_2) s_toc_jump_open = s_toc_jump_drag = false;
        else if (key == UI_KEY_1 || key == UI_KEY_3) {
            s_toc_jump_percent += key == UI_KEY_1 ? -5 : 5;
            if (s_toc_jump_percent < 0) s_toc_jump_percent = 0;
            if (s_toc_jump_percent > 100) s_toc_jump_percent = 100;
        }
        return APP_REDRAW_PAGE;
    }
    if (s_view != READING && s_view != TOC) {
        if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
        if (s_view == EDIT) { free(s_editor_cover); s_editor_cover = NULL; s_view = MANAGE; return APP_REDRAW_PAGE; }
        if (s_view == SEARCH) { search_finish(ctx, false); return APP_REDRAW_PAGE; }
        if (s_batch_confirm) { s_batch_confirm = false; return APP_REDRAW_PAGE; }
        if(s_view==BULK&&leaves()>1){int next=ctx->leaf+(key==UI_KEY_1?-1:1);if(next<0||next>=leaves())return APP_REDRAW_NONE;ctx->leaf=next;return APP_REDRAW_PAGE;}
        if (s_view == MANAGE || s_view == BULK || s_view == IMPORT) {
            if (s_view == BULK) { clear_selection(); s_batch_message[0] = 0; }
            s_view = SHELF; return APP_REDRAW_PAGE;
        }
        if (s_view == SHELF) return shelf_turn_page(ctx,key==UI_KEY_1?-1:1);
    }
    if (s_scan_pending || s_clear_confirm) return APP_REDRAW_NONE;
    if (key == UI_KEY_2) {
        if (s_view == BULK) s_view = SHELF;
        else if (s_view == SHELF) { ui_nav_request(ctx, 3); return APP_REDRAW_NONE; }
        else if (s_view == READING) {
            s_reader_slider = -1;
    s_progress_drag = false;
            s_reader_panel = READER_PANEL_TOOLS;s_control_page=0;
            invalidate_prep();
        } else if (s_view == TOC) {
            set_reader_view(s_text ? READING : SHELF);
            s_reader_panel = READER_PANEL_NONE;
        } else {
            ctx->request_menu = true;
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_PAGE;
    }
    if (key != UI_KEY_1 && key != UI_KEY_3) return APP_REDRAW_NONE;
    int dir = key == UI_KEY_1 ? -1 : 1;
    if (s_view == READING) { s_stats_activity_ms = ctx->now_ms; return turn_page(ctx, dir); }
    int next = ctx->leaf + dir;
    if (next < 0 || next >= leaves()) return APP_REDRAW_NONE;
    ctx->leaf = next;
    return APP_REDRAW_PAGE;
}
static app_redraw_t on_power_short(app_ctx_t* ctx) {(void)ctx;return APP_REDRAW_NONE;}
static app_redraw_t on_key_long(app_ctx_t* ctx,int key){
    if(key!=UI_KEY_2)return APP_REDRAW_NONE;
    if(s_view==READING||s_view==TOC)return reader_return(ctx,true);
    ui_nav_request(ctx,0);return APP_REDRAW_NONE;
}
static bool menu_handle_enabled(app_ctx_t* ctx) {
    (void)ctx;
    return !s_sort_open && s_view != MANAGE && s_view != BULK && s_view != DOCUMENT_BULK && s_view != READING && s_view != SHELF && s_view != DOCUMENTS && s_view != TOC && s_view != EDIT && s_view != SEARCH;
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction) {
    int next = ctx->leaf + direction;
    if (s_view != SHELF || s_scan_pending || s_clear_confirm || next < 0 || next >= leaves())
        return APP_REDRAW_NONE;
    ctx->leaf = next;
    s_pressed_control = -1;
    prepare_covers(ctx);
    render(ctx, ctx->fb);
    s_area = (EpdRect){0,196,UI_LOCK_WIDTH,UI_NAV_TOP-196};
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    track_ticket_stats(ctx);
    const cst836u_touch_t *touch=ctx->touch;
    bool multi_enabled=s_view==READING&&!s_toolbar&&!s_clear_confirm&&!s_document_zoom&&!ctx->consumed;
    if(multi_enabled&&touch&&touch->count==2)multi_enabled=ui_rect_hit(reader_area(),touch->points[0].x,touch->points[0].y)&&ui_rect_hit(reader_area(),touch->points[1].x,touch->points[1].y);
    int multi=reader_multi_feed(&s_reader_multi,multi_enabled,touch&&touch->touched,touch?touch->count:0,
        touch?touch->points[0].x:0,touch?touch->points[0].y:0,touch?touch->points[1].x:0,touch?touch->points[1].y:0,ctx->now_ms);
    if(multi>=0){s_stats_activity_ms=ctx->now_ms;return reader_execute_shortcut(ctx,multi);}

    if (ctx->consumed) return APP_REDRAW_NONE;
    reader_prefetch_idle(ctx);
    if(s_view==TOC&&s_toc_jump_open&&s_toc_jump_drag&&touch&&touch->touched&&touch->count==1)return reader_toc_preview(ctx,touch->x,false);
    if(s_progress_drag&&s_reader_panel==READER_PANEL_TOOLS&&touch&&touch->touched&&touch->count==1)return reader_progress_preview(ctx,touch->x);
    if(s_reader_slider>=0&&touch&&touch->touched&&touch->count==1&&!s_reader_slider_endpoint){app_redraw_t preview=reader_style_preview(ctx,touch->x);if(preview!=APP_REDRAW_NONE)return preview;}
    if (s_reader_notice[0] && ctx->now_ms >= s_reader_notice_until) {
        s_reader_notice[0] = 0;
        if(s_view==READING&&s_toolbar)return APP_REDRAW_PAGE;
        if (s_view == READING) {
            // 区域刷新不会自动重绘帧缓冲；先移除提示，再只更新提示所在区域。
            // Area refreshes do not rerender the framebuffer; erase the toast before updating its region.
            render(ctx, ctx->fb);
            s_area = (EpdRect){208, 150, 268, 68};
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
    }
    if (s_save_failed && ctx->now_ms - s_save_retry_ms >= 15000) {
        s_save_retry_ms = ctx->now_ms;
        bool failed = s_save_failed;
        retry_progress();
        if (failed != s_save_failed) { invalidate_prep(); return APP_REDRAW_PAGE; }
    }
    if (s_scan_pending && ctx->now_ms - s_poll_ms >= 500) {
        s_poll_ms = ctx->now_ms;
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_scan_pending = false;
        scan_shelf(ctx);
        scan_documents(ctx);
        (void)open_requested_book(ctx);
        return APP_REDRAW_PAGE;
    }
    if (s_view == SHELF && !(ctx->touch && ctx->touch->touched)) {
        for (int row = 0; row < shelf_rows(); ++row) {
            unsigned bit = 1u << row;
            if (!(s_cover_pending_mask & bit)) continue;
            s_cover_pending_mask &= ~bit;
            int index = ctx->leaf * shelf_rows() + row;
            if (index >= s_visible_count || s_covers[row].index != index) continue;
            bool pending = false;
            s_covers[row].gray = load_cover_gray(s_shelf[index].path, s_shelf[index].name,
                                                 s_shelf[index].author, true, &pending);
            if (!s_covers[row].gray) continue;
            EpdRect area = row_rect(row);
            render(ctx, ctx->fb);
            s_area = area;
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
    }
    // 目录暂借系统字体，不应把正文按系统字体重新分页。/ The directory borrows the system face without repaginating the reader.
    if (s_view == READING && s_text && strcmp(s_font_path, ttf_font_path())) {
        size_t off = book_layout_page_start_offset(s_page);
        save_progress();
        lock_draw();
        invalidate_prep();
        bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        if (ok) s_page = book_layout_page_for_offset(off);
        copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
        unlock_draw();
        if (!ok) { free_book(); s_view = SHELF; copy_text(s_message, sizeof(s_message), "字体重排失败，请重新打开图书"); }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static void before_lock(app_ctx_t* ctx) {
    s_progress_drag=false;
    s_reader_multi=(reader_multi_t){.blocked=true};
    track_ticket_stats(ctx);
    save_progress();
    // 锁屏票根始终以当前正在阅读的书为准；不要依赖书架最近项是否已经同步。
    if (s_text && s_path[0]) book_progress_set_last_path(s_path);
    flush_ticket_stats();
    s_stats_last_ms = 0;
}
static EpdRect area_hint(app_ctx_t* ctx) { (void)ctx; return s_area; }

const app_desc_t app_book = {
    .title = "图书 Books", .detail = "TF 卡 txt / epub 阅读", .enter_full = false, .owns_keys = true,
    .defer_middle_short = true,
    .menu_handle_enabled = menu_handle_enabled,
    .render = render, .present = present, .on_enter = on_enter, .on_exit = book_on_exit,
    .on_media_lost = book_on_media_lost, .on_media_ready = book_on_media_ready,
    .on_before_lock = before_lock,
    .on_gesture = gesture_event, .on_key = on_key, .on_key_long = on_key_long,
    .on_power_short = on_power_short,
    .on_tick = on_tick, .area_hint = area_hint,
};
