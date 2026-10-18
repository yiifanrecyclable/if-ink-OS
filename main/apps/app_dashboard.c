/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * Modified derivative: compact home layout and draw four calendar weeks of measured reading.
 * 自定义固件修改：首页精简标题，阅读记录改为四周灰阶热力图。
 *
 * 中文：首页分列显示最近阅读的图书、文档与四周阅读记录；封面跨页面缓存。
 * English: Home shows separate recent book/document columns and four weeks of history; covers survive tab changes.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#ifndef PICO_HOST_SIMULATOR
// TF卡FatFS不支持符号链接；模拟器保留lstat检查。/ FatFS has no symlinks; retain lstat checks on the host.
#define lstat stat
#endif

#include "app.h"
#include "app_content_open.h"
#include "app_registry.h"
#include "book_cover.h"
#include "ui_image_dither.h"
#include "book_epub.h"
#include "book_progress.h"
#include "book_store.h"
#include "book_title.h"
#include "book_ticket.h"
#include "document_store.h"
#include "content_library.h"
#include "ui_lunar.h"
#include "ui_banner.h"
#include "ui_bitmap.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    uint8_t *cover;
    time_t modified;
    book_progress_t progress;
    bool has_progress;
} home_book_t;

static home_book_t s_current, s_recent[6];
#define HOME_DOCUMENT_COUNT 4
static document_entry_t s_recent_documents[HOME_DOCUMENT_COUNT];
static size_t s_recent_document_count;
#define HOME_DOCUMENT_X 272
#define HOME_DOCUMENT_WIDTH (648 - HOME_DOCUMENT_X)
typedef struct {
    int heat_title, week_y, grid_y, col_x, col_step, row_step, month_x;
    int recent_rule, recent_title, content_y, content_end, rows, row_height;
} home_geometry_t;
static home_geometry_t home_geometry(void) {
    home_geometry_t g={0};
    // 日历固定位置和标准字级；七列格子本身以屏幕为中心。/ Fixed calendar metrics; center the seven-cell array on the screen.
    g.heat_title=410;g.week_y=452;g.grid_y=478;
    g.col_step=62;g.col_x=UI_LOCK_WIDTH/2-3*g.col_step;g.month_x=g.col_x-42;
    g.row_step=52;g.recent_rule=706;g.recent_title=740;
    g.content_y=790;g.content_end=UI_NAV_TOP-16;
    g.rows=app_settings_system_font_size()==160?3:4;
    g.row_height=(g.content_end-g.content_y)/g.rows;
    return g;
}
typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    off_t size;
    time_t modified;
    uint8_t *pixels;
} home_cover_cache_t;
static home_cover_cache_t s_cover_cache[7];
static int s_cover_next;
static bool s_scan_pending;
static bool s_books_cache_valid;
static bool s_cached_sd_present;
static bool s_cached_sd_mounted;
static unsigned s_cached_store_revision;
static char s_cached_books_dir[128];
static char s_cached_last_path[BOOK_STORE_PATH_MAX];
static EpdRect s_area;
static uint32_t s_reading_days[30];
static bool s_reading_days_valid;
static uint32_t s_reading_end_day;
static bool s_home_force_full_once;

static home_book_t *book_at(int index);

static bool is_book(const char *name) {
    if (!name[0] || name[0] == '.' || !strncmp(name, "._", 2)) return false;
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".epub") || !strcasecmp(dot, ".txt"));
}
static void copy_title(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void title_for(home_book_t *item) {
    if (!book_title_from_path(item->path, item->title, sizeof(item->title)))
        copy_title(item->title, sizeof(item->title), "未命名");
}

static void consider_book(const char *path, const struct stat *st) {
    if (s_current.path[0] && !strcmp(path, s_current.path)) return;
    book_progress_t progress = {0};
    bool has_progress = st->st_size >= 0 && (uint64_t)st->st_size <= UINT32_MAX &&
                        book_progress_load(path, (uint32_t)st->st_size, &progress);
    if (!has_progress) return;
    uint64_t score = has_progress ? (UINT64_C(1) << 32) + progress.last_open_s : (uint64_t)st->st_mtime;
    int slot = -1;
    for (int i = 0; i < 6; ++i) {
        uint64_t other = s_recent[i].has_progress ?
            (UINT64_C(1) << 32) + s_recent[i].progress.last_open_s : (uint64_t)s_recent[i].modified;
        if (!s_recent[i].path[0] || score > other) { slot = i; break; }
    }
    if (slot < 0) return;
    for (int i = 5; i > slot; --i) s_recent[i] = s_recent[i - 1];
    memset(&s_recent[slot], 0, sizeof(s_recent[slot]));
    snprintf(s_recent[slot].path, sizeof(s_recent[slot].path), "%s", path);
    s_recent[slot].modified = st->st_mtime;
    s_recent[slot].has_progress = has_progress;
    if (has_progress) s_recent[slot].progress = progress;
}

static void scan_root(const char *root) {
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!is_book(entry->d_name)) continue;
        char path[BOOK_STORE_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= sizeof(path)) continue;
        struct stat st;
        if (!stat(path, &st) && S_ISREG(st.st_mode)) consider_book(path, &st);
    }
    closedir(dir);
}

static void scan_books(void) {
    char last[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(last, sizeof(last)) && is_book(strrchr(last, '/') ? strrchr(last, '/') + 1 : last)) {
        struct stat st;
        if (!stat(last, &st) && S_ISREG(st.st_mode) && st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX &&
            book_progress_load(last, (uint32_t)st.st_size, &s_current.progress)) {
            snprintf(s_current.path, sizeof(s_current.path), "%s", last);
            s_current.has_progress = true;
            title_for(&s_current);
        }
    }
    read_pico_sd_info_t sd = {0};
    read_pico_sd_get_info(&sd);
    if (sd.mounted) {
        if (!strcmp(app_settings_books_dir(), "/sdcard/books")) scan_root("/sdcard/book");
        scan_root(app_settings_books_dir());
    }
    if (book_store_flash_ready()) scan_root("/flash/books");
    for (int i = 0; i < 6; ++i) if (s_recent[i].path[0]) title_for(&s_recent[i]);
    home_book_t *featured = s_current.path[0] ? &s_current : &s_recent[0];
    const char *ext = strrchr(featured->path, '.');
    if (ext && !strcasecmp(ext, ".epub")) {
        char metadata_title[256];
        if (book_epub_metadata(featured->path, metadata_title, sizeof(metadata_title),
                               featured->author, sizeof(featured->author)) != ESP_OK)
            featured->author[0] = 0;
    }
}

// 按持久阅读序号挑选四篇文档，不读取同步日期或置顶顺序，也不扫描整个入库列表。
// Select four documents by persistent reading order, independent of sync dates/pins or a full import scan.
static bool consider_document(const char *path, const book_progress_t *progress, void *unused) {
    (void)unused;
    const size_t prefix = strlen(DOCUMENT_ROOT "/");
    if (strncmp(path, DOCUMENT_ROOT "/", prefix)) return true;
    const char *name = path + prefix;
    const char *ext = strrchr(name, '.');
    if (!name[0] || name[0] == '.' || strchr(name, '/') || !ext || strcasecmp(ext, ".md")) return true;
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size != progress->file_size) return true;
    size_t slot = 0;
    while (slot < s_recent_document_count &&
           (s_recent_documents[slot].recent > progress->last_open_s ||
            (s_recent_documents[slot].recent == progress->last_open_s &&
             strcmp(s_recent_documents[slot].path, path) < 0))) ++slot;
    if (slot >= HOME_DOCUMENT_COUNT) return true;
    if (s_recent_document_count < HOME_DOCUMENT_COUNT) ++s_recent_document_count;
    for (size_t i = s_recent_document_count - 1; i > slot; --i)
        s_recent_documents[i] = s_recent_documents[i - 1];
    document_entry_t *entry = &s_recent_documents[slot];
    memset(entry, 0, sizeof(*entry));
    copy_title(entry->name, sizeof(entry->name), name);
    copy_title(entry->path, sizeof(entry->path), path);
    entry->size = progress->file_size;
    entry->recent = progress->last_open_s;
    entry->pct = progress->pct;
    entry->has_progress = true;
    return true;
}

static void refresh_recent_documents(void) {
    s_recent_document_count = 0;
    memset(s_recent_documents, 0, sizeof(s_recent_documents));
    if (book_progress_list(consider_document, NULL) != ESP_OK) {
        s_recent_document_count = 0;
        memset(s_recent_documents, 0, sizeof(s_recent_documents));
    }
}

static void refresh_progress(void) {
    for (int i = 0; i < 7; ++i) {
        home_book_t *item = book_at(i);
        struct stat st;
        if (!item->path[0] || stat(item->path, &st) || !S_ISREG(st.st_mode) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) continue;
        item->has_progress = book_progress_load(item->path, (uint32_t)st.st_size, &item->progress);
    }
}

static void remember_scan_state(const read_pico_sd_info_t *sd) {
    s_books_cache_valid = true;
    s_cached_store_revision = book_store_revision();
    s_cached_sd_present = sd->present;
    s_cached_sd_mounted = sd->mounted;
    snprintf(s_cached_books_dir, sizeof(s_cached_books_dir), "%s", app_settings_books_dir());
    s_cached_last_path[0] = 0;
    (void)book_progress_last_path(s_cached_last_path, sizeof(s_cached_last_path));
}

static home_book_t *book_at(int index) { return index ? &s_recent[index - 1] : &s_current; }
static void clear_cover_cache(void) {
    for (int i = 0; i < 7; ++i) {
        free(s_cover_cache[i].pixels);
        memset(&s_cover_cache[i], 0, sizeof(s_cover_cache[i]));
    }
    s_current.cover = NULL;
    for (int i = 0; i < 6; ++i) s_recent[i].cover = NULL;
}

static void restore_cover_cache(void) {
    home_cover_cache_t old[7];
    memcpy(old, s_cover_cache, sizeof(old));
    memset(s_cover_cache, 0, sizeof(s_cover_cache));
    for (int i = 0; i < 7; ++i) {
        home_book_t *book = book_at(i);
        struct stat st;
        book->cover = NULL;
        if (!book->path[0] || stat(book->path, &st) || !S_ISREG(st.st_mode)) continue;
        for (int j = 0; j < 7; ++j) {
            if (!old[j].pixels || strcmp(book->path, old[j].path) ||
                strcmp(book->title, old[j].title) || strcmp(book->author, old[j].author) ||
                st.st_size != old[j].size || st.st_mtime != old[j].modified) continue;
            s_cover_cache[i] = old[j];
            book->cover = old[j].pixels;
            old[j].pixels = NULL;
            break;
        }
    }
    for (int i = 0; i < 7; ++i) free(old[i].pixels);
}

static bool load_cover(home_book_t *item, int slot) {
    uint8_t *pixels = heap_caps_malloc(BOOK_COVER_W * BOOK_COVER_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = pixels && book_cover_load_gray(item->path, item->title, item->author,
                                              pixels, true, NULL);
    if (!ok) { free(pixels); return false; }
    struct stat st;
    if (stat(item->path, &st) || !S_ISREG(st.st_mode)) { free(pixels); return false; }
    home_cover_cache_t *cached = &s_cover_cache[slot];
    free(cached->pixels);
    snprintf(cached->path, sizeof(cached->path), "%s", item->path);
    snprintf(cached->title, sizeof(cached->title), "%s", item->title);
    snprintf(cached->author, sizeof(cached->author), "%s", item->author);
    cached->size = st.st_size;
    cached->modified = st.st_mtime;
    cached->pixels = pixels;
    item->cover = pixels;
    return ok;
}

static void fit(char *title, int px, int width) {
    while (*title && ui_text_fixed_width_px(ui_text_effective_px(px), title) > width) {
        size_t n = strlen(title) - 1;
        while (n && ((unsigned char)title[n] & 0xc0) == 0x80) --n;
        title[n] = 0;
    }
}

// 两列标题单行截断，不加提示；注释可以两行，保留UTF8边界。
// Keep titles on one silently truncated line; captions may wrap to two lines at UTF-8 boundaries.
typedef struct {
    char lines[2][256];
    int px, count, line_height;
} recent_title_layout_t;

static recent_title_layout_t recent_title_layout_role(const char *title, int width, int role, int max_lines) {
    recent_title_layout_t layout = {.px = ui_text_effective_px(role),.line_height=ui_text_line_height(role)};
    int px = layout.px;
    char (*lines)[256] = layout.lines;
    const char *next = title;
    for (int row = 0; row < max_lines; ++row) {
        size_t used = 0;
        while (*next && used + 5 < sizeof(lines[row])) {
            unsigned char ch = (unsigned char)*next;
            size_t bytes = ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
            if (strlen(next) < bytes) break;
            memcpy(lines[row] + used, next, bytes);
            lines[row][used + bytes] = 0;
            if (ui_text_fixed_width_px(px, lines[row]) > width) { lines[row][used] = 0; break; }
            used += bytes;
            next += bytes;
        }
    }
    layout.count = lines[1][0] ? 2 : lines[0][0] ? 1 : 0;
    return layout;
}

static recent_title_layout_t recent_title_layout(const char *title,int width) { return recent_title_layout_role(title,width,UI_TEXT_BODY,1); }
static int draw_recent_title_layout(uint8_t *fb, const recent_title_layout_t *layout, int x, int y, bool centered) {
    for (int row = 0; row < layout->count; ++row)
        ui_text_fixed_vc(fb, x, y+row*layout->line_height, layout->px, layout->lines[row],
                         centered ? EPD_DRAW_ALIGN_CENTER : EPD_DRAW_ALIGN_LEFT, false);
    return y+(layout->count>0?layout->count-1:0)*layout->line_height+layout->line_height/2;
}

static int draw_recent_title(uint8_t *fb, const char *title, int x, int y, int width, bool centered) {
    recent_title_layout_t layout = recent_title_layout(title, width);
    return draw_recent_title_layout(fb, &layout, x, y, centered);
}

// 图书和文档共用四像素极简进度条，读取真实进度并限制填充范围。
// Books and documents share a four-pixel progress bar with bounded real-progress fill.
static void draw_recent_progress(uint8_t *fb, int x, int y, int width, unsigned pct) {
    EpdRect track = {x, y, width, 4};
    epd_fill_rect(track, 0xc0, fb);
    if (pct > 100) pct = 100;
    track.width = (width * pct + 99) / 100;
    if (track.width > 0) epd_fill_rect(track, UI_GRAY_BLACK, fb);
}

static void draw_cover(uint8_t *fb, const home_book_t *item, EpdRect box) {
    epd_fill_rect(box, UI_GRAY_LIGHT, fb);
    if (item->cover) {
        // 封面缓冲固定 176×240，而卡片未必同比例；按长边铺满再居中裁剪，避免拉伸变形。
        // The cover buffer is fixed at 176x240 while the card is a different shape; fill by the
        // longer side and centre-crop so the artwork is never stretched.
        const unsigned box_width = (unsigned)box.width, box_height = (unsigned)box.height;
        const book_crop_t crop = book_cover_crop(BOOK_COVER_W, BOOK_COVER_H, box_width, box_height);
        for (int y = 0; y < box.height; ++y)
            for (int x = 0; x < box.width; ++x) {
                int sx = (int)(crop.x + (uint64_t)(unsigned)x * crop.width / box_width);
                int sy = (int)(crop.y + (uint64_t)(unsigned)y * crop.height / box_height);
                const uint8_t gray = ui_gray_level(item->cover[sy * BOOK_COVER_W + sx]);
                epd_draw_pixel(box.x + x, box.y + y,
                               ui_image_dither_gray(gray, box.x + x, box.y + y), fb);
            }
    } else {
        char title[128]; snprintf(title, sizeof(title), "%s", item->title);
        fit(title, 24, box.width - 16);
        ui_text_vc(fb, box.x + box.width / 2, box.y + box.height / 2, 24, title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, box, 0, UI_GRAY_BLACK);
}

// 周一为第一列，末行是当前周；未来日期不索引历史数组。
// Monday starts each row; the last row is the current week. Future cells have no history index.
static int home_heatmap_history_index(uint32_t end_day, int row, int column) {
    if (!end_day || row < 0 || row >= 4 || column < 0 || column >= 7) return -1;
    int today_column = (end_day + 3) % 7; // Unix day zero was Thursday.
    int ago = 21 + today_column - (row * 7 + column);
    return ago < 0 ? -1 : 29 - ago;
}

// 使用面板的离散灰阶：只要读过就有填色，达到120分钟才到最深灰。
// Quantize to panel gray levels: any measured time gets a fill; deepest gray starts at 120 minutes.
static uint8_t home_heatmap_gray(uint32_t seconds) {
    if (!seconds) return 0xff;
    if (seconds >= 7200) return 0x33;
    unsigned steps = 1 + (seconds - 1) * 11 / 7199;
    return (uint8_t)((15 - steps) * 17);
}

static const char *const home_months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// 月初按格子的实际日历日期识别，包括本周尚未来到的日期。/ Detect first-of-month cells by calendar date, including upcoming weekdays.
static int home_heatmap_cell_month(uint32_t end_day, int row, int col) {
    if (!end_day || row < 0 || row >= 4 || col < 0 || col >= 7) return -1;
    int64_t day = (int64_t)end_day - 21 - (end_day + 3) % 7 + row * 7 + col;
    if (day < 0) return -1;
    time_t seconds = (time_t)(day * 86400); struct tm date;
    return gmtime_r(&seconds, &date) && date.tm_mday == 1 ? date.tm_mon : -1;
}
static int home_heatmap_row_month(uint32_t end_day, int row) {
    for (int col = 0; col < 7; ++col) {
        int month=home_heatmap_cell_month(end_day,row,col);
        if(month>=0)return month;
    }
    return -1;
}

// 日期与热力图共用UTC+8日快照；格言独立于状态栏签名，长按使用共用输入界面。
// Date and heatmap share the UTC+8 day snapshot; the independent quote opens the common editor on hold.
static uint8_t *s_quote_bitmap;
static void clear_quote_bitmap(void){free(s_quote_bitmap);s_quote_bitmap=NULL;}
static void prepare_quote_bitmap(void){
    clear_quote_bitmap();bool fits;
    (void)ui_banner_preview(content_home_quote(),content_home_quote_font(),content_home_quote_size(),content_home_quote_alignment(),
        s_reading_end_day,s_reading_days_valid,&s_quote_bitmap,&fits);
}
static void home_exit(app_ctx_t *ctx){(void)ctx;clear_quote_bitmap();}

static void draw_home_banner(uint8_t *fb) {
    if(s_quote_bitmap){ui_bitmap_draw(fb,(EpdRect){0,80,UI_LOCK_WIDTH,300},s_quote_bitmap);return;}
    char date[160];ui_banner_date_line(s_reading_end_day,s_reading_days_valid,date,sizeof(date));
    ui_banner_positions_t p=ui_banner_positions(date);
    ui_text_fixed_ink_top(fb,342,p.date_top,p.date_px,date,EPD_DRAW_ALIGN_CENTER);
    ui_banner_brand_draw(fb,p);
    ui_banner_quote_draw(fb,content_home_quote(),false,p);
}

static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    ui_clear_page(fb);
    // 最近阅读分左右列，图书封面窄列、文档列表宽列。/ Recent reads use a narrow cover column and a wide document column.
    epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    draw_home_banner(fb);
    home_geometry_t g=home_geometry();
    epd_fill_rect((EpdRect){36,g.recent_rule,612,2},0x68,fb);
    ui_text_vc(fb,36,g.recent_title,UI_TEXT_TITLE,"最近阅读",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_vc(fb,648,g.recent_title,UI_TEXT_CAPTION,"markdown文档",EPD_DRAW_ALIGN_RIGHT,false);
    home_book_t *featured=s_current.path[0]?&s_current:&s_recent[0];
    if(featured->path[0]) {
        unsigned pct=featured->has_progress?featured->progress.pct:0;if(pct>100)pct=100;
        recent_title_layout_t title=recent_title_layout(featured->title,172);
        char detail[48];snprintf(detail,sizeof(detail),"阅读进度%u%%",pct);
        recent_title_layout_t caption=recent_title_layout_role(detail,172,UI_TEXT_CAPTION,2);
        int title_h=title.count*title.line_height;
        int caption_h=caption.count*caption.line_height;
        int cover_y=g.content_y+24;
        int cover_h=g.content_end-cover_y-title_h-caption_h-30;
        if(cover_h>234)cover_h=234;
        if(cover_h<48)cover_h=48;
        int cover_w=cover_h*176/240;if(cover_w>172)cover_w=172;
        draw_cover(fb,featured,(EpdRect){36+(172-cover_w)/2,cover_y,cover_w,cover_h});
        draw_recent_progress(fb,36,cover_y+cover_h+8,172,pct);
        int title_y=cover_y+cover_h+20;
        draw_recent_title_layout(fb,&title,122,title_y+title.line_height/2,true);
        draw_recent_title_layout(fb,&caption,122,title_y+title_h+10+caption.line_height/2,true);
    } else draw_recent_title(fb,"暂无阅读图书",122,g.content_y+(g.content_end-g.content_y)/2,172,true);
    // 竖线起点随标题的实际墨迹上沿对齐。/ Align the divider start with the title's actual ink top.
    int title_above=0,title_below=0;
    ui_text_fixed_measure_px(ui_text_effective_px(UI_TEXT_TITLE),"最近阅读",&title_above,&title_below);
    int divider_top=g.recent_title+(title_above-title_below)/2-title_above;
    epd_fill_rect((EpdRect){240,divider_top,2,g.content_y+g.rows*g.row_height+1-divider_top},0x68,fb);
    for(int i=0;i<=g.rows;++i)epd_fill_rect((EpdRect){HOME_DOCUMENT_X,g.content_y+i*g.row_height,HOME_DOCUMENT_WIDTH,1},0x88,fb);
    for(size_t i=0;i<s_recent_document_count && i<(size_t)g.rows;++i) {
        int y=g.content_y+(int)i*g.row_height;char name[256];copy_title(name,sizeof(name),s_recent_documents[i].name);
        size_t n=strlen(name);if(n>=3)name[n-3]=0;
        recent_title_layout_t layout=recent_title_layout_role(name,HOME_DOCUMENT_WIDTH-36,UI_TEXT_BODY,1);
        int title_h=layout.count*layout.line_height;
        int top=y+(g.row_height-title_h-10)/2;
        draw_recent_title_layout(fb,&layout,HOME_DOCUMENT_X,top+layout.line_height/2,false);
        draw_recent_progress(fb,HOME_DOCUMENT_X,top+title_h+6,HOME_DOCUMENT_WIDTH-36,s_recent_documents[i].pct);
        ui_text_vc(fb,646,y+g.row_height/2,UI_TEXT_BODY,"›",EPD_DRAW_ALIGN_RIGHT,false);
    }
    if(!s_recent_document_count)draw_recent_title(fb,"暂无阅读文档",HOME_DOCUMENT_X,g.content_y+g.row_height/2,HOME_DOCUMENT_WIDTH-36,false);
    epd_fill_rect((EpdRect){36,380,612,2},0x68,fb);
    ui_text_vc(fb,36,g.heat_title,UI_TEXT_TITLE,"阅读记录",EPD_DRAW_ALIGN_LEFT,false);
    uint64_t sum = 0;
    if (s_reading_days_valid) {
        // 只汇总当前周（末行）从周一到今天，保留四周热力图。
        // Sum only the current week's Monday-to-today cells; keep the four-week heatmap.
        for (int col = 0; col < 7; ++col) {
            int index = home_heatmap_history_index(s_reading_end_day, 3, col);
            if (index >= 0) sum += s_reading_days[index];
        }
    }
    char value[64];
    snprintf(value, sizeof(value), "本周已读%llu小时%llu分",
             (unsigned long long)(sum / 3600), (unsigned long long)(sum / 60 % 60));
    int week_px=ui_text_effective_px(UI_TEXT_CAPTION);
    ui_text_fixed_vc(fb, 648, g.heat_title, week_px, s_reading_days_valid ? value : "未设置日期", EPD_DRAW_ALIGN_RIGHT, false);
    if (!s_reading_days_valid) {
        ui_text_vc(fb, 342, 582, 22, "完成日期与时间设置后显示阅读记录", EPD_DRAW_ALIGN_CENTER, false);
    } else {
        static const char *const weekdays[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
        int calendar_px=34;
        for (int col = 0; col < 7; ++col)
            ui_text_fixed_vc(fb, g.col_x + col * g.col_step, g.week_y, calendar_px, weekdays[col], EPD_DRAW_ALIGN_CENTER, false);
        for (int row = 0; row < 4; ++row) {
            int month = home_heatmap_row_month(s_reading_end_day, row);
            if (month >= 0) ui_text_fixed_vc(fb, g.month_x, g.grid_y+22+row*g.row_step, calendar_px, home_months[month], EPD_DRAW_ALIGN_RIGHT, false);
            for (int col = 0; col < 7; ++col) {
                EpdRect cell = {g.col_x-22+col*g.col_step, g.grid_y+row*g.row_step, 44, 44};
                int index = home_heatmap_history_index(s_reading_end_day, row, col);
                uint8_t gray=index>=0?home_heatmap_gray(s_reading_days[index]):0xff;
                if (index < 0 || !s_reading_days[index]) {
                    // 未阅读与未来日期统一浅框白底，短路条件避免索引未来历史。/ Unread and future dates share a faint white cell; short-circuit before indexing future history.
                    epd_fill_rect(cell, 0xdd, fb);
                    epd_fill_rect(ui_inset_rect(cell, 2), UI_GRAY_WHITE, fb);
                } else {
                    epd_fill_rect(cell, gray, fb);
                }
                if(home_heatmap_cell_month(s_reading_end_day,row,col)>=0){
                    // 右上四分之一放月初数字；深于中灰时反白，与真实格子底色混合。/ Place the month-start digit in the upper-right quarter; reverse below mid-gray and blend against the actual fill.
                    ui_text_fixed_vc_gray(fb,cell.x+33,cell.y+11,24,"1",EPD_DRAW_ALIGN_CENTER,
                        gray<0x88?UI_INK_WHITE:UI_INK_BLACK,gray>>4);
                }
                if (index == 29) epd_fill_rect((EpdRect){cell.x + 14, cell.y + 48, 16, 3}, UI_GRAY_BLACK, fb);
            }
        }
    }
    ui_nav_draw(fb, 0);
}

static bool present(app_ctx_t *ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE) return true;
    render(ctx, ctx->fb);
    if (redraw == APP_REDRAW_AREA) guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_area));
    else if (redraw == APP_REDRAW_FULL || s_home_force_full_once) {
        s_home_force_full_once = false;
        guard_draw_result(ctx->hl, update_display_home_clean(ctx->hl));
    }
    else guard_draw_result(ctx->hl, update_display_fast_page(ctx->hl));
    return true;
}

static void on_enter(app_ctx_t *ctx) {
    (void)ctx;
    refresh_recent_documents();
    s_home_force_full_once = app_settings_home_full_refresh();
    s_reading_days_valid = book_ticket_recent_days(s_reading_days, &s_reading_end_day);
    prepare_quote_bitmap();
    read_pico_sd_info_t sd = {0};
    esp_err_t sd_err = read_pico_sd_get_info(&sd);
    bool cache_matches = s_books_cache_valid && sd_err != ESP_ERR_NOT_FINISHED &&
                         s_cached_store_revision == book_store_revision() &&
                         s_cached_sd_present == sd.present && s_cached_sd_mounted == sd.mounted &&
                         !strcmp(s_cached_books_dir, app_settings_books_dir());
    char last_path[BOOK_STORE_PATH_MAX] = {0};
    (void)book_progress_last_path(last_path, sizeof(last_path));
    bool last_changed = strcmp(last_path, s_cached_last_path) != 0;
    if (cache_matches && !last_changed) {
        refresh_progress();
        s_cover_next = 0;
        s_scan_pending = false;
        return;
    }
    memset(&s_current, 0, sizeof(s_current));
    memset(s_recent, 0, sizeof(s_recent));
    s_cover_next = 0;
    // 阅读记录换书时重建续读选择；TF 卡状态没变则无需再次探测。
    // Rebuild the featured book when the latest read changes without probing an unchanged card.
    s_scan_pending = !cache_matches;
    if (s_scan_pending) read_pico_sd_start_probe();
    scan_books();
    restore_cover_cache();
    if (cache_matches || sd_err != ESP_ERR_NOT_FINISHED) remember_scan_state(&sd);
}
static void on_media_lost(app_ctx_t *ctx) {
    (void)ctx;clear_quote_bitmap();
    clear_cover_cache();
    memset(&s_current, 0, sizeof(s_current));
    memset(s_recent, 0, sizeof(s_recent));
    s_books_cache_valid = false;
    s_recent_document_count = 0;
    memset(s_recent_documents, 0, sizeof(s_recent_documents));
}
static void on_media_ready(app_ctx_t *ctx) {
    (void)ctx;prepare_quote_bitmap();
    s_books_cache_valid = false;
    s_scan_pending = true;
}

static app_redraw_t on_tick(app_ctx_t *ctx) {
    if (ctx->touch && ctx->touch->touched) return APP_REDRAW_NONE;
    if (s_scan_pending) {
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_scan_pending = false;
        if (info.mounted) {
            memset(&s_current, 0, sizeof(s_current));
            memset(s_recent, 0, sizeof(s_recent));
            s_cover_next = 0;
            scan_books();
            refresh_recent_documents();
            restore_cover_cache();
            remember_scan_state(&info);
            return APP_REDRAW_PAGE;
        }
        remember_scan_state(&info);
    }
    bool cover_changed = false;
    if (!s_cover_next) {
        s_cover_next = 1;
        int slot = s_current.path[0] ? 0 : 1;
        home_book_t *item = book_at(slot);
        if (item->path[0] && !item->cover) cover_changed = load_cover(item, slot);
    }
    // 只更新续读卡片，不再逐本刷新首页。/ Update only the current-read card, never flash covers one by one.
    if (cover_changed) {
        render(ctx, ctx->fb);
        int top=home_geometry().content_y;s_area=(EpdRect){36,top,612,UI_NAV_TOP-top};
        return APP_REDRAW_AREA;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type == UI_GESTURE_LONG_PRESS && ev->x0 >= 36 && ev->x0 < 648 && ev->y0 >= 80 && ev->y0 < 370) {
        app_device_settings_request_home_quote(); ui_nav_request(ctx, 3); return APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    home_geometry_t g=home_geometry();
    if (ev->x0 >= HOME_DOCUMENT_X && ev->x0 < 648 && ev->y0 >= g.content_y &&
        ev->y0 < g.content_y + g.rows*g.row_height) {
        size_t i = (ev->y0-g.content_y)/g.row_height;
        if (i < s_recent_document_count && app_book_request_open_from_home(s_recent_documents[i].path))
            ui_nav_request(ctx, 1);
        return APP_REDRAW_NONE;
    }
    if (ev->x0 >= 36 && ev->x0 < 208 && ev->y0 >= g.content_y && ev->y0 < UI_NAV_TOP) {
        home_book_t *featured = s_current.path[0] ? &s_current : &s_recent[0];
        if (featured->path[0] && app_book_request_open_from_home(featured->path)) ui_nav_request(ctx, 1);
        return APP_REDRAW_NONE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    // 首页是导航根页，返回停留在首页。/ Home is the navigation root; Back stays here.
    (void)ctx;
    (void)key;
    return APP_REDRAW_NONE;
}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }
static EpdRect area_hint(app_ctx_t *ctx) { (void)ctx; return s_area; }

const app_desc_t app_dashboard = {
    .title = "首页 Home", .detail = "最近阅读与四周阅读记录", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .on_enter = on_enter, .on_exit=home_exit, .on_media_lost = on_media_lost, .on_media_ready = on_media_ready,
    .render = render, .present = present, .on_tick = on_tick,
    .on_gesture = on_gesture, .on_key = on_key, .area_hint = area_hint,
};
