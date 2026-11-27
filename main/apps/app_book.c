#include <stdatomic.h>
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 自定义固件修改：图书与文档共用搜索和排序入口，排序偏好独立保存。
 * English: Books and documents share search and sort controls while keeping separate sort preferences.
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
 * 用户修订：长按保留单项管理；内容页暂时关闭批量入口，批量逻辑留待迁移到存储卡管理。
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
 * User revision: keep individual actions on long press and suspend the bulk entry until Storage Management takes over.
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
#include "book_hyphen.h"
#include "book_cover.h"
#include "book_epub.h"
#include "book_policy.h"
#include "book_progress.h"
#include "book_progress_kv.h"
#include "book_progress_snapshot.h"
#include "book_recent_index.h"
#include "book_shelf_disk.h"
#include "book_shelf_index.h"
#include "book_title.h"
#include "book_ticket.h"
#include "operation_log.h"
#include "shelf_perf.h"
#include "book_index_cache.h"
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
#include "book/record_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "read_pico_init.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "read_pico_search.h"
#include "settings.h"
#include "sleep_resume.h"
#include "reader_input.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_image_dither.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_nav.h"

// 阅读工具栏图标盒边长；行内图标在 1096..1216 的工具条里以 1138 为中心。
// Reader toolbar icon box; the row centres on y=1138 inside the 1096..1216 bar.
#define READER_TOOL_ICON_PX 40
// 二级面板返回键里的箭头边长；圆底半径 24，原来的箭头是 20 像素高。
// Chevron box inside a secondary panel's back control; the old arrow was 20 px tall.
#define READER_SHEET_BACK_PX 34

#define BOOK_ROWS 13

static int document_rows(void);
#define BOOK_PX_STEP 4
#define BOOK_MARGIN_MIN 8
#define BOOK_MARGIN_MAX 60
#define BOOK_MARGIN_CHOICES (BOOK_MARGIN_MAX - BOOK_MARGIN_MIN + 1)
#define BOOK_TOOL_COUNT 5
#define BOOKMARK_MAX 24

// 保留现行页面编号，供模拟器及历史性能日志对照。/ Preserve live page IDs for simulator and performance logs.
typedef enum { SHELF, READING, TOC, MANAGE, SEARCH = 6, EDIT, DOCUMENTS } book_view_t;
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
typedef book_shelf_entry_t shelf_entry_t;

/* 用户授权新增 Markdown 文档页、原生阅读器和长图查看。 Modified derivative. */
static document_entry_t *s_documents;
static size_t s_document_count;
static size_t *s_document_matches, s_document_match_count, s_document_match_capacity;
static bool s_document_mode, s_document_delete_confirm, s_document_clear_confirm, s_document_menu, s_document_starred_only, s_document_completed;
static char s_document_message[64], s_document_query[65];
static document_entry_t s_document_detail;
static void draw_document_details(uint8_t *fb);
static void fit_fixed_text(char *text, int px, int width);
static app_redraw_t document_details_action(app_ctx_t *ctx, const ui_gesture_event_t *event);
static content_options_t s_book_options, s_document_options, s_sort_options;
static bool s_sort_open, s_sort_documents;
static void content_sort_open(bool documents);
static void draw_content_sort(uint8_t *fb);
static document_image_t s_document_image;
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
static bool content_name_matches(const char *name, const char *path, const char *query) {
    if (read_pico_search_match(name, query)) return true;
    const char *base = strrchr(path, '/');
    return read_pico_search_match(base ? base+1 : path, query);
}
// 完整清单由扫描维护；搜索只生成按当前排序排列的结果索引。
// Scans own the full catalog; search builds an index in the current sort order.
static size_t document_visible_count(void) {
    return s_document_query[0] ? s_document_match_count : s_document_count;
}
static document_entry_t *document_visible_at(size_t index) {
    if (index >= document_visible_count()) return NULL;
    return &s_documents[s_document_query[0] ? s_document_matches[index] : index];
}
static bool filter_documents(const char *query) {
    if (!query[0]) {
        free(s_document_matches);
        s_document_matches = NULL;
        s_document_match_count = s_document_match_capacity = 0;
        s_document_query[0] = 0;
        return true;
    }
    if (s_document_count > s_document_match_capacity) {
        size_t *matches = realloc(s_document_matches, s_document_count * sizeof(*matches));
        if (!matches) return false;
        s_document_matches = matches;
        s_document_match_capacity = s_document_count;
    }
    s_document_match_count = 0;
    for (size_t i = 0; i < s_document_count; ++i) {
        if (content_name_matches(s_documents[i].name, s_documents[i].path, query) || s_documents[i].cleanup_pending)
            s_document_matches[s_document_match_count++] = i;
    }
    if (query != s_document_query)
        memcpy(s_document_query, query, strlen(query) + 1);
    return true;
}
static void document_search_memory_notice(void) {
    ui_nav_notice_show(UI_MSG("内存不足，请稍后重试"), esp_timer_get_time()/1000);
}
static void sort_documents(void) {
    if (s_document_count > 1)
        qsort(s_documents, s_document_count, sizeof(*s_documents), compare_documents);
    if (s_document_query[0] && !filter_documents(s_document_query)) {
        filter_documents("");
        document_search_memory_notice();
    }
}
static bool s_document_scan_pending;
static bool s_document_cache_valid;
static unsigned s_document_revision,s_document_import_revision;
static uint32_t s_document_media_epoch;
static void discard_document_cache(void) {
    filter_documents("");
    free(s_documents);
    s_documents=NULL;
    s_document_count=0;
    s_document_cache_valid=false;
    s_document_scan_pending=false;
}
static bool document_cache_same_media(void) {
    return s_document_cache_valid && record_store_ready() &&
        s_document_media_epoch==record_store_media_epoch() &&
        s_document_revision==book_store_revision();
}
static bool document_cache_current(void) {
    return document_cache_same_media() && s_document_import_revision==document_store_revision();
}
static bool load_document_progress(document_entry_t *entry) {
    book_progress_t progress={0};
    bool has_progress=book_progress_load(entry->path,entry->size,&progress);
    bool changed=entry->has_progress!=has_progress||entry->pct!=progress.pct||entry->recent!=progress.last_open_s;
    entry->pct=progress.pct;
    entry->recent=progress.last_open_s;
    entry->has_progress=has_progress;
    return changed;
}
static bool update_cached_document_progress(const char *path,uint32_t size) {
    if(!document_cache_current())return false;
    for(size_t i=0;i<s_document_count;++i){
        document_entry_t *entry=&s_documents[i];
        if(strcmp(entry->path,path))continue;
        SHELF_PERF_SCOPE(progress,SP_DOC_PROGRESS,1,1);
        entry->size=size;
        bool changed=load_document_progress(entry);
        if(!entry->date[0]){
            document_entry_t summary;
            if(document_store_read_entry(path,&summary)==ESP_OK&&summary.date[0]){
                memcpy(entry->date,summary.date,sizeof(entry->date));
                changed=true;
            }
        }
        return changed;
    }
    return false;
}
static size_t find_document(const document_entry_t *entries, size_t count, const char *path) {
    for(size_t i=0;i<count;++i)if(!strcmp(entries[i].path,path))return i;
    return count;
}
static bool refresh_document_imports(unsigned limit) {
    if(!document_cache_same_media())return false;
    unsigned revision=document_store_revision(),count=revision-s_document_import_revision;
    if(!count||count>limit)return false;
    document_entry_t *updates=malloc(count*sizeof(*updates));
    if(!updates)return false;
    esp_err_t err=document_store_read_changes(s_document_import_revision,revision,updates,count);
    if(err!=ESP_OK){free(updates);return false;}
    size_t added=0;
    SHELF_PERF_SCOPE(progress,SP_DOC_PROGRESS,0,2);
    for(unsigned i=0;i<count;++i){
        document_entry_t *entry=&updates[i];
        if(find_document(updates,i,entry->path)!=i){entry->path[0]=0;continue;}
        book_title_display(entry->path,entry->name,sizeof(entry->name));
        (void)load_document_progress(entry);
        ++progress.a;
        if(find_document(s_documents,s_document_count,entry->path)==s_document_count)++added;
    }
    if(!document_cache_same_media()||revision!=document_store_revision()){
        free(updates);return false;
    }
    if(added){
        if(s_document_count>SIZE_MAX/sizeof(*s_documents)-added){free(updates);return false;}
        document_entry_t *grown=realloc(s_documents,(s_document_count+added)*sizeof(*grown));
        if(!grown){free(updates);return false;}
        s_documents=grown;
    }
    for(unsigned i=0;i<count;++i){
        if(!updates[i].path[0])continue;
        size_t index=find_document(s_documents,s_document_count,updates[i].path);
        if(index==s_document_count)++s_document_count;
        s_documents[index]=updates[i];
    }
    free(updates);
    s_document_import_revision=revision;
    sort_documents();
    return true;
}
static void scan_documents(app_ctx_t *ctx) {
    s_document_scan_pending = false;
    uint32_t media_epoch=record_store_media_epoch();
    if(s_document_media_epoch!=media_epoch)discard_document_cache();
    s_document_media_epoch=media_epoch;
    s_document_cache_valid=false;
    if(!record_store_ready())return;
    unsigned revision=book_store_revision(),import_revision=document_store_revision();
    SHELF_PERF_SCOPE(perf,SP_DOCUMENTS,0,0);
    document_entry_t *next=NULL;size_t count=0;
    esp_err_t scan_error=document_store_scan(&next,&count);perf.b=scan_error;
    if(scan_error!=ESP_OK){free(next);ui_nav_notice_show(UI_MSG("文档读取失败，请重试"),ctx->now_ms);return;}
    size_t pending=0;
    for(size_t i=0;i<s_document_count;++i)if(s_documents[i].cleanup_pending)++pending;
    if(pending){
        if(count+pending>SIZE_MAX/sizeof(*next)){free(next);return;}
        document_entry_t *grown=realloc(next,(count+pending)*sizeof(*next));
        if(!grown){free(next);ui_nav_notice_show(UI_MSG("内存不足，请稍后重试"),ctx->now_ms);return;}next=grown;
    }
    {
        SHELF_PERF_SCOPE(titles,SP_DOC_TITLES,(int)count,0);
        for(size_t i=0;i<count;++i)
            book_title_display(next[i].path, next[i].name, sizeof(next[i].name));
    }
    {
        SHELF_PERF_SCOPE(progress,SP_DOC_PROGRESS,(int)count,0);
        for(size_t i=0;i<count;++i)load_document_progress(&next[i]);
    }
    // 同名文件重新出现时，旧删除请求作废。/ Drop old cleanup requests when a file reappears.
    for(size_t j=0;j<s_document_count;++j){
        const document_entry_t *old=&s_documents[j];
        if(!old->cleanup_pending)continue;
        size_t i=find_document(next,count,old->path);
        if(i==count)next[count++]=*old;
    }
    if(media_epoch!=record_store_media_epoch()){free(next);discard_document_cache();return;}
    if(revision!=book_store_revision()||import_revision!=document_store_revision()){free(next);return;}
    free(s_documents);s_documents=next;s_document_count=count;
    s_document_revision=revision;
    s_document_import_revision=import_revision;
    s_document_cache_valid=true;
    s_document_starred_only=false;
    {
        SHELF_PERF_SCOPE(order,SP_DOC_ORDER,(int)count,0);
        sort_documents();
    }
    int rows=document_rows();
    if(ctx->leaf<0||(size_t)ctx->leaf*rows>=document_visible_count())ctx->leaf=0;
    perf.a=(int)s_document_count;
}
static void ensure_documents(void) {
    if(s_document_media_epoch!=record_store_media_epoch()||!record_store_ready())discard_document_cache();
    bool reuse=document_cache_current()||refresh_document_imports(1);
    shelf_perf_event(SP_DOC_REUSE,reuse,(int)s_document_count,0,0);
    s_document_scan_pending=!reuse&&record_store_ready();
}
// 一体式双段胶囊，副标题与点击区域共用；墨水屏直接切换，不做滑动动画。
// One two-segment capsule shares subtitle hit targets; switch directly without ink-display animation.
#define CONTENT_HEADER_RULE_Y (UI_STATUS_HEIGHT+80)
#define CONTENT_LIST_TOP (UI_STATUS_HEIGHT+92)
static EpdRect content_tab(int i){return (EpdRect){36+i*152,UI_STATUS_HEIGHT+4,152,64};}
static void draw_content_tabs(uint8_t *fb,bool documents){
    EpdRect capsule={36,UI_STATUS_HEIGHT+4,304,64};
    ui_fill_round_rect(fb,capsule,32,0xd0);
    ui_draw_round_rect(fb,capsule,32,0x90);
    for(int i=0;i<2;i++){
        EpdRect r=content_tab(i);bool active=(i==1)==documents;
        if(active)ui_fill_round_rect(fb,(EpdRect){r.x+4,r.y+4,r.width-8,r.height-8},28,UI_NAV_ACTIVE_GRAY);
        uint8_t fg=active?15:0,bg=active?(UI_NAV_ACTIVE_GRAY>>4):13;
        ui_text_fixed_vc_gray(fb,r.x+r.width/2,r.y+20,(ui_i18n_english()?ui_text_fit_px(UI_TEXT_BODY,i?UI_MSG("文档"):UI_MSG("图书"),r.width-16,31):31),i?UI_MSG("文档"):UI_MSG("图书"),EPD_DRAW_ALIGN_CENTER,fg,bg);
        ui_text_fixed_vc_gray(fb,r.x+r.width/2,r.y+48,23,i?"Markdown":"EPUB/TXT",EPD_DRAW_ALIGN_CENTER,fg,bg);
    }
}
static int document_row_step(void) { return ui_list_row_height(false)+22; }
static int document_rows(void) { return ui_list_visible_rows(CONTENT_LIST_TOP,1042,document_row_step()); }
static EpdRect document_row(int row){return (EpdRect){36,CONTENT_LIST_TOP+row*document_row_step(),612,document_row_step()-10};}
static EpdRect content_action(int i){return (EpdRect){504+i*75,UI_STATUS_HEIGHT+10,69,52};}
static EpdRect content_search_chip(void) { return (EpdRect){350, UI_STATUS_HEIGHT+10, 144, 52}; }
static void draw_content_actions(uint8_t *fb, const char *query) {
    for (int i = 0; i < 2; ++i) {
        EpdRect r = content_action(i);
        ui_draw_icon(fb, r.x+r.width/2, r.y+r.height/2, 40,
                     i ? UI_ICON_ARROW_UP_DOWN : UI_ICON_SEARCH, UI_GRAY_BLACK);
    }
    if (!query[0]) return;
    EpdRect r = content_search_chip();
    ui_fill_round_rect(fb, r, 16, 0xe0);
    char label[65];
    snprintf(label, sizeof(label), "%s", query);
    int px = ui_text_effective_px(UI_TEXT_CAPTION);
    fit_fixed_text(label, px, r.width - 44);
    ui_text_fixed_vc(fb, r.x+10, r.y+r.height/2, px, label, EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_close_in(fb, (EpdRect){r.x+r.width-36, r.y, 36, r.height});
}
static void draw_documents(app_ctx_t *ctx,uint8_t *fb){
    ui_clear_page(fb);epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,UI_NAV_TOP},UI_GRAY_WHITE,fb);ui_nav_status(fb);draw_content_tabs(fb,true);
    ui_hairline(fb,CONTENT_HEADER_RULE_Y,36,612,0x68);
    if(s_document_scan_pending||!s_document_cache_valid){
        const char *label=s_document_scan_pending?UI_MSG("加载中"):
            record_store_ready()?UI_MSG("文档读取失败，请重试"):UI_MSG("TF 卡尚未挂载");
        int y=s_document_scan_pending?UI_LOCK_HEIGHT/2:408;
        ui_text_vc(fb,UI_LOCK_WIDTH/2,y,27,label,EPD_DRAW_ALIGN_CENTER,false);
        ui_nav_draw(fb,1);
        return;
    }
    draw_content_actions(fb, s_document_query);
    size_t count = document_visible_count();
    if(!count)ui_text_vc(fb,342,408,27,s_document_query[0]?UI_MSG("没有匹配的文档"):UI_MSG("还没有 Markdown 文档"),EPD_DRAW_ALIGN_CENTER,false);
    for(int row=0;row<document_rows();row++){
        document_entry_t *d = document_visible_at((size_t)ctx->leaf*document_rows()+row);
        if (!d) break;
        EpdRect r=document_row(row);epd_fill_rect(r,UI_GRAY_WHITE,fb);
        char title[256];ui_snprintf(title,sizeof(title),"%s",d->name);
        // 按 UTF-8 边界截断，日期与进度区域固定保留。
        // Trim at UTF-8 boundaries and reserve fixed date/progress columns.
        int title_px=ui_text_effective_px(UI_TEXT_BODY);
        int date_px=ui_text_effective_px(UI_TEXT_CAPTION);
        const char *date_label=d->cleanup_pending?UI_MSG("清理待重试"):d->date[0]?d->date:UI_MSG("未导入");
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
    int pages=count?(int)((count+document_rows()-1)/document_rows()):1;
    ui_nav_draw_pager(fb,(int)count,UI_MSG("篇文档"),ctx->leaf,pages);ui_nav_draw(fb,1);
    if(s_document_menu)draw_document_details(fb);
}
static ui_action_card_t content_sort_card(void) {
    int top=UI_NAV_TOP-284;
    return (ui_action_card_t){.top=top,.close={592,top+12,76,76}};
}
static EpdRect content_sort_mode(int i) {
    int width=UI_LOCK_WIDTH/4;
    return (EpdRect){i*width,content_sort_card().top+84,width,118};
}
static EpdRect content_sort_direction(void) {
    return (EpdRect){172,UI_NAV_TOP-74,340,60};
}
static void content_sort_open(bool documents){
    if(content_options_load_checked(documents,app_settings_shelf_recent_sort(),&s_sort_options)!=ESP_OK){
        ui_nav_notice_show(UI_MSG("设置读取失败，请重试"),esp_timer_get_time()/1000);
        return;
    }
    s_sort_documents=documents;
    s_sort_open=true;
}
static void draw_content_sort(uint8_t *fb){
    ui_action_card_t card=content_sort_card();
    ui_nav_draw_action_card(fb,card);
    ui_text_vc(fb,36,card.close.y+card.close.height/2,UI_TEXT_TITLE,UI_MSG("排序"),EPD_DRAW_ALIGN_LEFT,false);
    ui_nav_close_in(fb,card.close);
    const char *labels[]={s_sort_documents?UI_MSG("文档\n名称"):UI_MSG("书籍\n名称"),
                         UI_MSG("阅读\n进度"),UI_MSG("加入\n日期"),UI_MSG("最近\n阅读")};
    int single_line_px=ui_text_effective_px(UI_TEXT_BODY);
    if(ui_i18n_english())for(int i=0;i<4;++i){
        int fitted=ui_text_fit_px(UI_TEXT_BODY,labels[i],content_sort_mode(i).width-20,48);
        if(fitted<single_line_px)single_line_px=fitted;
    }
    for(int i=0;i<4;++i){
        EpdRect r=content_sort_mode(i);
        int cx=r.x+r.width/2,cy=r.y+r.height/2-6;
        if(i)epd_fill_rect((EpdRect){r.x,cy-27,1,54},UI_GRAY_ITEM_LINE,fb);
        const char *label=ui_message(labels[i]);
        const char *second=strchr(label,'\n');
        if(second){
            char first[16];
            snprintf(first,sizeof(first),"%.*s",(int)(second-label),label);
            int gap=ui_text_line_height(UI_TEXT_BODY);
            ui_text_vc(fb,cx,cy-gap/2,UI_TEXT_BODY,first,EPD_DRAW_ALIGN_CENTER,false);
            ui_text_vc(fb,cx,cy+(gap+1)/2,UI_TEXT_BODY,second+1,EPD_DRAW_ALIGN_CENTER,false);
        }else{
            ui_text_fixed_vc(fb,cx,cy,single_line_px,label,EPD_DRAW_ALIGN_CENTER,false);
        }
        if(s_sort_options.mode==i)
            ui_fill_round_rect(fb,(EpdRect){cx-18,r.y+r.height-12,36,6},3,UI_GRAY_BLACK);
    }
    const char *direction=s_sort_options.mode==CONTENT_NAME?(s_sort_options.ascending?UI_MSG("名称正序"):UI_MSG("名称倒序")):
        s_sort_options.mode==CONTENT_PROGRESS?(s_sort_options.ascending?UI_MSG("进度从低到高"):UI_MSG("进度从高到低")):
        (s_sort_options.ascending?UI_MSG("时间从早到晚"):UI_MSG("时间从晚到早"));
    ui_draw_button(fb,content_sort_direction(),direction,false);
}
static const char* TAG = "book";
static int s_presented_view = -1;
static bool s_reader_image_refresh_pending;
static char s_requested_open[BOOK_STORE_PATH_MAX];
static bool s_requested_open_home;
static bool s_reader_return_home;
static shelf_entry_t* s_shelf;
static shelf_entry_t *s_shelf_page;
static book_shelf_index_t s_shelf_disk;
static bool s_shelf_paged, s_shelf_disk_valid, s_shelf_index_dirty;
static uint32_t s_shelf_page_first, s_shelf_page_count, s_shelf_disk_media;
static unsigned s_shelf_disk_titles, s_shelf_titles;
static uint64_t s_shelf_contents;
static bool s_shelf_audit_pending, s_shelf_force_scan;
static int64_t s_shelf_audit_next_ms;
static struct {
    uint64_t contents;
    uint32_t media;
    unsigned books;
    bool valid;
} s_shelf_audited;
typedef struct {
    int index;
    uint8_t *gray;
    unsigned width, height;
    uint64_t identity;
    uint32_t file_epoch;
    bool pending, ready;
} shelf_cover_t;
static shelf_cover_t s_covers[BOOK_ROWS];
#define SHELF_SPARE_COVERS 9
#define SHELF_SPARE_RESERVE (1024u * 1024u)
#define READER_COVER_FREE_RESERVE (2u * 1024u * 1024u)
#define READER_COVER_BLOCK_RESERVE (1024u * 1024u)
static shelf_cover_t s_spare_covers[SHELF_SPARE_COVERS];
static uint32_t s_shelf_cover_epoch;
static int64_t s_cover_next_ms;
static unsigned s_cover_pending_mask;
static int s_reader_shelf_leaf;
static char s_shelf_progress_path[BOOK_STORE_PATH_MAX];
static unsigned s_shelf_progress_revision;
static bool s_shelf_progress_valid;
static size_t s_shelf_capacity;
static int s_count, s_visible_count;
static bool s_recent_sort;
static shelf_entry_t s_managed;
static bool s_delete_confirm, s_file_removed;
static char s_shelf_warning[128], s_manage_message[128];
static char s_query[65], s_search_draft[65];
static book_view_t s_search_parent;
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
static bool s_editor_document;
static bool s_save_failed;
static bool s_pending_invalidated;
static int64_t s_save_retry_ms;
static unsigned s_store_revision;
static bool s_shelf_cache_valid, s_cache_sd_present, s_cache_sd_mounted;
static bool s_scan_pending, s_clear_confirm;
static reader_panel_t s_reader_panel;
static app_book_screen_t s_reader_screen;
static bool s_reader_landscape_reverse;
#ifndef PICO_HOST_SIMULATOR
static bool s_reader_sensor_owned;
static int64_t s_reader_sensor_next_ms;
static unsigned s_reader_opposite_samples;
#endif
enum { WIDE_SPLIT_GAP = 12, WIDE_PANE_WIDTH = (UI_LOCK_HEIGHT - WIDE_SPLIT_GAP) / 2,
       WIDE_PAGER_Y = 616, WIDE_TOC_ROWS = 9, WIDE_TOC_TOP = 24, WIDE_LIST_STEP = 64,
       WIDE_PROGRESS_Y = 222, WIDE_ACTIONS_Y = 340, WIDE_TABS_Y = 530 };
static int wide_panel_x(bool tools) {
    return tools == s_reader_landscape_reverse ? 0 : WIDE_PANE_WIDTH + WIDE_SPLIT_GAP;
}

static bool reader_landscape(void) { return s_reader_screen != APP_BOOK_PORTRAIT; }
static bool wide_panel_open(void) {
    return reader_landscape() && (s_view == TOC || (s_view == READING && s_reader_panel != READER_PANEL_NONE));
}
static void reader_set_landscape_rotation(void) {
#ifdef PICO_HOST_SIMULATOR
    epd_set_rotation(s_reader_landscape_reverse ? EPD_ROT_INVERTED_PORTRAIT : EPD_ROT_PORTRAIT);
#else
    epd_set_rotation(s_reader_landscape_reverse ? EPD_ROT_INVERTED_LANDSCAPE : EPD_ROT_LANDSCAPE);
#endif
}
static bool s_reader_fullscreen;
static bool s_reader_immersive;
static char s_message[128], s_path[BOOK_STORE_PATH_MAX], s_title[128];
static bool s_epub_section_has_text;
static char s_book_title[128];
static char s_chapter_heading_title[128], s_chapter_heading_label[32];
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_height;
static char s_font_path[192];
static bool s_reader_english;
static bool s_reader_localized_cover;
static char* s_text;
bool app_book_reader_body_visible(void) {
    return s_view == READING && s_text && s_reader_panel == READER_PANEL_NONE && !s_clear_confirm;
}
bool app_book_reader_active(void) {
    return s_view == READING || s_view == TOC;
}
bool app_book_landscape_visible(void) {
    return reader_landscape() && s_text && app_book_reader_active();
}
static blk_t* s_blocks;
static text_style_t *s_text_styles;
static size_t s_text_style_count;
static markdown_cell_t *s_text_cells;
static size_t s_text_cell_count;
static bool s_md_committed;
static char s_md_notice[64];
static bool s_reader_hyphenate = true;
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
static reader_panel_t s_help_return_panel;
static bool s_progress_drag;
static int s_progress_percent;
static bool s_jump_back_valid;
static size_t s_jump_back_chapter, s_jump_back_offset;
static bool s_note_open;
static bool s_note_origin_is_annotation;
static book_progress_t s_note_origin;
static size_t s_note_origin_stored_offset;
static size_t s_note_source;
enum { NOTE_CARD_HEADER = 64, NOTE_CARD_TITLE_PAD = 12, NOTE_CARD_MARGIN = 36, NOTE_CARD_PADDING = 22,
       NOTE_CARD_WIDE_RAIL = 176, NOTE_CARD_WIDE_GAP = 24, NOTE_CARD_WIDE_TOP = 18, NOTE_CARD_DIVIDER_PAD = 14 };
typedef struct { size_t offset, len; int x, baseline; bool hyphen; } reader_note_line_t;
typedef struct {
    reader_note_line_t *lines;
    size_t body_offset, count;
    int px, height;
    char text[]; // Title, NUL, then the note body.
} reader_note_card_t;
static reader_note_card_t *s_note_card;
static html_link_t *s_chapter_links;
static size_t s_chapter_link_count;
static html_positions_t s_chapter_positions;
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
static size_t current_toc_position(void);
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
static bool shelf_load_page(int leaf);
static void shelf_require_complete(app_ctx_t *ctx);
static void shelf_index_checkpoint(app_ctx_t *ctx, bool allow_large);
static void shelf_index_publish(void);

static const shelf_entry_t *shelf_entry_at(int index) {
    if (index < 0 || index >= s_visible_count) return NULL;
    if (!s_shelf_paged) return &s_shelf[index];
    uint32_t offset = (uint32_t)index - s_shelf_page_first;
    return offset < s_shelf_page_count ? &s_shelf_page[offset] : NULL;
}
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
static void lock_draw(void) { if (s_draw_lock) xSemaphoreTake(s_draw_lock, portMAX_DELAY); }
static void unlock_draw(void) { if (s_draw_lock) xSemaphoreGive(s_draw_lock); }
static size_t fb_bytes(void) { return (size_t)epd_width() * epd_height() / 2; }
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 18)
#define LANDSCAPE_FULLSCREEN_PROGRESS_Y (UI_LOCK_WIDTH - 9)
#define LANDSCAPE_REGULAR_FOOTER_Y (UI_LOCK_WIDTH - 64)
#define LANDSCAPE_REGULAR_PROGRESS_Y (UI_LOCK_WIDTH - 52)
static EpdRect reader_note_footer(void) {
    bool wide = reader_landscape();
    int width = wide ? UI_LOCK_HEIGHT : UI_LOCK_WIDTH;
    int height = wide ? UI_LOCK_WIDTH : UI_LOCK_HEIGHT;
    int band = !wide && !s_reader_fullscreen ? 88 : 58;
    return (EpdRect){0, height - band, width, band};
}

static int reader_body_bottom(int bottom, bool note) {
    if (!note) return bottom;
    int clear_to = reader_note_footer().y - 8;
    return clear_to < bottom ? clear_to : bottom;
}
// 正文上方留出一致的呼吸空间，下沿靠近进度区；两种模式共用排版与刷新边界。
// Give the body a consistent top inset and extend it toward the progress strip in both modes.
static EpdRect reader_area(void) {
    const int top = s_reader_fullscreen ? (s_reader_immersive ? 0 : UI_STATUS_HEIGHT) : UI_PAGE_Y(160);
    // 全屏进度线单独差分，翻页时不让正文刷新反复驱动整条横线。
    // Keep the full-screen progress strip out of body updates so turns do not drive the whole bar.
    const int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP : UI_BAR_TOP + 8;
    return (EpdRect){0, top, UI_LOCK_WIDTH, bottom - top};
}
static EpdRect reader_fullscreen_progress_area(void) {
    return (EpdRect){0, READER_FULLSCREEN_PROGRESS_TOP,
                     UI_LOCK_WIDTH, UI_LOCK_HEIGHT - READER_FULLSCREEN_PROGRESS_TOP};
}
static EpdRect landscape_reader_area(void) {
    bool immersive = s_reader_fullscreen && s_reader_immersive;
    const int top = immersive ? 0 : UI_STATUS_HEIGHT;
    const int bottom = immersive ? UI_LOCK_WIDTH : s_reader_fullscreen ?
        LANDSCAPE_FULLSCREEN_PROGRESS_Y : LANDSCAPE_REGULAR_FOOTER_Y;
    return (EpdRect){0, top, UI_LOCK_HEIGHT, bottom - top};
}
static EpdRect reader_body_rect(bool note) {
    if (reader_landscape()) {
        EpdRect area = landscape_reader_area();
        int margin = s_margin ? s_margin : 36;
        const bool immersive = s_reader_fullscreen && s_reader_immersive;
        const int top_inset = immersive ? 8 : 12;
        const int bottom_inset = immersive ? 5 : s_reader_fullscreen ? 6 : 12;
        int top = area.y + top_inset;
        int bottom = reader_body_bottom(area.y + area.height - bottom_inset, note);
        return (EpdRect){margin, top, area.width - 2 * margin, bottom - top};
    }
    int top = s_reader_fullscreen ? (s_reader_immersive ? 24 : UI_STATUS_HEIGHT+16) : UI_PAGE_Y(188);
    int margin = s_margin ? s_margin : 36;
    // Leave only a narrow strip for the full-book progress line in full screen.
    int bottom = s_reader_fullscreen ? READER_FULLSCREEN_PROGRESS_TOP - 2 : UI_BAR_TOP + 8;
    return (EpdRect){margin, top, UI_LOCK_WIDTH - 2 * margin, reader_body_bottom(bottom, note) - top};
}
static EpdRect body_rect(void) {
    return reader_body_rect(s_note_open);
}
static EpdRect progress_rect(void) {
    const int offset = 20;
    return (EpdRect){36, UI_BAR_TOP + offset, UI_LOCK_WIDTH - 72, UI_BAR_H - offset};
}
// 大/特大字级改用四个大封面；命中与缓存使用相同分页数量。/ Large type uses four covers; hits and cache share pagination.
static int shelf_grid_side(void){return app_settings_system_font_size()>=140?2:3;}
static int shelf_rows(void){int n=shelf_grid_side();return n*n;}
static int shelf_cell_height(void){return (UI_NAV_PAGER_TOP-CONTENT_LIST_TOP)/shelf_grid_side();}
static EpdRect row_rect(int row) {
    int n=shelf_grid_side(),col=row%n,line=row/n;
    int margin=n==3?12:UI_MARGIN;
    int cell=(UI_LOCK_WIDTH-2*margin-(n-1)*UI_GAP)/n,h=shelf_cell_height();
    return (EpdRect){margin+col*(cell+UI_GAP),CONTENT_LIST_TOP+line*h,cell,h};
}
static void release_spare_covers(void) {
    for (int i = 0; i < SHELF_SPARE_COVERS; ++i) free(s_spare_covers[i].gray);
    memset(s_spare_covers, 0, sizeof(s_spare_covers));
}

static void invalidate_covers(void) {
    release_spare_covers();
    s_cover_pending_mask = 0;
    for (int i = 0; i < BOOK_ROWS; ++i) {
        free(s_covers[i].gray);
        s_covers[i] = (shelf_cover_t){.index = -1};
    }
}
// 正文和插图优先使用内存；只在打开或分配正文资源时检查。
// Give text and illustrations priority; check only when opening or allocating reader resources.
typedef enum { READER_COVERS_OPEN, READER_COVERS_CHECK, READER_COVERS_RETRY } reader_cover_check_t;
static bool trim_reader_covers(reader_cover_check_t check) {
    int count = 0;
    for (int i = 0; i < BOOK_ROWS; ++i) count += s_covers[i].gray != NULL;
    if (!count) return false;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t available = heap_caps_get_free_size(caps);
    const size_t largest = heap_caps_get_largest_free_block(caps);
    const bool retry = check == READER_COVERS_RETRY;
    const bool discard = retry || available < READER_COVER_FREE_RESERVE || largest < READER_COVER_BLOCK_RESERVE;
    if (check == READER_COVERS_OPEN || discard)
        shelf_perf_event(SP_SHELF_READER_COVERS, discard ? 0 : count, available / 1024,
                         largest / 1024, retry ? 2 : discard ? 1 : 0);
    if (discard) invalidate_covers();
    return discard;
}
static uint8_t* load_cover_gray(const char* source, const char* title, const char* author,
                                bool decode, bool* pending, unsigned *width,unsigned *height) {
    SHELF_PERF_SCOPE(perf,SP_COVER_LOAD,decode,0);
    *pending = false;
    uint8_t* gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gray) {
        release_spare_covers();
        gray = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!gray) return NULL;
    }
    if (!book_cover_load_fitted_gray(source, title, author, gray, decode, pending,width,height)) {
        free(gray); return NULL;
    }
    perf.b=1;return gray;
}
static uint64_t shelf_cover_identity(const shelf_entry_t *item) {
    const char *parts[] = {item->path, item->name, item->author, ttf_font_path()};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < sizeof(parts) / sizeof(*parts); ++i) {
        const unsigned char *p = (const unsigned char *)parts[i];
        while (*p) hash = (hash ^ *p++) * UINT64_C(1099511628211);
        hash = (hash ^ 255u) * UINT64_C(1099511628211);
    }
    hash = (hash ^ item->size) * UINT64_C(1099511628211);
    hash = (hash ^ (uint64_t)item->modified) * UINT64_C(1099511628211);
    hash = (hash ^ (unsigned)ttf_get_weight()) * UINT64_C(1099511628211);
    return hash ? hash : 1;
}

static void prepare_covers(app_ctx_t* ctx) {
    if ((s_view != SHELF && s_view != MANAGE) || s_scan_pending)
        return;
    uint32_t epoch_now = book_index_cache_epoch();
    if (s_shelf_cover_epoch != epoch_now) {
        invalidate_covers();
        s_shelf_cover_epoch = epoch_now;
    }
    // 排序与重扫后按文件身份归位，保留仍属于当前页的位图。
    // Reconcile by file identity after sorting or scanning and keep surviving page bitmaps.
    shelf_cover_t previous[BOOK_ROWS];
    memcpy(previous, s_covers, sizeof(previous));
    memset(s_covers, 0, sizeof(s_covers));
    s_cover_pending_mask = 0;
    int rows = shelf_rows();
    for (int row = 0; row < rows; ++row) {
        int index = ctx->leaf * rows + row;
        shelf_cover_t *slot = &s_covers[row];
        slot->index = index;
        slot->width = BOOK_COVER_W;
        slot->height = BOOK_COVER_H;
        const shelf_entry_t *item = shelf_entry_at(index);
        if (!item || item->removed) continue;
        uint64_t identity = shelf_cover_identity(item);
        uint32_t epoch = book_index_cache_file_epoch(item->size, item->modified);
        int match = 0;
        while (match < BOOK_ROWS &&
               (previous[match].identity != identity || previous[match].file_epoch != epoch))
            ++match;
        if (match < BOOK_ROWS) {
            *slot = previous[match];
            slot->index = index;
            previous[match] = (shelf_cover_t){0};
        } else {
            int spare = 0;
            while (spare < SHELF_SPARE_COVERS &&
                   (s_spare_covers[spare].identity != identity || s_spare_covers[spare].file_epoch != epoch))
                ++spare;
            if (spare < SHELF_SPARE_COVERS) {
                *slot = s_spare_covers[spare];
                slot->index = index;
                s_spare_covers[spare] = (shelf_cover_t){0};
            } else {
                slot->identity = identity;
                slot->file_epoch = epoch;
                // 绘制首帧不读卡。/ Keep card reads out of the first frame.
                slot->pending = true;
            }
        }
        if (slot->pending) s_cover_pending_mask |= 1u << row;
    }
    // 空间不足时只保留当前页。/ Retain only the active page under memory pressure.
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < SHELF_SPARE_RESERVE) {
        release_spare_covers();
        for (int i = 0; i < BOOK_ROWS; ++i) free(previous[i].gray);
        return;
    }
    shelf_cover_t stash[SHELF_SPARE_COVERS] = {0};
    int used = 0;
    for (int i = 0; i < BOOK_ROWS; ++i) {
        if (!previous[i].gray) continue;
        if (used < SHELF_SPARE_COVERS) stash[used++] = previous[i];
        else free(previous[i].gray);
    }
    for (int i = 0; i < SHELF_SPARE_COVERS; ++i) {
        if (!s_spare_covers[i].gray) continue;
        if (used < SHELF_SPARE_COVERS) stash[used++] = s_spare_covers[i];
        else free(s_spare_covers[i].gray);
    }
    memcpy(s_spare_covers,stash,sizeof(stash));
}

static int shelf_cover_height(void) {
    int ideal=shelf_grid_side()==3?224:300;
    int available=shelf_cell_height()-2*ui_text_line_height(UI_TEXT_BODY)-16;
    return available<ideal?available:ideal;
}
// 大封面的封面/进度/两行书名整体在各行内居中，触区仍覆盖整行。/ Center the full large-cover block within each row while retaining full-row hit targets.
static int shelf_cover_top(EpdRect card) {
    int spare=card.height-shelf_cover_height()-16-2*ui_text_line_height(UI_TEXT_BODY);
    return card.y+(shelf_grid_side()==2&&spare>0?spare/2:0);
}
static int shelf_caption_width(EpdRect card) { return card.width-12; }
// 所有书籍的进度条沿用标准封面比例宽度，不受各自图片宽度影响。/ Progress uses the standard cover aspect width independently of each image.
static EpdRect shelf_progress_rect(EpdRect card) {
    int width=shelf_cover_height()*BOOK_COVER_W/BOOK_COVER_H;
    if(width>shelf_caption_width(card))width=shelf_caption_width(card);
    return (EpdRect){card.x+(card.width-width)/2,shelf_cover_top(card)+shelf_cover_height()+8,width,4};
}
static EpdRect shelf_image_rect(EpdRect card,int row) {
    unsigned source_w=s_covers[row].width? s_covers[row].width:BOOK_COVER_W;
    unsigned source_h=s_covers[row].height?s_covers[row].height:BOOK_COVER_H;
    unsigned width,height;int available=shelf_cover_height();if(available<1)available=1;
    book_cover_fit_size(source_w,source_h,(unsigned)shelf_caption_width(card),(unsigned)available,&width,&height);
    // 常规封面等高；超宽封面缩小后在同一高度区域居中。/ Keep normal covers equally tall and center width-limited covers vertically in that slot.
    return (EpdRect){card.x+(card.width-(int)width)/2,shelf_cover_top(card)+(available-(int)height)/2,(int)width,(int)height};
}
static void draw_shelf_cover(uint8_t* fb, EpdRect card, int row, const char* name) {
    EpdRect image=shelf_image_rect(card,row);
    ui_fill_round_rect(fb,image,BOOK_COVER_RADIUS,UI_GRAY_LIGHT);
    if(s_covers[row].gray){
        const uint8_t *gray=s_covers[row].gray;
        unsigned source_w=s_covers[row].width,source_h=s_covers[row].height;
        for(int y=0;y<image.height;++y){
            unsigned sy=(unsigned)((uint64_t)(unsigned)y*source_h/(unsigned)image.height);
            for(int x=0;x<image.width;++x){
                if(!book_cover_pixel_inside(x,y,image.width,image.height))continue;
                unsigned sx=(unsigned)((uint64_t)(unsigned)x*source_w/(unsigned)image.width);
                uint8_t tone=ui_gray_level(gray[sy*source_w+sx]);
                epd_draw_pixel(image.x+x,image.y+y,ui_image_dither_gray(tone,image.x+x,image.y+y),fb);
            }
        }
    }else{
        char title[80];copy_text(title,sizeof(title),name);char *dot=strrchr(title,'.');if(dot)*dot=0;
        fit_text(title,26,image.width-22);ui_text_vc(fb,image.x+image.width/2,image.y+image.height/2,26,title,EPD_DRAW_ALIGN_CENTER,false);
    }
    ui_draw_round_rect(fb,image,BOOK_COVER_RADIUS,UI_GRAY_BLACK);
    // 最终显示尺寸上统一裁角，包含占位封面及边线。/ Clip the final-sized corners, including placeholder covers and their outline.
    for(int y=0;y<BOOK_COVER_RADIUS&&y<image.height;++y){
        for(int x=0;x<BOOK_COVER_RADIUS&&x<image.width;++x){
            if(book_cover_pixel_inside(x,y,image.width,image.height))continue;
            epd_draw_pixel(image.x+x,image.y+y,UI_GRAY_WHITE,fb);
            epd_draw_pixel(image.x+image.width-1-x,image.y+y,UI_GRAY_WHITE,fb);
            epd_draw_pixel(image.x+x,image.y+image.height-1-y,UI_GRAY_WHITE,fb);
            epd_draw_pixel(image.x+image.width-1-x,image.y+image.height-1-y,UI_GRAY_WHITE,fb);
        }
    }
}
static EpdRect tool_rect(int i) {
    int x0 = i * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    int x1 = (i + 1) * UI_LOCK_WIDTH / BOOK_TOOL_COUNT;
    return (EpdRect){x0, 1096, x1 - x0, 120};
}
static int leaves(void) {
    if (s_view == DOCUMENTS) {
        size_t count = document_visible_count();
        return count ? 1+(int)(count-1)/document_rows() : 1;
    }
    if (s_view == TOC) {
        size_t count = book_toc_entry_count();
        return reader_landscape() && count ? 1 + (int)((count - 1) / WIDE_TOC_ROWS) : book_toc_pages(count);
    }
    int count = s_visible_count;
    int rows = shelf_rows();
    return count ? 1 + (count - 1) / rows : 1;
}
static EpdRect shelf_page_arrow_rect(int direction) {return ui_nav_pager_rect(direction);}
static void draw_shelf_pager(uint8_t *fb,int count,int page,int pages) {
    ui_nav_draw_pager(fb,count,UI_MSG("本书"),page,pages);
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction);
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
    src=ui_message(src);
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void fit_ttf_text(char* text, int px, int width) {
    while (*text && ttf_text_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void fit_text(char* text, int px, int width) {
    fit_ttf_text(text, ui_text_effective_px(px), width);
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
    char *chapter_mark = !strncmp(name, UI_MSG("第"), strlen(UI_MSG("第"))) ? strstr(name, UI_MSG("章")) : NULL;
    if (chapter_mark && chapter_mark - name < 24) {
        char *after = chapter_mark + strlen(UI_MSG("章"));
        while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
               !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、")) ||
               !strncmp(after, "·", strlen("·"))) {
            if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
            else after += strlen("：");
        }
        if (*after || book_kind() != BOOK_KIND_EPUB) name = after;
    }
    if (!*name || !strcmp(name, UI_MSG("本章")) || !strcmp(name, UI_MSG("章节"))) name = UI_MSG("未命名章节");
    copy_text(dst, cap, name);
}
static void reader_footer_chapter_name(char *dst, size_t cap) {
    if (book_kind() == BOOK_KIND_EPUB) {
        if (s_title[0]) reader_footer_strip_number(dst, cap, s_title);
        else if (cap) dst[0] = 0;
        return;
    }
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
    int caption_width=shelf_caption_width(card);
    int px=ui_text_effective_px(UI_TEXT_BODY);
    if(ui_text_fixed_width_px(px,full)<=caption_width){
        ui_text_fixed_vc(fb,card.x+card.width/2,shelf_cover_top(card)+shelf_cover_height()+16+ui_text_line_height(UI_TEXT_BODY)/2,px,full,EPD_DRAW_ALIGN_CENTER,false);
        return;
    }
    char lines[2][256]={{0}};const char *next=full;
    for(int row=0;row<2;++row){size_t used=0;
        while(*next&&used+5<sizeof(lines[row])){
            unsigned char ch=(unsigned char)*next;size_t bytes=ch<0x80?1:ch<0xe0?2:ch<0xf0?3:4;
            if(strlen(next)<bytes)break;
            memcpy(lines[row]+used,next,bytes);lines[row][used+bytes]=0;
            if(ui_text_fixed_width_px(px,lines[row])>caption_width){lines[row][used]=0;break;}
            used+=bytes;next+=bytes;
        }
    }
    if(*next){size_t n=strlen(lines[1]);
        while(n&&ui_text_fixed_width_px(px,lines[1])+ui_text_fixed_width_px(px,"…")>caption_width){do{--n;}while(n&&((unsigned char)lines[1][n]&0xc0)==0x80);lines[1][n]=0;}
        strlcat(lines[1],"…",sizeof(lines[1]));
    }
    for(int i=0;i<2;++i)if(lines[i][0])ui_text_fixed_vc(fb,card.x+card.width/2,shelf_cover_top(card)+shelf_cover_height()+16+ui_text_line_height(UI_TEXT_BODY)/2+i*ui_text_line_height(UI_TEXT_BODY),px,lines[i],EPD_DRAW_ALIGN_CENTER,false);
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
static size_t reader_stored_offset(size_t offset) {
    return html_position_convert(&s_chapter_positions, offset, false);
}
static unsigned percent(size_t page) {
    if (s_note_open && s_chapter >= book_chapter_count()) return s_note_origin.pct;
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
static bool document_search_view(book_view_t view) {
    return view == DOCUMENTS;
}
static void search_begin(void) {
    s_search_parent = s_view;
    memcpy(s_search_draft, document_search_view(s_view) ? s_document_query : s_query,
           sizeof(s_search_draft));
    ui_text_input_open(&s_search_input,s_search_draft,sizeof(s_search_draft),true,false,UI_INPUT_ENGLISH);
    s_view = SEARCH;
}
static void refresh_search_matches(void) {
    for (int i = 0; i < s_count; ++i)
        s_shelf[i].search_match = content_name_matches(s_shelf[i].name, s_shelf[i].path, s_query);
}
static void clear_content_search(app_ctx_t *ctx) {
    if (s_query[0]) {
        s_query[0] = 0;
        refresh_search_matches();
        sort_shelf(ctx);
    }
    if (s_document_query[0]) {
        filter_documents("");
        ctx->leaf = 0;
    }
}
static void search_finish(app_ctx_t* ctx, bool apply) {
    s_view = s_search_parent;
    if (apply) {
        char *query = s_search_draft;
        while (*query == ' ') ++query;
        size_t len = strlen(query);
        while (len && query[len-1] == ' ') query[--len] = 0;
        if (document_search_view(s_view)) {
            if (filter_documents(query)) {
                s_document_message[0] = 0;
                ctx->leaf = 0;
            } else document_search_memory_notice();
        } else {
            memcpy(s_query, query, len + 1);
            refresh_search_matches();
            sort_shelf(ctx);
        }
    }
    ui_text_input_clear(&s_search_input);
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
    // 对账快照只接纳这一次已确认的写入；并发变更或写失败走全量重查。
    // Extend the shelf snapshot only for a confirmed write, excluding other writers.
    record_store_lock();
    const unsigned before = progress_kv_revision();
    const bool already_saved = p->progress_saved;
    bool ok = already_saved || book_progress_save_with_sequence(p->path, &p->value, &p->value.last_open_s) == ESP_OK;
    if (ok) {
        p->progress_saved = true;
        ok = strcmp(p->path, s_latest_path) || book_progress_set_last_path(p->path) == ESP_OK;
    }
    const unsigned after = progress_kv_revision();
    if (ok && !already_saved) {
        book_progress_snapshot_note(p->path, &p->value, before, after);
        book_recent_index_note(p->path, &p->value, before, after);
    }
    if (s_shelf_progress_valid && before != after) {
        if (!ok || s_shelf_progress_revision != before ||
            (s_shelf_progress_path[0] && strcmp(s_shelf_progress_path,p->path))) {
            s_shelf_progress_valid = false;
        } else {
            copy_text(s_shelf_progress_path,sizeof(s_shelf_progress_path),p->path);
            s_shelf_progress_revision = after;
        }
    }
    record_store_unlock();
    if (ok) p->dirty = false;
    return ok;
}
static bool retry_progress(void) {
    s_save_failed = false;
    bool had_dirty = false, documents_changed=false;
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        bool dirty = p->dirty;
        had_dirty |= dirty;
        if (s_text && !strcmp(p->path, s_path)) { p = next; continue; }
        if (!pending_flush(p)) s_save_failed = true;
        else {
            if(dirty)documents_changed|=update_cached_document_progress(p->path,p->value.file_size);
            if(strcmp(p->path,s_path))pending_discard(p->path);
            else if(dirty)s_unsaved=0;
        }
        p = next;
    }
    if (s_text && (s_unsaved || had_dirty)) save_progress();
    if(documents_changed)sort_documents();
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
    return documents_changed;
}

/* ---- 管理详情 / Management details ---- */
enum { DETAIL_RENAME, DETAIL_PIN, DETAIL_DELETE, DETAIL_CLEAR, DETAIL_ACTIONS };

static ui_action_card_t details_card(bool confirm, const char *message) {
    int caption = ui_text_line_height(UI_TEXT_CAPTION);
    int extra = (confirm ? 2*caption+32 : 0) + (message[0] ? caption+12 : 0);
    return ui_nav_action_card(extra);
}

static void draw_details_line(uint8_t *fb, int x, int y, int width, int role, const char *text) {
    char line[288];
    copy_text(line, sizeof(line), text);
    int px = ui_text_effective_px(role);
    fit_fixed_text(line, px, width);
    ui_text_fixed_vc(fb, x, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
}

static void draw_details(uint8_t *fb, const char *name, const char *path, uint32_t size,
                         bool has_progress, unsigned pct, bool pinned, bool retry,
                         bool confirm, const char *warning, const char *message, bool deleting) {
    ui_action_card_t card = details_card(confirm, message);
    ui_nav_draw_action_card(fb, card);
    int body = ui_text_line_height(UI_TEXT_BODY), caption = ui_text_line_height(UI_TEXT_CAPTION);
    ui_draw_icon(fb, 57, card.top+41, 46, UI_ICON_FILE, UI_GRAY_BLACK);
    ui_text_fixed_vc(fb, card.close.x+card.close.width/2, card.close.y+card.close.height/2,
                     36, "×", EPD_DRAW_ALIGN_CENTER, false);
    draw_details_line(fb, 102, card.top+20+body/2, card.close.x-118, UI_TEXT_BODY, name);
    char file_size[24], progress[48], detail[88];
    if (size >= 1024*1024) ui_snprintf(file_size, sizeof(file_size), "%.1f MB", size/1048576.0);
    else if (size >= 1024) ui_snprintf(file_size, sizeof(file_size), "%.1f KB", size/1024.0);
    else ui_snprintf(file_size, sizeof(file_size), "%lu B", (unsigned long)size);
    ui_snprintf(progress, sizeof(progress), has_progress ? UI_MSG("阅读进度 %u%%") : UI_MSG("尚无阅读进度"), pct);
    ui_snprintf(detail, sizeof(detail), "%s · %s", file_size, progress);
    draw_details_line(fb, 102, card.top+20+body+caption/2, card.close.x-118, UI_TEXT_CAPTION, detail);
    int path_y = card.top + card.header_height;
    draw_details_line(fb, 36, path_y+card.path_height/2, 612, UI_TEXT_CAPTION, path);
    int next_y = path_y + card.path_height;
    if (message[0]) {
        draw_details_line(fb, 36, next_y+caption/2, 612, UI_TEXT_CAPTION, message);
        next_y += caption+12;
    }
    if (confirm) {
        draw_details_line(fb, 36, next_y+caption, 612, UI_TEXT_CAPTION, warning);
        const char *labels[] = {UI_MSG("取消"), deleting ? UI_MSG("确认删除") : UI_MSG("确认清除")};
        ui_nav_draw_action_row(fb, labels, 2);
    } else if (retry) {
        const char *labels[] = {UI_MSG("关闭"), UI_MSG("重试清理")};
        ui_nav_draw_action_row(fb, labels, 2);
    } else {
        const char *labels[] = {UI_MSG("重命名"), pinned ? UI_MSG("取消置顶") : UI_MSG("置顶"),
                                UI_MSG("删除"), UI_MSG("清进度")};
        ui_nav_draw_action_row(fb, labels, DETAIL_ACTIONS);
    }
}

static void draw_manage(uint8_t* fb) {
    draw_details(fb, s_managed.name, s_managed.path, s_managed.size, s_managed.has_progress,
                 s_managed.pct, s_managed.pinned, s_file_removed, s_clear_confirm,
                 s_delete_confirm ? UI_MSG("删除后文件无法恢复") : UI_MSG("仅清阅读进度，保留图书文件"),
                 s_manage_message, s_delete_confirm);
}

static void draw_document_details(uint8_t *fb) {
    const document_entry_t *d = &s_document_detail;
    draw_details(fb, d->name, d->path, d->size, d->has_progress, d->pct, d->pinned, d->cleanup_pending,
                 s_document_delete_confirm || s_document_clear_confirm,
                 s_document_delete_confirm ? UI_MSG("关联插图与阅读进度也会删除") : UI_MSG("仅清阅读进度，保留文档和图片"),
                 s_document_message, s_document_delete_confirm);
}

static void draw_editor(uint8_t *fb){
    ui_text_input_draw(fb,&s_title_input,UI_MSG("重命名"),UI_MSG("仅修改设备上的显示名称"),NULL,UI_MSG("完成"),s_editor_notice);
}
static void draw_search(uint8_t *fb){
    ui_text_input_draw(fb,&s_search_input,document_search_view(s_search_parent)?UI_MSG("搜索文档"):UI_MSG("搜索图书"),
        UI_MSG("支持中文、拼音首字母、完整拼音或英文"),NULL,UI_MSG("应用"),NULL);
}

static void save_progress(void) {
    if (!s_text || !s_path[0] || !book_layout_page_count()) return;
    bool was_failed = s_save_failed;
    pending_progress_t* pending = pending_find(s_path);
    if (!pending) { s_save_failed = true; return; }
    book_progress_t p = {
        .file_size = s_file_size, .chapter = (uint16_t)s_chapter,
        .byte_off = (uint32_t)reader_stored_offset(reader_page_offset(s_page)),
        .px = (uint8_t)s_px, .pct = (uint8_t)percent(s_page),
        .last_open_s = 0,
    };
    if (s_note_open) {
        p.chapter = s_note_origin.chapter;
        p.byte_off = s_note_origin_stored_offset;
        p.pct = s_note_origin.pct;
    }
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
    ui_snprintf(key, 11, "m_%08lx", (unsigned long)hash);
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
    ui_snprintf(key, 11, "h_%08lx", (unsigned long)hash);
}
static bool shelf_hidden_read_handle(records_handle_t h, const char *path) {
    char stored[BOOK_STORE_PATH_MAX];
    char key[11];
    shelf_hidden_key(path, key);
    size_t len = sizeof(stored);
    return records_get_str(h, key, stored, &len) == ESP_OK && strcmp(stored, path) == 0;
}
static bool shelf_hidden_load(const char *path) {
    records_handle_t h;
    if (records_open(SHELF_HIDDEN_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool hidden = shelf_hidden_read_handle(h, path);
    records_close(h);
    return hidden;
}
static bool shelf_hidden_save(const char *path, bool hidden) {
    records_handle_t h;
    if (records_open(SHELF_HIDDEN_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11];
    shelf_hidden_key(path, key);
    char stored[BOOK_STORE_PATH_MAX];
    size_t len = sizeof(stored);
    esp_err_t existing = records_get_str(h, key, stored, &len);
    esp_err_t err = existing == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : existing;
    if (existing == ESP_OK && strcmp(stored, path) != 0) err = ESP_ERR_INVALID_STATE;
    // 恢复上架也校验路径，避免擦掉碰撞键属于另一书的隐藏记录。
    // Check ownership when unhiding too, so a collision cannot erase a different book removal.
    if (err == ESP_OK) book_shelf_disk_invalidate();
    if (err == ESP_OK && hidden) err = records_set_str(h, key, path);
    else if (err == ESP_OK) {
        err = records_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK) err = records_commit(h);
    records_close(h);
    return err == ESP_OK;
}
static bool bookmark_read_handle(records_handle_t h, reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    memset(marks, 0, sizeof(*marks));
    char key[11]; bookmark_key(s_path, key);
    size_t size = 0;
    if (records_get_blob(h, key, NULL, &size) != ESP_OK) return false;
    if (size == sizeof(legacy_reader_bookmark_t)) {
        legacy_reader_bookmark_t old;
        size = sizeof(old);
        if (records_get_blob(h, key, &old, &size) != ESP_OK ||
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
    if (size != sizeof(*marks) || records_get_blob(h, key, marks, &size) != ESP_OK) return false;
    return marks->magic == UINT32_C(0x52504d32) && marks->file_size == s_file_size &&
        marks->count <= BOOKMARK_MAX && !strcmp(marks->path, s_path);
}

static bool bookmark_load(reader_bookmarks_t* marks) {
    if (!marks || !s_path[0]) return false;
    records_handle_t h;
    if (records_open("rp_marks", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = bookmark_read_handle(h, marks);
    records_close(h);
    return ok;
}
static bool bookmark_is_current(void) {
    reader_bookmarks_t marks;
    if (!bookmark_load(&marks)) return false;
    size_t offset = book_kind() == BOOK_KIND_MARKDOWN ? book_layout_page_start_offset(s_page) :
        reader_stored_offset(reader_page_offset(s_page));
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
    records_handle_t h;
    if (records_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    esp_err_t err = valid ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (valid) {
        bookmark_compact(&marks, s_bookmark_selected);
        char key[11]; bookmark_key(s_path, key);
        err = marks.count ? records_set_blob(h, key, &marks, sizeof(marks)) : records_erase_key(h, key);
        if (err == ESP_OK) err = records_commit(h);
    }
    records_close(h);
    if (err != ESP_OK) return false;
    s_bookmark_selected = 0;
    s_bookmark_edit = marks.count != 0;
    return true;
}
static bool bookmark_toggle(void) {
    if (!s_path[0] || s_chapter >= book_chapter_count()) return false;
    records_handle_t h;
    if (records_open("rp_marks", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[11]; bookmark_key(s_path, key);
    reader_bookmarks_t marks;
    bool valid = bookmark_read_handle(h, &marks);
    if (!valid) {
        memset(&marks, 0, sizeof(marks));
        marks.magic = UINT32_C(0x52504d32);
        marks.file_size = s_file_size;
        copy_text(marks.path, sizeof(marks.path), s_path);
    }
    uint32_t offset = (uint32_t)(book_kind() == BOOK_KIND_MARKDOWN ?
        book_layout_page_start_offset(s_page) : reader_stored_offset(reader_page_offset(s_page)));
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
    esp_err_t err = marks.count ? records_set_blob(h, key, &marks, sizeof(marks)) : records_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && !marks.count) err = ESP_OK;
    if (err == ESP_OK) err = records_commit(h);
    records_close(h);
    if (err == ESP_OK) {
        copy_text(s_reader_notice, sizeof(s_reader_notice), remove ? UI_MSG("书签已移除") : UI_MSG("书签已添加"));
        s_reader_notice_until = esp_timer_get_time() / 1000 + 1600;
    } else {
        copy_text(s_reader_notice, sizeof(s_reader_notice), UI_MSG("书签保存失败"));
        s_reader_notice_until = esp_timer_get_time() / 1000 + 2200;
    }
    return err == ESP_OK;
}

static content_document_layout_t s_document_layout;

static content_document_layout_t legacy_reader_layout(void) {
    return (content_document_layout_t){
        .margin=app_settings_book_margin(), .line=app_settings_book_line_spacing(),
        .paragraph=app_settings_book_paragraph_spacing(), .tracking=app_settings_book_tracking(),
        .indent=app_settings_book_indent(), .rule=app_settings_book_reading_line(),
        .rule_offset=app_settings_book_reading_line_offset(), .fullscreen=app_settings_reader_fullscreen(),
        .immersive=app_settings_reader_immersive(),
    };
}
static content_document_layout_t reader_layout(void) {
    return book_kind()==BOOK_KIND_MARKDOWN ? s_document_layout : legacy_reader_layout();
}
static esp_err_t preserve_document_layout(void) {
    content_document_layout_t stored;
    esp_err_t err=content_document_layout_load(&stored);
    if(err==ESP_ERR_NVS_NOT_FOUND||err==ESP_ERR_INVALID_RESPONSE){
        stored=legacy_reader_layout();
        return content_document_layout_save(&stored);
    }
    return err;
}
typedef enum { READER_SPACING, READER_TRACKING, READER_INDENT, READER_RULE,
               READER_RULE_OFFSET, READER_FULLSCREEN, READER_IMMERSIVE } reader_layout_change_t;
static esp_err_t save_reader_layout(content_document_layout_t next, reader_layout_change_t change) {
    if(book_kind()==BOOK_KIND_MARKDOWN){
        esp_err_t err=content_document_layout_save(&next);
        if(err==ESP_OK)s_document_layout=next;
        return err;
    }
    esp_err_t err=preserve_document_layout();
    if(err!=ESP_OK)return err;
    switch(change){
    case READER_SPACING:
        err=app_settings_set_book_margin(next.margin);
        if(err==ESP_OK)err=app_settings_set_book_line_spacing(next.line);
        return err==ESP_OK?app_settings_set_book_paragraph_spacing(next.paragraph):err;
    case READER_TRACKING: return app_settings_set_book_tracking(next.tracking);
    case READER_INDENT: return app_settings_set_book_indent(next.indent);
    case READER_RULE: return app_settings_set_book_reading_line(next.rule);
    case READER_RULE_OFFSET: return app_settings_set_book_reading_line_offset(next.rule_offset);
    case READER_FULLSCREEN:
        err=app_settings_set_reader_fullscreen(next.fullscreen);
        return err==ESP_OK?app_settings_set_reader_immersive(next.immersive):err;
    case READER_IMMERSIVE: return app_settings_set_reader_immersive(next.immersive);
    }
    return ESP_ERR_INVALID_ARG;
}
static esp_err_t save_reader_immersive(bool on) {
    content_document_layout_t next=reader_layout();
    next.immersive=on;
    return save_reader_layout(next,READER_IMMERSIVE);
}
static app_redraw_t save_reader_rule(int value,bool offset) {
    content_document_layout_t next=reader_layout();
    if(offset)next.rule_offset=value;else next.rule=value;
    if(save_reader_layout(next,offset?READER_RULE_OFFSET:READER_RULE)!=ESP_OK){
        copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("保存失败，请重试"));
        s_reader_notice_until=esp_timer_get_time()/1000+2500;
        return APP_REDRAW_PAGE;
    }
    book_layout_set_reading_line(next.rule);
    book_layout_set_reading_line_offset(next.rule_offset);
    invalidate_prep();
    return APP_REDRAW_PAGE;
}

static bool reader_book_vertical(void) {
    return !s_reader_english && book_kind() != BOOK_KIND_MARKDOWN && app_settings_book_vertical();
}

static EpdRect reader_portrait_close_rect(void) {
    return ui_nav_subpage_close_rect();
}

#include "reader_controls_ui.h"

/* ---- 绘制与预渲染 / Drawing and preparation ---- */
static uint8_t inline_ink_gray(uint8_t gray) {
    // 解码器已提供灰阶；再次加深并叠加系统对比度会压死抖动前的中间色阶。
    // The decoder already supplies grayscale; extra darkening and UI contrast clip midtones.
    return gray;
}

static EpdRect reader_close_rect(void) {
    if (reader_landscape()) return (EpdRect){12, 6, 60, 60};
    return reader_portrait_close_rect();
}
static void draw_reader_header(uint8_t *fb) {
    bool landscape = reader_landscape();
    int width = landscape ? UI_LOCK_HEIGHT : UI_LOCK_WIDTH;
    EpdRect close = reader_close_rect();
    if (landscape) ui_nav_close_in(fb, close);
    else ui_nav_subpage_frame(fb);
    char header[128];
    copy_text(header, sizeof(header), s_book_title[0] ? s_book_title : s_title);
    bool use_system = s_reader_english && !ttf_font_has_text(header);
    if (use_system) app_font_activate_system();
    const int title_px = 32;
    int above = 0, below = 0;
    fit_ttf_text(header, title_px, width - 234);
    ttf_measure_line_px(title_px, header, &above, &below);
    int baseline = close.y + close.height / 2 + (above - below) / 2;
    ttf_draw_text_px(fb, width / 2, baseline, title_px, header,
                     EPD_DRAW_ALIGN_CENTER, UI_INK_BLACK, UI_INK_WHITE);
    if (use_system) app_font_restore_reading(s_font_path);
    if (landscape) ui_hairline(fb, 69, 0, width, UI_GRAY_SECTION_LINE);
}
static int reader_image_slots(size_t page) {
    int count = book_layout_page_image_count(page);
    return count ? count : book_layout_page_image(page) >= 0 ? 1 : 0;
}
static bool reader_image_slot(size_t page, int slot, int *index, EpdRect *rect) {
    EpdRect body = body_rect();
    if (book_layout_page_image_count(page)) {
        unsigned column;
        int y, width, height;
        if (!book_layout_page_image_at(page, slot, index, &y, &width, &height, &column)) return false;
        EpdRect col = book_layout_column_rect(body, column);
        *rect = (EpdRect){col.x + (col.width - width) / 2, body.y + y, width, height};
        return true;
    }
    int image = book_layout_page_image(page);
    if (slot != 0 || image < 0) return false;
    if (index) *index = image;
    *rect = body;
    return true;
}
static void draw_reader_images(uint8_t *fb, size_t page) {
    // 位图和布局代次必须匹配；预渲染另一页不能复用当前页插图。
    // Match page and layout generation; a prefetched page must never borrow current-page bitmaps.
    if (!app_settings_reader_hide_images() && page == s_page &&
        s_page_images_for == page && s_page_images_generation == book_layout_generation()) {
        for (int i = 0; i < reader_image_slots(page); ++i) {
            EpdRect bounds;
            if (!reader_image_slot(page, i, NULL, &bounds)) continue;
            const reader_image_t *image = s_page_images && i < s_page_image_count ? &s_page_images[i] : NULL;
            if (image && image->gray) {
                int left = bounds.x + (bounds.width - image->width) / 2;
                int top = bounds.y + (bounds.height - image->height) / 2;

                for (int iy = 0; iy < image->height; ++iy)
                    for (int ix = 0; ix < image->width; ++ix)
                        epd_draw_pixel(left + ix, top + iy,
                            ui_image_dither_gray(inline_ink_gray(image->gray[(size_t)iy * image->width + ix]),
                                                 left + ix, top + iy), fb);
            } else if (bounds.height >= UI_PX_CAPTION + 4 && bounds.width >= 70) {
                char notice[64]; copy_text(notice, sizeof(notice), UI_MSG("此插图暂无法显示"));
                fit_text(notice, UI_PX_CAPTION, bounds.width - 8);
                ui_text_vc(fb, bounds.x + bounds.width / 2, bounds.y + bounds.height / 2,
                           UI_PX_CAPTION, notice, EPD_DRAW_ALIGN_CENTER, false);
            }
        }
    }
}
#ifdef PICO_HOST_SIMULATOR
static unsigned s_sim_reader_draws,s_sim_water_turns;
#endif
static void draw_reader_chapter(uint8_t *fb, size_t page, EpdRect body) {
    body = book_layout_column_rect(body, 0);
    if (page == 0 && s_chapter_lead_height && s_chapter_heading_title[0] &&
        !((reader_book_vertical() || s_reader_screen == APP_BOOK_LANDSCAPE_TWO_COLUMNS) &&
          book_layout_page_image(page) >= 0 && !book_layout_page_image_count(page))) {
        char heading[128]; copy_text(heading, sizeof(heading), s_chapter_heading_title);
        if (reader_book_vertical()) {
            fit_text(heading, 49, body.height - 36);
            ttf_draw_vertical_px(fb, body.x + body.width - 26, body.y + 18,
                                 body.height - 36, 27, s_chapter_heading_label, 0);
            ttf_draw_vertical_px(fb, body.x + body.width - 84, body.y + 18,
                                 body.height - 36, 49, heading, 0);
        } else {
            fit_text(heading, 49, reader_landscape() ? body.width : ui_content_width());
            int center = body.x + body.width / 2;
            ui_text(fb, center, body.y + 15, 27, s_chapter_heading_label, EPD_DRAW_ALIGN_CENTER, false);
            ui_text(fb, center, body.y + 87, 49, heading, EPD_DRAW_ALIGN_CENTER, false);
            ui_hairline(fb, body.y + 174, center - 132, 264, UI_GRAY_LIGHT);
        }
    }
}

static const char *reader_note_return_label(void) {
    return s_note_origin_is_annotation ? UI_MSG("返回注释") : UI_MSG("返回正文");
}

static void draw_note_return(uint8_t *fb) {
    EpdRect footer = reader_note_footer();
    epd_fill_rect(footer, 0xe0, fb);
    ui_hairline(fb, footer.y, 0, footer.width, 0xd0);
    ui_text_fixed_vc(fb, footer.width / 2, footer.y + footer.height / 2,
                     UI_PX_BTN, reader_note_return_label(), EPD_DRAW_ALIGN_CENTER, false);
}

static void reader_note_card_free(reader_note_card_t *card) {
    if (!card) return;
    free(card->lines);
    free(card);
}

static void reader_note_card_close(void) {
    reader_note_card_free(s_note_card);
    s_note_card = NULL;
}

static EpdRect reader_note_card_rect(const reader_note_card_t *card) {
    bool wide = reader_landscape();
    return (EpdRect){0, (wide ? UI_LOCK_WIDTH : UI_LOCK_HEIGHT) - card->height,
                     wide ? UI_LOCK_HEIGHT : UI_LOCK_WIDTH, card->height};
}

static EpdRect reader_note_card_exit(EpdRect card) {
    return (EpdRect){card.width - 84, card.y, 72, NOTE_CARD_HEADER};
}

static void draw_reader_note_card(uint8_t *fb, reader_note_card_t *card) {
    enum EpdRotation previous = epd_get_rotation();
    bool wide = reader_landscape();
    if (wide) reader_set_landscape_rotation();
    EpdRect rect = reader_note_card_rect(card);
    int top = s_reader_fullscreen && s_reader_immersive ? 0 : UI_STATUS_HEIGHT;
    ui_fade_region(fb, (EpdRect){0,top,rect.width,rect.y-top}, 0x99);
    for (int i = 0; i < UI_NAV_SHADOW_DEPTH; ++i)
        epd_fill_rect((EpdRect){0,rect.y-UI_NAV_SHADOW_DEPTH+i,rect.width,1},
                      (uint8_t)(0xff-(i+1)*0x66/UI_NAV_SHADOW_DEPTH), fb);
    epd_fill_rect(rect, UI_GRAY_WHITE, fb);
    ui_hairline(fb, rect.y, 0, rect.width, UI_GRAY_SECTION_LINE);
    if (wide) {
        epd_fill_rect((EpdRect){NOTE_CARD_WIDE_RAIL, rect.y + NOTE_CARD_DIVIDER_PAD, 1,
                              rect.height - 2 * NOTE_CARD_DIVIDER_PAD}, UI_GRAY_ITEM_LINE, fb);
    } else ui_nav_close_in(fb, reader_note_card_exit(rect));
    for (size_t i = 0; i < card->count; ++i) {
        const reader_note_line_t *line = &card->lines[i];
        char *text = card->text + line->offset;
        char *end = text + line->len;
        char tail = *end, after = line->hyphen ? end[1] : 0;
        *end = line->hyphen ? '-' : 0;
        if (line->hyphen) end[1] = 0;
        ttf_draw_text_px(fb, line->x, rect.y + line->baseline, card->px, text, EPD_DRAW_ALIGN_LEFT, 0, 15);
        *end = tail;
        if (line->hyphen) end[1] = after;
    }
    epd_set_rotation(previous);
}

static void draw_reader_outer(uint8_t *fb,size_t page) {
    if (s_reader_fullscreen && !s_reader_immersive) reader_page_status(fb);
    else if (!s_reader_fullscreen) reader_status(fb,NULL,false);
    if (!s_reader_fullscreen) draw_reader_header(fb);
    EpdRect body = body_rect();
    draw_reader_chapter(fb, page, body);
    if (book_kind() == BOOK_KIND_EPUB || (book_kind()==BOOK_KIND_MARKDOWN && book_layout_page_image_count(page))) draw_reader_images(fb, page);
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
                   UI_PX_CAPTION, UI_MSG("此插图暂无法显示"), EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_note_open) {
        if (!s_toolbar) draw_note_return(fb);
    } else if (s_reader_fullscreen) {
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
        const int footer_px = UI_STATUS_SIGNATURE_PX;
        char progress[48];
        ui_snprintf(progress, sizeof(progress), UI_MSG("%u/%u · 全书 %u%%"),
                 (unsigned)page + 1, (unsigned)book_layout_page_count(), percent(page));
        char chapter_line[192];
        ui_snprintf(chapter_line, sizeof(chapter_line), "%s%s",
                 s_save_failed ? UI_MSG("未保存 · ") : "", chapter_name);
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
static void draw_landscape_body(uint8_t *fb, size_t page, bool example) {
    enum EpdRotation previous = epd_get_rotation();
    reader_set_landscape_rotation();
    ui_clear_page(fb);
    EpdRect body = body_rect();
    if (!example) {
        book_layout_draw_page(fb, page, body, s_px);
        draw_reader_chapter(fb, page, body);
        if (book_kind() == BOOK_KIND_EPUB) draw_reader_images(fb, page);
    }
    if (!(s_reader_fullscreen && s_reader_immersive)) {
        reader_status(fb,example ? "荷塘月色" : s_book_title[0] ? s_book_title : s_title,true);
        ui_hairline(fb, UI_STATUS_HEIGHT, 0, UI_LOCK_HEIGHT, UI_GRAY_SECTION_LINE);
    }
    if (!example && s_note_open) {
        draw_note_return(fb);
    } else if (!example && !(s_reader_fullscreen && s_reader_immersive)) {
        int width = UI_LOCK_HEIGHT * 2 / 3;
        EpdRect track = {(UI_LOCK_HEIGHT - width) / 2,
                         s_reader_fullscreen ? LANDSCAPE_FULLSCREEN_PROGRESS_Y : LANDSCAPE_REGULAR_PROGRESS_Y,
                         width, s_reader_fullscreen ? 3 : 12};
        int radius = s_reader_fullscreen ? 1 : 5;
        ui_fill_round_rect(fb, track, radius, UI_GRAY_LIGHT);
        EpdRect fill = track;
        fill.width = width * (int)percent(page) / 100;
        if (fill.width) ui_fill_round_rect(fb, fill, radius, UI_GRAY_BLACK);
        if (!s_reader_fullscreen) {
            char label[48], chapter[128], chapter_line[192];
            reader_footer_chapter_name(chapter, sizeof(chapter));
            ui_snprintf(chapter_line, sizeof(chapter_line), "%s%s",
                        s_save_failed ? UI_MSG("未保存 · ") : "", chapter);
            ui_snprintf(label, sizeof(label), UI_MSG("%u/%u · 全书 %u%%"),
                        (unsigned)page + 1, (unsigned)book_layout_page_count(), percent(page));
            fit_fixed_text(chapter_line, UI_STATUS_SIGNATURE_PX,
                           width - ui_text_fixed_width_px(UI_STATUS_SIGNATURE_PX, label) - 20);
            ui_text_fixed(fb, track.x, track.y + 19, UI_STATUS_SIGNATURE_PX, chapter_line, EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, track.x + width, track.y + 19, UI_STATUS_SIGNATURE_PX, label, EPD_DRAW_ALIGN_RIGHT, false);
        }
    }
    if (!example && s_reader_notice[0] && s_reader_panel == READER_PANEL_NONE) {
        char notice[sizeof(s_reader_notice)];
        copy_text(notice, sizeof(notice), s_reader_notice);
        fit_fixed_text(notice, 24, 376);
        epd_fill_rect((EpdRect){UI_LOCK_HEIGHT / 2 - 200, 8, 400, 40}, UI_GRAY_WHITE, fb);
        ui_text_fixed_vc(fb, UI_LOCK_HEIGHT / 2, 28, 24, notice, EPD_DRAW_ALIGN_CENTER, false);
    }
    epd_set_rotation(previous);
}

#include "reader_landscape_ui.h"

static void draw_landscape_reader(app_ctx_t *ctx, uint8_t *fb, size_t page, bool keep_background) {
    if (!wide_panel_open()) { draw_landscape_body(fb, page, false); return; }
    if (s_view == READING && s_reader_panel == READER_PANEL_HELP) { draw_wide_help(fb, page); return; }
    bool details = s_view == TOC || s_reader_panel != READER_PANEL_TOOLS;
    if (!details && !keep_background) draw_landscape_body(fb, page, false);
    enum EpdRotation previous = epd_get_rotation();
    reader_set_landscape_rotation();
    int detail_x = wide_panel_x(false);
    if (!details && !keep_background) ui_fade_region(fb, (EpdRect){detail_x,0,WIDE_PANE_WIDTH,UI_LOCK_WIDTH},0x99);
    epd_fill_rect((EpdRect){WIDE_PANE_WIDTH,0,WIDE_SPLIT_GAP,UI_LOCK_WIDTH},UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){UI_LOCK_HEIGHT/2,0,1,UI_LOCK_WIDTH},UI_GRAY_SECTION_LINE,fb);

    bool reading_face = s_view != TOC;
    char face[TTF_FONT_PATH_MAX], label[96];
    copy_text(face,sizeof(face),ttf_font_path());
    copy_text(label,sizeof(label),ttf_font_localized_name(ttf_font_display_name()));
    bool system_controls = reading_face &&
        (s_reader_panel == READER_PANEL_FONT_SETTINGS || app_font_system_choice()[0]);
    if (system_controls) app_font_activate_system();
    draw_wide_tools(fb);
    if (details) draw_wide_detail(ctx, fb, label);
    if (system_controls) app_font_restore_reading(face);
    if (s_view == READING && s_reader_panel == READER_PANEL_FONT_SETTINGS) draw_wide_font_sample(fb);
    epd_set_rotation(previous);
}

static void draw_reader(app_ctx_t *ctx,uint8_t *fb,size_t page) {
#ifdef PICO_HOST_SIMULATOR
    ++s_sim_reader_draws;
#endif
    if (reader_landscape()) draw_landscape_reader(ctx, fb, page, false);
    else {
        ui_clear_page(fb);
        book_layout_draw_page(fb, page, body_rect(), s_px);
        draw_reader_outer(fb, page);
    }
    if (s_note_card) draw_reader_note_card(fb, s_note_card);
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
    if(reader_landscape()||s_view!=READING||!s_text||s_toolbar||s_note_card||s_clear_confirm||s_reader_panel!=READER_PANEL_NONE||s_page+1>=book_layout_page_count()||
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
    if(reader_landscape()||s_view!=READING||!s_text||s_toolbar||s_note_card||s_clear_confirm||s_reader_panel!=READER_PANEL_NONE||ctx->consumed||ctx->request_app||ctx->request_menu||
       (ctx->touch&&ctx->touch->touched)||s_prep_page!=(int)s_page+1||s_next_page==s_prep_page)return;
    lock_draw();int64_t deadline=esp_timer_get_time()+4000;
    for(unsigned row=0;row<8;++row)if(reader_prefetch_step()!=0||esp_timer_get_time()>=deadline)break;
    unlock_draw();
}
static size_t current_toc_position(void) {
    size_t count = book_navigation_count();
    if(book_kind()==BOOK_KIND_MARKDOWN){size_t best=0,offset=book_layout_page_start_offset(s_page);for(size_t i=0;i<count;i++){if(book_navigation_source_offset(i)>offset)break;best=i;}return best;}
    const bool has_start = book_kind() == BOOK_KIND_EPUB;
    if (s_selected_toc < count && book_navigation_chapter(s_selected_toc) == s_chapter)
        return s_selected_toc + has_start;
    size_t selected = SIZE_MAX;
    size_t best_chapter = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t chapter = book_navigation_chapter(i);
        if (chapter > s_chapter) continue;
        if (selected == SIZE_MAX || chapter > best_chapter) { selected = i; best_chapter = chapter; }
    }
    return selected == SIZE_MAX ? (has_start ? 0 : SIZE_MAX) : selected + has_start;
}
// 补封面只擦除本格图片区；标题、进度和其他区域保持原帧，输出范围仍沿用整格。
// Replace only this cell's image band; retain titles, progress and all other pixels, with the existing cell output area.
static void render_shelf_cover(app_ctx_t *ctx, int row) {
    SHELF_PERF_SCOPE(perf,SP_RENDER,s_view,1);
    ui_text_set_system_scale(true);
    lock_draw();
    EpdRect card=row_rect(row);
    EpdRect band={card.x,card.y,card.width,shelf_cover_top(card)-card.y+shelf_cover_height()};
    epd_fill_rect(band,UI_GRAY_WHITE,ctx->fb);
    const shelf_entry_t *item = shelf_entry_at(ctx->leaf * shelf_rows() + row);
    char name[128];copy_text(name,sizeof(name),item ? item->name : "");
    draw_shelf_cover(ctx->fb,card,row,name);
    unlock_draw();
}
static void render_view(app_ctx_t* ctx, uint8_t* fb) {
    if (s_view == DOCUMENTS) {draw_documents(ctx,fb);return;}
    if (s_view == EDIT) { draw_editor(fb); return; }
    if (s_view == SEARCH) { draw_search(fb); return; }
    if (s_view == TOC) {
        if (reader_landscape() && s_text) { draw_landscape_reader(ctx, fb, s_page, false); return; }
        book_toc_render(fb, book_toc_entry_count(), current_toc_position(), ctx->leaf, s_message);
        reader_page_status(fb);
        reader_control_header(fb, UI_MSG("目录"));
        if (s_toc_jump_open)
            book_toc_render_jump(fb, book_toc_entry_count(), s_toc_jump_percent);
        else reader_control_footer(fb);
        return;
    }
    if(s_view==READING&&s_text)draw_reader(ctx,fb,s_page);
    else{
        // 详情复用书架背景，不再绘制独立详情页的管理栏。/ Details reuse the shelf background without a separate management page.
        ui_clear_page(fb);epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,UI_NAV_TOP},UI_GRAY_WHITE,fb);
        ui_nav_status(fb);draw_content_tabs(fb,false);draw_content_actions(fb,s_query);
        ui_hairline(fb,CONTENT_HEADER_RULE_Y,36,612,0x68);
        if (s_view == SHELF && s_scan_pending) {
            ui_text_vc(fb,UI_LOCK_WIDTH/2,UI_LOCK_HEIGHT/2,27,UI_MSG("加载中"),EPD_DRAW_ALIGN_CENTER,false);
            ui_nav_draw(fb,1);
            return;
        }
        if(s_message[0]||s_shelf_warning[0])ui_text(fb,UI_MARGIN,1023,17,s_message[0]?s_message:s_shelf_warning,EPD_DRAW_ALIGN_LEFT,false);
        if(!s_visible_count&&s_count)ui_text(fb,UI_MARGIN,308,UI_PX_CAPTION,UI_MSG("当前筛选没有图书"),EPD_DRAW_ALIGN_LEFT,false);
        if(!s_message[0]||s_visible_count){
            int rows=shelf_rows();
            for (int row = 0; row < rows; ++row) {
                const shelf_entry_t *item = shelf_entry_at(ctx->leaf * rows + row);
                if (!item) break;
                EpdRect r=row_rect(row);
                char name[128];copy_text(name,sizeof(name),item->name);
                draw_shelf_cover(fb,r,row,name);
                draw_shelf_title(fb,r,name);
                EpdRect track=shelf_progress_rect(r);
                epd_fill_rect(track,0xc0,fb);unsigned pct=item->pct>100?100:item->pct;
                if(pct){EpdRect fill=track;fill.width=(track.width*(int)pct+99)/100;epd_fill_rect(fill,UI_GRAY_BLACK,fb);}
            }
        }
        if(s_visible_count||s_shelf_cache_valid) draw_shelf_pager(fb,s_visible_count,ctx->leaf,leaves());
        ui_nav_draw(fb,1);
        if(s_view==MANAGE)draw_manage(fb);
    }
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    SHELF_PERF_SCOPE(perf,SP_RENDER,s_view,0);
    ui_text_set_system_scale(s_view != READING);
    if (s_view == SHELF || s_view == MANAGE) prepare_covers(ctx);
    lock_draw();
    render_view(ctx,fb);
    if (s_sort_open) draw_content_sort(fb);
    unlock_draw();
}
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    // 通用入口会开启系统缩放；局刷/空闲也须在预渲染前恢复阅读字号。
    // Restore reader scale even for area/idle presents, before any prefetched header is drawn.
    ui_text_set_system_scale(s_view != READING);
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return true;
    SHELF_PERF_SCOPE(present_perf,SP_PRESENT,redraw,s_view);
    if (s_presented_view != (int)s_view && redraw == APP_REDRAW_AREA) redraw = APP_REDRAW_PAGE;
    if ((redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) &&
        (s_view != READING || s_reader_panel != READER_PANEL_FONT_PICKER)) ttf_name_preview_pause();
    if (redraw == APP_REDRAW_AREA && s_view == READING && s_reader_panel == READER_PANEL_FONT_PICKER && !s_reader_cleanup) {
        guard_draw_result(ctx->hl,update_display_natural_area(ctx->hl,s_area));
        return true;
    }
    if((s_view==EDIT||s_view==SEARCH)&&redraw==APP_REDRAW_AREA){
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_text_input_area()));
        return true;
    }
    int64_t start = esp_timer_get_time();
    operation_log_event(OPLOG_PRESENT,0,redraw,s_view,s_water_turn_pending,0);
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) render(ctx, ctx->fb);
    bool prep = kick_prep();
    int64_t drawn = esp_timer_get_time();
    shelf_perf_span_t epd_perf=shelf_perf_begin(SP_EPD,redraw,s_view);
    SHELF_PERF_SCOPE(nav_output_perf,SP_NAV_OUTPUT,1,redraw);
    enum EpdDrawError err;
    const bool landscape = reader_landscape();
    bool image_gray_refresh = s_reader_image_refresh_pending && s_view == READING &&
        s_reader_panel == READER_PANEL_NONE && !s_toolbar && !s_clear_confirm;
    if (image_gray_refresh && !landscape && !s_reader_cleanup && s_presented_view == (int)READING) {
        // 灰阶只驱动正文，避免每张插图都把整屏先清白再闪回。
        // Drive the grayscale body from the current frame instead of clearing the whole screen to white.
        err = update_display_area_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16, reader_area());
        if (err == EPD_DRAW_SUCCESS && s_reader_footer_pending)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, progress_rect());
        if (err == EPD_DRAW_SUCCESS && s_reader_fullscreen)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                reader_fullscreen_progress_area());
    }
    else if (image_gray_refresh && !landscape) err = update_display_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16);
    else if (redraw == APP_REDRAW_FULL || s_reader_cleanup) err = update_display_full(ctx->hl);
    else if (image_gray_refresh) err = update_display_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16);
    else if(landscape && s_water_turn_pending) {
        // Stagger the body in visual coordinates and update status and progress independently.
#ifdef PICO_HOST_SIMULATOR
        ++s_sim_water_turns;
#endif
        enum EpdRotation previous = epd_get_rotation();
        reader_set_landscape_rotation();
        EpdRect body = landscape_reader_area();
        err = update_display_water_turn(ctx->hl, body, s_water_turn_dir, true);
        if (err == EPD_DRAW_SUCCESS && body.y)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                                                (EpdRect){0,0,body.width,body.y});
        if (err == EPD_DRAW_SUCCESS && body.y + body.height < UI_LOCK_WIDTH)
            err = update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16,
                (EpdRect){0,body.y+body.height,body.width,UI_LOCK_WIDTH-body.y-body.height});
        epd_set_rotation(previous);
    }
    else if(wide_panel_open() && redraw==APP_REDRAW_AREA)err=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,s_area);
    else if(s_view==READING&&s_toolbar)err=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,redraw==APP_REDRAW_AREA?s_area:(EpdRect){0,0,684,1216});
    else if (redraw == APP_REDRAW_AREA) {
        // 翻页动画与页脚刷新独立：全屏没有页脚，仍使用用户选择的水波纹。
        // The turn effect is independent of the footer: full-screen turns still use the selected water effect.
        err = s_water_turn_pending
            ? update_display_water_turn(ctx->hl, s_area, s_water_turn_dir, false)
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
    else err = s_view == READING || (s_view == TOC && landscape)
        ? update_display_mode_diff(ctx->hl, APP_PAGE_REFRESH_MODE)
        : s_view==SHELF?update_display_navigation_page(ctx->hl,(EpdRect){0,128,684,928},ui_nav_rect()):
          s_view==DOCUMENTS?update_display_navigation_page(ctx->hl,(EpdRect){0},ui_nav_rect()):update_display_fast_page(ctx->hl);
    shelf_perf_end(&epd_perf);
    int64_t displayed = esp_timer_get_time();
    reader_prefetch_end(prep);
    operation_log_event(OPLOG_PRESENT,1,redraw,s_view,s_water_turn_pending,(int)((esp_timer_get_time()-start)/1000));
    operation_log_event(OPLOG_PRESENT,2,(int)((drawn-start)/1000),(int)((displayed-drawn)/1000),0,(int)((esp_timer_get_time()-displayed)/1000));
    if (redraw == APP_REDRAW_AREA && s_mode == MODE_DU &&
        !s_reader_cleanup && !image_gray_refresh) {
        s_du_area = s_du_count ? ui_rect_union(s_du_area, s_area) : s_area;
        ++s_du_count;
        s_du_ms = esp_timer_get_time() / 1000;
    } else s_du_count = 0;
    ESP_LOGI(TAG, "present draw=%lld display=%lld join=%lld ms", (drawn - start) / 1000,
             (displayed - drawn) / 1000, (esp_timer_get_time() - displayed) / 1000);
    guard_draw_result(ctx->hl, err);
    // 给主循环留出采样窗口，避免接连推送封面时吞掉点按。
    // Give the input loop time to sample taps between cover updates.
    if (s_view == SHELF) {
        if ((redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) && err == EPD_DRAW_SUCCESS)
            for (int i = 0; i < shelf_rows(); ++i) s_covers[i].ready = false;
        if (s_cover_pending_mask || s_scan_pending || redraw == APP_REDRAW_AREA)
            s_cover_next_ms = esp_timer_get_time() / 1000 + 150;
    }
    shelf_perf_event(SP_FRAME,s_view,s_visible_count,(int)s_cover_pending_mask,err);
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
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() && (reader_image_slots(s_page)>0);
    if (reader_landscape()) return APP_REDRAW_PAGE;
    lock_draw();
    bool cached = !s_toolbar && !s_clear_confirm && s_next_fb &&
        ((s_next_page==(int)s_page&&s_next_generation==book_layout_generation())||reader_prefetch_resume(s_page));
    if (cached) {
        memcpy(ctx->fb,s_next_fb,fb_bytes());
        if (s_reader_fullscreen && !s_reader_immersive) {
            epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_STATUS_HEIGHT}, UI_GRAY_WHITE, ctx->fb);
            reader_page_status(ctx->fb);
        } else if (!s_reader_fullscreen) reader_status(ctx->fb,NULL,false);
    }
    else draw_reader(ctx, ctx->fb, s_page);
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

static bool reader_image_dims(void* ctx, int image, int* width, int* height) {
    const reader_dims_ctx_t* c = (const reader_dims_ctx_t*)ctx;
    if (!c || image < 0 || (size_t)image >= c->count) return false;
    if(book_kind()==BOOK_KIND_MARKDOWN){
        char path[512];uint32_t w=0,h=0;EpdRect body=body_rect();
        if(!book_markdown_attachment(c->images[image],path,sizeof(path))||!document_image_dimensions(path,&w,&h))return false;
        // 长图保留独页预览；普通图参与现有纵向排版。/ Keep tall-image previews; flow ordinary images with text.
        if((uint64_t)h*body.width>(uint64_t)w*body.height)return false;
        if(width)*width=(int)w;
        if(height)*height=(int)h;
        return true;
    }
    const size_t prefix_capacity = 4096;
    uint8_t* encoded = heap_caps_malloc(prefix_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t size = 0;
    unsigned w = 0, h = 0;
    bool measured = false;
    if (encoded) {
        if (book_chapter_image_prefix(c->chapter, c->images[image], encoded, prefix_capacity, &size) == ESP_OK) {
            bool png = size >= 8 && !memcmp(encoded, "\x89PNG\r\n\x1a\n", 8);
            measured = book_image_dimensions(encoded, size, png, &w, &h);
        }
        free(encoded);
    }
    if (!measured) {
        // SVG 包装与较晚的 JPEG 尺寸头仍走完整解包及校验。
        // SVG wrappers and late JPEG frame headers still use full extraction and validation.
        bool png = false;
        encoded = NULL;
        if (book_chapter_image(c->chapter, c->images[image], &encoded, &size, &png) != ESP_OK) return false;
        bool ok = book_image_dimensions(encoded, size, png, &w, &h);
        free(encoded);
        if (!ok) return false;
    }
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
    // 休眠前将基准清零；唤醒首帧重开活跃窗口，不计算睡眠间隔。
    // Lock clears the baseline; the first awake tick restarts activity without crediting sleep.
    if (!s_stats_last_ms) s_stats_activity_ms = ctx->now_ms;
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
    reader_note_card_close();
    book_layout_set_links(NULL, 0);
    for (size_t i = 0; i < s_chapter_link_count; ++i) free(s_chapter_links[i].href);
    free(s_chapter_links);
    s_chapter_links = NULL;
    s_chapter_link_count = 0;
    free(s_chapter_positions.large);
    s_chapter_positions = (html_positions_t){0};
    s_note_open = false;
    s_note_origin_is_annotation = false;
    memset(&s_document_image,0,sizeof(s_document_image));
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
    s_epub_section_has_text = false;
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
    s_reader_screen = APP_BOOK_PORTRAIT;
    book_layout_set_two_columns(false);
    book_layout_set_vertical(false);
    book_layout_set_markdown(false,NULL,NULL,0);
    s_block_count = 0;
    s_text = NULL;
    s_text_len = 0;
    s_selected_toc = SIZE_MAX;
    s_jump_offset = SIZE_MAX;
    s_reader_english = false;
    s_reader_localized_cover = false;
    book_layout_set_english_justification(false);
    app_font_set_english_mode(false, false);
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
static void prepare_page_images(void) {
    if (app_settings_reader_hide_images()) { release_page_images(); return; }
    uint32_t generation = book_layout_generation();
    if (s_page_images_for == s_page && s_page_images_generation == generation) return;
    int count = reader_image_slots(s_page);
    // 纯文字页保留预渲染结果，只有插图页需重画。/ Preserve prefetched text frames; only image pages require repainting.
    if (count) {invalidate_prep();free(s_next_fb);s_next_fb=NULL;}
    release_page_images();
    s_page_images_for = s_page; s_page_images_generation = generation;
    if (!count || count > (int)HTML_TEXT_MAX_BLOCKS) return;
    trim_reader_covers(READER_COVERS_CHECK);
    s_page_images = heap_caps_calloc((size_t)count, sizeof(*s_page_images), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_page_images) return;
    s_page_image_count = count;
    // 逐张解码并立即释放压缩数据；全页灰阶位图的总面积不超过正文区域。
    // Decode serially and release each encoded buffer; the page's bitmap area stays within its body.
    size_t pixels_left = (size_t)body_rect().width * body_rect().height;
    for (int i = 0; i < count; ++i) {
        int index = -1;
        EpdRect bounds;
        if (!reader_image_slot(s_page, i, &index, &bounds) ||
            index < 0 || (size_t)index >= s_image_count || bounds.width <= 0 || bounds.height <= 0) continue;
        int width = bounds.width, height = bounds.height;
        if(book_kind()==BOOK_KIND_MARKDOWN){
            char path[512];document_image_t image={0};size_t pixels=(size_t)width*height;
            if(pixels>pixels_left||!book_markdown_attachment(s_images[index],path,sizeof(path))||!document_image_open(path,&image))continue;
            uint8_t *gray=heap_caps_malloc(pixels,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(gray&&document_image_sample(&image,0,image.height,width,height,gray)){
                s_page_images[i]=(reader_image_t){gray,width,height};pixels_left-=pixels;
            }else free(gray);
            vTaskDelay(1);continue;
        }
        uint8_t *encoded = NULL; size_t size = 0; bool png = false;
        esp_err_t err = book_chapter_image(s_chapter, s_images[index], &encoded, &size, &png);
        if (err == ESP_ERR_NO_MEM && trim_reader_covers(READER_COVERS_RETRY))
            err = book_chapter_image(s_chapter, s_images[index], &encoded, &size, &png);
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
    if (book_kind() == BOOK_KIND_EPUB || (book_kind()==BOOK_KIND_MARKDOWN && book_layout_page_image_count(s_page))) {
        free(s_inline_gray);s_inline_gray=NULL;s_inline_index=-1;prepare_page_images();return;
    }
    release_page_images();
    if (app_settings_reader_hide_images()) return;
    int index = book_layout_page_image(s_page);
    if (index < 0) { free(s_inline_gray); s_inline_gray=NULL; s_inline_index=-1; return; }
    if ((index == s_inline_index && s_inline_gray) || (size_t)index >= s_image_count) return;
    invalidate_prep();
    free(s_inline_gray); s_inline_gray = NULL;
    s_inline_index = index;
    memset(&s_document_image,0,sizeof(s_document_image));
    if(book_kind()==BOOK_KIND_MARKDOWN){
        char path[512];if(!book_markdown_attachment(s_images[index],path,sizeof(path))||!document_image_open(path,&s_document_image))return;
        EpdRect body=body_rect();
        double scale_x=(double)body.width/s_document_image.width,scale_y=(double)(body.height)/s_document_image.height;
        double scale=scale_x<scale_y?scale_x:scale_y;if(scale>1)scale=1;
        s_inline_w=(unsigned)(s_document_image.width*scale);s_inline_h=(unsigned)(s_document_image.height*scale);if(!s_inline_w)s_inline_w=1;if(!s_inline_h)s_inline_h=1;
        s_inline_gray=heap_caps_malloc((size_t)s_inline_w*s_inline_h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
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
typedef enum { CHAPTER_KEEP_NOTE, CHAPTER_FOLLOW_LINK, CHAPTER_FOLLOW_NOTE,
               CHAPTER_LEAVE_NOTE, CHAPTER_STORED_POSITION, CHAPTER_BOOK_START } chapter_note_action_t;

static bool note_returns_to_source(const html_text_t *text, size_t anchor) {
    while (anchor < text->len && (text->utf8[anchor] == ' ' || text->utf8[anchor] == '\n')) ++anchor;
    if (anchor == s_note_source) return true;
    for (size_t i = 0; i < text->count && text->blocks[i].offset <= s_note_source; ++i) {
        const blk_t *block = &text->blocks[i];
        size_t end = (size_t)block->offset + block->len;
        if (s_note_source >= end) continue;
        if (anchor != block->offset) return false;
        // 旧书会跳回段首；仅在该段的注释角标唯一时视为原路返回。
        // Legacy backlinks can target the paragraph start; require an unambiguous marker.
        for (size_t n = 0; n < text->link_count && text->links[n].start < end; ++n) {
            const html_link_t *link = &text->links[n];
            if (link->start >= block->offset && link->start != s_note_source) return false;
        }
        return true;
    }
    return false;
}

static bool note_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool note_marker(const html_link_t *link, const char *text) {
    if (link->image_label[0]) return true;
    const char *at = text + link->start, *end = text + link->end;
    while (at < end && note_space(end[-1])) --end;
    bool bracketed = end - at > 2 && ((*at == '[' && end[-1] == ']') ||
                                     (*at == '(' && end[-1] == ')'));
    if (bracketed) { ++at; --end; }
    if (bracketed && end - at == 1 && *at == 'R') return true;
    unsigned count = 0;
    while (at < end && count < 5) {
        size_t bytes;
        uint32_t cp = book_word_cp(at, end, &bytes);
        if (cp == '*' || cp == 0x2020 || cp == 0x2021 || cp == 0x21a9 || cp == 0x6ce8)
            return at + bytes == end && !count;
        if (!((cp >= '0' && cp <= '9') || (cp >= 0xff10 && cp <= 0xff19) ||
              (cp >= 0x2074 && cp <= 0x2079) || cp == 0x2070 ||
              cp == 0xb9 || cp == 0xb2 || cp == 0xb3 || (cp >= 0x2460 && cp <= 0x2473)))
            return false;
        at += bytes;
        ++count;
    }
    return count && at == end;
}

static const blk_t *reader_link_block(const blk_t *blocks, size_t count, size_t offset) {
    for (size_t i = 0; i < count && blocks[i].offset <= offset; ++i) {
        if (offset - blocks[i].offset < blocks[i].len) return &blocks[i];
    }
    return NULL;
}

static bool reader_link_is_note(const html_link_t *source, size_t chapter, const char *fragment,
                                const html_text_t *target, size_t anchor, bool *from_note) {
    *from_note = false;
    if (!fragment || source->navigation) return false;
    *from_note = source->backlink;
    if (source->backlink || source->noteref || target->anchor_in_note) return true;

    const blk_t *destination = reader_link_block(target->blocks, target->count, anchor);
    if (!destination || destination->heading || !note_marker(source, s_text)) return false;
    const blk_t *origin = reader_link_block(s_blocks, s_block_count, source->start);
    if (!origin || origin->heading) return false;
    const bool source_at_start = origin->offset == source->start;
    // 旧书的脚注常是「正文内角标 ↔ 注释段首」，单独的数字目录项不算脚注。
    // Legacy notes pair an inline marker with an annotation paragraph's opening marker.
    size_t end = (size_t)destination->offset + destination->len;
    for (size_t i = 0; i < target->link_count && target->links[i].start < end; ++i) {
        const html_link_t *ref = &target->links[i];
        if (ref->start < destination->offset || ref->navigation ||
            !(ref->backlink || ref->noteref || note_marker(ref, target->utf8))) continue;
        bool ref_at_start = destination->offset == ref->start;
        if (!ref->backlink && !ref->noteref && source_at_start == ref_at_start) continue;
        size_t back_chapter;
        char back_anchor[HTML_LINK_ANCHOR_CAP];
        if (!book_chapter_link_target(chapter, ref->href, &back_chapter, back_anchor, sizeof(back_anchor)) ||
            back_chapter != s_chapter) continue;
        if ((source->anchor && !strcmp(back_anchor, source->anchor)) ||
            (source->block_anchor && !strcmp(back_anchor, source->block_anchor))) {
            *from_note = !ref->backlink && (ref->noteref || source_at_start);
            return true;
        }
    }
    return false;
}

static bool note_points_to(const html_link_t *link, const char *target) {
    if (!target) return false;
    size_t chapter;
    char anchor[HTML_LINK_ANCHOR_CAP];
    return book_chapter_link_target(s_chapter, link->href, &chapter, anchor, sizeof(anchor)) &&
           chapter == s_chapter && !strcmp(anchor, target);
}

static bool note_paragraph_boundary(const html_text_t *text, size_t offset) {
    while (offset < text->len && note_space(text->utf8[offset])) ++offset;
    if (offset == text->len) return true;
    for (size_t i = 0; i < text->link_count && text->links[i].start <= offset; ++i) {
        const html_link_t *next = &text->links[i];
        if (next->start != offset || next->noteref) continue;
        if (next->backlink) return true;
        for (size_t n = 0; n < i && text->links[n].start < text->note_start; ++n) {
            const html_link_t *ref = &text->links[n];
            if (!ref->backlink && note_points_to(next, ref->anchor) && note_points_to(ref, next->anchor))
                return true;
        }
    }
    return false;
}

static bool wrap_reader_note_text(reader_note_card_t *card, char *at, int x, int width,
                                  int step, int limit, int *y, bool hyphenate) {
    const char *section = at, *limit_text = at + strlen(at);
    int dash_width = hyphenate ? ttf_text_width_px(card->px, "-") : 0;
    while (*at) {
        while (note_space(*at)) ++at;
        if (!*at) break;
        char *start = at, *word_start = at, *split = NULL;
        uint32_t previous = 0;
        bool hyphen = false, previous_joiner = false, split_draws_dash = false;
        book_hyphen_range_t range = {0};
        int used = 0;
        while (*at && *at != '\n') {
            size_t bytes;
            uint32_t cp = book_word_cp(at, limit_text, &bytes);
            char *next = at + bytes;
            char glyph[5];
            memcpy(glyph, at, bytes);
            glyph[bytes] = 0;
            bool previous_part = book_word_part(previous);
            bool joiner = (previous_part || previous_joiner) && book_word_joiner(cp, next, limit_text);
            bool continues = (previous_part || previous_joiner) && (book_word_part(cp) || joiner);
            if (hyphenate && continues && previous_joiner && previous == '-' && book_word_part(cp)) {
                split = at; split_draws_dash = false;
            } else if (continues && book_hyphen_fits_range(range, at) && used + dash_width <= width) {
                split = at; split_draws_dash = true;
            }
            int advance = ttf_text_width_px(card->px, glyph);
            if (used + advance > width) {
                if (continues && split) { at = split; hyphen = split_draws_dash; }
                else if (continues && word_start > start) at = word_start;
                break;
            }
            if (!continues) {
                word_start = at;
                split = NULL;
                range = hyphenate && book_latin_letter(cp) && (at > start || !book_word_before(section, at))
                    ? book_hyphen_range(at, limit_text) : (book_hyphen_range_t){0};
            }
            previous = cp;
            previous_joiner = joiner;
            used += advance;
            at = next;
        }
        if (at == start) return false;
        char *end = at;
        while (end > start && note_space(end[-1])) --end;
        int above, below;
        char tail = *end, after = hyphen ? end[1] : 0;
        *end = hyphen ? '-' : 0;
        if (hyphen) end[1] = 0;
        ttf_measure_line_px(card->px, start, &above, &below);
        *end = tail;
        if (hyphen) end[1] = after;
        int height = above + below + card->px / 6;
        if (height < step) height = step;
        if (height > limit - *y) return false;
        card->lines[card->count++] = (reader_note_line_t){
            .offset = start - card->text, .len = end - start, .x = x,
            .baseline = *y + (height - above - below) / 2 + above, .hyphen = hyphen,
        };
        *y += height;
    }
    return true;
}

static bool layout_reader_note_card(reader_note_card_t *card) {
    bool wide = reader_landscape();
    int limit = (wide ? UI_LOCK_WIDTH : UI_LOCK_HEIGHT) / 2 - NOTE_CARD_PADDING;
    card->px = s_px * 9 / 10;
    int step = card->px * 5 / 4;
    // Each row consumes at least step pixels within its column.
    size_t capacity = (size_t)(limit / step) * (wide ? 2 : 1);
    card->lines = heap_caps_calloc(capacity, sizeof(*card->lines), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!card->lines) return false;
    int title_y = wide ? NOTE_CARD_WIDE_TOP : NOTE_CARD_TITLE_PAD;
    int title_x = wide ? NOTE_CARD_WIDE_GAP : NOTE_CARD_MARGIN;
    int title_width = wide ? NOTE_CARD_WIDE_RAIL - 2 * NOTE_CARD_WIDE_GAP : UI_LOCK_WIDTH - 2 * NOTE_CARD_MARGIN - 64;
    if (!wrap_reader_note_text(card, card->text, title_x, title_width, step, limit, &title_y, false)) return false;
    int body_y = NOTE_CARD_WIDE_TOP;
    int body_x = NOTE_CARD_WIDE_RAIL + NOTE_CARD_WIDE_GAP;
    int body_width = UI_LOCK_HEIGHT - body_x - NOTE_CARD_MARGIN;
    if (!wide) {
        title_y += NOTE_CARD_TITLE_PAD;
        if (title_y < NOTE_CARD_HEADER) {
            for (size_t i = 0; i < card->count; ++i)
                card->lines[i].baseline += (NOTE_CARD_HEADER - title_y) / 2;
            title_y = NOTE_CARD_HEADER;
        }
        body_y = title_y;
        body_x = NOTE_CARD_MARGIN;
        body_width = UI_LOCK_WIDTH - 2 * NOTE_CARD_MARGIN;
    }
    size_t title_lines = card->count;
    if (!wrap_reader_note_text(card, card->text + card->body_offset, body_x, body_width, step, limit, &body_y, s_reader_hyphenate) ||
        card->count == title_lines) return false;
    card->height = (body_y > title_y ? body_y : title_y) + NOTE_CARD_PADDING;
    return true;
}

static bool open_reader_note_card(size_t chapter, const html_link_t *ref, const html_text_t *text,
                                  size_t anchor) {
    if (chapter != s_chapter || ref->backlink || text->note_kind == HTML_NOTE_NONE) return false;
    size_t start = text->note_start, end = text->note_end;
    if (start >= end || end > text->len || anchor < start || anchor >= end) return false;
    bool paragraph = text->note_kind == HTML_NOTE_PARAGRAPH;
    if (paragraph && (anchor <= ref->start || !note_paragraph_boundary(text, end))) return false;
    for (size_t i = 0; i < text->count && text->blocks[i].offset < end; ++i) {
        const blk_t *block = &text->blocks[i];
        if (block->offset + block->len > start && block->image >= 0) return false;
    }
    const char *marker = s_text + ref->start;
    size_t marker_len = ref->end - ref->start;
    if (ref->image_label[0]) {
        marker = ref->image_label[0] == '*' ? "" : ref->image_label;
        marker_len = strlen(marker);
    }
    while (marker_len && note_space(marker[marker_len - 1])) --marker_len;
    const char *prefix = ui_message(UI_MSG("注释"));
    lock_draw();
    bool latin_title = ui_i18n_english() || !ttf_font_has_text(prefix);
    unlock_draw();
    if (latin_title) prefix = marker_len ? "Note " : "Note";
    size_t prefix_len = strlen(prefix), body_offset = prefix_len + marker_len + 1;
    reader_note_card_t *card = heap_caps_calloc(1, sizeof(*card) + body_offset + end - start + 1,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!card) return false;
    card->body_offset = body_offset;
    memcpy(card->text, prefix, prefix_len);
    memcpy(card->text + prefix_len, marker, marker_len);
    char *body = card->text + body_offset;
    size_t at = start, used = 0;
    bool reciprocal = false, complete = true;
    for (size_t i = 0; i < text->link_count && text->links[i].start < end; ++i) {
        const html_link_t *link = &text->links[i];
        if (link->end <= at) continue;
        if (link->start < at || link->end > end || !note_points_to(link, ref->anchor)) {
            complete = false;
            break;
        }
        size_t before_link = link->start - at;
        memcpy(body + used, text->utf8 + at, before_link);
        used += before_link;
        at = link->end;
        reciprocal = true;
    }
    memcpy(body + used, text->utf8 + at, end - at);
    if (complete && (!paragraph || reciprocal)) {
        lock_draw();
        bool fits = layout_reader_note_card(card);
        unlock_draw();
        if (fits) {
            s_note_card = card;
            return true;
        }
    }
    reader_note_card_free(card);
    return false;
}

static bool load_chapter_at(app_ctx_t* ctx, size_t chapter, size_t offset,
                            bool last_page, const char *anchor, size_t source_offset,
                            chapter_note_action_t note_action, const html_link_t *note_link) {
    html_text_t loaded = {0};
    bool following_link = note_action == CHAPTER_FOLLOW_LINK;
    bool from_note = false;
    size_t anchor_offset = 0;
    trim_reader_covers(READER_COVERS_CHECK);
    const bool book_start = note_action == CHAPTER_BOOK_START;
    esp_err_t err;
    for (;;) {
        err = book_chapter_load_blocks_target(chapter, anchor, source_offset, &anchor_offset, &loaded);
        if (err == ESP_ERR_NO_MEM && trim_reader_covers(READER_COVERS_RETRY)) {
            html_text_free(&loaded);
            err = book_chapter_load_blocks_target(chapter, anchor, source_offset, &anchor_offset, &loaded);
        }
        // 书首可跳过空白或损坏条目，遇到存储和内存错误则直接报告。/ Skip blank or malformed leading sections, but surface I/O and memory failures.
        if (!book_start || !((err == ESP_OK && !loaded.count) || err == ESP_ERR_INVALID_RESPONSE ||
                             err == ESP_ERR_INVALID_CRC)) break;
        html_text_free(&loaded);
        if (++chapter >= book_chapter_count()) {
            err = ESP_ERR_NOT_FOUND;
            break;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chapter load failed: %s", esp_err_to_name(err));
        copy_text(s_message, sizeof(s_message), UI_MSG("加载失败，请检查存储卡后重试"));
        return false;
    }
    if (book_start) note_action = CHAPTER_LEAVE_NOTE;
    if (anchor_offset == SIZE_MAX) {
        if (following_link) { html_text_free(&loaded); return false; }
        anchor_offset = 0;
    }
    if (following_link) {
        if (reader_link_is_note(note_link, chapter, anchor, &loaded, anchor_offset, &from_note)) {
            note_action = CHAPTER_FOLLOW_NOTE;
        } else {
            if (chapter >= book_chapter_count()) { html_text_free(&loaded); return false; }
            note_action = CHAPTER_LEAVE_NOTE;
        }
    }
    bool save_link_destination = following_link && note_action == CHAPTER_LEAVE_NOTE;
    if (note_action == CHAPTER_STORED_POSITION) {
        offset = html_position_convert(&loaded.positions, offset, true);
        note_action = CHAPTER_KEEP_NOTE;
    }
    bool localized_cover;
    const content_language_t language = book_language_for_text(chapter, &loaded, &localized_cover);
    const bool english = language == CONTENT_LANGUAGE_ENGLISH;
    if (note_action == CHAPTER_FOLLOW_NOTE && !s_note_open && note_link) {
        if (english == s_reader_english && localized_cover == s_reader_localized_cover &&
            open_reader_note_card(chapter, note_link, &loaded, anchor_offset)) {
            html_text_free(&loaded);
            return true;
        }
        s_note_origin = (book_progress_t){.chapter=s_chapter,.byte_off=reader_page_offset(s_page),.pct=percent(s_page)};
        s_note_origin_stored_offset = reader_stored_offset(s_note_origin.byte_off);
        s_note_source = note_link->start;
        save_progress();
    }
    if (note_action == CHAPTER_FOLLOW_NOTE && s_note_open && chapter == s_note_origin.chapter &&
        note_returns_to_source(&loaded, anchor_offset)) {
        note_action = CHAPTER_LEAVE_NOTE;
        offset = s_note_origin.byte_off;
        anchor = NULL;
    }
    bool note_open = note_action == CHAPTER_KEEP_NOTE ? s_note_open : note_action == CHAPTER_FOLLOW_NOTE;
    bool note_origin_is_annotation = s_note_open ? s_note_origin_is_annotation : from_note;
    EpdRect new_body = reader_body_rect(note_open);
    char heading[128] = {0}, label[32] = {0}, toc_title[128] = {0};
    size_t lead_skip = 0;
    unsigned lead_height = 0;
    const bool epub = book_kind() == BOOK_KIND_EPUB;
    bool section_has_text = false;
    if (epub && chapter < book_chapter_count()) {
        for (size_t i = 0; i < loaded.count; ++i) {
            if (loaded.blocks[i].image < 0 && loaded.blocks[i].len) {
                section_has_text = true;
                break;
            }
        }
    }
    if (section_has_text && book_epub_reading_title(chapter, SIZE_MAX, toc_title, sizeof(toc_title)) == ESP_OK) {
        copy_text(heading, sizeof(heading), toc_title);
        char *chapter_mark = strstr(heading, UI_MSG("章"));
        if (chapter_mark && chapter_mark - heading < 24) {
            char *after = chapter_mark + strlen(UI_MSG("章"));
            while (*after == ' ' || *after == '\t' || *after == ':' || *after == '-' ||
                   !strncmp(after, "：", strlen("：")) || !strncmp(after, "、", strlen("、"))) {
                if (*after == ' ' || *after == '\t' || *after == ':' || *after == '-') ++after;
                else after += strlen("：");
            }
            if (*after && (size_t)(chapter_mark + strlen(UI_MSG("章")) - heading) < sizeof(label)) {
                size_t label_len = (size_t)(chapter_mark + strlen(UI_MSG("章")) - heading);
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
        // 只重新排版书中已有的起始标题。/ Restyle only a heading already present at the start.
        if (skip_blocks && skip_blocks < loaded.count &&
            loaded.blocks[skip_blocks].image < 0 && new_body.height > 214) {
            lead_skip = loaded.blocks[skip_blocks].offset;
            lead_height = 214;
        }
    }
    size_t old_lead_skip = s_chapter_lead_skip;
    unsigned old_lead_height = s_chapter_lead_height;
    reader_dims_ctx_t old_dims_ctx = s_reader_dims_ctx;
    lock_draw();
    invalidate_prep();
    const bool previous_english = s_reader_english;
    const bool previous_localized_cover = s_reader_localized_cover;
    const bool font_profile_changed = english != previous_english || localized_cover != previous_localized_cover;
    if (!s_text || font_profile_changed) {
        s_reader_english = english;
        s_reader_localized_cover = localized_cover;
        app_font_set_english_mode(english, localized_cover);
        app_font_activate_reading();
    }
    book_layout_set_chapter_lead(lead_skip, lead_height);
    book_layout_set_english_justification(english);
    s_reader_dims_ctx = (reader_dims_ctx_t){chapter, loaded.images, loaded.image_count};
    book_layout_set_image_dims(book_kind()==BOOK_KIND_EPUB||book_kind()==BOOK_KIND_MARKDOWN ? reader_image_dims : NULL, &s_reader_dims_ctx);
    book_layout_set_markdown(book_kind()==BOOK_KIND_MARKDOWN,s_md_sizes+1,loaded.styles,loaded.style_count);
    book_layout_set_markdown_tables(loaded.cells, loaded.cell_count);
    book_layout_set_vertical(reader_book_vertical());
    book_layout_set_two_columns(s_reader_screen == APP_BOOK_LANDSCAPE_TWO_COLUMNS);
    book_layout_set_links(loaded.links, loaded.link_count);
    bool ok = book_layout_build_blocks(loaded.utf8, loaded.len, loaded.blocks, loaded.count, new_body, s_px);
    if (!ok && trim_reader_covers(READER_COVERS_RETRY))
        ok = book_layout_build_blocks(loaded.utf8, loaded.len, loaded.blocks, loaded.count, new_body, s_px);
    if (!ok) {
        if (font_profile_changed) {
            s_reader_english = previous_english;
            s_reader_localized_cover = previous_localized_cover;
            book_layout_set_english_justification(previous_english);
            app_font_set_english_mode(previous_english, previous_localized_cover);
            if (s_text) app_font_restore_reading(s_font_path);
            book_layout_set_vertical(reader_book_vertical());
        }
        s_reader_dims_ctx = old_dims_ctx;
        book_layout_set_markdown(book_kind()==BOOK_KIND_MARKDOWN,s_md_sizes+1,s_text_styles,s_text_style_count);
        book_layout_set_markdown_tables(s_text_cells, s_text_cell_count);
        book_layout_set_links(s_chapter_links, s_chapter_link_count);
        html_text_free(&loaded);
        book_layout_set_chapter_lead(old_lead_skip, old_lead_height);
        bool restored = s_text && book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            ctx->leaf = 0;
        }
        copy_text(s_message, sizeof(s_message), UI_MSG("排版失败：内存不足或章节过长"));
        return false;
    }
    free(s_text);
    free(s_blocks);
    free(s_text_styles);
    s_text_styles = loaded.styles; s_text_style_count = loaded.style_count;
    free(s_text_cells);
    s_text_cells = loaded.cells; s_text_cell_count = loaded.cell_count;
    if (chapter != s_chapter || (!anchor && source_offset == SIZE_MAX)) s_selected_toc = SIZE_MAX;
    s_text = loaded.utf8;
    for (size_t i = 0; i < s_chapter_link_count; ++i) free(s_chapter_links[i].href);
    free(s_chapter_links);
    s_chapter_links = loaded.links;
    s_chapter_link_count = loaded.link_count;
    free(s_chapter_positions.large);
    s_chapter_positions = loaded.positions;
    s_text_len = loaded.len;
    s_blocks = loaded.blocks;
    s_block_count = loaded.count;
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_images = loaded.images; s_image_count = loaded.image_count;
    free(s_inline_gray); s_inline_gray = NULL; s_inline_index = -1;
    s_chapter = chapter;
    s_note_open = note_open;
    s_note_origin_is_annotation = note_open && note_origin_is_annotation;
    if (note_action == CHAPTER_LEAVE_NOTE) {
        s_reader_panel = READER_PANEL_NONE;
        s_reader_notice[0] = 0;
    }
    s_chapter_lead_skip = lead_skip;
    s_chapter_lead_height = lead_height;
    copy_text(s_chapter_heading_title, sizeof(s_chapter_heading_title), heading);
    copy_text(s_chapter_heading_label, sizeof(s_chapter_heading_label), label);
    s_page = last_page ? book_layout_page_count() - 1 :
             book_layout_page_for_offset(anchor || source_offset != SIZE_MAX ? anchor_offset : offset);
    s_jump_offset = anchor || source_offset != SIZE_MAX ? anchor_offset :
        (book_kind() != BOOK_KIND_MARKDOWN && !last_page && offset < loaded.len ? offset : SIZE_MAX);
    s_jump_page = s_page;
    s_epub_section_has_text = section_has_text;
    if (chapter >= book_chapter_count()) copy_text(s_title, sizeof(s_title), UI_MSG("注释"));
    else if (epub) copy_text(s_title, sizeof(s_title), toc_title);
    else if (book_chapter_title(chapter, s_title, sizeof(s_title)) != ESP_OK)
        ui_snprintf(s_title, sizeof(s_title), UI_MSG("第 %u 节"), (unsigned)chapter + 1);
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    book_remember_language(language);
    s_message[0] = 0;
    unlock_draw();
    if (save_link_destination) save_progress();
    prepare_inline_image();
    return true;
}
static bool load_chapter(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page) {
    if (chapter >= book_chapter_count() && !s_note_open) return false;
    return load_chapter_at(ctx, chapter, offset, last_page, NULL, SIZE_MAX, CHAPTER_KEEP_NOTE, NULL);
}
static bool load_saved_chapter(app_ctx_t *ctx, size_t chapter, size_t offset) {
    return chapter < book_chapter_count() &&
        load_chapter_at(ctx, chapter, offset, false, NULL, SIZE_MAX, CHAPTER_STORED_POSITION, NULL);
}
// 关闭图片时跳过纯插图页，避免出现空白翻页。/ Skip illustration-only pages when images are disabled.
static void skip_hidden_image_pages(app_ctx_t *ctx, int direction) {
    if (!app_settings_reader_hide_images()) return;
    for (int guard = 0; guard < 128 && book_layout_page_image(s_page) >= 0; ++guard) {
        if (direction > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
        else if (direction < 0 && s_page > 0) --s_page;
        else if (s_chapter >= book_chapter_count()) break;
        else if (direction > 0 && s_chapter + 1 < book_chapter_count()) {
            if (!load_chapter(ctx, s_chapter + 1, 0, false)) break;
        } else if (direction < 0 && s_chapter > 0) {
            if (!load_chapter(ctx, s_chapter - 1, 0, true)) break;
        } else break;
    }
}
static bool open_book(app_ctx_t* ctx, const char* path) {
    book_shelf_index_audit_stop();
    if (document_store_sync_recover() != ESP_OK) {
        ui_snprintf(s_message, sizeof(s_message), UI_MSG("同步恢复尚未完成，请重新进入同步页"));
        return false;
    }
    bool is_document=strrchr(path,'.')&&!strcasecmp(strrchr(path,'.'),".md");
    s_reader_return_home = false;
    if (!strncmp(path, "/sdcard/", 8)) {
        read_pico_sd_info_t sd = {0};
        read_pico_sd_get_info(&sd);
        if (!sd.present || !sd.mounted) {
            copy_text(s_message, sizeof(s_message), UI_MSG("TF 卡不可用，请重新挂载后打开"));
            return false;
        }
    }
    // 自动续读也不能绕过同路径旧文件的清理重试。
    // Automatic resume must also finish cleanup of the old file at this path.
    if (delete_retry_find(path)) {
        copy_text(s_message, sizeof(s_message), UI_MSG("文件已删除，进度清理失败，请重试"));
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        copy_text(s_message, sizeof(s_message), UI_MSG("文件不可读，请检查 TF 卡和书籍文件"));
        return false;
    }
    if (strncmp(path, "/flash/", 7) == 0 && st.st_size > BOOK_STORE_FLASH_FILE_MAX) {
        copy_text(s_message, sizeof(s_message), UI_MSG("内置存储单本限 1 MB"));
        return false;
    }
    const int shelf_leaf = s_view == SHELF ? ctx->leaf : 0;
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    // 当前书架页可供返回时复用；先释放备用页与上一阅读页的预渲染缓冲。
    // Keep the visible shelf page for return; release the spare page and old reader prefetch first.
    release_spare_covers();
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
    if (!pending_reserve(path) && !(trim_reader_covers(READER_COVERS_RETRY) && pending_reserve(path))) {
        copy_text(s_message, sizeof(s_message), UI_MSG("内存不足，无法保留待保存进度"));
        return false;
    }
    save_progress();
    free_book();
    trim_reader_covers(READER_COVERS_OPEN);
    // 更新可能使旧监视失效，须在刷新之后重新预留保存对象。/ Refresh can invalidate old watches; reserve after revision validation.
    if (is_document) {
        bool changed = false;
        esp_err_t err = document_store_refresh(path, &changed);
        if (err == ESP_ERR_NO_MEM && trim_reader_covers(READER_COVERS_RETRY))
            err = document_store_refresh(path, &changed);
        if (err != ESP_OK) {
            copy_text(s_message, sizeof(s_message), UI_MSG("文档或插图未完整入库，请检查后重试"));
            app_font_activate_system();
            return false;
        }
        if (changed) pending_discard(path);
    }
    if (!pending_reserve(path) && !(trim_reader_covers(READER_COVERS_RETRY) && pending_reserve(path))) {
        copy_text(s_message, sizeof(s_message), UI_MSG("内存不足，无法打开图书"));
        app_font_activate_system();
        return false;
    }
    esp_err_t err = book_open(path);
    if (err == ESP_ERR_NO_MEM && trim_reader_covers(READER_COVERS_RETRY)) err = book_open(path);
    s_document_mode=is_document;
    if (err != ESP_OK) {
        pending_progress_t* pending = pending_find(path);
        if (pending && !pending->dirty) pending_discard(path);
        copy_text(s_message, sizeof(s_message),
                  err == ESP_ERR_NOT_SUPPORTED ? UI_MSG("文件格式或压缩方式暂不支持") :
                  err == ESP_ERR_NO_MEM ? UI_MSG("内存不足，请重启后重试") :
                  err == ESP_ERR_INVALID_SIZE ? UI_MSG("图书资源过大或章节过多") :
                  UI_MSG("无法打开图书，请检查文件"));
        ESP_LOGW(TAG, "open failed: %s internal=%u/%u psram=%u/%u", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        app_font_activate_system();
        return false;
    }
    if (is_document) {
        s_document_layout=legacy_reader_layout();
        (void)content_document_layout_load(&s_document_layout);
    }
    content_document_layout_t layout=reader_layout();
    s_margin=layout.margin;
    s_line_spacing=layout.line;
    s_reader_fullscreen=layout.fullscreen;
    s_reader_immersive=layout.immersive;
    lock_draw();
    s_reader_hyphenate = content_english_hyphenation();
    book_layout_set_hyphenation(s_reader_hyphenate);
    book_layout_set_spacing(layout.line,layout.paragraph);
    book_layout_set_typography(((int)layout.tracking-2)*2);
    book_layout_set_first_line_indent(layout.indent);
    book_layout_set_reading_line(layout.rule);
    book_layout_set_reading_line_offset(layout.rule_offset);
    unlock_draw();
    s_reader_screen = is_document ? APP_BOOK_PORTRAIT : app_settings_book_screen();
    copy_text(s_path, sizeof(s_path), path);
    const char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    if (!book_title_display(path, s_book_title, sizeof(s_book_title)))
        clean_filename(s_book_title, sizeof(s_book_title), filename);
    s_file_size = (uint32_t)st.st_size;
    book_progress_t p = {0};
    bool resume = pending_restore(path, s_file_size, &p) || book_progress_load(path, s_file_size, &p);
    s_document_completed=is_document&&resume&&p.pct==100;
    s_md_notice[0]=0; s_md_committed=false;
    content_markdown_sizes(s_md_sizes);
    s_px = is_document ? s_md_sizes[0] : app_settings_book_px();
    if (!book_text_size_valid(s_px)) s_px = 48;
    size_t chapter = resume && p.chapter < book_chapter_count() ? p.chapter : 0;
    bool loaded = resume && p.chapter == chapter ? load_saved_chapter(ctx, chapter, p.byte_off) :
                                                  load_chapter(ctx, chapter, 0, false);
    if (!loaded) {
        free_book();
        return false;
    }
    skip_hidden_image_pages(ctx, 1);
    if (!s_text) { free_book(); return false; }
    s_reader_shelf_leaf = shelf_leaf;
    s_view = READING;
    s_reader_image_refresh_pending = !app_settings_reader_hide_images() && (reader_image_slots(s_page)>0);
    s_stats_last_ms = s_stats_activity_ms = ctx->now_ms;
    s_help_return_panel = READER_PANEL_NONE;
    s_reader_panel = reader_input_needs_help(s_path) ? READER_PANEL_HELP : READER_PANEL_NONE;
    s_control_page=0;reader_input_preferences_t input_prefs;reader_input_export(&input_prefs);s_help_hide=input_prefs.hide_help!=0;s_jump_back_valid=false;s_progress_drag=false;s_reader_multi=(reader_multi_t){0};
    s_clear_confirm = false;
    s_session_read_ms = s_session_turns = 0;
    s_reader_notice[0] = 0;
    s_turns = s_unsaved = 0;
    pending_mark_latest(s_path);
    save_progress();
    // 打开即重新上书架：移出书架只隐藏条目，重新读这本书就是它该在架上的信号。
    // Opening puts the book back on the shelf: removal only hides the entry, and reading it
    // again is the signal that it belongs there.
    if (shelf_hidden_load(s_path) && shelf_hidden_save(s_path, false) &&
        strcmp(s_shelf_progress_path,s_path)) s_shelf_progress_valid = false;
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
    return item->search_match;
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
#define BOOK_SCAN_DEPTH_LIMIT BOOK_SHELF_SCAN_DEPTH
typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} shelf_scan_frame_t;
static bool shelf_load_progress(const book_recent_view_t *recent, const char *path, uint32_t size, book_progress_t *out) {
    if (recent) {
        for (uint32_t i = 0; i < recent->book_count; ++i) {
            const book_recent_entry_t *entry = &recent->books[i];
            if (size != entry->progress.file_size || strcmp(path, entry->path)) continue;
            *out = entry->progress;
            return true;
        }
    }
    return book_progress_load(path, size, out);
}

static void scan_shelf_dir(const char *root, bool is_flash, records_handle_t hidden, bool *truncated, bool *unreadable,
                           bool *skipped, const book_recent_view_t *recent) {
    if (*truncated) return;
    shelf_scan_frame_t *frames = heap_caps_calloc(BOOK_SCAN_DEPTH_LIMIT + 1, sizeof(*frames),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frames) { *truncated = true; return; }
    frames[0].dir = opendir(root);
    if (!frames[0].dir) { *unreadable = true; free(frames); return; }
    ui_snprintf(frames[0].path, sizeof(frames[0].path), "%s", root);
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
        int len = ui_snprintf(path, sizeof(path), "%s/%s", frames[depth].path, ent->d_name);
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
        s_shelf_contents += book_shelf_index_file_stamp(path, st.st_size, st.st_mtime);
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
        if (!book_title_display(item->path, item->name, sizeof(item->name)))
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
        item->search_match = content_name_matches(item->name, item->path, s_query);
        item->size = st.st_size;
        item->modified = st.st_mtime;
        item->is_flash = is_flash;
        book_progress_t p;
        item->has_progress = shelf_load_progress(recent, item->path, item->size, &p);
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
    records_handle_t hidden;
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
    if (!book_title_display(item->path, item->name, sizeof(item->name)))
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
    item->modified = st.st_mtime;
    // 内置书根固定在 /flash 下，与 book_store.c 和 open_book 的前缀判断一致。
    // The internal root lives under /flash, matching the prefix test in book_store.c and open_book.
    item->is_flash = !strncmp(path, "/flash/", 7);
    item->has_progress = true;
    item->pct = progress->pct;
    item->chapter = progress->chapter;
    item->recent = progress->last_open_s;
    item->search_match = content_name_matches(item->name, item->path, s_query);
    item->added=content_first_added(item->path);
        item->pinned=content_book_pinned(item->path);
        s_shelf[s_count++] = candidate;
    return true;
}

// 初次扫描补齐书根之外的已读书；保留对账时观察的写入代号。
// Initial scan includes read books outside the roots and records the progress revision.
static void shelf_backfill_read_books(bool* truncated) {
    SHELF_PERF_SCOPE(backfill_perf,SP_SHELF_BACKFILL,0,0);
    unsigned before = progress_kv_revision();
    records_handle_t hidden = 0;
    esp_err_t hidden_err = records_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    shelf_backfill_t backfill = {.hidden = hidden};
    const book_recent_view_t *recent = book_recent_index_get(false);
    esp_err_t listed = ESP_OK;
    if (recent && recent->all_books) {
        backfill_perf.a = 1;
        backfill_perf.b = (int)recent->book_count;
        for (uint32_t i = 0; i < recent->book_count; ++i) {
            const book_recent_entry_t *entry = &recent->books[i];
            if (!shelf_backfill_visit(entry->path, &entry->progress, &backfill)) break;
        }
    } else {
        listed = book_progress_snapshot_list(shelf_backfill_visit, &backfill);
    }
    if (hidden) records_close(hidden);
    if (truncated && backfill.truncated) *truncated = true;
    s_shelf_progress_valid = listed == ESP_OK && !backfill.truncated &&
        (hidden_err == ESP_OK || hidden_err == ESP_ERR_NVS_NOT_FOUND) && before == progress_kv_revision();
    s_shelf_progress_revision = before;
    s_shelf_progress_path[0] = 0;
}

static void scan_shelf(app_ctx_t* ctx) {
    book_shelf_index_audit_stop();
    s_shelf_audit_pending = s_shelf_force_scan = false;
    s_shelf_paged = s_shelf_disk_valid = s_shelf_index_dirty = false;
    s_shelf_contents = 0;
    const unsigned titles_before = book_title_revision(), store_before = book_store_revision();
    SHELF_PERF_SCOPE(perf,SP_SCAN,0,0);
    unsigned progress_before = progress_kv_revision();
    s_count = 0;
    s_visible_count = 0;
    s_message[0] = 0;
    s_shelf_warning[0] = 0;
    // 内置分区每次启动都必须重新挂载；已有文件不会因此格式化。
    // Flash mounts on each boot; existing files are not formatted by that mount.
    book_store_root_t roots[BOOK_STORE_ROOT_MAX];
    int n = 0;
    esp_err_t root_err;
    {SHELF_PERF_SCOPE(roots_perf,SP_ROOTS,0,0);root_err=book_store_roots(roots,&n);roots_perf.a=n;roots_perf.b=root_err;}
    bool truncated = false, unreadable = root_err != ESP_OK || book_store_roots_degraded(), skipped = false;
    records_handle_t hidden = 0;
    (void)records_open(SHELF_HIDDEN_NS, NVS_READONLY, &hidden);
    const book_recent_view_t *recent = book_recent_index_get(false);
    for (int i = 0; i < n; ++i) {
        {SHELF_PERF_SCOPE(enum_perf,SP_ENUMERATE,roots[i].is_flash,0);
        scan_shelf_dir(roots[i].path, roots[i].is_flash, hidden,
                       &truncated, &unreadable, &skipped, recent);enum_perf.b=s_count;}
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
        s_shelf[i].search_match = content_name_matches(p->entry.name, p->entry.path, s_query);
    }
    if (hidden) records_close(hidden);
    // 补齐书根目录之外读过的书；显式移出书架的除外。
    // Backfill books read outside the roots, minus the ones removed from the shelf.
    shelf_backfill_read_books(&truncated);
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
    if (truncated) ui_snprintf(s_shelf_warning, sizeof(s_shelf_warning), UI_MSG("内存不足，仅列 %d 本；释放后重扫"), s_count);
    else if (unreadable) copy_text(s_shelf_warning, sizeof(s_shelf_warning), UI_MSG("部分目录或文件不可读，请检查后重扫"));
    else if (skipped) copy_text(s_shelf_warning, sizeof(s_shelf_warning), UI_MSG("部分文件过大或名称过长，未列入"));
    else if (s_pending_invalidated) copy_text(s_shelf_warning, sizeof(s_shelf_warning), UI_MSG("图书已更新，旧待保存进度已作废"));
    if (!s_count && unreadable) copy_text(s_message, sizeof(s_message), UI_MSG("存储不可用，请检查 TF 卡"));
    else if (!s_count && !truncated) copy_text(s_message, sizeof(s_message), UI_MSG("暂无图书，请传入 TXT 或 EPUB"));
    read_pico_sd_info_t cache_sd = {0};
    read_pico_sd_get_info(&cache_sd);
    s_cache_sd_present = cache_sd.present;
    s_cache_sd_mounted = cache_sd.mounted;
    s_store_revision = book_store_revision();
    s_shelf_titles = book_title_revision();
    s_shelf_cache_valid = !unreadable && !truncated && titles_before == s_shelf_titles &&
                          store_before == s_store_revision;
    if (progress_before != progress_kv_revision()) s_shelf_progress_valid = false;
    s_shelf_index_dirty = s_shelf_cache_valid && s_shelf_progress_valid && !skipped;
    if (s_shelf_index_dirty) {
        s_shelf_audited.contents = s_shelf_contents;
        s_shelf_audited.media = record_store_media_epoch();
        s_shelf_audited.books = s_store_revision;
        s_shelf_audited.valid = true;
    }
    perf.a=s_visible_count;perf.b=(unreadable?1:0)|(truncated?2:0)|(skipped?4:0);
    ESP_LOGI(TAG, "shelf books=%d roots=%d", s_count, n);
}

static bool apply_cached_progress(app_ctx_t* ctx) {
    unsigned revision = progress_kv_revision();
    if (!s_shelf_progress_valid || revision != s_shelf_progress_revision) {
        return false;
    }
    if (!s_shelf_progress_path[0]) return true;
    const char *path = s_shelf_progress_path;
    const char *ext = strrchr(path,'.');
    if (ext && (!strcasecmp(ext,".epub") || !strcasecmp(ext,".txt"))) {
        shelf_entry_t *item = NULL;
        for (int i = 0; i < s_count; ++i) {
            if (!strcmp(s_shelf[i].path,path) && !s_shelf[i].removed) { item = &s_shelf[i]; break; }
        }
        struct stat st;
        book_progress_t progress;
        if (stat(path,&st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX ||
            !book_progress_load(path,(uint32_t)st.st_size,&progress)) {
            return false;
        }
        if (item && (item->size != (uint32_t)st.st_size || item->modified != st.st_mtime)) {
            return false;
        }
        if (item) {
            item->has_progress = true;
            item->pct = progress.pct;
            item->chapter = progress.chapter;
            item->recent = progress.last_open_s;
        } else {
            records_handle_t hidden = 0;
            esp_err_t err = records_open(SHELF_HIDDEN_NS,NVS_READONLY,&hidden);
            shelf_backfill_t backfill = {.hidden=hidden};
            if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND)
                (void)shelf_backfill_visit(path,&progress,&backfill);
            if (hidden) records_close(hidden);
            if (backfill.truncated || (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)) {
                return false;
            }
        }
        sort_shelf(ctx);
        s_shelf_index_dirty = true;
    }
    s_shelf_progress_path[0] = 0;
    return true;
}

static void refresh_cached_progress(app_ctx_t* ctx) {
    if (!apply_cached_progress(ctx)) scan_shelf(ctx);
}
static void return_to_cached_shelf(app_ctx_t* ctx) {
    if (s_shelf_paged) {
        shelf_index_checkpoint(ctx, true);
        if (s_shelf_paged && s_shelf_disk_valid && !s_shelf_progress_path[0]) return;
    }
    if (s_shelf_cache_valid && s_store_revision == book_store_revision()) refresh_cached_progress(ctx);
    else scan_shelf(ctx);
    shelf_index_publish();
}

static uint32_t shelf_index_order(void) {
    return s_book_options.mode | (s_book_options.ascending ? 4u : 0u);
}

static bool shelf_load_page(int leaf) {
    if (!s_shelf_paged) return true;
    int rows = shelf_rows();
    uint32_t first = (uint32_t)leaf * rows;
    if (first > s_shelf_disk.count || !s_shelf_disk_valid ||
        s_shelf_disk_media != record_store_media_epoch() ||
        s_store_revision != book_store_revision() || s_shelf_disk_titles != book_title_revision() ||
        s_shelf_progress_revision != progress_kv_revision() || !s_shelf_progress_valid)
        return false;
    uint32_t count = s_shelf_disk.count - first;
    if (count > (uint32_t)rows) count = rows;
    if (!book_shelf_index_page(&s_shelf_disk, first, count, s_shelf_page)) {
        s_shelf_page_count = 0;
        return false;
    }
    s_shelf_page_first = first;
    s_shelf_page_count = count;
    return true;
}

static bool shelf_try_index(void) {
    SHELF_PERF_SCOPE(perf, SP_SHELF_INDEX, 0, 0);
    if (s_query[0] || s_delete_retries || s_shelf_force_scan) return false;
    struct stat index_stat;
    if (stat(BOOK_SHELF_INDEX_FILE, &index_stat) || !S_ISREG(index_stat.st_mode)) return false;
    record_store_lock();
    const unsigned progress = progress_kv_revision(), titles = book_title_revision(), books = book_store_revision();
    const uint32_t media = record_store_media_epoch();
    uint64_t roots;
    uint32_t sequence;
    bool ok = book_shelf_index_sequence(&sequence) && book_shelf_index_fingerprint(&roots) &&
              book_shelf_index_open(&s_shelf_disk, roots, sequence, shelf_index_order());
    if (ok && !s_shelf_page)
        s_shelf_page = heap_caps_malloc(BOOK_ROWS * sizeof(*s_shelf_page), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok && s_shelf_page) {
        s_shelf_disk_valid = s_shelf_paged = true;
        s_shelf_progress_valid = true;
        s_shelf_progress_revision = progress;
        s_shelf_progress_path[0] = 0;
        s_shelf_disk_titles = s_shelf_titles = titles;
        s_shelf_disk_media = media;
        s_store_revision = books;
        s_shelf_index_dirty = false;
        ok = shelf_load_page(0);
        if (ok) {
            s_visible_count = (int)s_shelf_disk.count;
            s_shelf_contents = s_shelf_disk.contents;
            s_shelf_audit_pending = !s_shelf_audited.valid || s_shelf_audited.media != media ||
                                    s_shelf_audited.books != books || s_shelf_audited.contents != s_shelf_contents;
            s_shelf_audit_next_ms = 0;
            s_scan_pending = false;
            read_pico_sd_info_t sd = {0};
            read_pico_sd_get_info(&sd);
            s_cache_sd_present = sd.present;
            s_cache_sd_mounted = sd.mounted;
            s_message[0] = s_shelf_warning[0] = 0;
            if (!s_visible_count) copy_text(s_message, sizeof(s_message), UI_MSG("暂无图书，请传入 TXT 或 EPUB"));
            s_latest_path[0] = 0;
            perf.b = s_visible_count;
        }
    } else ok = false;
    if (!ok) s_shelf_disk_valid = s_shelf_paged = false;
    perf.a = ok ? 1 : 0;
    record_store_unlock();
    return ok;
}

static bool shelf_materialize(void) {
    if (!s_shelf_paged) return s_shelf_cache_valid;
    SHELF_PERF_SCOPE(perf, SP_SHELF_INDEX, 3, 0);
    if (!s_shelf_disk_valid || s_shelf_disk_media != record_store_media_epoch() ||
        s_store_revision != book_store_revision() || s_shelf_disk_titles != book_title_revision())
        return false;
    size_t count = s_shelf_disk.count;
    if (count > s_shelf_capacity) {
        shelf_entry_t *entries = heap_caps_realloc(s_shelf, count * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!entries) return false;
        s_shelf = entries;
        s_shelf_capacity = count;
    }
    if (!book_shelf_index_load(&s_shelf_disk, s_shelf)) return false;
    s_count = (int)count;
    s_shelf_paged = false;
    read_pico_sd_info_t sd = {0};
    read_pico_sd_get_info(&sd);
    s_cache_sd_present = sd.present;
    s_cache_sd_mounted = sd.mounted;
    s_shelf_cache_valid = true;
    perf.b = s_count;
    return true;
}

static void shelf_require_complete(app_ctx_t *ctx) {
    book_shelf_index_audit_stop();
    if (!s_shelf_paged && !s_scan_pending && !s_shelf_audit_pending && !s_shelf_force_scan) return;
    int leaf = ctx->leaf;
    if (s_scan_pending || s_shelf_audit_pending || s_shelf_force_scan ||
        !shelf_materialize() || !apply_cached_progress(ctx)) {
        scan_shelf(ctx);
        s_scan_pending = false;
    }
    int count = leaves();
    ctx->leaf = leaf < count ? leaf : count - 1;
}

static void shelf_index_publish(void) {
    if (!s_shelf_index_dirty || s_shelf_paged || s_shelf_audit_pending || s_query[0] || s_delete_retries || s_save_failed ||
        !s_shelf_cache_valid || !s_shelf_progress_valid) return;
    SHELF_PERF_SCOPE(perf, SP_SHELF_INDEX, 2, 0);
    record_store_lock();
    s_shelf_index_dirty = false;
    uint64_t roots;
    uint32_t sequence;
    if (s_store_revision == book_store_revision() && s_shelf_titles == book_title_revision() &&
        s_shelf_progress_revision == progress_kv_revision() &&
        book_shelf_index_sequence(&sequence) && book_shelf_index_fingerprint(&roots) &&
        book_shelf_index_save(roots, s_shelf_contents, sequence, shelf_index_order(), s_count, s_shelf)) {
        s_shelf_disk_valid = book_shelf_index_open(&s_shelf_disk, roots, sequence, shelf_index_order());
        s_shelf_disk_media = record_store_media_epoch();
        s_shelf_disk_titles = s_shelf_titles;
        perf.b = s_count;
    } else s_shelf_disk_valid = false;
    record_store_unlock();
}

// 常见续读只变动当前一页；页两端仍符合比较器时，只提交这一页。
// Common reading updates stay on one page. Check its neighbours before publishing just that page.
static bool shelf_index_patch_progress(app_ctx_t *ctx) {
    if (!s_shelf_disk_valid || s_shelf_index_dirty || s_query[0] ||
        s_shelf_disk.order != shelf_index_order() || s_shelf_disk_media != record_store_media_epoch() ||
        !s_shelf_disk.count) return false;
    SHELF_PERF_SCOPE(perf, SP_SHELF_INDEX, 6, 0);
    const uint32_t width = shelf_rows(), first = (uint32_t)s_reader_shelf_leaf * width;
    if (first >= s_shelf_disk.count) return false;
    uint32_t count = s_shelf_disk.count - first;
    if (count > width) count = width;
    shelf_entry_t *page = heap_caps_malloc(count * sizeof(*page), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!page) return false;
    record_store_lock();
    bool ok = s_shelf_progress_revision == progress_kv_revision() &&
              book_shelf_index_page(&s_shelf_disk, first, count, page);
    uint32_t at = 0;
    if (ok) {
        while (at < count && strcmp(page[at].path, s_shelf_progress_path)) ++at;
        ok = at < count;
    }
    book_progress_t progress;
    uint32_t sequence;
    if (ok) ok = book_progress_load(page[at].path, page[at].size, &progress) &&
                 book_shelf_index_sequence(&sequence);
    if (ok) {
        page[at].has_progress = true;
        page[at].pct = progress.pct;
        page[at].chapter = progress.chapter;
        page[at].recent = progress.last_open_s;
        qsort(page, count, sizeof(*page), compare_books);
        shelf_entry_t adjacent;
        if (first) ok = book_shelf_index_page(&s_shelf_disk, first - 1, 1, &adjacent) &&
                        compare_books(&adjacent, &page[0]) <= 0;
        if (ok && first + count < s_shelf_disk.count)
            ok = book_shelf_index_page(&s_shelf_disk, first + count, 1, &adjacent) &&
                 compare_books(&page[count - 1], &adjacent) <= 0;
        if (ok && !book_shelf_index_update_page(&s_shelf_disk, first, count, page, sequence)) {
            book_shelf_disk_invalidate();
            s_shelf_disk_valid = false;
            ok = false;
        }
    }
    if (ok) {
        if (s_shelf_paged) {
            if (s_shelf_page_first == first) memcpy(s_shelf_page, page, count * sizeof(*page));
            s_shelf_progress_path[0] = 0;
        } else if (apply_cached_progress(ctx)) s_shelf_index_dirty = false;
        else s_shelf_cache_valid = false;
        perf.b = count;
    }
    record_store_unlock();
    free(page);
    return ok;
}

static void shelf_index_checkpoint(app_ctx_t *ctx, bool allow_large) {
    if (s_save_failed || !s_shelf_progress_valid || !s_shelf_progress_path[0] ||
        s_shelf_progress_revision != progress_kv_revision() ||
        s_store_revision != book_store_revision() || s_shelf_titles != book_title_revision()) return;
    if (!book_shelf_disk_book_path(s_shelf_progress_path) && s_shelf_disk_valid && !s_shelf_index_dirty) {
        record_store_lock();
        uint32_t sequence;
        s_shelf_disk_valid = book_shelf_index_sequence(&sequence) &&
                             book_shelf_index_advance(&s_shelf_disk, sequence);
        record_store_unlock();
        if (s_shelf_disk_valid) s_shelf_progress_path[0] = 0;
        if (s_shelf_disk_valid || s_shelf_paged) return;
        s_shelf_index_dirty = true;
    }
    int leaf = ctx->leaf;
    if (shelf_index_patch_progress(ctx)) {
        ctx->leaf = leaf;
        return;
    }
    // 锁屏只允许有限规模的顺序重写。/ Bound sequential rewrites on the power path.
    if (!allow_large && (s_shelf_paged ? s_shelf_disk.count : (uint32_t)s_count) > 64) return;
    if (!shelf_materialize()) {
        if (s_shelf_paged) s_shelf_disk_valid = false;
        return;
    }
    if (apply_cached_progress(ctx)) shelf_index_publish();
    else s_shelf_cache_valid = false;
    ctx->leaf = leaf;
}

static void manage_apply(app_ctx_t* ctx) {
    if (!strcmp(s_path, s_managed.path)) free_book();
    esp_err_t err;
    if (s_delete_confirm && !s_file_removed) {
        delete_retry_t* retry = delete_retry_reserve(&s_managed);
        if (!retry) {
            copy_text(s_manage_message, sizeof(s_manage_message), UI_MSG("内存不足，无法保留删除重试记录"));
            s_clear_confirm = false;
            return;
        }
        bool removed = false;
        err = book_store_delete(s_managed.path, &removed);
        if (removed) {
            // 物理目录有变动，待所有清理完成后重新对账。/ Reconcile the changed directory after cleanup completes.
            s_shelf_cache_valid = false;
            retry->entry.removed = true;
            s_file_removed = true;
            pending_discard(s_managed.path);
            book_store_notify_changed();
            s_store_revision = book_store_revision();
            for (int i = 0; i < s_count; ++i) if (!strcmp(s_shelf[i].path, s_managed.path)) s_shelf[i].removed = true;
        } else delete_retry_discard(s_managed.path);
    } else {
        pending_discard(s_managed.path);
        err = book_progress_forget(s_managed.path);
    }
    if (err == ESP_OK && s_file_removed) err = book_title_clear(s_managed.path);
    s_clear_confirm = false;
    if (err != ESP_OK) {
        copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ?
                  UI_MSG("文件已删除，进度清理失败，请重试") : s_delete_confirm ?
                  UI_MSG("文件删除失败，请检查存储后重试") : UI_MSG("进度清理失败，请重新尝试"));
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
    s_shelf_index_dirty = true;
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
}

/* ---- 输入与生命周期 / Input and lifecycle ---- */
static app_redraw_t turn_page(app_ctx_t* ctx, int dir) {
    operation_log_event(OPLOG_TURN,dir,(int)s_page,(int)s_chapter,ctx->pressed?1:0,(int)percent(s_page));
    if (!s_text || s_clear_confirm) return APP_REDRAW_NONE;
    if(book_kind()==BOOK_KIND_MARKDOWN&&book_layout_page_count()==1&&dir>0&&!s_document_completed){s_document_completed=true;save_progress();return APP_REDRAW_PAGE;}
    bool text_transition=app_settings_reader_hide_images()||reader_image_slots(s_page)==0;
    bool changed = true;
    if (dir > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
    else if (dir < 0 && s_page) --s_page;
    else if (s_chapter >= book_chapter_count()) return APP_REDRAW_NONE;
    else if (dir > 0 && s_chapter + 1 < book_chapter_count()) { save_progress(); changed = load_chapter(ctx, s_chapter + 1, 0, false); }
    else if (dir < 0 && s_chapter) { save_progress(); changed = load_chapter(ctx, s_chapter - 1, 0, true); }
    else return APP_REDRAW_NONE;
    if (!changed) {
        set_reader_view(s_text ? TOC : SHELF);
        size_t at = s_text ? current_toc_position() : SIZE_MAX;
        ctx->leaf = at == SIZE_MAX ? 0 : (int)(at / (reader_landscape() ? WIDE_TOC_ROWS : BOOK_TOC_ROWS));
        return APP_REDRAW_PAGE;
    }
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
    // 离开插图也清理正文灰阶；纯文字间的水波纹路径不变。
    // Clean body grayscale when leaving an image; preserve text-to-text water turns.
    if (!text_transition && (reader_landscape() || book_kind()==BOOK_KIND_MARKDOWN))
        s_reader_image_refresh_pending = true;
    bool water_redraw = redraw == APP_REDRAW_AREA || (reader_landscape() && redraw == APP_REDRAW_PAGE);
    s_water_turn_pending = text_transition && water_redraw && !s_reader_cleanup &&
                           (app_settings_reader_hide_images() || !reader_image_slots(s_page)) &&
                           app_settings_reader_turn_effect() == 1;
    // 横屏沿物理扫描行推进，每行只需一次相位查表。/ Sweep along physical scan lines in landscape, using one phase LUT per line.
    s_water_turn_dir = reader_landscape()
        ? (dir > 0 ? E0470_TURN_TTB : E0470_TURN_BTT)
        : (dir > 0 ? E0470_TURN_RTL : E0470_TURN_LTR);
    return redraw;
}


static app_redraw_t apply_reader_layout(app_ctx_t* ctx, int next_px, int next_margin,
                                        uint8_t next_line, uint8_t next_para) {
    if (!book_text_size_valid(next_px) ||
        next_margin < BOOK_MARGIN_MIN || next_margin > BOOK_MARGIN_MAX ||
        next_line < 110 || next_line > 150 ||
        next_para > 75) return APP_REDRAW_NONE;
    if (next_px == s_px && next_margin == s_margin && next_line == reader_layout().line &&
        next_para == reader_layout().paragraph) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    int old_px = s_px, old_margin = s_margin;
    uint8_t old_line = reader_layout().line;
    uint8_t old_para = reader_layout().paragraph;
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
    content_document_layout_t next=reader_layout();
    next.margin=next_margin;next.line=next_line;next.paragraph=next_para;
    esp_err_t stored=save_reader_layout(next,READER_SPACING);
    if(stored==ESP_OK&&book_kind()!=BOOK_KIND_MARKDOWN)stored=app_settings_set_book_px((uint8_t)s_px);
    if(stored!=ESP_OK){
        lock_draw();s_px=book_kind()==BOOK_KIND_MARKDOWN?old_px:app_settings_book_px();s_margin=reader_layout().margin;s_line_spacing=reader_layout().line;
        book_layout_set_spacing(s_line_spacing,reader_layout().paragraph);
        bool restored=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
        if(restored)s_page=book_layout_page_for_offset(off);
        unlock_draw();if(!restored){free_book();s_view=SHELF;}
        copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("保存失败，请重试"));s_reader_notice_until=esp_timer_get_time()/1000+2500;
    }
    prepare_inline_image();
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_typography(app_ctx_t* ctx, int tracking_index) {
    (void)ctx;
    if (!s_text || tracking_index < 0 || tracking_index > 4)
        return APP_REDRAW_NONE;
    int old_index = reader_layout().tracking;
    if (tracking_index == old_index) return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_typography((tracking_index - 2) * 2);
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count,
                                       body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        book_layout_set_typography((old_index - 2) * 2);
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    unlock_draw();
    if (!ok) return APP_REDRAW_NONE;
    content_document_layout_t next=reader_layout();next.tracking=tracking_index;
    if(save_reader_layout(next,READER_TRACKING)!=ESP_OK){
        lock_draw();book_layout_set_typography((old_index-2)*2);
        bool restored=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
        if(restored)s_page=book_layout_page_for_offset(off);
        unlock_draw();if(!restored){free_book();s_view=SHELF;}
        copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("保存失败，请重试"));s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;
    }
    prepare_inline_image();
    save_progress();
    return APP_REDRAW_PAGE;
}

static app_redraw_t apply_reader_indent(app_ctx_t* ctx, int em) {
    (void)ctx;
    if (!s_text || em < 0 || em > 3 || em == reader_layout().indent)
        return APP_REDRAW_NONE;
    size_t off = book_layout_page_start_offset(s_page);
    uint8_t old_em = reader_layout().indent;
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
    content_document_layout_t next=reader_layout();next.indent=em;
    if(save_reader_layout(next,READER_INDENT)!=ESP_OK){
        lock_draw();book_layout_set_first_line_indent(old_em);
        bool restored=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
        if(restored)s_page=book_layout_page_for_offset(off);
        unlock_draw();if(!restored){free_book();s_view=SHELF;}
        copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("保存失败，请重试"));s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;
    }
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
    esp_err_t saved=kind==1?save_reader_immersive(!previous):app_settings_set_reader_hide_images(!previous);
    if(saved!=ESP_OK){unlock_draw();copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("保存失败，请重试"));s_reader_notice_until=esp_timer_get_time()/1000+2500;return APP_REDRAW_PAGE;}
    if (kind == 1) {s_reader_immersive=!previous;}
    else {
        book_layout_set_images_visible(previous);
    }
    bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    if (ok) s_page = book_layout_page_for_offset(off);
    else {
        if (kind == 1) {(void)save_reader_immersive(previous);s_reader_immersive=reader_layout().immersive;}
        else {
            (void)app_settings_set_reader_hide_images(previous);
            book_layout_set_images_visible(!app_settings_reader_hide_images());
        }
        (void)book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        copy_text(s_reader_notice, sizeof(s_reader_notice), UI_MSG("排版失败，请重试"));
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

static void refresh_finished_document(void) {
    if(s_text&&s_document_mode&&update_cached_document_progress(s_path,s_file_size))
        sort_documents();
}
static app_redraw_t reader_return(app_ctx_t *ctx, bool home) {
    track_ticket_stats(ctx);
    save_progress();
    if (home || s_reader_return_home) {
        ui_nav_request(ctx, 0);
        return APP_REDRAW_NONE;
    }
    book_recent_index_flush();
    shelf_index_checkpoint(ctx, true);
    refresh_finished_document();
    free_book();
    s_view = s_document_mode ? DOCUMENTS : SHELF;
    s_reader_panel = READER_PANEL_NONE;
    if (s_document_mode) { ctx->leaf=0; ensure_documents(); }
    else {
        return_to_cached_shelf(ctx);
        const int page_count = leaves();
        ctx->leaf = s_reader_shelf_leaf < page_count ? s_reader_shelf_leaf : page_count - 1;
        if (!shelf_load_page(ctx->leaf)) s_scan_pending = true;
    }
    return APP_REDRAW_PAGE;
}

// 三态布局共用正文锚点，控制面板保持当前状态。/ Three layout states preserve the text anchor and the open panel.
static app_redraw_t toggle_reader_fullscreen(app_ctx_t* ctx) {
    if (!s_text) return APP_REDRAW_NONE;
    size_t off=reader_page_offset(s_page);
    bool old_full=s_reader_fullscreen,old_immersive=s_reader_immersive;
    int mode=old_full?(old_immersive?2:1):0,next=(mode+1)%3;
    lock_draw();invalidate_prep();s_reader_fullscreen=next!=0;s_reader_immersive=next==2;
    bool ok=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
    if(ok)s_page=book_layout_page_for_offset(off);
    bool restored = true;
    if(!ok){
        s_reader_fullscreen=old_full;s_reader_immersive=old_immersive;
        restored=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
        if(restored)s_page=book_layout_page_for_offset(off);
    }
    unlock_draw();
    if(!ok){
        if(!restored){free_book();s_view=SHELF;}
        else {s_jump_offset=off;s_jump_page=s_page;prepare_inline_image();}
        copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("排版失败，请重试"));
        s_reader_notice_until=esp_timer_get_time()/1000+2500;
        return APP_REDRAW_PAGE;
    }
    s_jump_offset=off;s_jump_page=s_page;
    content_document_layout_t layout=reader_layout();
    layout.fullscreen=s_reader_fullscreen;layout.immersive=s_reader_immersive;
    esp_err_t stored=save_reader_layout(layout,READER_FULLSCREEN);
    if(stored!=ESP_OK){
        lock_draw();s_reader_fullscreen=reader_layout().fullscreen;s_reader_immersive=reader_layout().immersive;
        bool restored=book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px);
        if(restored)s_page=book_layout_page_for_offset(off);
        unlock_draw();if(!restored){free_book();s_view=SHELF;}
        copy_text(s_reader_notice,sizeof(s_reader_notice),UI_MSG("保存失败，请重试"));s_reader_notice_until=esp_timer_get_time()/1000+2500;
    }
    if(s_text){
        s_jump_offset=off;s_jump_page=s_page;
        prepare_inline_image();
        save_progress();
    }
    return APP_REDRAW_PAGE;
}

static bool reflow_reading_font(size_t offset) {
    if (!book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body_rect(),s_px)) return false;
    s_page = book_layout_page_for_offset(offset);
    s_jump_offset = offset;
    s_jump_page = s_page;
    return true;
}

static app_redraw_t toggle_reader_hyphenation(app_ctx_t *ctx) {
    if (!s_text) return APP_REDRAW_NONE;
    bool enabled = !s_reader_hyphenate;
    size_t offset = reader_page_offset(s_page);
    lock_draw();
    invalidate_prep();
    book_layout_set_hyphenation(enabled);
    bool laid_out = reflow_reading_font(offset);
    if (laid_out && content_set_english_hyphenation(enabled) == ESP_OK) {
        s_reader_hyphenate = enabled;
        unlock_draw();
        prepare_inline_image();
        save_progress();
        return APP_REDRAW_PAGE;
    }
    book_layout_set_hyphenation(s_reader_hyphenate);
    bool restored = reflow_reading_font(offset);
    unlock_draw();
    const char *notice = laid_out ? UI_MSG("保存失败，请重试") : UI_MSG("排版失败，请重试");
    ui_nav_notice_show(notice, ctx->now_ms);
    if (!restored) return reader_return(ctx, false);
    prepare_inline_image();
    return APP_REDRAW_PAGE;
}

static app_redraw_t select_reading_font_item(app_ctx_t* ctx, const ttf_font_item_t* item) {
    if (!item || !s_text) return APP_REDRAW_NONE;
    const char *saved_path = app_font_reading_choice();
    bool same_face = !strcmp(s_font_path,item->path) && ttf_font_ready() && !strcmp(ttf_font_path(),item->path);
    if (same_face && !strcmp(saved_path,item->path)) {
        s_reader_panel = READER_PANEL_FONT_SETTINGS;
        return APP_REDRAW_PAGE;
    }
    ttf_name_preview_stop();
    char previous[TTF_FONT_PATH_MAX];
    copy_text(previous,sizeof(previous),s_font_path);
    size_t off = reader_page_offset(s_page);
    if (s_unsaved) save_progress();
    lock_draw();
    invalidate_prep();
    free(s_next_fb);
    s_next_fb = NULL;
    bool loaded = ttf_font_select_context(false,item->path) == ESP_OK && !strcmp(ttf_font_path(),item->path);
    bool reflowed = loaded && (same_face || reflow_reading_font(off));
    unlock_draw();
    esp_err_t saved = ESP_FAIL;
    if (reflowed) saved = app_font_save_choice(APP_FONT_READING, s_reader_english, item->path);
    if (saved != ESP_OK) {
        lock_draw();
        bool intact = (!loaded || same_face) && ttf_font_ready() && !strcmp(ttf_font_path(),previous);
        bool restored = intact || (ttf_font_select_context(false,previous)==ESP_OK &&
                                   reflow_reading_font(off));
        if (!restored) book_layout_free();
        unlock_draw();
        const char *notice = !loaded ? UI_MSG("字体无法加载") :
            !reflowed ? UI_MSG("排版失败，请重试") : UI_MSG("字体保存失败，请重试");
        if (!restored) {
            ui_nav_notice_show(notice,ctx->now_ms);
            return reader_return(ctx,false);
        }
        if (!intact) prepare_inline_image();
        copy_text(s_reader_notice,sizeof(s_reader_notice),notice);
        s_reader_notice_until = esp_timer_get_time()/1000+2500;
        return APP_REDRAW_PAGE;
    }
    copy_text(s_font_path, sizeof(s_font_path), item->path);
    if (s_reader_english) app_font_set_english_mode(true, s_reader_localized_cover);
    s_reader_panel = READER_PANEL_FONT_SETTINGS;
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
// 为96px标题预留100px，再排滑块。/ Reserve 100px for a 96px heading before the slider.
static EpdRect reader_markdown_row(int index){
    return index?(EpdRect){36,252+(index-1)*144,612,144}:(EpdRect){36,684,612,100};
}
static EpdRect reader_markdown_slider_rect(int index){
    EpdRect r=reader_markdown_row(index);
    return (EpdRect){r.x,r.y+(index?98:50),r.width,44};
}
static EpdRect reader_slider_rect(int slider) {
    if(reader_landscape()) return wide_slider_rect(slider);
    if(slider>=5&&reader_markdown_font_panel())return reader_markdown_slider_rect(slider-5);
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
    if (reader_landscape() && s_reader_panel == READER_PANEL_LAYOUT_SETTINGS && !s_control_page) return -1;
    int first = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 0 : 1;
    int end = s_reader_panel == READER_PANEL_FONT_SETTINGS ? 1 : 5;
    for (int i = first; i < end; ++i) if (ui_rect_hit(reader_slider_rect(i), x, y)) return i;
    return -1;
}

static void reader_slider_map(int slider, int x) {
    if (slider >= 5 && slider <= 8) {
        int i=slider-5, min=i?24:BOOK_PX_MIN,max=i?96:BOOK_PX_MAX;
        s_md_draft[i]=min+slider_index(reader_slider_rect(slider),x,max-min+1);
    } else if (slider == 0) s_reader_preview_px = BOOK_PX_MIN + slider_index(reader_slider_rect(0), x, BOOK_PX_MAX-BOOK_PX_MIN+1);
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
    s_reader_preview_line = reader_layout().line;
    s_reader_preview_para = reader_layout().paragraph;
    s_reader_preview_tracking = reader_layout().tracking;
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
    uint8_t confirmed[4];if(content_markdown_sizes_checked(confirmed)!=ESP_OK){copy_text(s_md_notice,sizeof(s_md_notice),UI_MSG("设置读取失败，请重试"));return APP_REDRAW_PAGE;}
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
        copy_text(restored?s_md_notice:s_message,restored?sizeof(s_md_notice):sizeof(s_message),UI_MSG("字号保存或排版失败，请重试"));
        return APP_REDRAW_PAGE;
    }
    s_md_notice[0]=0; s_md_committed=true;
    s_page=book_layout_page_for_offset(off);
    unlock_draw();
    prepare_inline_image();
    save_progress();
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

static app_redraw_t apply_book_presentation(bool vertical, app_book_screen_t screen) {
    if (!s_text || book_kind() == BOOK_KIND_MARKDOWN || (vertical && s_reader_english)) return APP_REDRAW_NONE;
    bool previous_vertical = reader_book_vertical();
    app_book_screen_t previous_screen = s_reader_screen;
    size_t anchor = reader_page_offset(s_page);
    lock_draw();
    invalidate_prep();
    s_reader_screen = screen;
    book_layout_set_vertical(vertical);
    book_layout_set_two_columns(screen == APP_BOOK_LANDSCAPE_TWO_COLUMNS);
    bool built = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    esp_err_t saved = ESP_FAIL;
    if (built)
        saved = vertical != previous_vertical ? app_settings_set_book_vertical(vertical)
                                              : app_settings_set_book_screen(screen);
    bool restored = true;
    if (saved != ESP_OK) {
        s_reader_screen = previous_screen;
        book_layout_set_vertical(previous_vertical);
        book_layout_set_two_columns(previous_screen == APP_BOOK_LANDSCAPE_TWO_COLUMNS);
        restored = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
    }
    if (restored) {
        s_page = book_layout_page_for_offset(anchor);
        s_jump_offset = anchor;
        s_jump_page = s_page;
    }
    unlock_draw();
    if (!restored) {
        free_book();
        s_view = SHELF;
        return APP_REDRAW_PAGE;
    }
    prepare_inline_image();
    if (saved == ESP_OK) {
        if ((previous_screen == APP_BOOK_PORTRAIT) != (screen == APP_BOOK_PORTRAIT)) s_control_page = 0;
        save_progress();
    } else {
        copy_text(s_reader_notice, sizeof(s_reader_notice),
                  built ? UI_MSG("保存失败，请重试") : UI_MSG("排版失败，请重试"));
        s_reader_notice_until = esp_timer_get_time()/1000 + 2500;
    }
    return APP_REDRAW_PAGE;
}

static bool reader_close_note(app_ctx_t *ctx) {
    return s_note_open && load_chapter_at(ctx, s_note_origin.chapter, s_note_origin.byte_off, false,
                                          NULL, SIZE_MAX, CHAPTER_LEAVE_NOTE, NULL);
}

static app_redraw_t reader_note_notice(app_ctx_t *ctx, const char *message) {
    if (s_view == READING) {
        lock_draw();
        copy_text(s_reader_notice, sizeof(s_reader_notice), message);
        s_reader_notice_until = ctx->now_ms + 2200;
        invalidate_prep();
        unlock_draw();
    } else {
        ui_nav_notice_show(message, ctx->now_ms);
    }
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_return_from_note(app_ctx_t *ctx) {
    const char *failure = s_note_origin_is_annotation ? UI_MSG("无法返回注释") : UI_MSG("无法返回正文");
    if (!reader_close_note(ctx)) return reader_note_notice(ctx, failure);
    return APP_REDRAW_PAGE;
}

static app_redraw_t reader_link_tap(app_ctx_t *ctx, uint16_t x, uint16_t y) {
    if (s_note_open) {
        EpdRect footer = reader_note_footer();
        if (ui_rect_hit(footer, x, y)) {
            if (abs((int)x - footer.width / 2) <= 108) return reader_return_from_note(ctx);
            return APP_REDRAW_DONE;
        }
    }
    if (!s_chapter_link_count || !ui_rect_hit(body_rect(), x, y)) return APP_REDRAW_NONE;
    bool ambiguous;
    lock_draw();
    const html_link_t *link = book_layout_link_at(s_page, body_rect(), s_px, x, y, &ambiguous);
    unlock_draw();
    if (!link) return APP_REDRAW_NONE;
    const bool marked_note = !link->navigation && (link->noteref || link->backlink || link->image_label[0]);
    if (ambiguous) return reader_note_notice(ctx, marked_note ? UI_MSG("请点按要查看的注释") : UI_MSG("请点按要打开的链接"));
    size_t chapter;
    char anchor[HTML_LINK_ANCHOR_CAP];
    if (!book_chapter_link_target(s_chapter, link->href, &chapter, anchor, sizeof(anchor)) ||
        !load_chapter_at(ctx, chapter, 0, false, anchor[0] ? anchor : NULL, SIZE_MAX, CHAPTER_FOLLOW_LINK, link))
        return reader_note_notice(ctx, marked_note ? UI_MSG("找不到注释") : UI_MSG("找不到链接目标"));
    s_reader_notice[0] = 0;
    return APP_REDRAW_PAGE;
}

// Touch arrives in the device's portrait coordinates, independent of drawing rotation.
static void reader_landscape_point(uint16_t *x, uint16_t *y) {
    uint16_t portrait_x = *x, portrait_y = *y;
    *x = s_reader_landscape_reverse ? UI_LOCK_HEIGHT - 1 - portrait_y : portrait_y;
    *y = s_reader_landscape_reverse ? portrait_x : UI_LOCK_WIDTH - 1 - portrait_x;
}

static app_redraw_t reader_note_card_gesture(const ui_gesture_event_t *event) {
    if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    uint16_t x0 = event->x0, y0 = event->y0, x = event->x, y = event->y;
    if (reader_landscape()) {
        if (x0 >= UI_LOCK_WIDTH || y0 >= UI_LOCK_HEIGHT || x >= UI_LOCK_WIDTH || y >= UI_LOCK_HEIGHT)
            return APP_REDRAW_NONE;
        reader_landscape_point(&x0, &y0);
        reader_landscape_point(&x, &y);
    }
    EpdRect card = reader_note_card_rect(s_note_card), close = reader_note_card_exit(card);
    bool close_tapped = !reader_landscape() && ui_rect_hit(close, x0, y0) && ui_rect_hit(close, x, y);
    if ((y0 < card.y && y < card.y) || close_tapped) {
        reader_note_card_close();
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static bool reader_body_hit(uint16_t x, uint16_t y) {
    if (reader_landscape() && s_reader_panel == READER_PANEL_NONE) {
        if (x >= UI_LOCK_WIDTH || y >= UI_LOCK_HEIGHT) return false;
        reader_landscape_point(&x, &y);
        return ui_rect_hit(landscape_reader_area(), x, y);
    }
    return ui_rect_hit(reader_area(), x, y);
}

static app_redraw_t reader_wide_preview(app_ctx_t *ctx) {
    lock_draw();
    draw_landscape_reader(ctx, ctx->fb, s_page, true);
    unlock_draw();
    s_area = (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT};
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}

static void reader_stop_landscape_sensor(app_ctx_t *ctx) {
#ifndef PICO_HOST_SIMULATOR
    if (s_reader_sensor_owned) read_pico_sensor_sleep(ctx->acc);
    s_reader_sensor_owned = false;
    s_reader_sensor_next_ms = 0;
    s_reader_opposite_samples = 0;
#else
    (void)ctx;
#endif
}

static bool reader_poll_landscape_direction(app_ctx_t *ctx) {
#ifndef PICO_HOST_SIMULATOR
    if (!reader_landscape() || (s_view != READING && s_view != TOC) || !s_text) {
        reader_stop_landscape_sensor(ctx);
        return false;
    }
    // 手势松开前画面与触控映射必须保持同一个方向。/ Keep the image and touch mapping in one orientation until release.
    if (ctx->consumed || ctx->released || (ctx->touch && ctx->touch->touched) ||
        s_reader_slider >= 0 || s_progress_drag || s_toc_jump_drag) {
        s_reader_opposite_samples = 0;
        return false;
    }
    if (!ctx->acc || ctx->now_ms < s_reader_sensor_next_ms) return false;
    if (!sc7a20h_powered(ctx->acc)) {
        read_pico_sensor_wake(ctx->acc);
        s_reader_sensor_owned = sc7a20h_powered(ctx->acc);
        // 传感器以 12.5 Hz 采样，下一次轮询再取新值。/ Let the 12.5 Hz sensor finish conversion before the next poll.
        s_reader_sensor_next_ms = ctx->now_ms + (s_reader_sensor_owned ? 120 : 1000);
        s_reader_opposite_samples = 0;
        return false;
    }
    s_reader_sensor_next_ms = ctx->now_ms + 160;
    sc7a20h_sample_t sample;
    if (read_pico_accel_read(ctx->acc, &sample) != ESP_OK || !sc7a20h_sample_fresh(&sample)) {
        s_reader_opposite_samples = 0;
        return false;
    }

    // 平放、握持方向不明确或加速度偏离 1g 时保持原方向。
    // Preserve orientation when flat, ambiguous, or far from 1g.
    // 捏住两条短边的中点倾斜时，横屏的上下方向对应设备 Y 轴。
    // Tilting around the short-edge midpoints puts landscape up/down on device Y.
    const int vertical = abs(sample.y_mg), lateral = abs(sample.x_mg), normal = abs(sample.z_mg);
    const int64_t total_sq = (int64_t)vertical*vertical + (int64_t)lateral*lateral + (int64_t)normal*normal;
    const bool reverse = sample.y_mg > 0;
    if (vertical < 220 || vertical < lateral + 100 || total_sq < 750*750 || total_sq > 1250*1250 ||
        reverse == s_reader_landscape_reverse) {
        s_reader_opposite_samples = 0;
        return false;
    }
    if (++s_reader_opposite_samples < 2) return false;
    s_reader_opposite_samples = 0;
    s_reader_landscape_reverse = reverse;
    ESP_LOGI(TAG, "landscape orientation=%s gravity=%d/%d/%d mg",
             reverse ? "reversed" : "normal", sample.x_mg, sample.y_mg, sample.z_mg);
    return true;
#else
    (void)ctx;
    return false;
#endif
}
#include "reader_controls_actions.h"



static bool content_tapped(EpdRect rect, const ui_gesture_event_t *event) {
    return ui_rect_hit(rect, event->x0, event->y0) && ui_rect_hit(rect, event->x, event->y);
}
static bool details_dismissed(ui_action_card_t card, const ui_gesture_event_t *event) {
    return (event->y0 < card.top && event->y < card.top) || content_tapped(card.close, event);
}
static int details_hit(const ui_gesture_event_t *event, int count) {
    for (int i = 0; i < count; ++i)
        if (content_tapped(ui_nav_action_button(i, count), event)) return i;
    return -1;
}
static void editor_start(bool document) {
    const char *name = document ? s_document_detail.name : s_managed.name;
    copy_text(s_editor_title, sizeof(s_editor_title), name);
    s_editor_notice[0] = 0;
    s_editor_document = document;
    ui_text_input_open(&s_title_input, s_editor_title, sizeof(s_editor_title), true, false, UI_INPUT_CHINESE);
    s_view = EDIT;
}
static void editor_cancel(void) {
    ui_text_input_clear(&s_title_input);
    s_view = s_editor_document ? DOCUMENTS : MANAGE;
}
static app_redraw_t manage_action(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    if (details_dismissed(details_card(s_clear_confirm, s_manage_message), event)) {
        s_clear_confirm = false;
        s_view = SHELF;
        return APP_REDRAW_PAGE;
    }
    if (s_clear_confirm || s_file_removed) {
        int action = details_hit(event, 2);
        if (action < 0) return APP_REDRAW_NONE;
        if (action == 1) manage_apply(ctx);
        else if (s_clear_confirm) s_clear_confirm = false;
        else s_view = SHELF;
        return APP_REDRAW_PAGE;
    }
    int action = details_hit(event, DETAIL_ACTIONS);
    switch (action) {
    case DETAIL_RENAME:
        editor_start(false);
        break;
    case DETAIL_PIN: {
        bool next = !s_managed.pinned;
        if (content_book_pin_set(s_managed.path, next) != ESP_OK) {
            copy_text(s_manage_message, sizeof(s_manage_message), UI_MSG("置顶保存失败，请重试"));
            return APP_REDRAW_PAGE;
        }
        s_managed.pinned = next;
        s_manage_message[0] = 0;
        for (int i = 0; i < s_count; ++i)
            if (!strcmp(s_shelf[i].path, s_managed.path)) s_shelf[i].pinned = next;
        sort_shelf(ctx);
        s_shelf_index_dirty = true;
        break;
    }
    case DETAIL_DELETE:
    case DETAIL_CLEAR:
        s_clear_confirm = true;
        s_delete_confirm = action == DETAIL_DELETE;
        s_manage_message[0] = 0;
        break;
    default:
        return APP_REDRAW_NONE;
    }
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_save(app_ctx_t* ctx) {
    if (s_title_input.pinyin[0]) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), UI_MSG("请先选择候选字"));
        return APP_REDRAW_PAGE;
    }
    char *start = s_editor_title;
    while (*start == ' ') ++start;
    if (start != s_editor_title) memmove(s_editor_title, start, strlen(start) + 1);
    size_t len = strlen(s_editor_title);
    while (len && s_editor_title[len - 1] == ' ') s_editor_title[--len] = 0;
    s_title_input.cursor=strlen(s_editor_title);
    if (!s_editor_title[0]) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), UI_MSG("名称不能为空"));
        return APP_REDRAW_PAGE;
    }
    const char *path = s_editor_document ? s_document_detail.path : s_managed.path;
    esp_err_t err = book_title_set(path, s_editor_title);
    if (err != ESP_OK) {
        copy_text(s_editor_notice, sizeof(s_editor_notice), UI_MSG("保存失败，请检查名称或存储"));
        return APP_REDRAW_PAGE;
    }
    if (s_editor_document) {
        for (size_t i = 0; i < s_document_count; ++i)
            if (!strcmp(s_documents[i].path, path))
                copy_text(s_documents[i].name, sizeof(s_documents[i].name), s_editor_title);
        s_document_menu = false;
        s_view = DOCUMENTS;
        ctx->leaf = 0;
        sort_documents();
    } else {
        for (int i = 0; i < s_count; ++i)
            if (!strcmp(s_shelf[i].path, path))
                copy_text(s_shelf[i].name, sizeof(s_shelf[i].name), s_editor_title);
        s_view = SHELF;
        s_shelf_titles = book_title_revision();
        refresh_search_matches();
        sort_shelf(ctx);
        s_shelf_index_dirty = true;
    }
    ui_text_input_clear(&s_title_input);
    return APP_REDRAW_PAGE;
}
static app_redraw_t editor_paint(app_ctx_t *ctx){
    render(ctx,ctx->fb);s_area=ui_text_input_area();s_mode=MODE_GL16;return APP_REDRAW_AREA;
}
static app_redraw_t editor_action(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    ui_input_result_t result=ui_text_input_gesture(&s_title_input,ev);
    if(result==UI_INPUT_CANCEL){editor_cancel();return APP_REDRAW_PAGE;}
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
static app_redraw_t content_sort_action(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    ui_action_card_t card=content_sort_card();
    if(details_dismissed(card,ev)||content_tapped(ui_nav_rect(),ev)){
        s_sort_open=false;
        return APP_REDRAW_PAGE;
    }
    content_options_t next=s_sort_options;
    if(content_tapped(content_sort_direction(),ev))next.ascending=!next.ascending;
    else{
        int mode=0;
        while(mode<=CONTENT_RECENT&&!content_tapped(content_sort_mode(mode),ev))++mode;
        if(mode>CONTENT_RECENT||mode==next.mode)return APP_REDRAW_NONE;
        next.mode=mode;
        next.ascending=mode==CONTENT_NAME||mode==CONTENT_PROGRESS;
    }
    if (!s_sort_documents) shelf_require_complete(ctx);
    if(content_options_save(s_sort_documents,next)!=ESP_OK){
        ui_nav_notice_show(UI_MSG("排序保存失败，请重试"),ctx->now_ms);
        return APP_REDRAW_PAGE;
    }
    s_sort_options=next;
    if(s_sort_documents){
        s_document_options=next;sort_documents();ctx->leaf=0;
    }else{
        s_book_options=next;s_recent_sort=s_book_options.mode==CONTENT_RECENT;
        sort_shelf(ctx);
        s_shelf_index_dirty = true;
    }
    return APP_REDRAW_PAGE;
}
static void document_details_apply(app_ctx_t *ctx,bool deleting){
    document_entry_t *d=&s_document_detail;
    if(!strcmp(s_path,d->path))free_book();
    pending_discard(d->path);
    esp_err_t e=deleting?document_store_delete(d->path):book_progress_forget(d->path);
    if (e == ESP_OK && deleting) e = book_title_clear(d->path);
    s_document_delete_confirm=s_document_clear_confirm=false;
    if(e!=ESP_OK){
        if(deleting){struct stat st;d->cleanup_pending=stat(d->path,&st)!=0&&errno==ENOENT;
            if(d->cleanup_pending)book_store_notify_list_changed();
            for(size_t i=0;i<s_document_count;++i)if(!strcmp(s_documents[i].path,d->path))s_documents[i].cleanup_pending=d->cleanup_pending;
            scan_documents(ctx);
        }
        ui_snprintf(s_document_message,sizeof(s_document_message),deleting?UI_MSG("文档删除或清理失败，请重试"):UI_MSG("进度清理失败，请重试"));return;
    }
    s_document_message[0]=0;
    if(deleting){
        s_document_menu=false;d->cleanup_pending=false;
        for(size_t i=0;i<s_document_count;++i)if(!strcmp(s_documents[i].path,d->path))s_documents[i].cleanup_pending=false;
        scan_documents(ctx);
    }else{
        d->has_progress=false;d->pct=0;d->recent=0;
        for(size_t i=0;i<s_document_count;++i)if(!strcmp(s_documents[i].path,d->path)){
            s_documents[i].has_progress=false;
            s_documents[i].pct=0;
            s_documents[i].recent=0;
            break;
        }
        sort_documents();
    }
}
static app_redraw_t document_details_action(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    document_entry_t *d = &s_document_detail;
    bool confirm = s_document_delete_confirm || s_document_clear_confirm;
    if (details_dismissed(details_card(confirm, s_document_message), event)) {
        s_document_delete_confirm = s_document_clear_confirm = s_document_menu = false;
        return APP_REDRAW_PAGE;
    }
    if (confirm || d->cleanup_pending) {
        int action = details_hit(event, 2);
        if (action < 0) return APP_REDRAW_NONE;
        if (action == 1) document_details_apply(ctx, d->cleanup_pending || s_document_delete_confirm);
        else if (confirm) s_document_delete_confirm = s_document_clear_confirm = false;
        else s_document_menu = false;
        return APP_REDRAW_PAGE;
    }
    int action = details_hit(event, DETAIL_ACTIONS);
    switch (action) {
    case DETAIL_RENAME:
        editor_start(true);
        break;
    case DETAIL_PIN:
        if (document_store_set_flags(d->path, !d->pinned, d->starred) != ESP_OK) {
            ui_snprintf(s_document_message, sizeof(s_document_message), UI_MSG("置顶保存失败，请重试"));
            return APP_REDRAW_PAGE;
        }
        d->pinned = !d->pinned;
        s_document_message[0] = 0;
        for (size_t i = 0; i < s_document_count; ++i)
            if (!strcmp(s_documents[i].path, d->path)) s_documents[i].pinned = d->pinned;
        ctx->leaf = 0;
        sort_documents();
        break;
    case DETAIL_DELETE:
    case DETAIL_CLEAR:
        s_document_message[0] = 0;
        s_document_delete_confirm = action == DETAIL_DELETE;
        s_document_clear_confirm = action == DETAIL_CLEAR;
        break;
    default:
        return APP_REDRAW_NONE;
    }
    return APP_REDRAW_PAGE;
}
// 正文高度三等分；页眉、页脚保留各自操作。/ Split the body into thirds, keeping header/footer actions separate.


static app_redraw_t reader_toc_target(app_ctx_t *ctx, int target, uint16_t x) {
    if(target>=0&&book_kind()==BOOK_KIND_MARKDOWN&&x<106+book_markdown_navigation_depth((size_t)target)*24){
        if(book_markdown_navigation_toggle((size_t)target)){s_selected_toc=SIZE_MAX;if(ctx->leaf>=leaves())ctx->leaf=leaves()-1;return APP_REDRAW_PAGE;}
    }
    bool book_start = target == BOOK_TOC_START && book_kind() == BOOK_KIND_EPUB;
    if (book_start || (target >= 0 && !s_message[0])) {
        size_t chapter = book_start ? 0 : book_navigation_chapter((size_t)target);
        if (chapter >= book_chapter_count()) return APP_REDRAW_NONE;
        save_progress();
        bool leaving_note = book_start && s_note_open;
        size_t previous_chapter = leaving_note ? s_note_origin.chapter : s_chapter;
        size_t previous_offset = leaving_note ? s_note_origin.byte_off : reader_page_offset(s_page);
        const char *anchor = book_start ? NULL : book_navigation_anchor((size_t)target);
        size_t source_offset = book_start ? SIZE_MAX : book_navigation_source_offset((size_t)target);
        set_reader_view(READING);
        if (load_chapter_at(ctx, chapter, 0, false, anchor, source_offset,
                            book_start ? CHAPTER_BOOK_START : CHAPTER_KEEP_NOTE, NULL)) {
            s_jump_back_valid = true;
            s_jump_back_chapter = previous_chapter;
            s_jump_back_offset = previous_offset;
            if (!book_start) {
                s_selected_toc = (size_t)target;
                char entry_title[sizeof(s_title)];
                esp_err_t title_result = book_kind() == BOOK_KIND_EPUB
                    ? (s_epub_section_has_text
                        ? book_epub_reading_title(chapter, (size_t)target, entry_title, sizeof(entry_title))
                        : ESP_ERR_NOT_FOUND)
                    : book_navigation_title((size_t)target, entry_title, sizeof(entry_title));
                if (title_result == ESP_OK)
                    copy_text(s_title, sizeof(s_title), entry_title);
                else if (book_kind() == BOOK_KIND_EPUB && title_result == ESP_ERR_NOT_FOUND)
                    s_title[0] = 0;
            }
            s_view = READING;
            s_reader_panel = READER_PANEL_NONE;
            save_progress();
        } else set_reader_view(s_text ? TOC : SHELF);
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t action_at(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_view == TOC) {
        if (ui_rect_hit(reader_portrait_close_rect(), x, y)) {
            s_message[0] = 0;
            s_toc_jump_open = s_toc_jump_drag = false;
            set_reader_view(READING);
            return reader_show_panel(READER_PANEL_TOOLS);
        }
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
        int target = book_toc_hit(x, y, ctx->leaf, book_toc_entry_count());
        if (target == BOOK_TOC_PREV || target == BOOK_TOC_NEXT ||
            target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT) {
            int step = target == BOOK_TOC_SKIP_PREV || target == BOOK_TOC_SKIP_NEXT ? 10 : 1;
            int next = ctx->leaf + (target == BOOK_TOC_PREV || target == BOOK_TOC_SKIP_PREV ? -step : step);
            if (next < 0) next = 0;
            if (next >= leaves()) next = leaves() - 1;
            if (next != ctx->leaf) { ctx->leaf = next; return APP_REDRAW_PAGE; }
            return APP_REDRAW_NONE;
        }
        if (target == BOOK_TOC_JUMP && book_toc_entry_count()) {
            s_toc_jump_open = true;
            s_toc_jump_percent = (ctx->leaf + 1) * 100 / leaves();
            return APP_REDRAW_PAGE;
        }
        return reader_toc_target(ctx,target,x);
    }

    if (s_view == READING) {
        if (s_toolbar) return reader_panel_action(ctx, x, y);
        if (!s_reader_fullscreen && ui_rect_hit(reader_close_rect(), x, y))
            return reader_return(ctx, false);

        if(!ui_rect_hit(reader_area(),x,y))return APP_REDRAW_NONE;
        if(x<UI_LOCK_WIDTH/3)return turn_page(ctx,-1);
        if(x>=UI_LOCK_WIDTH*2/3)return turn_page(ctx,1);
        return reader_show_panel(READER_PANEL_TOOLS);
    }
    if (s_view != SHELF) return APP_REDRAW_NONE;
    int tab = ui_nav_hit(x, y);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    if (ui_rect_hit(shelf_page_arrow_rect(-1), x, y)) return shelf_turn_page(ctx, -1);
    if (ui_rect_hit(shelf_page_arrow_rect(1), x, y)) return shelf_turn_page(ctx, 1);
    if (s_save_failed && y >= UI_CONTENT_BOTTOM - UI_PX_CAPTION && y < UI_CONTENT_BOTTOM) {
        retry_progress();
        return APP_REDRAW_PAGE;
    }
    if (s_message[0] && !s_visible_count) return APP_REDRAW_NONE;
    int rows = shelf_rows();
    for (int row = 0; row < rows; ++row) {
        if (!ui_rect_hit(row_rect(row), x, y)) continue;
        int index = ctx->leaf * rows + row;
        if (index >= s_visible_count) return APP_REDRAW_PAGE;
        const shelf_entry_t *entry = shelf_entry_at(index);
        if (!entry) return APP_REDRAW_NONE;
        if (entry->removed) {
            s_managed = *entry;
            s_view = MANAGE;
            s_file_removed = s_delete_confirm = true;
            s_clear_confirm = false;
            copy_text(s_manage_message, sizeof(s_manage_message), UI_MSG("文件已删除，进度清理失败，请重试"));
        } else {
            char path[BOOK_STORE_PATH_MAX];
            copy_text(path, sizeof(path), entry->path);
            open_book(ctx, path);
        }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static void on_enter(app_ctx_t* ctx) {
    shelf_perf_request(s_view,(int)book_index_cache_epoch());
    // 设置页试按的旧事件不得在开书后翻页。/ Discard remote tests from earlier pages before opening a book.

    book_layout_set_spacing(reader_layout().line, reader_layout().paragraph);
    book_layout_set_images_visible(!app_settings_reader_hide_images());
    book_layout_set_typography(((int)reader_layout().tracking - 2) * 2);
    book_layout_set_first_line_indent(reader_layout().indent);
    book_layout_set_reading_line(reader_layout().rule);
    book_layout_set_reading_line_offset(reader_layout().rule_offset);
    s_reader_fullscreen = reader_layout().fullscreen;
    s_reader_immersive = reader_layout().immersive;
    s_bookmark_edit = s_bookmark_delete_confirm = s_bookmark_delete_error = false;
    s_bookmark_selected = 0;
    s_view = SHELF;
    s_document_delete_confirm=s_document_clear_confirm=s_document_menu=false;s_document_message[0]=0;
    s_sort_open=false;
    s_book_options=content_options_load(false,app_settings_shelf_recent_sort());
    s_document_options=content_options_load(true,false);
    s_document_starred_only=false;
    s_reader_return_home = false;
    s_presented_view = -1;
    ctx->leaf = 0;
    s_px = app_settings_book_px();
    s_recent_sort = s_book_options.mode==CONTENT_RECENT;
    s_margin = reader_layout().margin;
    s_line_spacing = reader_layout().line;
    // 传书页已停止并 join HTTP，安全注销且只丢弃对应路径的旧进度。
    // Transfer has stopped and joined HTTP; safely unregister only invalidated paths.
    pending_drop_invalidated();
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_progress_drag = false;
    s_clear_confirm = false;
    s_pressed_control = -1;
    s_du_count = 0;
    s_reader_cleanup = false;
    s_poll_ms = 0;
    read_pico_sd_info_t sd = {0};
    esp_err_t media_err = read_pico_sd_get_info(&sd);
    bool reuse = s_shelf_cache_valid && media_err != ESP_ERR_NOT_FINISHED &&
                 s_shelf_titles == book_title_revision() &&
                 sd.present == s_cache_sd_present && sd.mounted == s_cache_sd_mounted &&
                 s_store_revision == book_store_revision();
    shelf_perf_event(SP_REUSE,reuse,(int)book_store_revision(),sd.present,sd.mounted);
    s_scan_pending = !reuse;
    if (reuse) {
        refresh_cached_progress(ctx);
        sort_shelf(ctx);
        (void)open_requested_book(ctx);
    } else {
        s_count = s_visible_count = 0;
        s_shelf_cache_valid = s_shelf_progress_valid = false;
        s_message[0] = 0;
        if (sd.present != s_cache_sd_present || sd.mounted != s_cache_sd_mounted)
            invalidate_covers();
        s_shelf_paged = s_shelf_disk_valid = false;
        if (s_view == SHELF && sd.mounted && media_err == ESP_OK) (void)shelf_try_index();
        // 已指定文件可直接打开；书架目录留到真正进入书架时再扫描。
        // Open a requested file directly; scan the shelf only when it is actually shown.
        if (open_requested_book(ctx)) {
            s_scan_pending = false;
            return;
        }
        if (!sd.mounted && media_err != ESP_ERR_NOT_FINISHED) read_pico_sd_start_probe();
    }
}
static void book_on_exit(app_ctx_t* ctx) {
    book_shelf_index_audit_stop();
    reader_stop_landscape_sensor(ctx);
    ttf_name_preview_pause();
    track_ticket_stats(ctx);
    s_document_scan_pending=false;
    clear_content_search(ctx);
    if (s_view == SEARCH) ui_text_input_clear(&s_search_input);
    s_presented_view = -1;
    if (s_view == EDIT) ui_text_input_clear(&s_title_input);
    s_pressed_control = -1;
    s_reader_slider = -1;
    s_progress_drag = false;
    save_progress();
    book_recent_index_flush();
    shelf_index_checkpoint(ctx, true);
    shelf_index_publish();
    refresh_finished_document();
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
    book_shelf_index_audit_stop();
    s_shelf_audit_pending = false;
    s_shelf_force_scan = true;
    reader_stop_landscape_sensor(ctx);
    lock_draw();
    save_progress();
    free_book();
    unlock_draw();
    if (s_view == EDIT) ui_text_input_clear(&s_title_input);
    s_view = SHELF;
    s_sort_open=false;
    discard_document_cache();
    s_count = s_visible_count = 0;
    clear_content_search(ctx);
    ui_text_input_clear(&s_search_input);
    s_shelf_cache_valid = s_shelf_paged = s_shelf_disk_valid = s_shelf_index_dirty = false;
    free(s_shelf_page); s_shelf_page = NULL;
    free(s_shelf); s_shelf = NULL; s_shelf_capacity = 0;
    invalidate_covers();
    ctx->leaf = 0;
    s_reader_panel = READER_PANEL_NONE;
    s_reader_slider = -1;
    s_progress_drag = false;
    s_clear_confirm = false;
    s_pressed_control = -1;
    s_scan_pending = true;
    s_du_count = 0;
    copy_text(s_message, sizeof(s_message), UI_MSG("TF 卡已移除"));
}
static void book_on_media_ready(app_ctx_t* ctx) {
    (void)ctx;
    s_shelf_cache_valid = s_shelf_paged = s_shelf_disk_valid = s_shelf_index_dirty = false;
    if(s_document_media_epoch!=record_store_media_epoch())discard_document_cache();
    if (s_view == SHELF || s_view == DOCUMENTS)
        s_scan_pending = true;
    if (s_view == DOCUMENTS)
        s_document_scan_pending = true;
}
// 控件编号仅用于保持按下与抬起命中同一个目标。/ IDs pair a press with release on the same control.
static int control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
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
    if (s_view != SHELF) return -1;
    for (int i = 0; i < 2; ++i) {
        *rect = shelf_page_arrow_rect(i ? 1 : -1);
        if (ui_rect_hit(*rect, x, y)) return 116 + i;
    }
    *rect = (EpdRect){UI_MARGIN, UI_CONTENT_BOTTOM - UI_PX_CAPTION, ui_content_width(), UI_PX_CAPTION};
    if (s_view == SHELF && s_save_failed && ui_rect_hit(*rect, x, y)) return 113;
    if (s_message[0] && !s_visible_count) return -1;
    int rows = shelf_rows();
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
static app_redraw_t content_tab_action(app_ctx_t *ctx, bool documents) {
    if ((s_view == DOCUMENTS) != documents) clear_content_search(ctx);
    s_document_mode = documents;
    s_view = documents ? DOCUMENTS : SHELF;
    ctx->leaf = 0;
    if (documents) {
        book_shelf_index_audit_stop();
        s_document_delete_confirm = s_document_clear_confirm = s_document_menu = false;
        ensure_documents();
    } else {
        s_document_scan_pending=false;
        if (!shelf_load_page(0)) {
            s_scan_pending = true;
            s_shelf_disk_valid = false;
        }
        prepare_covers(ctx);
    }
    return APP_REDRAW_PAGE;
}
static app_redraw_t gesture_event_local(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if(s_view==READING&&!s_toolbar)operation_log_event(OPLOG_GESTURE,ev->type,ev->x0,ev->y0,(s_reader_multi.active?1:0)|(s_reader_multi.blocked?2:0),(int)ev->hold_ms);
    if(s_sort_open)return ev->type==UI_GESTURE_TAP?content_sort_action(ctx,ev):APP_REDRAW_NONE;
    if(s_view==MANAGE)return ev->type==UI_GESTURE_TAP?manage_action(ctx,ev):APP_REDRAW_NONE;
    if(s_view==DOCUMENTS&&s_document_menu&&s_document_cache_valid&&!s_document_scan_pending)
        return ev->type==UI_GESTURE_TAP?document_details_action(ctx,ev):APP_REDRAW_NONE;
    if(s_view==READING&&!s_clear_confirm){s_stats_activity_ms=ctx->now_ms;return reader_controls_gesture(ctx,ev);}
    if(s_view==TOC&&!s_toc_jump_open&&ev->type==UI_GESTURE_TAP&&ev->y0>=1096)return reader_show_tab(ctx,ev->x0*5/684);
    bool documents_loading=s_view==DOCUMENTS&&(s_document_scan_pending||!s_document_cache_valid);
    if ((s_view == SHELF || s_view == DOCUMENTS) && ev->type == UI_GESTURE_TAP) {
        bool documents = s_view == DOCUMENTS;
        for (int i = 0; i < 2; ++i)
            if (content_tapped(content_tab(i), ev)) return content_tab_action(ctx, i == 1);
        if(documents_loading){
            int tab=ui_nav_hit(ev->x0,ev->y0);
            if(tab>=0)ui_nav_request(ctx,tab);
            return APP_REDRAW_NONE;
        }
        if ((documents ? s_document_query[0] : s_query[0]) && content_tapped(content_search_chip(), ev)) {
            clear_content_search(ctx);
            return APP_REDRAW_PAGE;
        }
        if (content_tapped(content_action(0), ev)) {
            if (!documents) shelf_require_complete(ctx);
            search_begin();
            return APP_REDRAW_PAGE;
        }
        if (content_tapped(content_action(1), ev)) {
            content_sort_open(documents);
            return APP_REDRAW_PAGE;
        }
    }
    if(documents_loading)return APP_REDRAW_NONE;
    if(s_view==DOCUMENTS){
        if(ev->type==UI_GESTURE_TAP){
            for(int direction=-1;direction<=1;direction+=2)if(ui_rect_hit(ui_nav_pager_rect(direction),ev->x0,ev->y0)){
                int next=ctx->leaf+direction;if(next>=0&&next<leaves()){ctx->leaf=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;
            }
            int tab=ui_nav_hit(ev->x0,ev->y0);if(tab>=0){ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
        }
        if(ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_D){int next=ctx->leaf+(ev->type==UI_GESTURE_SWIPE_U?1:-1);if(next>=0&&next<leaves()){ctx->leaf=next;return APP_REDRAW_PAGE;}}
        if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_LONG_PRESS) {
            int rows = document_rows();
            for (int row = 0; row < rows; ++row) {
                if (!ui_rect_hit(document_row(row), ev->x0, ev->y0)) continue;
                document_entry_t *d = document_visible_at((size_t)ctx->leaf * rows + row);
                if (!d) break;
                if (ev->type == UI_GESTURE_LONG_PRESS) {
                    s_document_detail = *d;
                    s_document_menu = true;
                    s_document_delete_confirm = s_document_clear_confirm = false;
                    s_document_message[0] = 0;
                    return APP_REDRAW_PAGE;
                }
                char path[288];
                ui_snprintf(path, sizeof(path), "%s", d->path);
                if(!open_book(ctx,path))ui_nav_notice_show(s_message,ctx->now_ms);
                return APP_REDRAW_PAGE;
            }
        }
        return APP_REDRAW_NONE;
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
            EpdRect close = reader_portrait_close_rect();
            if (content_tapped(close, ev)) return action_at(ctx, ev->x0, ev->y0);
            int start = book_toc_hit(ev->x0, ev->y0, ctx->leaf, book_toc_entry_count());
            int end = book_toc_hit(ev->x, ev->y, ctx->leaf, book_toc_entry_count());
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
            const shelf_entry_t *entry = shelf_entry_at(ctx->leaf * shelf_rows() + start);
            if (!entry) return APP_REDRAW_NONE;
            s_managed = *entry;
            shelf_require_complete(ctx);
            int current = 0;
            while (current < s_count && strcmp(s_shelf[current].path, s_managed.path)) ++current;
            if (current == s_count || s_shelf[current].size != s_managed.size ||
                s_shelf[current].modified != s_managed.modified) return APP_REDRAW_PAGE;
            s_managed = s_shelf[current];
            s_view = MANAGE;
            s_clear_confirm = false;
            s_delete_confirm = s_file_removed = s_managed.removed;
            copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ? UI_MSG("文件已删除，进度清理失败，请重试") : "");
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
        }
    }
    return decorated ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
}
#include "reader_landscape_actions.h"

static app_redraw_t gesture_event(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    if (!wide_panel_open()) return gesture_event_local(ctx, event);
    if (event->x0 >= UI_LOCK_WIDTH || event->y0 >= UI_LOCK_HEIGHT) return APP_REDRAW_NONE;
    if (s_view == READING) s_stats_activity_ms = ctx->now_ms;
    ui_gesture_event_t ev = *event;
    reader_landscape_point(&ev.x0, &ev.y0);
    reader_landscape_point(&ev.x, &ev.y);
    return wide_gesture(ctx, &ev);
}

static int reader_page_key_direction(int key) {
    if (key != UI_KEY_1 && key != UI_KEY_3) return 0;
    int direction = key == UI_KEY_1 ? -1 : 1;
    // A clockwise landscape puts KEY1 below KEY3; reverse landscape puts it above.
    return reader_landscape() && !s_reader_landscape_reverse ? -direction : direction;
}

static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if (s_note_card) {
        if (key != UI_KEY_2) return APP_REDRAW_NONE;
        reader_note_card_close();
        return APP_REDRAW_PAGE;
    }
    if (!app_book_reader_active()) {
        if (key != UI_KEY_2) {
            if (s_sort_open || s_document_menu || s_clear_confirm)
                return APP_REDRAW_NONE;
            int direction = key == UI_KEY_1 ? -1 : 1;
            if (s_view == SHELF) return shelf_turn_page(ctx, direction);
            if ((s_view == DOCUMENTS) && s_document_scan_pending)
                return APP_REDRAW_NONE;
            if (s_view == DOCUMENTS) {
                int next = ctx->leaf + direction;
                if (next >= 0 && next < leaves()) { ctx->leaf = next; return APP_REDRAW_PAGE; }
            }
            return APP_REDRAW_NONE;
        }
        if (s_sort_open) { s_sort_open = false; return APP_REDRAW_PAGE; }
        if (s_view == EDIT) { editor_cancel(); return APP_REDRAW_PAGE; }
        if (s_view == SEARCH) { search_finish(ctx, false); return APP_REDRAW_PAGE; }
        if (s_view == DOCUMENTS && s_document_menu) {
            if (s_document_delete_confirm || s_document_clear_confirm)
                s_document_delete_confirm = s_document_clear_confirm = false;
            else s_document_menu = false;
            return APP_REDRAW_PAGE;
        }
        if (s_view == MANAGE && s_clear_confirm) { s_clear_confirm = false; return APP_REDRAW_PAGE; }
        if (s_view == MANAGE) {
            s_clear_confirm = false;
            s_view = SHELF;
            return APP_REDRAW_PAGE;
        }
        ui_nav_request(ctx, 0);
        return APP_REDRAW_NONE;
    }
    int direction = reader_page_key_direction(key);
    if(s_view==READING&&s_toolbar){
        s_reader_multi=(reader_multi_t){.blocked=true};
        if(key==UI_KEY_2)return s_reader_panel==READER_PANEL_HELP?reader_help_close():reader_show_panel(READER_PANEL_NONE);
        if (!direction) return APP_REDRAW_NONE;
        if(reader_landscape()) return wide_detail_turn(direction);
        int next=s_control_page+direction,count=reader_control_count();
        if(next>=0&&next*reader_control_rows()<count){reader_set_control_page(next);return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    s_pressed_control = -1;
    if (s_view == TOC && key == UI_KEY_2) {
        s_toc_jump_open = s_toc_jump_drag = false;
        set_reader_view(s_text ? READING : SHELF);
        return reader_show_panel(READER_PANEL_NONE);
    }
    if (s_view == TOC && s_toc_jump_open) {
        if (key == UI_KEY_1 || key == UI_KEY_3) {
            s_toc_jump_percent += direction * 5;
            if (s_toc_jump_percent < 0) s_toc_jump_percent = 0;
            if (s_toc_jump_percent > 100) s_toc_jump_percent = 100;
        }
        return APP_REDRAW_PAGE;
    }
    if (s_scan_pending || s_clear_confirm) return APP_REDRAW_NONE;
    if (key == UI_KEY_2) return reader_show_panel(READER_PANEL_TOOLS);
    if (!direction) return APP_REDRAW_NONE;
    if (s_view == READING) { s_stats_activity_ms = ctx->now_ms; return turn_page(ctx, direction); }
    int next = ctx->leaf + direction;
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
    return false;
}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx, int direction) {
    int next = ctx->leaf + direction;
    if (s_view != SHELF || s_scan_pending || s_clear_confirm || next < 0 || next >= leaves())
        return APP_REDRAW_NONE;
    if (s_shelf_paged) {
        SHELF_PERF_SCOPE(perf, SP_SHELF_INDEX, 4, next);
        if (!shelf_load_page(next)) {
            s_shelf_disk_valid = false;
            s_scan_pending = true;
            ctx->leaf = next;
            return APP_REDRAW_PAGE;
        }
    }
    ctx->leaf = next;
    s_pressed_control = -1;
    prepare_covers(ctx);
    render(ctx, ctx->fb);
    s_area = (EpdRect){0,CONTENT_LIST_TOP,UI_LOCK_WIDTH,UI_NAV_TOP-CONTENT_LIST_TOP};
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}

// One source read/decode per tick, then leave an input window. Push at most one
// region per grid row; a new page can use any ready bitmaps without waiting for a job.
static app_redraw_t shelf_cover_tick(app_ctx_t *ctx) {
    const int side = shelf_grid_side();
    for (int first = 0; first < side * side; first += side) {
        const unsigned row_mask = ((1u << side) - 1) << first;
        const unsigned pending = s_cover_pending_mask & row_mask;
        if (pending) {
            const int row = __builtin_ctz(pending);
            shelf_cover_t *slot = &s_covers[row];
            s_cover_pending_mask &= ~(1u << row);
            slot->pending = false;
            const int index = ctx->leaf * side * side + row;
            if (index < s_visible_count && slot->index == index) {
                const shelf_entry_t *item = shelf_entry_at(index);
                if (item && slot->file_epoch == book_index_cache_file_epoch(item->size, item->modified)) {
                    bool unused;
                    slot->gray = load_cover_gray(item->path, item->name, item->author, true,
                                                 &unused, &slot->width, &slot->height);
                    slot->ready = slot->gray != NULL;
                }
            }
            s_cover_next_ms = esp_timer_get_time() / 1000 + 120;
            return APP_REDRAW_NONE;
        }
        EpdRect area = {0};
        int count = 0;
        for (int row = first; row < first + side; ++row) {
            if (!s_covers[row].ready) continue;
            s_covers[row].ready = false;
            render_shelf_cover(ctx, row);
            area = count++ ? ui_rect_union(area, row_rect(row)) : row_rect(row);
        }
        if (count) {
            shelf_perf_event(SP_SHELF_COVER_BATCH, first / side, count, ctx->leaf, side);
            s_area = area;
            s_mode = MODE_GL16;
            return APP_REDRAW_AREA;
        }
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_tick(app_ctx_t* ctx) {
    track_ticket_stats(ctx);
    const cst836u_touch_t *touch=ctx->touch;
    bool multi_enabled=s_view==READING&&!s_toolbar&&!s_note_card&&!s_clear_confirm&&!ctx->consumed;
    if(multi_enabled&&touch&&touch->count==2)multi_enabled=reader_body_hit(touch->points[0].x,touch->points[0].y)&&reader_body_hit(touch->points[1].x,touch->points[1].y);
    bool was_multi_active=s_reader_multi.active,was_multi_blocked=s_reader_multi.blocked;
    int multi=reader_multi_feed(&s_reader_multi,multi_enabled,touch&&touch->touched,touch?touch->count:0,
        touch?touch->points[0].x:0,touch?touch->points[0].y:0,touch?touch->points[1].x:0,touch?touch->points[1].y:0,ctx->now_ms);
    if(multi>=0||was_multi_active!=s_reader_multi.active||was_multi_blocked!=s_reader_multi.blocked)operation_log_event(OPLOG_MULTI,s_reader_multi.active,s_reader_multi.blocked,touch?touch->count:0,multi_enabled,multi);
    if(multi>=0){s_stats_activity_ms=ctx->now_ms;return reader_execute_shortcut(ctx,multi);}

    if (reader_poll_landscape_direction(ctx)) return APP_REDRAW_PAGE;
    if (ctx->consumed) return APP_REDRAW_NONE;
    reader_prefetch_idle(ctx);
    if(!reader_landscape()&&s_view==TOC&&s_toc_jump_open&&s_toc_jump_drag&&touch&&touch->touched&&touch->count==1)return reader_toc_preview(ctx,touch->x,false);
    if(!reader_landscape()&&s_progress_drag&&s_reader_panel==READER_PANEL_TOOLS&&touch&&touch->touched&&touch->count==1)return reader_progress_preview(ctx,touch->x);
    if(!reader_landscape()&&s_reader_slider>=0&&touch&&touch->touched&&touch->count==1&&!s_reader_slider_endpoint){app_redraw_t preview=reader_style_preview(ctx,touch->x);if(preview!=APP_REDRAW_NONE)return preview;}
    if (s_reader_notice[0] && ctx->now_ms >= s_reader_notice_until) {
        s_reader_notice[0] = 0;
        if(s_view==READING&&(s_toolbar||reader_landscape()))return APP_REDRAW_PAGE;
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
        bool documents_changed=retry_progress();
        if (failed != s_save_failed) { invalidate_prep(); return APP_REDRAW_PAGE; }
        if(documents_changed&&(s_view==DOCUMENTS))return APP_REDRAW_PAGE;
    }
    if (s_view == READING && s_reader_panel == READER_PANEL_FONT_PICKER) {
        if (ttf_font_picker_pending()) {
            if ((touch && touch->touched) || ctx->pressed || ctx->released) return APP_REDRAW_NONE;
            return ttf_font_picker_step() ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
        }
        return reader_landscape() ? wide_font_preview_tick(ctx) : reader_font_preview_tick(ctx);
    }
    // 卡片打开时暂停后台扫描和封面补绘，避免覆盖淡显区域。/ Pause background scans and cover painting while the sort card owns the display.
    if (s_sort_open) return APP_REDRAW_NONE;
    if (s_document_scan_pending && (s_view == DOCUMENTS) &&
        !(touch && touch->touched) && !ctx->released) {
        s_document_message[0] = 0;
        if(document_cache_current()||refresh_document_imports(DOCUMENT_STORE_CHANGE_LIMIT))
            s_document_scan_pending=false;
        else scan_documents(ctx);
        return APP_REDRAW_PAGE;
    }
    if (s_scan_pending && s_view == SHELF &&
        ctx->now_ms >= s_cover_next_ms && !(touch && touch->touched) &&
        !ctx->released && ctx->now_ms - s_poll_ms >= 500) {
        s_poll_ms = ctx->now_ms;
        esp_err_t media;
        {
            SHELF_PERF_SCOPE(wait_perf,SP_SCAN_WAIT,s_view,0);
            read_pico_sd_info_t info = {0};
            media = read_pico_sd_get_info(&info);
        }
        if (media == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_scan_pending = false;
        int leaf = ctx->leaf;
        bool try_index = !leaf && s_view == SHELF && !s_shelf_paged;
        if (!try_index || !shelf_try_index()) {
            scan_shelf(ctx);
            int count = leaves();
            ctx->leaf = leaf < count ? leaf : count - 1;
        }
        (void)open_requested_book(ctx);
        return APP_REDRAW_PAGE;
    }
    if (s_view == SHELF && !s_scan_pending && ctx->now_ms >= s_cover_next_ms &&
        !(touch && touch->touched) && !ctx->released) {
        app_redraw_t covers = shelf_cover_tick(ctx);
        if (covers != APP_REDRAW_NONE) return covers;
        if (!s_cover_pending_mask) shelf_index_publish();
    }
    if (s_view == SHELF && !s_scan_pending && s_shelf_audit_pending && s_shelf_disk_valid &&
        ctx->now_ms >= s_shelf_audit_next_ms && !(touch && touch->touched) && !ctx->pressed && !ctx->released) {
        int status = book_shelf_index_audit_step(&s_shelf_disk);
        if (status) shelf_perf_event(SP_SHELF_INDEX, 5, status, 0, 0);
        s_shelf_audit_next_ms = esp_timer_get_time()/1000 + 20;
        if (status) s_shelf_audit_pending = false;
        if (status > 0) {
            s_shelf_audited.contents = s_shelf_contents;
            s_shelf_audited.media = s_shelf_disk_media;
            s_shelf_audited.books = s_store_revision;
            s_shelf_audited.valid = true;
        }
        if (status < 0) {
            book_shelf_disk_invalidate();
            s_shelf_disk_valid = false;
            s_scan_pending = s_shelf_force_scan = true;
            return APP_REDRAW_PAGE;
        }
    }
    // 目录暂借系统字体，不应把正文按系统字体重新分页。/ The directory borrows the system face without repaginating the reader.
    if (s_view == READING && s_text && strcmp(s_font_path, ttf_font_path())) {
        size_t off = reader_page_offset(s_page);
        save_progress();
        lock_draw();
        invalidate_prep();
        bool ok = reflow_reading_font(off);
        copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
        unlock_draw();
        if (!ok) {
            ui_nav_notice_show(UI_MSG("排版失败，请重试"),ctx->now_ms);
            return reader_return(ctx,false);
        }
        prepare_inline_image();
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static void before_lock(app_ctx_t* ctx) {
    book_shelf_index_audit_stop();
    reader_stop_landscape_sensor(ctx);
    reader_note_card_close();
    if (s_view == READING) (void)reader_close_note(ctx);
    s_progress_drag=false;
    s_reader_slider=-1;
    s_toc_jump_drag=false;
    s_reader_multi=(reader_multi_t){.blocked=true};
    track_ticket_stats(ctx);
    save_progress();
    book_recent_index_flush();
    shelf_index_checkpoint(ctx, false);
    if (s_count <= 64) shelf_index_publish();
    if(s_text&&s_path[0])sleep_resume_set_target(1,s_path);
    // 锁屏票根始终以当前正在阅读的书为准；不要依赖书架最近项是否已经同步。
    if (s_text && s_path[0]) {
        record_store_lock();
        unsigned before = progress_kv_revision();
        if (book_progress_set_last_path(s_path) != ESP_OK) s_save_failed = true;
        else if (s_shelf_progress_revision == before) s_shelf_progress_revision = progress_kv_revision();
        record_store_unlock();
    }
    flush_ticket_stats();
    s_stats_last_ms = 0;
}
static esp_err_t prepare_sleep(app_ctx_t *ctx){
    before_lock(ctx);
    return s_save_failed||s_stats_pending_ms>=1000||s_stats_pending_turns?ESP_FAIL:ESP_OK;
}

static EpdRect area_hint(app_ctx_t* ctx) { (void)ctx; return s_area; }

const app_desc_t app_book = {
    .title = UI_MSG("图书 Books"), .detail = UI_MSG("TF 卡 txt / epub 阅读"), .enter_full = false, .owns_keys = true,
    .defer_middle_short = true,
    .menu_handle_enabled = menu_handle_enabled,
    .render = render, .present = present, .on_enter = on_enter, .on_exit = book_on_exit,
    .on_media_lost = book_on_media_lost, .on_media_ready = book_on_media_ready,
    .on_before_lock = before_lock, .prepare_sleep=prepare_sleep,
    .on_gesture = gesture_event, .on_key = on_key, .on_key_long = on_key_long,
    .on_power_short = on_power_short,
    .on_tick = on_tick, .area_hint = area_hint,
};
