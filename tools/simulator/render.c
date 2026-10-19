/* SPDX-License-Identifier: Apache-2.0
 * 中文：宿主屏幕原型，复用固件真实字体、HTML块及阅读分页。
 * English: Host screen prototype using production fonts, text blocks and pagination.
 * 冻结：模拟云目录，不联网；输出不是可烧录固件。
 * Frozen: Cloud entries are fixtures, never network data or flashable firmware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "book_layout.h"
#include "html_text.h"
#include "markdown_text.h"
#include "ttf_font.h"
#include "ui_nav.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_power_dialog.h"
#include "book_ticket.h"
#include "ui_wallpaper.h"
#include "app_font_context.h"
extern const uint8_t simulator_lock[] asm("_binary_lock_4bpp_bin_start");
extern const uint8_t simulator_loading[] asm("_binary_loading_4bpp_bin_start");
#include "settings.h"
#include "cloud_config.h"
extern const cloud_config_t *simulator_cloud_backup_restored(void);
extern bool simulator_cloud_backup_used(void);
#define W 684
#define H 1216
int simulator_settings(uint8_t *fb,int page,int scroll,const char *events);
void simulator_cloud_load(const char *file);
extern bool app_device_settings_cloud_requested(void);
extern bool app_device_settings_home_quote_requested(void);
extern bool app_device_settings_quote_committed(void);
extern int app_device_settings_quote_size(void);
int simulator_banner_draft(unsigned part);
const char *simulator_banner_text(void),*simulator_banner_font(void),*simulator_banner_image(void),*simulator_banner_item(unsigned i);
void simulator_cloud(uint8_t *fb,const char *file,int page,const char *events);
extern int simulator_cloud_command,simulator_cloud_index,simulator_cloud_page;
void simulator_dashboard(uint8_t *fb,const uint32_t *days,const char *events);
int simulator_home_geometry(int item);
int simulator_history_state(unsigned part);
unsigned long long simulator_history_seconds(unsigned row);
int simulator_home_cover_geometry(unsigned part);
int simulator_settings_geometry(int item);
int simulator_settings_update_state(void);
int simulator_settings_update_confirmation(void);
int simulator_document_rows(void);
const char *simulator_document_page_first(void);
int simulator_document_row_height(void);
int simulator_management_rows(void);
void simulator_files(uint8_t *fb,int view,int folder,const char *dir,const char *events);
const char *simulator_book_path(void);
bool simulator_document_zoom(void);
bool simulator_document_image_ready(void);
unsigned simulator_reader_progress(void);
bool simulator_markdown_committed(void);
unsigned simulator_markdown_size(unsigned index);
unsigned simulator_document_image_top(void);
size_t simulator_document_count(void);
bool simulator_document_starred_only(void),simulator_document_menu(void),simulator_document_delete_confirm(void);
const char *simulator_document_first(void);
const char *simulator_document_item(unsigned i),*simulator_shelf_item(unsigned i);
int simulator_shelf_geometry(unsigned row,unsigned part);
bool simulator_content_sort_open(void),simulator_document_batch_confirm(void);
unsigned simulator_document_selected_count(void),simulator_content_sort_mode(bool documents);
#include "book_source.h"
#include "document_store.h"
#include "content_library.h"
#include "ui_font.h"
int simulator_transfer(uint8_t *fb,int view,int mode,const char *events);
size_t simulator_book(uint8_t *fb,int view,int panel,int page,int px,const char *path,const char *events);
void simulator_image(uint8_t *fb,int viewing,const char *events);
static uint8_t fb[W * H / 2];
int simulator_route=-1;
int simulator_transfer_mode;
int simulator_transfer_entry(void);
int simulator_files_view=-1,simulator_files_folder=-1,simulator_image_view=-1;
extern char simulator_files_dir[288];
int simulator_book_view=-1,simulator_reader_panel,simulator_reader_page,simulator_reader_px;
bool simulator_reader_fullscreen,simulator_toc_jump;
static int simulator_system_view=-1;
static app_redraw_t simulator_system_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e){
 if(simulator_system_view==1){ui_power_action_t a=ui_power_dialog_handle(e);if(a==UI_POWER_ACTION_CANCEL)simulator_system_view=0;else if(a==UI_POWER_ACTION_SHUTDOWN)simulator_system_view=2;else if(a==UI_POWER_ACTION_RESTART)simulator_system_view=3;}
 else if(simulator_system_view==4&&e->type==UI_GESTURE_TAP){int hit=ui_menu_hit_test(e->x0,e->y0,0);if(hit>=0)simulator_route=hit;}
 return APP_REDRAW_PAGE;
}
static app_redraw_t simulator_system_key(app_ctx_t *ctx,int key){simulator_route=0;return APP_REDRAW_PAGE;}
#include "replay.h"
static void text(int x, int y, int px, const char *s, uint8_t fg, enum EpdFontFlags align) {
    ttf_draw_text_px(fb, x, y + ttf_ascender_px(px), px, s, align, fg, 15);
}
static void line(int y) { epd_fill_rect((EpdRect){36, y, 612, 2}, 0x90, fb); }
static void rounded(int x, int y, int w, int h, int r, uint8_t gray) {
    for (int py = 0; py < h; ++py) for (int p = 0; p < w; ++p) {
        int dx = p < r ? r - p : p >= w - r ? p - (w - r - 1) : 0;
        int dy = py < r ? r - py : py >= h - r ? py - (h - r - 1) : 0;
        if (dx * dx + dy * dy <= r * r) epd_draw_pixel(x + p, y + py, gray, fb);
    }
}
static void outlined(int x, int y, int w, int h, int r) {
    rounded(x, y, w, h, r, 0x30);
    rounded(x + 2, y + 2, w - 4, h - 4, r - 2, 0xf0);
}
static void ellipsis(char *s, int px, int max_width) {
    if (ttf_text_width_px(px, s) <= max_width) return;
    size_t n = strlen(s);
    while (n && ttf_text_width_px(px, s) + ttf_text_width_px(px, "...") > max_width) {
        --n;
        while (n && ((unsigned char)s[n] & 0xc0) == 0x80) --n;
        s[n] = 0;
    }
    strcat(s, "...");
}
static void nav(int active) {
    ui_nav_draw(fb,active);
}
static void header(const char *title) {
    text(36, 28, 25, "PICO", 0, EPD_DRAW_ALIGN_LEFT);
    text(648, 28, 21, "0.3.3-rc58", 5, EPD_DRAW_ALIGN_RIGHT);
    text(36, 92, 49, title, 0, EPD_DRAW_ALIGN_LEFT);
    line(161);
}
static void list_titles(const char *path, char items[8][260], int *count) {
    *count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (*count < 8 && fgets(items[*count], 256, f)) {
        items[*count][strcspn(items[*count], "\r\n")] = 0;
        if (items[*count][0]) ++*count;
    }
    fclose(f);
}
static void parse_reading_days(const char *values, uint32_t days[30]) {
    uint32_t parsed[30] = {0};
    int count_days = 0;
    memset(days, 0, 30 * sizeof(*days));
    const char *cursor = values;
    while (cursor && *cursor && count_days < 30) {
        char *end = NULL;
        unsigned long value = strtoul(cursor, &end, 10);
        if (end == cursor) break;
        parsed[count_days++] = (uint32_t)(value > UINT32_MAX ? UINT32_MAX : value);
        cursor = *end == ',' ? end + 1 : NULL;
    }
    memcpy(days + 30 - count_days, parsed, count_days * sizeof(*days));
}
static void home(char items[8][260], int count, const char *days) {
    header("首页");
    text(36, 190, 29, "继续阅读", 0, EPD_DRAW_ALIGN_LEFT);
    outlined(36, 248, 172, 252, 12);
    text(122, 314, 27, "PICO", 0, EPD_DRAW_ALIGN_CENTER);
    text(122, 368, 27, "阅读", 0, EPD_DRAW_ALIGN_CENTER);
    text(122, 458, 19, "Markdown", 5, EPD_DRAW_ALIGN_CENTER);
    char title[264];
    snprintf(title, sizeof(title), "%s", count ? items[0] : "阅读示例");
    ellipsis(title, 36, 386);
    text(244, 263, 36, title, 0, EPD_DRAW_ALIGN_LEFT);
    text(244, 329, 24, "离线阅读", 5, EPD_DRAW_ALIGN_LEFT);
    text(244, 408, 24, "已读 0%", 5, EPD_DRAW_ALIGN_LEFT);
    rounded(244, 450, 386, 7, 3, 0xc0);
    text(244, 484, 26, "继续阅读 >", 0, EPD_DRAW_ALIGN_LEFT);
    line(555);
    text(36, 579, 29, "近7天阅读", 0, EPD_DRAW_ALIGN_LEFT);
    uint32_t history[30];parse_reading_days(days,history);
    const uint32_t *seconds=history+23;
    unsigned long long total = 0;
    for (int i = 0; i < 7; ++i) total += seconds[i];
    char label[64];
    snprintf(label, sizeof(label), "%llu min", total / 60);
    text(648, 583, 23, label, 5, EPD_DRAW_ALIGN_RIGHT);
    const char *names[] = {"日", "一", "二", "三", "四", "五", "六"};
    time_t now = time(NULL) + 8 * 60 * 60;
    struct tm calendar; gmtime_r(&now, &calendar);
    for (int i = 0; i < 7; ++i) {
        int gray = 15 - (int)((seconds[i] > 7200 ? 7200 : seconds[i]) * 15 / 7200);
        outlined(36 + i * 89, 641, 76, 76, 15);
        rounded(38 + i * 89, 643, 72, 72, 13, (uint8_t)(gray << 4));
        text(74 + i * 89, 732, 22, names[(calendar.tm_wday + i + 1) % 7], 5, EPD_DRAW_ALIGN_CENTER);
    }
    text(36, 914, 29, "最近同步", 0, EPD_DRAW_ALIGN_LEFT);
    if (!count) text(36, 968, 23, "还没有同步文档", 6, EPD_DRAW_ALIGN_LEFT);
    for (int i = 0; i < count && i < 3; ++i) {
        snprintf(title, sizeof(title), "%s", items[i]);
        ellipsis(title, 27, 540);
        text(36, 965 + i * 43, 27, title, 0, EPD_DRAW_ALIGN_LEFT);
        text(648, 967 + i * 43, 22, ">", 5, EPD_DRAW_ALIGN_RIGHT);
    }
    nav(0);
}
static void listing(const char *screen, char items[8][260], int count, unsigned mask) {
    int cloud = !strcmp(screen, "cloud");
    header(cloud ? "坚果云同步" : "书架");
    text(36, 193, 24, cloud ? "示例目录 · 多选下载" : "本地文档 · 离线阅读", 5, EPD_DRAW_ALIGN_LEFT);
    for (int i = 0; i < count; ++i) {
        int y = 270 + i * 88;
        char title[264]; snprintf(title, sizeof(title), "%s", items[i]);
        ellipsis(title, 30, cloud ? 504 : 565);
        if (cloud) {
            outlined(38, y + 4, 32, 32, 6);
            if (mask & (1u << i)) rounded(44, y + 10, 20, 20, 3, 0x00);
        }
        text(cloud ? 92 : 36, y, 30, title, 0, EPD_DRAW_ALIGN_LEFT);
        text(cloud ? 92 : 36, y + 39, 20, "Markdown", 6, EPD_DRAW_ALIGN_LEFT);
        line(y + 74);
    }
    if (cloud) {
        rounded(36, 982, 612, 86, 18, 0x00);
        unsigned selected = 0;
        for (int i = 0; i < count; ++i) selected += !!(mask & (1u << i));
        char label[80]; snprintf(label, sizeof(label), "下载选中文档 (%u)", selected);
        ttf_draw_text_px(fb, 342, 1008 + ttf_ascender_px(31), 31, label, EPD_DRAW_ALIGN_CENTER, 15, 0);
    }
    nav(1);
}
static int reading(const char *path, int wanted, int px, size_t *pages) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 1; }
    long n = ftell(f);
    if (n < 0 || n > 262144) { fclose(f); return 1; }
    rewind(f);
    char *source = calloc(1, (size_t)n + 1);
    if (!source) { fclose(f); return 1; }
    size_t read = fread(source, 1, (size_t)n, f); fclose(f);
    if (read != (size_t)n) { free(source); return 1; }
    html_text_t parsed = {0};
    esp_err_t err = markdown_to_blocks(source, read, &parsed);
    free(source);
    if (err != ESP_OK) return 1;
    uint8_t sizes[4];content_markdown_sizes(sizes);
    book_layout_set_markdown(true,sizes+1,parsed.styles,parsed.style_count);
    book_layout_set_markdown_tables(parsed.cells, parsed.cell_count);
    book_layout_set_first_line_indent(0);
    book_layout_set_spacing(150, 60);
    EpdRect rect = {42, 175, 600, 892};
    if (!book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count, rect, px)) {
        html_text_free(&parsed); return 1;
    }
    *pages = book_layout_page_count();
    size_t page = wanted < 0 ? 0 : (size_t)wanted >= *pages ? *pages - 1 : (size_t)wanted;
    text(36, 42, 28, "阅读", 0, EPD_DRAW_ALIGN_LEFT);
    line(119);
    book_layout_draw_page(fb, page, rect, px);
    char position[64]; snprintf(position, sizeof(position), "%zu / %zu", page + 1, *pages);
    line(1108);
    text(342, 1143, 26, position, 5, EPD_DRAW_ALIGN_CENTER);
    book_layout_free(); book_layout_set_markdown(false,NULL,NULL,0); html_text_free(&parsed);
    return 0;
}
static void json_string(const char *s){putchar('"');for(;*s;++s){unsigned char c=*s;if(c=='"'||c=='\\')putchar('\\');if(c<32)printf("\\u%04x",c);else putchar(c);}putchar('"');}
int main(int argc, char **argv) {
    if (argc < 10||argc>12) { fprintf(stderr, "render screen book page px frame list mask days font [events]\n"); return 2; }
    memset(fb, 0xff, sizeof(fb));
    app_settings_init();
    if (ttf_font_open(argv[9]) != ESP_OK) return 3;
    app_font_activate_system();
    int px = atoi(argv[4]);
    if (px < 28 || px > 72) return 2;
    char items[8][260]; int count;
    list_titles(argv[6], items, &count);
    size_t pages = 1;
    int settings_page=-1;
    int transfer_view=-1;
    int err = 0;
    bool font_probe=!strcmp(argv[1],"font-probe");
    bool font_text_covered=font_probe&&ttf_font_has_text(argv[2]);
    bool builtin_text_covered=font_probe&&ui_font_has_text(argv[2]);
    if (!strcmp(argv[1], "home")) home(items, count, argv[8]);
    else if(font_probe){header("字体覆盖验证");text(36,230,28,argv[2],0,EPD_DRAW_ALIGN_LEFT);nav(3);}
    else if (!strcmp(argv[1], "cloud") || !strcmp(argv[1], "shelf")) listing(argv[1], items, count, (unsigned)strtoul(argv[7], NULL, 10));
    else if (!strcmp(argv[1],"markdown-probe")) {if(document_store_validate(argv[2])!=ESP_OK)err=1;else err=reading(argv[2],atoi(argv[3]),px,&pages);}
    else if (!strcmp(argv[1], "reading")) err = reading(argv[2], atoi(argv[3]), px, &pages);
    else if(!strcmp(argv[1],"baseline-home")) {
        uint32_t days[30];parse_reading_days(argv[8],days);
        simulator_dashboard(fb,days,argc>=11?argv[10]:"");
    }
    else if(!strcmp(argv[1],"cloud-0"))simulator_cloud(fb,argv[2],atoi(argv[3]),argc>=11?argv[10]:"");
    else if(!strncmp(argv[1],"settings",8)){simulator_cloud_load(argv[2]);settings_page=simulator_settings(fb,argv[1][8]=='-'?atoi(argv[1]+9):0,atoi(argv[3]),argc>=11?argv[10]:"");}
    else if(!strncmp(argv[1],"files-",6)) simulator_files(fb,atoi(argv[1]+6),atoi(argv[3]),argc==12?argv[11]:"",argc>=11?argv[10]:"");
    else if(!strncmp(argv[1],"transfer-",9))transfer_view=simulator_transfer(fb,atoi(argv[1]+9),atoi(argv[3]),argc>=11?argv[10]:"");
    else if(!strncmp(argv[1],"book-",5)){pages=simulator_book(fb,atoi(argv[1]+5),atoi(argv[7]),atoi(argv[3]),atoi(argv[4]),argv[2],argc>=11?argv[10]:"");if(!pages)err=1;}
    else if(!strncmp(argv[1],"image-",6))simulator_image(fb,atoi(argv[1]+6),argc>=11?argv[10]:"");
    else if(!strcmp(argv[1],"menu")){simulator_system_view=4;app_ctx_t ctx={.fb=fb};simulator_replay(&ctx,simulator_system_gesture,simulator_system_key,argc>=11?argv[10]:"");ui_draw_menu_page(fb,NULL,0);}
    else if(!strcmp(argv[1],"power")){simulator_system_view=1;FILE *base=fopen("build/simulator/power-background.bin","rb");if(base){if(fread(fb,1,sizeof(fb),base)!=sizeof(fb))memset(fb,255,sizeof(fb));fclose(base);}else simulator_dashboard(fb,(uint32_t[30]){0},"");app_ctx_t ctx={.fb=fb};simulator_replay(&ctx,simulator_system_gesture,simulator_system_key,argc>=11?argv[10]:"");if(simulator_system_view==1)ui_power_dialog_draw(fb);else if(simulator_system_view>=2)ui_power_final_draw(fb,simulator_system_view==3);}
    else if(!strcmp(argv[1],"lock-default"))ui_draw_full_image(fb,simulator_lock);
    else if(!strcmp(argv[1],"loading"))ui_draw_full_image(fb,simulator_loading);

    else if(!strcmp(argv[1],"lock-wallpaper")){if(!ui_wallpaper_draw_lock(fb))ui_draw_full_image(fb,simulator_lock);}
    else if(!strcmp(argv[1],"shutdown"))ui_power_final_draw(fb,false);
    else if(!strcmp(argv[1],"restart"))ui_power_final_draw(fb,true);
    else { header("设置"); text(36, 230, 28, "本地阅读测试", 0, EPD_DRAW_ALIGN_LEFT); nav(3); }
    if (err) { fprintf(stderr, "text parsing or layout failed\n"); return 4; }
    FILE *out = fopen(argv[5], "wb");
    if (!out) return 5;
    int failed = fwrite(fb, 1, sizeof(fb), out) != sizeof(fb);
    if (fclose(out)) failed = 1;
    char active_font[TTF_FONT_PATH_MAX];snprintf(active_font,sizeof(active_font),"%s",ttf_font_path());
    bool active_builtin=ttf_font_is_builtin();
    ttf_font_unload();
    if (failed) return 5;
    printf("{\"pages\":%zu,\"width\":684,\"height\":1216,\"settings_page\":%d,\"transfer_view\":%d,\"route\":%d,\"book_view\":%d,\"reader_panel\":%d,\"reader_fullscreen\":%s,\"reader_page\":%d,\"reader_px\":%d,\"files_view\":%d,\"files_folder\":%d,\"image_view\":%d,\"book_path\":", pages,settings_page,transfer_view,simulator_route,simulator_book_view,simulator_reader_panel,simulator_reader_fullscreen?"true":"false",simulator_reader_page,simulator_reader_px,simulator_files_view,simulator_files_folder,simulator_image_view);
    json_string(simulator_book_path());
    extern int simulator_files_page(void);
    printf(",\"files_page\":%d",simulator_files_page());
    printf(",\"markdown_committed\":%s,\"markdown_sizes\":[%u,%u,%u,%u]",simulator_markdown_committed()?"true":"false",simulator_markdown_size(0),simulator_markdown_size(1),simulator_markdown_size(2),simulator_markdown_size(3));
    extern int simulator_reader_layout(void),simulator_reader_tap_layout(void),simulator_markdown_slider_y(unsigned);
    printf(",\"reader_layout\":%d,\"reader_tap_layout\":%d,\"markdown_slider_y\":[%d,%d,%d,%d],\"markdown_font_path\":",simulator_reader_layout(),simulator_reader_tap_layout(),simulator_markdown_slider_y(0),simulator_markdown_slider_y(1),simulator_markdown_slider_y(2),simulator_markdown_slider_y(3));json_string(content_markdown_font());
    printf(",\"quote_settings_committed\":%s,\"quote_size\":%d",app_device_settings_quote_committed()?"true":"false",app_device_settings_quote_size());printf(",\"home_quote_requested\":%s",app_device_settings_home_quote_requested()?"true":"false");printf(",\"cloud_settings_requested\":%s",app_device_settings_cloud_requested()?"true":"false");printf(",\"cloud_command\":%d,\"cloud_index\":%d,\"cloud_page\":%d",simulator_cloud_command,simulator_cloud_index,simulator_cloud_page);printf(",\"toc_jump\":%s,\"system_view\":%d,\"transfer_mode\":%d,\"transfer_entry\":%d,\"files_dir\":",simulator_toc_jump?"true":"false",simulator_system_view,simulator_transfer_mode,simulator_transfer_entry());json_string(simulator_files_dir);printf(",\"document_zoom\":%s,\"image_top\":%u,\"toc_count\":%zu,\"document_image_ready\":%s,\"reader_progress\":%u",simulator_document_zoom()?"true":"false",simulator_document_image_top(),book_navigation_count(),simulator_document_image_ready()?"true":"false",simulator_reader_progress());
    extern unsigned simulator_reader_binding(unsigned);
    extern unsigned simulator_reader_perf(unsigned);
    extern int simulator_toc_percent(void);
    extern bool simulator_toc_drag(void);
    printf(",\"reader_event_draws\":%u,\"reader_event_font_loads\":%u,\"reader_area_updates\":%u,\"reader_area_violations\":%u,\"toc_percent\":%d,\"toc_drag\":%s",simulator_reader_perf(0),simulator_reader_perf(1),simulator_reader_perf(2),simulator_reader_perf(3),simulator_toc_percent(),simulator_toc_drag()?"true":"false");
    extern int simulator_reader_progress_geometry(unsigned);
    printf(",\"reader_progress_geometry\":[%d,%d,%d]",simulator_reader_progress_geometry(0),simulator_reader_progress_geometry(1),simulator_reader_progress_geometry(2));
    extern int simulator_reader_margin(void),simulator_reader_control_geometry(unsigned);
    extern size_t simulator_reader_offset(void);
    extern unsigned simulator_reader_bookmark_count(void);printf(",\"reader_bookmark_count\":%u",simulator_reader_bookmark_count());
    printf(",\"reader_margin\":%d,\"reader_offset\":%zu,\"reader_controls_geometry\":[%d,%d,%d],\"reader_shortcuts\":[",simulator_reader_margin(),simulator_reader_offset(),simulator_reader_control_geometry(0),simulator_reader_control_geometry(1),simulator_reader_control_geometry(2));
    for(unsigned i=0;i<12;++i){if(i)printf(",");printf("%u",simulator_reader_binding(i));}printf("]");
    printf(",\"font_builtin\":%s,\"font_path\":",active_builtin?"true":"false");json_string(active_font);
    if(font_probe)printf(",\"font_text_covered\":%s,\"builtin_text_covered\":%s",font_text_covered?"true":"false",builtin_text_covered?"true":"false");
    printf(",\"ui_body_px\":%d,\"ui_title_px\":%d,\"ui_caption_px\":%d,\"ui_row_height\":%d,\"ui_detail_height\":%d,\"document_rows\":%d,\"document_row_step\":%d,\"management_rows\":%d,\"home_geometry\":[",ui_text_effective_px(UI_TEXT_BODY),ui_text_effective_px(UI_TEXT_TITLE),ui_text_effective_px(UI_TEXT_CAPTION),ui_list_row_height(false),ui_list_row_height(true),simulator_document_rows(),simulator_document_row_height(),simulator_management_rows());
    for(int i=0;i<12;++i){if(i)printf(",");printf("%d",simulator_home_geometry(i));}printf("]");
    printf(",\"update_state\":%d,\"update_confirmation\":%s",simulator_settings_update_state(),simulator_settings_update_confirmation()?"true":"false");
    printf(",\"settings_geometry\":[");for(int i=0;i<14;++i){if(i)printf(",");printf("%d",simulator_settings_geometry(i));}printf("]");
    printf(",\"document_page_first\":");json_string(simulator_document_page_first());
    printf(",\"wallpaper_rotating\":%s,\"wallpaper_path\":",app_settings_wallpaper_rotating()?"true":"false");json_string(app_settings_wallpaper_path());
    printf(",\"wallpaper_last\":");json_string(app_settings_wallpaper_last());
    printf(",\"system_size\":%u,\"lock_style\":%u",app_settings_system_font_size(),app_settings_lock_style());
    printf(",\"idle_sleep_minutes\":%u",app_settings_idle_sleep_minutes());
    printf(",\"status_show_date\":%s,\"status_date_format\":%d",app_settings_status_show_date()?"true":"false",app_settings_status_date_format());
    printf(",\"system_font_path\":");json_string(app_settings_system_font_path());
    printf(",\"reading_font_path\":");json_string(app_settings_font_path());
    printf(",\"books_dir\":");json_string(app_settings_books_dir());
    printf(",\"fonts_dir\":");json_string(app_settings_fonts_dir());
    printf(",\"document_count\":%zu,\"document_starred_only\":%s,\"document_menu\":%s,\"document_delete_confirm\":%s,\"document_first\":",simulator_document_count(),simulator_document_starred_only()?"true":"false",simulator_document_menu()?"true":"false",simulator_document_delete_confirm()?"true":"false");json_string(simulator_document_first());
    const cloud_config_t *restored=simulator_cloud_backup_restored();
    printf(",\"content_sort_open\":%s,\"book_sort_mode\":%u,\"document_sort_mode\":%u,\"document_selected_count\":%u,\"document_batch_confirm\":%s,\"document_items\":[",simulator_content_sort_open()?"true":"false",simulator_content_sort_mode(false),simulator_content_sort_mode(true),simulator_document_selected_count(),simulator_document_batch_confirm()?"true":"false");
    for(unsigned i=0;i<3;++i){if(i)printf(",");json_string(simulator_document_item(i));}printf("],\"book_items\":[");
    for(unsigned i=0;i<3;++i){if(i)printf(",");json_string(simulator_shelf_item(i));}printf("]");
    printf(",\"banner_draft\":[");for(unsigned i=0;i<13;++i){if(i)printf(",");printf("%d",simulator_banner_draft(i));}printf("]");
    printf(",\"banner_items\":[");for(unsigned i=0;i<10;++i){if(i)printf(",");json_string(simulator_banner_item(i));}printf("]");
    printf(",\"banner_text\":");json_string(simulator_banner_text());printf(",\"banner_font\":");json_string(simulator_banner_font());printf(",\"banner_image\":");json_string(simulator_banner_image());
    printf(",\"history_state\":[");for(unsigned p=0;p<9;++p){if(p)printf(",");printf("%d",simulator_history_state(p));}printf("]");
    printf(",\"history_seconds\":[");for(unsigned p=0;p<10;++p){if(p)printf(",");printf("%llu",simulator_history_seconds(p));}printf("]");
    printf(",\"home_cover_geometry\":[");for(unsigned part=0;part<6;++part){if(part)printf(",");printf("%d",simulator_home_cover_geometry(part));}printf("]");
    printf(",\"shelf_geometry\":[");for(unsigned row=0;row<9;++row){if(row)printf(",");printf("[");for(unsigned part=0;part<14;++part){if(part)printf(",");printf("%d",simulator_shelf_geometry(row,part));}printf("]");}printf("]");
    printf(",\"cloud_backup_used\":%s",simulator_cloud_backup_used()?"true":"false");
    if(restored){printf(",\"cloud_restore\":{\"folder\":");json_string(restored->folder);printf(",\"account\":");json_string(restored->account);printf(",\"password\":");json_string(restored->password);printf("}");}
    puts("}");
    return 0;
}
