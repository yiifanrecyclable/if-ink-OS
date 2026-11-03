/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 自定义固件修改：文件页支持 Markdown 打开和关联附件清理。
 * Modified derivative: Markdown open/cleanup; remove root title and move BOOT to Settings.
 * 自定义固件修改：根页内容上移，升级入口迁移至设置。
 *
 * 中文：应用入口、TF 卡目录浏览及 USB/Wi-Fi 传输。
 * English: Applications hub, SD directory browser and USB/Wi-Fi transfer routes.
 */
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "app_config.h"
#include "app_content_open.h"
#include "app_transfer_mode.h"
#include "book_store.h"
#include "book_progress.h"
#include "document_store.h"
#include "book_title.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "file_tree.h"
#include "file_references.h"
#include "file_thumbnail.h"
#include "book_cover.h"
#include "read_pico_sd.h"
#include "read_pico_search.h"
#include "settings.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_nav.h"
#include "ui_wallpaper.h"
#include "ui_bitmap.h"
#include "app_wallpaper_picker.h"
#include "ui_menu.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_timer.h"
#ifndef PICO_HOST_SIMULATOR
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define FILE_MAX 96
typedef struct { char path[288]; char name[128]; off_t size; bool is_dir; } file_item_t;
// 文件列表放 PSRAM：内部 RAM 已被静态占用到约 90%，而 BLE 控制器只能用内部 RAM，
// 列表这类大数组没必要跟它抢。
// The file list lives in PSRAM. Internal RAM is already around 90% statically used and the BLE
// controller can only use internal RAM, so a list this big has no business holding any of it.
static file_item_t *s_files;

static bool files_alloc(void) {
    if (!s_files)
        s_files = heap_caps_calloc(FILE_MAX, sizeof(file_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_files != NULL;
}
static int s_count, s_folder, s_page;
static int s_requested_folder = -1;
static bool s_scan_pending;
static char s_message[192];
static int64_t s_now_ms;
static unsigned s_notice_serial;
static bool s_wallpaper_eligible;
static EpdRect s_status_area;
static int64_t file_now_ms(void){
#ifdef PICO_HOST_SIMULATOR
    return s_now_ms;
#else
    return esp_timer_get_time()/1000;
#endif
}
static void file_notice(const char *format,...){
    char text[384];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),ui_message(format),args);va_end(args);
    ui_snprintf(s_message,sizeof(s_message),"%s",text);
    ui_nav_notice_show(s_message,file_now_ms());++s_notice_serial;
}
static char s_dir[288] = "/sdcard";
static read_pico_sd_info_t s_sd;
typedef enum { FILE_VIEW_LIST, FILE_VIEW_ACTIONS, FILE_VIEW_RENAME, FILE_VIEW_MOVE, FILE_VIEW_WALLPAPER_PREVIEW } file_view_t;
static file_view_t s_view;
static bool s_picker,s_picker_rotation,s_picker_done,s_picker_grid,s_picker_preview_ok,s_picker_eligible;
static uint8_t *s_picker_preview;
static bool picker_current(const file_item_t *item);
static void picker_header(uint8_t *fb);
static void picker_button(uint8_t *fb,const char *label,bool enabled);
static void picker_render_preview(uint8_t *fb);
static EpdRect file_pager_rect(int direction);

static file_item_t s_selected;
static bool s_delete_confirm;
static int s_move_origin_folder, s_move_origin_page;
static char s_move_origin_dir[288];
static char s_editor[121], s_editor_ext[16], s_editor_notice[96];
static ui_text_input_t s_text_input;
static void scan_folder(int folder);
// 仅图片查看返回时恢复浏览位置，普通进入应用仍从根界面开始。
// Restore browser context only on an image return, not on a normal Apps entry.
static struct {bool valid;int folder,page;char dir[288];} s_image_origin;
static bool s_image_restore;
static int s_image_restore_page=-1;
void app_files_request_image_return(void){s_image_restore=s_image_origin.valid;}


static const char *const names[] = {UI_MSG("书籍"), UI_MSG("图片"), UI_MSG("字体"), UI_MSG("存储卡管理")};
static const char *folder_root(int folder) {
    if (folder == 0) return app_settings_books_dir();
    if (folder == 1) return "/sdcard/pictures";
    if (folder == 2) return app_settings_fonts_dir();
    return s_dir;
}
static bool s_grid_modes[2];
typedef struct { uint8_t *pixels; unsigned width,height; bool image_folder; } file_thumb_t;
static file_thumb_t s_thumbs[9];
static int s_thumb_page=-1;
static char s_thumb_dir[288];
// 使用路径段边界，不能把pictures-old误判为图片目录。/ Match path segments, never pictures-old.
static int image_tree(const char *path) {
    static const char *const roots[]={"/sdcard/pictures","/sdcard/banner pic"};
    for(int i=0;i<2;++i){size_t n=strlen(roots[i]);if(!strncasecmp(path,roots[i],n)&&(!path[n]||path[n]=='/'))return i;}
    return -1;
}
static bool grid_available(void){return s_folder>=0&&s_view==FILE_VIEW_LIST&&(s_picker||image_tree(folder_root(s_folder))>=0);}
static bool grid_active(void){int tree=s_folder>=0?image_tree(folder_root(s_folder)):-1;return grid_available()&&(s_picker?s_picker_grid:s_grid_modes[tree]);}
static int grid_columns(void){return !s_picker&&image_tree(folder_root(s_folder))==1?2:3;}
static int grid_rows(void){return !s_picker&&image_tree(folder_root(s_folder))==1?4:3;}
static int file_row_height(void) { return ui_list_row_height(true); }
static int file_pager_top(void){return UI_NAV_PAGER_TOP-(s_picker?90:0);}
static int visible_rows(void) { return grid_active()?grid_columns()*grid_rows():ui_list_visible_rows(UI_PAGE_Y(214),s_view==FILE_VIEW_MOVE?865:file_pager_top(),file_row_height()); }
static EpdRect grid_toggle_rect(void){return(EpdRect){576,UI_PAGE_Y(75),72,64};}
static EpdRect grid_cell(int index){int cols=grid_columns(),height=(file_pager_top()-UI_PAGE_Y(214)-12)/grid_rows();return(EpdRect){36+(index%cols)*(612/cols),UI_PAGE_Y(214)+(index/cols)*height,612/cols,height};}
static void clear_thumbnails(void){for(unsigned i=0;i<9;++i){free(s_thumbs[i].pixels);s_thumbs[i]=(file_thumb_t){0};}s_thumb_page=-1;s_thumb_dir[0]=0;}
// 只在准备可见格子时验证子树，绘制时使用缓存；空目录和坏图片不冒充图片文件夹。
// Probe visible folder subtrees before rendering; empty/corrupt-only folders keep the plain icon.
static bool folder_image_accept(const char *path,void *context){
    (void)context;return ui_wallpaper_probe_image(path,s_picker);
}
static bool folder_has_images(const char *path){
    char selected[288];return wallpaper_rotation_pick(path,NULL,0,folder_image_accept,NULL,selected,sizeof(selected));
}
static void prepare_thumbnails(void){
    if(s_view!=FILE_VIEW_LIST)return;
    if(!grid_active()){clear_thumbnails();return;}
    const char *dir=folder_root(s_folder);
    if(s_thumb_page==s_page&&!strcmp(s_thumb_dir,dir))return;
    clear_thumbnails();s_thumb_page=s_page;snprintf(s_thumb_dir,sizeof(s_thumb_dir),"%s",dir);
    for(int row=0;row<visible_rows();++row){int index=s_page*visible_rows()+row;if(index>=s_count)break;
        if(s_files[index].is_dir){s_thumbs[row].image_folder=folder_has_images(s_files[index].path);continue;}
        const char *ext=strrchr(s_files[index].name,'.');if(!ext||(strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")&&strcasecmp(ext,".png")))continue;
        file_thumb_t *thumb=&s_thumbs[row];thumb->pixels=heap_caps_malloc(FILE_THUMB_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(thumb->pixels&&!file_thumbnail_load(s_files[index].path,thumb->pixels,&thumb->width,&thumb->height)){free(thumb->pixels);*thumb=(file_thumb_t){0};}
#ifndef PICO_HOST_SIMULATOR
        vTaskDelay(1);
#endif
    }
}
static const char *root_note(const char *path){
    if(!strcasecmp(path,"/sdcard/books"))return UI_MSG("书籍目录");
    if(!strcasecmp(path,"/sdcard/fonts"))return UI_MSG("字体目录");
    if(!strcasecmp(path,"/sdcard/markdown"))return UI_MSG("文档目录");
    if(!strcasecmp(path,"/sdcard/pictures"))return UI_MSG("图片目录");
    if(!strcasecmp(path,"/sdcard/banner pic"))return UI_MSG("Banner目录");
    return NULL;
}
static int apps_row_height(void) {
    int height = ui_list_row_height(true) + 30;
    return height < 140 ? 140 : height;
}
static EpdRect apps_row(int index) {
    return (EpdRect){36, UI_PAGE_Y(108) + index * apps_row_height(), 612, apps_row_height()};
}

// 容量无效时只显示浅环，不把读取失败画成零占用。/ An unavailable capacity keeps a pale ring, never a false zero-used reading.
static void storage_ring(uint8_t *fb, int cx, int cy) {
    bool valid = s_sd.mounted && s_sd.space_valid && s_sd.capacity_bytes;
    uint64_t used = valid && s_sd.capacity_bytes > s_sd.free_bytes ? s_sd.capacity_bytes - s_sd.free_bytes : 0;
    double fraction = valid ? (double)used / s_sd.capacity_bytes : 0;
    for (int y = -22; y <= 22; ++y) for (int x = -22; x <= 22; ++x) {
        int rr = x*x + y*y;
        if (rr < 16*16 || rr > 22*22) continue;
        double angle = atan2((double)x, (double)-y);
        if (angle < 0) angle += 6.283185307179586;
        epd_draw_pixel(cx+x, cy+y, valid && angle < fraction*6.283185307179586 ? UI_GRAY_BLACK : 0xdd, fb);
    }
}

static void copy_utf8(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n); dst[n] = 0;
}

static bool is_book_path(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".md"));
}

static bool sibling_path(const char *name, char out[288]) {
    const char *slash = strrchr(s_selected.path, '/');
    if (!slash || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':')) return false;
    size_t parent = (size_t)(slash - s_selected.path);
    if (!name[0] || name[0] == '.' || parent + 1 + strlen(name) >= 288) return false;
    memcpy(out, s_selected.path, parent);
    out[parent] = '/';
    strcpy(out + parent + 1, name);
    return true;
}

static void migrate_book_state(const char *old_path, const char *new_path, uint32_t size) {
    if (!is_book_path(old_path)) return;
    book_progress_t progress = {0};
    bool had_progress = book_progress_load(old_path, size, &progress);
    char last[BOOK_STORE_PATH_MAX] = {0};
    bool was_last = book_progress_last_path(last, sizeof(last)) && !strcmp(last, old_path);
    if(had_progress&&book_progress_save(new_path,&progress)!=ESP_OK){file_notice(UI_MSG("保存失败，请重试"));return;}
    (void)book_progress_forget(old_path);
    if (was_last) (void)book_progress_set_last_path(new_path);
    (void)book_title_clear(old_path);
}

// 文档事务由专用服务维护；普通文件操作不能拆散正文、图片和索引。/ Generic file operations must not split document bodies, assets and indexes.
// 标准根目录不可改名/移动/删除；其内部文件仍按各自规则管理。/ Protect fixed roots, retaining normal management of their contents.
static bool fixed_media_root(const char *path) {
    return path && root_note(path)!=NULL;
}
static bool if_managed_path(const char *path) {
    if (strncasecmp(path, "/sdcard/", 8)) return false;
    path += 8;
    static const char *const roots[] = {"markdown", ".pico-sync", ".pico-cloud", ".index"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        size_t n = strlen(roots[i]);
        if (!strncasecmp(path, roots[i], n) && (!path[n] || path[n] == '/')) return true;
    }
    return false;
}

static bool if_managed_document(const file_item_t *item) {
    const char *ext = strrchr(item->path, '.');
    return !item->is_dir && !strncmp(item->path, DOCUMENT_ROOT "/", strlen(DOCUMENT_ROOT) + 1) &&
           !strchr(item->path + strlen(DOCUMENT_ROOT) + 1, '/') && ext && !strcasecmp(ext, ".md");
}

static esp_err_t copy_selected_item(char out_name[128]) {
    if (if_managed_path(s_selected.path)) return ESP_ERR_NOT_SUPPORTED;
    const char *dot = s_selected.is_dir ? NULL : strrchr(s_selected.name, '.');
    size_t stem_len = dot && dot != s_selected.name ? (size_t)(dot - s_selected.name) : strlen(s_selected.name);
    char stem[100], candidate[128], destination[288];
    if (stem_len >= sizeof(stem)) stem_len = sizeof(stem) - 1;
    memcpy(stem, s_selected.name, stem_len); stem[stem_len] = 0;
    const char *ext = dot && dot != s_selected.name ? dot : "";
    for (int number = 1; number < 100; ++number) {
        int used = number == 1 ? ui_snprintf(candidate, sizeof(candidate), UI_MSG("%s 副本%s"), stem, ext) :
            ui_snprintf(candidate, sizeof(candidate), UI_MSG("%s 副本 %d%s"), stem, number, ext);
        if (used < 0 || used >= (int)sizeof(candidate)) return ESP_ERR_INVALID_SIZE;
        if (!sibling_path(candidate, destination)) return ESP_ERR_INVALID_ARG;
        struct stat exists;
        if (stat(destination, &exists) != 0) {
            if (errno == ENOENT) break;
            return ESP_FAIL;
        }
        if (number == 99) return ESP_ERR_INVALID_SIZE;
    }
    read_pico_sd_get_info(&s_sd);
    if (!s_sd.mounted) return ESP_ERR_INVALID_STATE;
    esp_err_t err = file_tree_copy(s_selected.path, destination, s_sd.free_bytes);
    if (err != ESP_OK) return err;
    copy_utf8(out_name, 128, candidate);
    book_store_notify_changed();
    return ESP_OK;
}

static esp_err_t references_edit(const char *source,const char *target,bool *done){
    char previous[160];copy_utf8(previous,sizeof(previous),app_settings_system_font_path());
    esp_err_t err=done?file_references_begin(source,target,done):file_references_finish();
    if(strcmp(previous,app_settings_system_font_path()))app_font_activate_system();
    return err;
}

static void notify_moved_tree(const char *old_path, const char *new_path, unsigned depth) {
    if (depth > 8) return;
    struct stat st;
    if (stat(new_path, &st)) return;
    if (S_ISREG(st.st_mode)) {
        migrate_book_state(old_path, new_path,
            st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX ? (uint32_t)st.st_size : 0);
        return;
    }
    if (!S_ISDIR(st.st_mode)) return;
    DIR *dir = opendir(new_path);
    if (!dir) return;
    char *paths = heap_caps_malloc(576, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!paths) { closedir(dir); return; }
    char *old_child = paths, *new_child = paths + 288;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (ui_snprintf(old_child, 288, "%s/%s", old_path, entry->d_name) >= 288 ||
            ui_snprintf(new_child, 288, "%s/%s", new_path, entry->d_name) >= 288) continue;
        notify_moved_tree(old_child, new_child, depth + 1);
    }
    free(paths);
    closedir(dir);
}

static void file_deleted(const char *path, bool directory, void *ctx) {
    (void)ctx;
    if (directory) return;
    if(!strncmp(path,DOCUMENT_ROOT"/",17)&&strrchr(path,'.')&&!strcasecmp(strrchr(path,'.'),".md"))(void)document_store_delete(path);
    if (is_book_path(path)) (void)book_progress_forget(path);
    (void)book_title_clear(path);

}

static void repair_deleted_directories(void) {
    struct stat st;
    if (stat(app_settings_books_dir(), &st) || !S_ISDIR(st.st_mode)) {
        (void)mkdir("/sdcard/books", 0775);
        (void)app_settings_set_books_dir("/sdcard/books");
    }
    if (stat(app_settings_fonts_dir(), &st) || !S_ISDIR(st.st_mode)) {
        (void)mkdir("/sdcard/fonts", 0775);
        (void)app_settings_set_fonts_dir("/sdcard/fonts");
        ttf_font_scan();
    }
}

static void start_move(void) {
    s_move_origin_folder = s_folder;
    s_move_origin_page = s_page;
    copy_utf8(s_move_origin_dir, sizeof(s_move_origin_dir), s_dir);
    copy_utf8(s_dir, sizeof(s_dir), "/sdcard");
    s_folder = 3;
    s_view = FILE_VIEW_MOVE;
    scan_folder(3);
}

static void leave_move(bool changed) {
    s_folder = s_move_origin_folder;
    copy_utf8(s_dir, sizeof(s_dir), s_move_origin_dir);
    s_view = FILE_VIEW_LIST;
    scan_folder(s_folder >= 0 ? s_folder : 3);
    if (!changed) s_page = s_move_origin_page;
}

static app_redraw_t move_selected(void) {
    if(fixed_media_root(s_selected.path)){file_notice(UI_MSG("系统目录不可修改"));return APP_REDRAW_PAGE;}
    if (if_managed_path(s_selected.path) || if_managed_path(s_dir)) {
        file_notice(UI_MSG("文档目录请通过坚果云同步管理")); return APP_REDRAW_PAGE;
    }
    read_pico_sd_get_info(&s_sd);
    if (!s_sd.mounted) {
        file_notice(UI_MSG("TF 卡不可用")); return APP_REDRAW_PAGE;
    }
    char destination[288];
    if (ui_snprintf(destination, sizeof(destination), "%s/%s", s_dir, s_selected.name) >= sizeof(destination)) {
        file_notice(UI_MSG("目标路径过长")); return APP_REDRAW_PAGE;
    }
    if (!strcmp(destination, s_selected.path)) {
        file_notice(UI_MSG("已在此文件夹")); return APP_REDRAW_PAGE;
    }
    bool done=false;
    esp_err_t err=references_edit(s_selected.path,destination,&done);
    if(err!=ESP_OK){file_notice(UI_MSG("关联设置保存失败，请重试"));return APP_REDRAW_PAGE;}
    if(!done)err=file_tree_move(s_selected.path,destination);
    esp_err_t refs=references_edit(NULL,NULL,NULL);
    if(err!=ESP_OK||refs!=ESP_OK){
        if(err==ESP_OK)book_store_notify_changed();
        file_notice(refs!=ESP_OK?UI_MSG("文件已变更，关联设置未完成，请重试"):
                 err==ESP_ERR_INVALID_STATE?UI_MSG("目标已有同名项目"):UI_MSG("移动失败，请检查 TF 卡"));
        return APP_REDRAW_PAGE;
    }
    notify_moved_tree(s_selected.path, destination, 0);
    book_store_notify_changed();
    leave_move(true);
    file_notice(UI_MSG("移动完成"));
    return APP_REDRAW_PAGE;
}

void app_files_request_folder(int folder) {
    if (folder >= 0 && folder < 3) s_requested_folder = folder;
}

static bool supported(int folder, const char *name) {
    if (!name[0] || name[0] == '.' || !strncmp(name, "._", 2)) return false;
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    if (folder == 0) return !strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".md");
    if (folder == 1) return !strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".png");
    return !strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".otf");
}

static int compare_files(const void *left, const void *right) {
    const file_item_t *a = left, *b = right;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

static void scan_folder(int folder) {
    clear_thumbnails();
    if (!files_alloc()) return;
    s_count = s_page = 0;
    s_message[0] = 0;
    read_pico_sd_get_info(&s_sd);
    if (!s_sd.mounted) { file_notice(UI_MSG("TF 卡尚未挂载")); return; }
    const char *root = folder_root(folder);
    DIR *dir = opendir(root);
    if (!dir) { file_notice(UI_MSG("目录不存在：%.60s"), root); return; }
    struct dirent *entry;
    while (s_count < FILE_MAX && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strncmp(entry->d_name, "._", 2)) continue;
        file_item_t *item = &s_files[s_count];
        if (ui_snprintf(item->path, sizeof(item->path), "%s/%s", root, entry->d_name) >= sizeof(item->path)) continue;
        struct stat st;
        bool is_dir = false, is_file = false;
        if (!stat(item->path, &st)) {
            is_dir = S_ISDIR(st.st_mode);
            is_file = S_ISREG(st.st_mode);
            item->size = st.st_size;
        } else {
            DIR *probe = opendir(item->path);
            if (probe) { is_dir = true; item->size = 0; closedir(probe); }
        }
        if (!is_dir && !is_file) continue;
        if(s_picker&&!is_dir&&!supported(1,entry->d_name))continue;
        if (s_view == FILE_VIEW_MOVE && !is_dir) continue;
        if (folder != 3 && (!is_file || !supported(folder, entry->d_name))) continue;
        if (strnlen(entry->d_name, sizeof(item->name)) >= sizeof(item->name)) continue;
        strcpy(item->name, entry->d_name);
        item->is_dir = is_dir;
        ++s_count;
    }
    if(s_picker&&s_count==FILE_MAX&&readdir(dir))file_notice(UI_MSG("文件过多，请使用子目录"));
    closedir(dir);
    qsort(s_files, s_count, sizeof(s_files[0]), compare_files);
}

static void rescan_current(void) {
    scan_folder(s_folder >= 0 ? s_folder : 3);
}

// 列表行的文件夹 / 文档图标，来自 Lucide；盒子 46 像素，中心对到原来的图形中心。
// Lucide folder / file marks for list rows; a 46 px box centred on the old artwork.
#define FILE_ICON_PX 46

static void file_icon(uint8_t *fb, int x, int y, const file_item_t *item) {
    ui_draw_icon(
        fb, x + 21, y + 21, FILE_ICON_PX,
        item->is_dir ? (image_tree(item->path)>=0?UI_ICON_FOLDER_IMAGE:UI_ICON_FOLDER) : UI_ICON_FILE, UI_GRAY_BLACK
    );
}

static void render_grid(uint8_t *fb){
    for(int row=0;row<visible_rows();++row){int index=s_page*visible_rows()+row;if(index>=s_count)break;
        const file_item_t *item=&s_files[index];EpdRect cell=grid_cell(row);
        int text_height=ui_text_line_height(UI_TEXT_CAPTION);
        EpdRect box={cell.x+6,cell.y+6,cell.width-12,cell.height-text_height-24};
        const file_thumb_t *thumb=&s_thumbs[row];
        if(thumb->pixels&&thumb->width&&thumb->height){unsigned w,h;book_cover_fit_size(thumb->width,thumb->height,(unsigned)box.width,(unsigned)box.height,&w,&h);
            int left=box.x+(box.width-(int)w)/2,top=box.y+(box.height-(int)h)/2;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){size_t pixel=(size_t)(y*thumb->height/h)*thumb->width+x*thumb->width/w;uint8_t byte=thumb->pixels[pixel/2];uint8_t shade=pixel&1?byte&15:byte>>4;epd_draw_pixel(left+(int)x,top+(int)y,(uint8_t)(shade<<4),fb);}
        }else ui_draw_icon(fb,box.x+box.width/2,box.y+box.height/2,72,item->is_dir?(thumb->image_folder?UI_ICON_FOLDER_IMAGE:UI_ICON_FOLDER):(supported(1,item->name)?UI_ICON_IMAGE:UI_ICON_FILE),item->is_dir?UI_GRAY_BLACK:0x88);
        epd_draw_rect(box,UI_GRAY_ITEM_LINE,fb);
        if(picker_current(item))for(int d=2;d<=4;d++)epd_draw_rect((EpdRect){box.x-d,box.y-d,box.width+2*d,box.height+2*d},UI_GRAY_BLACK,fb);
        char name[128];ui_snprintf(name,sizeof(name),"%s",item->name);
        while(*name&&ui_text_fixed_width_px(ui_text_effective_px(UI_TEXT_CAPTION),name)>box.width){size_t n=strlen(name)-1;while(n&&((unsigned char)name[n]&0xc0)==0x80)--n;name[n]=0;}
        ui_text_vc(fb,cell.x+cell.width/2,box.y+box.height+text_height/2+6,UI_TEXT_CAPTION,name,EPD_DRAW_ALIGN_CENTER,false);
    }
}

static void file_detail(char *dst, size_t cap, const file_item_t *item) {
    if (item->is_dir) ui_snprintf(dst, cap, UI_MSG("文件夹"));
    else if (item->size >= 1024 * 1024) ui_snprintf(dst, cap, "%.1f MB", item->size / 1048576.0);
    else if (item->size >= 1024) ui_snprintf(dst, cap, "%.1f KB", item->size / 1024.0);
    else ui_snprintf(dst, cap, "%lld B", (long long)item->size);
}

// 文件列表的分隔线保持两像素，墨水屏上比浅色发丝线更容易辨认。
// Two-pixel file dividers stay legible on the panel without changing the layout.
static void file_rule(uint8_t *fb, int x, int y, int width) {
    epd_fill_rect((EpdRect){x, y, width, 2}, UI_GRAY_SECTION_LINE, fb);
}

static void fit_name(char *name, int px, int width) {
    while (*name && ttf_text_width_px(ui_text_effective_px(px), name) > width) {
        size_t n = strlen(name) - 1;
        while (n && ((unsigned char)name[n] & 0xc0) == 0x80) --n;
        name[n] = 0;
    }
}



static void editor_start(void) {
    if (strlen(s_selected.name) >= sizeof(s_editor)) {
        file_notice(UI_MSG("名称过长，无法在设备上编辑"));
        return;
    }
    copy_utf8(s_editor, sizeof(s_editor), s_selected.name);
    s_editor_ext[0] = 0;
    char *dot = strrchr(s_editor, '.');
    if (!s_selected.is_dir && dot && dot != s_editor) {
        copy_utf8(s_editor_ext, sizeof(s_editor_ext), dot);
        *dot = 0;
    }
    s_editor_notice[0]=0;
    ui_text_input_open(&s_text_input,s_editor,sizeof(s_editor),true,false,UI_INPUT_CHINESE);
    s_text_input.suffix=s_editor_ext;
    s_view = FILE_VIEW_RENAME;
}







// 打开卡片时探测一次，不在render中读卡；屏幕宽高比允许5%误差。
// Probe once when opening, never during render; accept a 5% screen-aspect tolerance.
static void probe_wallpaper_action(void){
    s_wallpaper_eligible=false;
    if(s_selected.is_dir||!supported(1,s_selected.name)||s_selected.size<=0||s_selected.size>2*1024*1024)return;
    s_wallpaper_eligible=ui_wallpaper_eligible(s_selected.path);
}
static bool selected_wallpaper_image(void){return s_wallpaper_eligible;}
static int file_action_height(void){return ui_text_line_height(UI_TEXT_BODY)+48;}
static int file_card_header_height(void){return ui_text_line_height(UI_TEXT_BODY)+ui_text_line_height(UI_TEXT_CAPTION)+40;}
static int file_card_path_height(void){return ui_text_line_height(UI_TEXT_CAPTION)+24;}
static int file_card_top(void){
    int lower=s_delete_confirm?2*ui_text_line_height(UI_TEXT_CAPTION)+32+file_action_height():file_action_height()*(1+(selected_wallpaper_image()?1:0));
    return UI_NAV_TOP-file_card_header_height()-file_card_path_height()-lower-12;
}
static EpdRect file_card_close_rect(void){return(EpdRect){592,file_card_top()+12,76,76};}
static EpdRect wallpaper_action_rect(void){return(EpdRect){24,file_card_top()+file_card_header_height()+file_card_path_height(),636,file_action_height()};}
static EpdRect file_regular_action_rect(int action){
    int top=UI_NAV_TOP-12-file_action_height();
    // 四等分与导航栏共用边界，文字、分隔线与触区同时对齐。
    // Share navigation quarters so labels, dividers and touch bounds align.
    int width=UI_LOCK_WIDTH/4;
    return(EpdRect){action*width,top,width,file_action_height()};
}
static EpdRect file_delete_button_rect(bool confirm){return(EpdRect){24+(confirm?318:0),UI_NAV_TOP-12-file_action_height(),318,file_action_height()};}
static void render_actions(uint8_t *fb){
    int top=file_card_top(),header=file_card_header_height(),path_y=top+header;
    // 与阅读控制台一致的白底上层、向上渐变阴影；导航栏保留原位置。
    // Match the reader console's raised white surface and upward shadow, keeping navigation anchored.
    ui_fade_region(fb,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,top-UI_STATUS_HEIGHT},0x99);
    for(int d=0;d<UI_NAV_SHADOW_DEPTH;++d)epd_fill_rect((EpdRect){0,top-UI_NAV_SHADOW_DEPTH+d,UI_LOCK_WIDTH,1},(uint8_t)(0xff-(d+1)*0x66/UI_NAV_SHADOW_DEPTH),fb);
    epd_fill_rect((EpdRect){0,top,UI_LOCK_WIDTH,UI_NAV_TOP-top},UI_GRAY_WHITE,fb);
    ui_hairline(fb,top,0,UI_LOCK_WIDTH,UI_GRAY_SECTION_LINE);
    file_icon(fb,36,top+20,&s_selected);
    EpdRect close=file_card_close_rect();ui_text_fixed_vc(fb,close.x+close.width/2,close.y+close.height/2,36,"×",EPD_DRAW_ALIGN_CENTER,false);
    char name[128];copy_utf8(name,sizeof(name),s_selected.name);fit_name(name,UI_TEXT_BODY,close.x-102-16);
    ui_text_vc(fb,102,top+20+ui_text_line_height(UI_TEXT_BODY)/2,UI_TEXT_BODY,name,EPD_DRAW_ALIGN_LEFT,false);
    char detail[48];file_detail(detail,sizeof(detail),&s_selected);
    ui_text_vc(fb,102,top+20+ui_text_line_height(UI_TEXT_BODY)+ui_text_line_height(UI_TEXT_CAPTION)/2,UI_TEXT_CAPTION,detail,EPD_DRAW_ALIGN_LEFT,false);
    char path[288];copy_utf8(path,sizeof(path),s_selected.path);fit_name(path,UI_TEXT_CAPTION,612);
    ui_text_vc(fb,36,path_y+file_card_path_height()/2,UI_TEXT_CAPTION,path,EPD_DRAW_ALIGN_LEFT,false);
    if(s_delete_confirm){
        int cy=path_y+file_card_path_height()+ui_text_line_height(UI_TEXT_CAPTION);
        ui_text_vc(fb,342,cy,UI_TEXT_CAPTION,s_selected.is_dir?UI_MSG("将删除文件夹内所有内容，无法恢复"):UI_MSG("删除后无法恢复，是否继续？"),EPD_DRAW_ALIGN_CENTER,false);
        for(int i=0;i<2;++i){EpdRect r=file_delete_button_rect(i);ui_text_vc(fb,r.x+r.width/2,r.y+r.height/2,UI_TEXT_BODY,i?UI_MSG("确认删除"):UI_MSG("取消"),EPD_DRAW_ALIGN_CENTER,false);}
    }else{
        if(selected_wallpaper_image()){
            EpdRect r=wallpaper_action_rect();ui_hairline(fb,r.y,36,612,UI_GRAY_ITEM_LINE);
            ui_text_vc(fb,342,r.y+r.height/2,UI_TEXT_BODY,UI_MSG("设为息屏壁纸"),EPD_DRAW_ALIGN_CENTER,false);
        }
        static const char *const labels[]={UI_MSG("重命名"),UI_MSG("复制"),UI_MSG("移动"),UI_MSG("删除")};
        EpdRect first=file_regular_action_rect(0);ui_hairline(fb,first.y,36,612,UI_GRAY_ITEM_LINE);
        for(int i=0;i<4;++i){EpdRect r=file_regular_action_rect(i);
            if(i)epd_fill_rect((EpdRect){r.x,r.y+24,1,r.height-48},UI_GRAY_ITEM_LINE,fb);
            int px=ui_text_fit_px(UI_TEXT_BODY,labels[i],r.width-16,r.height-16);
            ui_text_fixed_vc(fb,r.x+r.width/2,r.y+r.height/2,px,labels[i],EPD_DRAW_ALIGN_CENTER,false);
        }
    }
}

static void render_rename(uint8_t *fb) {
    ui_text_input_draw(fb,&s_text_input,UI_MSG("重命名"),UI_MSG("文件扩展名保持不变"),s_editor_ext,UI_MSG("完成"),"");
}

static void render(app_ctx_t *ctx, uint8_t *fb) {
    if (s_view == FILE_VIEW_ACTIONS) {
        file_view_t restore = s_view;
        s_view = FILE_VIEW_LIST;
        render(ctx, fb);
        s_view = restore;
        render_actions(fb);
        return;
    }
    (void)ctx;
    ui_clear_page(fb);
    ui_nav_status(fb);
    if (s_view == FILE_VIEW_RENAME) {
        render_rename(fb);
        return;
    }
    if(s_picker&&s_view==FILE_VIEW_WALLPAPER_PREVIEW){picker_render_preview(fb);return;}
    if (s_folder >= 0) {
        if(s_picker)picker_header(fb);else ui_nav_back(fb, 36,UI_PAGE_Y(79));
        if(!s_picker)ui_text_vc(fb, 342,UI_PAGE_Y(107), 34, s_view == FILE_VIEW_MOVE ? UI_MSG("选择目标文件夹") : names[s_folder], EPD_DRAW_ALIGN_CENTER, false);
        if(grid_available()){EpdRect toggle=grid_toggle_rect();ui_draw_icon(fb,toggle.x+toggle.width/2,toggle.y+toggle.height/2,42,grid_active()?UI_ICON_LIST:UI_ICON_LAYOUT_GRID,UI_GRAY_BLACK);}
        char path[144];
        ui_snprintf(path, sizeof(path), UI_MSG("TF卡%s"), folder_root(s_folder) + 7);
        ui_text(fb, 36,UI_PAGE_Y(172), 20, path, EPD_DRAW_ALIGN_LEFT, false);
        int folders = 0, files = 0;
        for (int i = 0; i < s_count; ++i) s_files[i].is_dir ? ++folders : ++files;
        char summary[48]; ui_snprintf(summary, sizeof(summary), UI_MSG("%d个文件夹 · %d个文件"), folders, files);
        ui_text(fb, 648,UI_PAGE_Y(172), 20, summary, EPD_DRAW_ALIGN_RIGHT, false);
        file_rule(fb, 36,UI_PAGE_Y(202), 612);
        if (!s_count) ui_text(fb, 36,UI_PAGE_Y(238), 27, UI_MSG("此目录还没有文件"), EPD_DRAW_ALIGN_LEFT, false);
        if(grid_active())render_grid(fb);
        else for (int row = 0; row < visible_rows(); ++row) {
            int index = s_page * visible_rows() + row;
            if (index >= s_count) break;
            int y = UI_PAGE_Y(214) + row * file_row_height();
            char name[128]; ui_snprintf(name, sizeof(name), "%s", s_files[index].name);
            const char *tag = s_files[index].is_dir?root_note(s_files[index].path):NULL;
            int name_width=tag?588-ui_text_fixed_width_px(ui_text_effective_px(UI_TEXT_CAPTION),tag)-24-101:450;
            while (*name && ui_text_fixed_width_px(ui_text_effective_px(UI_TEXT_BODY),name)>name_width) {
                size_t n = strlen(name) - 1;
                while (n && ((unsigned char)name[n] & 0xc0) == 0x80) --n;
                name[n] = 0;
            }
            file_icon(fb, 48, y + 20, &s_files[index]);
            ui_text_vc(fb, 101, y+12+ui_text_line_height(UI_TEXT_BODY)/2, UI_TEXT_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
            char detail[32]; file_detail(detail, sizeof(detail), &s_files[index]);
            ui_text_fixed_vc(fb, 101, y+12+ui_text_line_height(UI_TEXT_BODY)+ui_text_line_height(UI_TEXT_CAPTION)/2, ui_text_effective_px(UI_TEXT_CAPTION), detail, EPD_DRAW_ALIGN_LEFT, false);
            if(tag)ui_text_fixed_vc(fb,588,y+file_row_height()/2,ui_text_effective_px(UI_TEXT_CAPTION),tag,EPD_DRAW_ALIGN_RIGHT,false);
            if(picker_current(&s_files[index]))epd_fill_circle(612,y+file_row_height()/2,6,UI_GRAY_BLACK,fb);
            else ui_text_vc(fb, 640, y+file_row_height()/2, UI_TEXT_BODY, "›", EPD_DRAW_ALIGN_RIGHT, false);
            epd_fill_rect((EpdRect){36,y+file_row_height()-2,612,2},UI_GRAY_ITEM_LINE,fb);
        }
        if (s_view == FILE_VIEW_MOVE) {
            ui_draw_round_rect(fb, (EpdRect){36,UI_PAGE_Y(875), 293, 76}, 8, UI_GRAY_BLACK);
            ui_draw_round_rect(fb, (EpdRect){355,UI_PAGE_Y(875), 293, 76}, 8, UI_GRAY_BLACK);
            ui_text(fb, 182,UI_PAGE_Y(897), 24, UI_MSG("取消移动"), EPD_DRAW_ALIGN_CENTER, false);
            ui_text(fb, 501,UI_PAGE_Y(897), 24, UI_MSG("移动到此处"), EPD_DRAW_ALIGN_CENTER, false);
        }
        if(s_count>visible_rows()){
            int pages=(s_count+visible_rows()-1)/visible_rows();
            if(!s_picker)ui_nav_draw_pager(fb,s_count,UI_MSG("项"),s_page,pages);
            else{char count[64];ui_snprintf(count,sizeof(count),UI_MSG("%d项 · %02d/%02d"),s_count,s_page+1,pages);int y=file_pager_top()+38;
                ui_text_vc(fb,342,y,UI_TEXT_CAPTION,count,EPD_DRAW_ALIGN_CENTER,false);
                ui_draw_icon(fb,58,y,28,UI_ICON_CHEVRON_LEFT,s_page?0:0x90);ui_draw_icon(fb,626,y,28,UI_ICON_CHEVRON_RIGHT,s_page+1<pages?0:0x90);}
        }
        if(s_picker)picker_button(fb,s_picker_rotation?UI_MSG("使用此文件夹"):UI_MSG("使用默认壁纸"),!s_picker_rotation||wallpaper_rotation_directory_valid(s_dir));
    } else {
        static const ui_icon_t icons[] = {UI_ICON_HARD_DRIVE, UI_ICON_WIFI, UI_ICON_CABLE, UI_ICON_NUTSTORE, UI_ICON_LAYOUT_GRID_CIRCLES};
        static const char *const labels[] = {UI_MSG("存储卡管理"), UI_MSG("WiFi/热点"), UI_MSG("USB连接"), UI_MSG("坚果云同步"), UI_MSG("数字拼豆")};
        static const char *const descriptions[] = {"",UI_MSG("通过浏览器管理文件"),UI_MSG("通过有线连接管理文件"),UI_MSG("拉取在线文件"),UI_MSG("创作6阶拼豆壁纸")};
        int title_width=0, title_px=ui_text_effective_px(UI_TEXT_BODY);
        for(int i=0;i<5;++i){int width=ui_text_fixed_width_px(title_px,labels[i]);if(width>title_width)title_width=width;}
        for (int i = 0; i < 5; ++i) {
            EpdRect row = apps_row(i);
            int cy = row.y + row.height/2;
            if (i) epd_fill_rect((EpdRect){36,row.y,612,2},UI_GRAY_ITEM_LINE,fb);
            ui_draw_icon(fb,64,cy,44,icons[i],UI_GRAY_BLACK);
            if(!ui_i18n_english()) {
                int above=0,below=0;ui_text_fixed_measure_px(title_px,labels[i],&above,&below);
                ui_text_justify_ink_top(fb,104,cy-(above+below)/2,title_px,labels[i],title_width,false);
            } else ui_text_vc(fb,104,cy,UI_TEXT_BODY,labels[i],EPD_DRAW_ALIGN_LEFT,false);
            if (i == 0) {
                char usage[80];
                if (!s_sd.mounted) ui_snprintf(usage,sizeof(usage),UI_MSG("未挂载"));
                else if (!s_sd.space_valid || !s_sd.capacity_bytes) ui_snprintf(usage,sizeof(usage),UI_MSG("容量未知"));
                else {
                    uint64_t used = s_sd.capacity_bytes > s_sd.free_bytes ? s_sd.capacity_bytes-s_sd.free_bytes : 0;
                    ui_snprintf(usage,sizeof(usage),UI_MSG("已用 %.1f/%.1f GB"),used/1073741824.0,s_sd.capacity_bytes/1073741824.0);
                }
                ui_text_fixed_vc_gray(fb,590,cy,ui_text_effective_px(UI_TEXT_CAPTION),usage,EPD_DRAW_ALIGN_RIGHT,4,15);
                storage_ring(fb,626,cy);
            } else {
                int available=648-104-title_width-24;
                int px=ui_text_fit_px(UI_TEXT_CAPTION,descriptions[i],available,row.height-24);
                if(i==1) {
                    const char *second=UI_MSG("设置首页banner及壁纸");
                    int second_px=ui_text_fit_px(UI_TEXT_CAPTION,second,available,row.height-24);
                    if(second_px<px)px=second_px;
                    int gap=px+8;
                    ui_text_fixed_vc_gray(fb,648,cy-gap/2,px,descriptions[i],EPD_DRAW_ALIGN_RIGHT,4,15);
                    ui_text_fixed_vc_gray(fb,648,cy+(gap+1)/2,px,second,EPD_DRAW_ALIGN_RIGHT,4,15);
                } else ui_text_fixed_vc_gray(fb,648,cy,px,descriptions[i],EPD_DRAW_ALIGN_RIGHT,4,15);
            }
        }
    }
    ui_nav_draw(fb, s_picker?3:2);
}

static void on_enter(app_ctx_t *ctx) {
    s_picker=false;
    s_now_ms=ctx->now_ms;ui_nav_notice_clear();s_status_area=(EpdRect){0};
    bool restore=s_image_restore&&s_image_origin.valid&&s_requested_folder<0;s_image_restore=false;
    s_folder = restore?s_image_origin.folder:s_requested_folder;s_requested_folder = -1;
    s_view = FILE_VIEW_LIST;
    s_delete_confirm = false;
    ui_snprintf(s_dir,sizeof(s_dir),"%s",restore?s_image_origin.dir:"/sdcard");
    s_image_restore_page=restore?s_image_origin.page:-1;
    s_page = 0; s_scan_pending = true;
    read_pico_sd_start_probe();
    read_pico_sd_get_info(&s_sd);
    if (s_folder >= 0) scan_folder(s_folder);
    else { s_count = 0; s_message[0] = 0; }
    if(restore&&s_sd.mounted){int last=s_count?(s_count-1)/visible_rows():0;s_page=s_image_restore_page<last?s_image_restore_page:last;s_image_restore_page=-1;s_scan_pending=false;}
}
static void on_media_lost(app_ctx_t *ctx) {
    (void)ctx; s_status_area=(EpdRect){0};clear_thumbnails();s_folder = -1; s_count = 0; s_scan_pending = false;
    s_view = FILE_VIEW_LIST; s_delete_confirm = false;
    read_pico_sd_get_info(&s_sd);
}
static void on_media_ready(app_ctx_t *ctx) {
    (void)ctx;
    s_scan_pending = true;
}
static app_redraw_t on_tick(app_ctx_t *ctx) {
    s_now_ms=ctx->now_ms;
    if(ui_nav_notice_tick(ctx->now_ms)){
        s_message[0]=0;s_editor_notice[0]=0;
        s_status_area=(EpdRect){0,0,UI_LOCK_WIDTH,UI_STATUS_HEIGHT};
        epd_fill_rect(s_status_area,UI_GRAY_WHITE,ctx->fb);ui_nav_status(ctx->fb);
        return APP_REDRAW_AREA;
    }
    if (!s_scan_pending) return APP_REDRAW_NONE;
    if (read_pico_sd_get_info(&s_sd) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
    s_scan_pending = false;
    if (s_folder >= 0) scan_folder(s_folder);
    else { s_count = 0; s_message[0] = 0; }
    if(s_image_restore_page>=0){int last=s_count?(s_count-1)/visible_rows():0;s_page=s_image_restore_page<last?s_image_restore_page:last;s_image_restore_page=-1;}
    return APP_REDRAW_PAGE;
}

static void open_selected(app_ctx_t *ctx) {
    const char *ext = strrchr(s_selected.name, '.');
    if (!ext) { file_notice(UI_MSG("没有可用的打开方式")); return; }
    if (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt") || !strcasecmp(ext, ".md")) {
        extern const app_desc_t app_book;
        if (app_book_request_open(s_selected.path)) ctx->request_app = &app_book;
    } else if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".png")) {
        extern const app_desc_t app_image;
        if(app_image_request_open(s_selected.path)){
            s_image_origin.valid=true;s_image_origin.folder=s_folder;s_image_origin.page=s_page;strlcpy(s_image_origin.dir,s_dir,sizeof(s_image_origin.dir));ctx->request_app=&app_image;
        }
    } else if (!strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".otf")) {
        if (ttf_font_open(s_selected.path) == ESP_OK) {
            app_settings_set_system_font_path(s_selected.path);
            app_font_activate_system();
            file_notice(UI_MSG("已设为系统字体"));
        } else file_notice(UI_MSG("字体无法加载"));
    } else file_notice(UI_MSG("没有可用的打开方式"));
}

static void delete_selected(void) {
    if(fixed_media_root(s_selected.path)){file_notice(UI_MSG("系统目录不可修改"));return;}
    if (if_managed_path(s_selected.path) && !if_managed_document(&s_selected)) {
        s_delete_confirm = false;
        file_notice(UI_MSG("请在内容页删除文档，图片会随文档清理")); return;
    }
    char old_path[288]; copy_utf8(old_path, sizeof(old_path), s_selected.path);
    bool md=!s_selected.is_dir&&!strncmp(old_path,DOCUMENT_ROOT"/",17)&&strrchr(old_path,'.')&&!strcasecmp(strrchr(old_path,'.'),".md");
    bool done=false;esp_err_t err=references_edit(old_path,NULL,&done);
    if(err!=ESP_OK){file_notice(UI_MSG("关联设置保存失败，请重试"));return;}
    if(!done)err=md?document_store_delete(old_path):file_tree_delete(old_path,file_deleted,NULL);
    esp_err_t refs=references_edit(NULL,NULL,NULL);
    if(refs!=ESP_OK){book_store_notify_changed();file_notice(UI_MSG("文件已变更，关联设置未完成，请重试"));return;}
    if(md&&err==ESP_OK)file_deleted(old_path,false,NULL);
    if (err != ESP_OK) {
        s_delete_confirm = false;
        repair_deleted_directories();
        book_store_notify_changed();
        rescan_current();
        file_notice(UI_MSG("删除未完成，请检查 TF 卡"));
        return;
    }
    repair_deleted_directories();
    book_store_notify_changed();
    s_view = FILE_VIEW_LIST; s_delete_confirm = false;
    rescan_current();
    file_notice(s_selected.is_dir ? UI_MSG("文件夹已删除") : UI_MSG("文件已删除"));
}

static app_redraw_t save_rename(void) {
    if(fixed_media_root(s_selected.path)){file_notice(UI_MSG("系统目录不可修改"));return APP_REDRAW_PAGE;}
    if (if_managed_path(s_selected.path)) {
        file_notice(UI_MSG("文档重命名请在 Obsidian 中操作")); return APP_REDRAW_PAGE;
    }
    if (s_text_input.pinyin[0]) {
        file_notice(UI_MSG("请先选择候选字"));
        return APP_REDRAW_PAGE;
    }
    char *start = s_editor;
    while (*start == ' ') ++start;
    if (start != s_editor) memmove(s_editor, start, strlen(start) + 1);
    size_t len = strlen(s_editor);
    while (len && (s_editor[len - 1] == ' ' || s_editor[len - 1] == '.')) s_editor[--len] = 0;
    s_text_input.cursor=strlen(s_editor);
    if (!len || strchr(s_editor, '/') || strchr(s_editor, '\\') || strchr(s_editor, ':')) {
        file_notice(UI_MSG("请输入有效文件名"));
        return APP_REDRAW_PAGE;
    }
    char name[128], destination[288];
    if (ui_snprintf(name, sizeof(name), "%s%s", s_editor, s_editor_ext) >= sizeof(name) ||
        !sibling_path(name, destination)) {
        file_notice(UI_MSG("文件名过长"));
        return APP_REDRAW_PAGE;
    }
    if (!strcmp(destination, s_selected.path)) { s_view = FILE_VIEW_ACTIONS; return APP_REDRAW_PAGE; }
    char old_path[288]; copy_utf8(old_path, sizeof(old_path), s_selected.path);
    bool done=false;esp_err_t err=references_edit(old_path,destination,&done);
    if(err!=ESP_OK){file_notice(UI_MSG("关联设置保存失败，请重试"));return APP_REDRAW_PAGE;}
    if(!done)err=file_tree_move(old_path,destination);
    esp_err_t refs=references_edit(NULL,NULL,NULL);
    if(err!=ESP_OK||refs!=ESP_OK){
        if(err==ESP_OK)book_store_notify_changed();
        file_notice(refs!=ESP_OK?UI_MSG("文件已变更，关联设置未完成，请重试"):UI_MSG("重命名失败，请检查 TF 卡"));
        return APP_REDRAW_PAGE;
    }
    notify_moved_tree(old_path, destination, 0);
    book_store_notify_changed();
    copy_utf8(s_selected.path, sizeof(s_selected.path), destination);
    copy_utf8(s_selected.name, sizeof(s_selected.name), name);
    s_view = FILE_VIEW_ACTIONS;
    file_notice(UI_MSG("重命名完成"));
    return APP_REDRAW_PAGE;
}

static app_redraw_t action_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if(!s_delete_confirm&&selected_wallpaper_image()&&ui_rect_hit(wallpaper_action_rect(),ev->x0,ev->y0)){
        // 与设置页目录校验共用8x8试解码；本次整页重绘会覆盖工作缓冲，不额外推屏。
        // Validate with the same 8x8 scratch decode used by Settings; page redraw replaces it without an extra refresh.
        if(!ui_wallpaper_draw(ctx->fb,s_selected.path,(EpdRect){0,0,8,8})){
            file_notice(UI_MSG("图片无法读取，请重新选择"));return APP_REDRAW_PAGE;
        }
        if(app_settings_save_wallpaper_path(s_selected.path)!=ESP_OK){
            file_notice(UI_MSG("壁纸保存失败，请重试"));return APP_REDRAW_PAGE;
        }
        app_settings_set_lock_style(1);
        file_notice(UI_MSG("已设为息屏壁纸"));return APP_REDRAW_PAGE;
    }
    int tab=ui_nav_hit(ev->x0,ev->y0);
    if(tab>=0){s_view=FILE_VIEW_LIST;s_delete_confirm=false;ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
    if (ev->y0 < file_card_top() || ui_rect_hit(file_card_close_rect(),ev->x0,ev->y0)) {
        s_view = FILE_VIEW_LIST; s_delete_confirm = false; rescan_current(); return APP_REDRAW_PAGE;
    }
    if (s_delete_confirm) {
        if (ui_rect_hit(file_delete_button_rect(false),ev->x0,ev->y0)||ui_rect_hit(file_delete_button_rect(true),ev->x0,ev->y0)) {
            if (ev->x0 < 342) s_delete_confirm = false;
            else delete_selected();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    EpdRect first=file_regular_action_rect(0);
    if (ev->y0>=first.y&&ev->y0<first.y+first.height&&ev->x0<UI_LOCK_WIDTH) {
        int action=ev->x0/(UI_LOCK_WIDTH/4);
        if(fixed_media_root(s_selected.path)){file_notice(UI_MSG("系统目录不可修改"));return APP_REDRAW_PAGE;}
        if (if_managed_path(s_selected.path) && (action != 3 || !if_managed_document(&s_selected))) {
            file_notice(UI_MSG("文档目录由同步管理；删除请在内容页操作"));
            return APP_REDRAW_PAGE;
        }
        if (action == 0) editor_start();
        else if (action == 1) {
            char copied[128] = {0};
            esp_err_t err = copy_selected_item(copied);
            if (err == ESP_OK) file_notice(UI_MSG("已复制：%.74s"), copied);
            else file_notice(err == ESP_ERR_NO_MEM ? UI_MSG("内存不足，复制失败") : UI_MSG("复制失败，请检查空间"));
        } else if (action == 2) start_move();
        else { s_delete_confirm = true; s_message[0] = 0; }
        return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t rename_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    ui_input_result_t result=ui_text_input_gesture(&s_text_input,ev);
    if(result==UI_INPUT_CANCEL){ui_text_input_clear(&s_text_input);s_view=FILE_VIEW_ACTIONS;return APP_REDRAW_PAGE;}
    if(result==UI_INPUT_DONE)return save_rename();
    if(result==UI_INPUT_CHANGED){s_editor_notice[0]=0;render_rename(ctx->fb);return APP_REDRAW_AREA;}
    return APP_REDRAW_NONE;
}

// 选择器保留设置页归属，不触发文件操作卡片。/ The picker stays in Settings and never opens file action cards.
static EpdRect picker_exit_rect(void){return ui_nav_close_rect();}
static EpdRect picker_up_rect(void){return(EpdRect){92,UI_PAGE_Y(73),68,68};}
static EpdRect picker_button_rect(void){return(EpdRect){142,UI_NAV_TOP-78,400,64};}
static EpdRect picker_preview_rect(void){return(EpdRect){166,UI_PAGE_Y(194),352,626};}
static EpdRect file_pager_rect(int direction){EpdRect r=ui_nav_pager_rect(direction);if(s_picker)r.y-=90;return r;}
static bool picker_current(const file_item_t *item){
    if(!s_picker||s_picker_rotation!=app_settings_wallpaper_rotating())return false;
    const char *saved=app_settings_wallpaper_path();size_t n=strlen(saved);while(n>7&&saved[n-1]=='/')--n;
    return strlen(item->path)==n&&!strncmp(item->path,saved,n);
}
static void picker_preview_clear(void){free(s_picker_preview);s_picker_preview=NULL;s_picker_preview_ok=s_picker_eligible=false;}
static void picker_preview_open(app_ctx_t *ctx){
    picker_preview_clear();EpdRect r=picker_preview_rect();
    s_picker_preview=heap_caps_malloc((size_t)((r.width+1)/2)*r.height,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(s_picker_preview){
        s_picker_preview_ok=ui_wallpaper_draw(ctx->fb,s_selected.path,r);
        // 真机缓冲按物理横屏存储，不能按逻辑竖屏行宽直接拷贝。
        // Capture through the rotation-aware bitmap API, never memcpy logical rows from the physical framebuffer.
        if(s_picker_preview_ok)ui_bitmap_capture(ctx->fb,r,s_picker_preview);
    }
    s_picker_eligible=s_picker_preview_ok&&ui_wallpaper_eligible(s_selected.path);
    s_view=FILE_VIEW_WALLPAPER_PREVIEW;
}
static void picker_header(uint8_t *fb){
    EpdRect up=picker_up_rect();ui_nav_close(fb);
    bool can_up=s_view==FILE_VIEW_WALLPAPER_PREVIEW||strcmp(s_dir,"/sdcard");
    ui_nav_parent(fb,up,can_up);
    const char *title=s_picker_rotation?UI_MSG("壁纸轮播"):UI_MSG("固定壁纸");
    ui_text_fixed_vc(fb,UI_LOCK_WIDTH/2,UI_PAGE_Y(107),ui_text_fit_px(UI_TEXT_HEADING,title,320,60),title,EPD_DRAW_ALIGN_CENTER,false);
}
static void picker_button(uint8_t *fb,const char *label,bool enabled){
    EpdRect r=picker_button_rect();ui_draw_round_rect(fb,r,r.height/2,enabled?UI_GRAY_BLACK:UI_GRAY_ITEM_LINE);
    ui_text_fixed_vc_gray(fb,r.x+r.width/2,r.y+r.height/2,ui_text_fit_px(UI_TEXT_BODY,label,r.width-24,r.height-8),label,EPD_DRAW_ALIGN_CENTER,enabled?0:9,15);
}
static void picker_render_preview(uint8_t *fb){
    ui_clear_page(fb);ui_nav_status(fb);picker_header(fb);EpdRect r=picker_preview_rect();
    if(s_picker_preview_ok)ui_bitmap_draw(fb,r,s_picker_preview);
    else ui_text_vc(fb,342,r.y+r.height/2,UI_TEXT_BODY,UI_MSG("图片无法预览"),EPD_DRAW_ALIGN_CENTER,false);
    char name[128];snprintf(name,sizeof(name),"%s",s_selected.name);fit_name(name,UI_TEXT_CAPTION,600);
    ui_text_vc(fb,342,UI_PAGE_Y(865),UI_TEXT_CAPTION,name,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_vc(fb,342,UI_PAGE_Y(925),UI_TEXT_CAPTION,s_picker_eligible?UI_MSG("轻点图片返回文件夹"):UI_MSG("此图片不适合壁纸"),EPD_DRAW_ALIGN_CENTER,false);
    picker_button(fb,s_picker_rotation?UI_MSG("返回文件夹"):UI_MSG("使用此图片"),s_picker_rotation||s_picker_eligible);ui_nav_draw(fb,3);
}
static void picker_parent(void){
    if(s_view==FILE_VIEW_WALLPAPER_PREVIEW){picker_preview_clear();s_view=FILE_VIEW_LIST;return;}
    if(!strcmp(s_dir,"/sdcard"))return;
    char *slash=strrchr(s_dir,'/');if(slash&&slash>s_dir+6)*slash=0;scan_folder(3);
}
static app_redraw_t picker_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){
    if(ev->type==UI_GESTURE_LONG_PRESS)return APP_REDRAW_NONE;
    if(ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_U||ev->type==UI_GESTURE_SWIPE_R||ev->type==UI_GESTURE_SWIPE_D){
        if(s_view!=FILE_VIEW_LIST)return APP_REDRAW_NONE;
        int direction=(ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_U)?1:-1,next=s_page+direction;
        if(next>=0&&next*visible_rows()<s_count){s_page=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;
    }
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=ev->x0,y=ev->y0;
    if(ui_rect_hit(picker_exit_rect(),x,y)){s_picker_done=true;return APP_REDRAW_PAGE;}
    if(ui_rect_hit(picker_up_rect(),x,y)){picker_parent();return APP_REDRAW_PAGE;}
    int tab=ui_nav_hit(x,y);if(tab>=0){s_picker_done=true;ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
    if(s_view==FILE_VIEW_WALLPAPER_PREVIEW){
        if(ui_rect_hit(picker_button_rect(),x,y)){
            if(s_picker_rotation){picker_parent();return APP_REDRAW_PAGE;}
            if(!s_picker_eligible||!ui_wallpaper_eligible(s_selected.path)){file_notice(UI_MSG("此图片不适合壁纸"));return APP_REDRAW_PAGE;}
            if(app_settings_save_wallpaper_path(s_selected.path)!=ESP_OK){(void)app_settings_take_save_error();file_notice(UI_MSG("保存失败，请重试"));return APP_REDRAW_PAGE;}
            file_notice(UI_MSG("已设为息屏壁纸"));s_picker_done=true;return APP_REDRAW_PAGE;
        }
        if(y>=UI_PAGE_Y(194)&&y<UI_NAV_TOP-100){picker_parent();return APP_REDRAW_PAGE;}
        return APP_REDRAW_NONE;
    }
    if(ui_rect_hit(grid_toggle_rect(),x,y)){
        int first=s_page*visible_rows();s_picker_grid=!s_picker_grid;s_page=first/visible_rows();clear_thumbnails();return APP_REDRAW_PAGE;
    }
    if(y<UI_PAGE_Y(202))return APP_REDRAW_NONE;
    if(ui_rect_hit(picker_button_rect(),x,y)){
        if(!s_picker_rotation){
            if(app_settings_save_wallpaper_path("")!=ESP_OK){(void)app_settings_take_save_error();file_notice(UI_MSG("保存失败，请重试"));return APP_REDRAW_PAGE;}
            file_notice(UI_MSG("已使用默认壁纸"));s_picker_done=true;return APP_REDRAW_PAGE;
        }
        if(!wallpaper_rotation_directory_valid(s_dir)){file_notice(UI_MSG("请勿选择根目录"));return APP_REDRAW_PAGE;}
        wallpaper_rotation_status_t status=ui_wallpaper_check_subtree(s_dir);
        if(status!=WALLPAPER_ROTATION_OK){file_notice(status==WALLPAPER_ROTATION_LIMIT?UI_MSG("目录过大，请选择子目录"):status==WALLPAPER_ROTATION_IO?UI_MSG("文件夹读取失败"):status==WALLPAPER_ROTATION_MEMORY?UI_MSG("内存不足，请重试"):UI_MSG("无合适壁纸，请重选"));return APP_REDRAW_PAGE;}
        if(app_settings_set_wallpaper_directory(s_dir)!=ESP_OK){(void)app_settings_take_save_error();file_notice(UI_MSG("保存失败，请重试"));return APP_REDRAW_PAGE;}
        file_notice(UI_MSG("已启用壁纸轮播"));s_picker_done=true;return APP_REDRAW_PAGE;
    }
    if(s_count>visible_rows())for(int direction=-1;direction<=1;direction+=2)if(ui_rect_hit(file_pager_rect(direction),x,y)){
        int next=s_page+direction;if(next>=0&&next*visible_rows()<s_count){s_page=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;
    }
    int row=(y-UI_PAGE_Y(214))/file_row_height();bool inside=x>=36&&x<648&&y>=UI_PAGE_Y(214)&&y<file_pager_top();
    if(grid_active()){EpdRect cell=grid_cell(0);inside=inside&&y<cell.y+cell.height*grid_rows();row=inside?(y-cell.y)/cell.height*grid_columns()+(x-36)/cell.width:-1;}
    int index=s_page*visible_rows()+row;
    if(inside&&row>=0&&row<visible_rows()&&index<s_count){
        s_selected=s_files[index];
        if(s_selected.is_dir){snprintf(s_dir,sizeof(s_dir),"%s",s_selected.path);scan_folder(3);}
        else picker_preview_open(ctx);
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    s_now_ms=ctx->now_ms;
    if (s_view == FILE_VIEW_ACTIONS) return action_gesture(ctx, ev);
    if (s_view == FILE_VIEW_RENAME) return rename_gesture(ctx,ev);
    if(ev->type==UI_GESTURE_TAP&&grid_available()&&ui_rect_hit(grid_toggle_rect(),ev->x0,ev->y0)){
        int first=s_page*visible_rows(),tree=image_tree(folder_root(s_folder));s_grid_modes[tree]=!s_grid_modes[tree];s_page=first/visible_rows();clear_thumbnails();return APP_REDRAW_PAGE;
    }
    if(ev->type==UI_GESTURE_TAP && (s_view==FILE_VIEW_LIST||s_view==FILE_VIEW_MOVE)) {
        int rows=visible_rows();
        if(s_folder>=0 && s_count>rows)for(int direction=-1;direction<=1;direction+=2)if(ui_rect_hit(ui_nav_pager_rect(direction),ev->x0,ev->y0)){
            int next=s_page+direction;if(next>=0&&next*rows<s_count){s_page=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;
        }
    }
    if (s_view == FILE_VIEW_MOVE) {
        if (ev->type == UI_GESTURE_SWIPE_L && (s_page + 1) * visible_rows() < s_count) {
            ++s_page; return APP_REDRAW_PAGE;
        }
        if (ev->type == UI_GESTURE_SWIPE_R && s_page > 0) { --s_page; return APP_REDRAW_PAGE; }
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        if (ui_rect_hit((EpdRect){24,UI_PAGE_Y(79),68,68},ev->x0,ev->y0)) {
            if (strcmp(s_dir, "/sdcard")) {
                char *slash = strrchr(s_dir, '/'); if (slash) *slash = 0;
                scan_folder(3);
            } else leave_move(false);
            return APP_REDRAW_PAGE;
        }
        if (ev->y0 >= UI_PAGE_Y(875) && ev->y0 < UI_PAGE_Y(951)) {
            if (ev->x0 < 342) leave_move(false);
            else return move_selected();
            return APP_REDRAW_PAGE;
        }
        int row = (ev->y0 - UI_PAGE_Y(214)) / file_row_height();
        int index = s_page * visible_rows() + row;
        if (ev->y0 >= UI_PAGE_Y(214) && row >= 0 && row < visible_rows() && index < s_count) {
            copy_utf8(s_dir, sizeof(s_dir), s_files[index].path);
            scan_folder(3);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_SWIPE_L && s_folder >= 0 && (s_page + 1) * visible_rows() < s_count) { ++s_page; return APP_REDRAW_PAGE; }
    if (ev->type == UI_GESTURE_SWIPE_R && s_folder >= 0 && s_page > 0) { --s_page; return APP_REDRAW_PAGE; }
    if (ev->type != UI_GESTURE_TAP && ev->type != UI_GESTURE_LONG_PRESS) return APP_REDRAW_NONE;
    bool long_press = ev->type == UI_GESTURE_LONG_PRESS;
    if (long_press && (s_folder < 0 || ev->y0 < UI_PAGE_Y(214))) return APP_REDRAW_NONE;
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (!long_press && tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    if (s_folder >= 0) {
        if (!long_press && ui_rect_hit((EpdRect){24,UI_PAGE_Y(79),68,68},ev->x0,ev->y0)) {
            if (s_folder == 3 && strcmp(s_dir, "/sdcard")) {
                char *slash = strrchr(s_dir, '/'); if (slash) *slash = 0;
                scan_folder(3);
            } else s_folder = -1;
            return APP_REDRAW_PAGE;
        }
        int row = (int)(ev->y0 - UI_PAGE_Y(214)) / file_row_height();
        bool inside=ev->y0>=UI_PAGE_Y(214);
        if(grid_active()){
            EpdRect cell=grid_cell(0);inside=ev->x0>=36&&ev->x0<648&&ev->y0>=cell.y&&ev->y0<cell.y+cell.height*grid_rows();
            row=inside?((int)ev->y0-cell.y)/cell.height*grid_columns()+((int)ev->x0-36)/cell.width:-1;
        }
        int index = s_page * visible_rows() + row;
        if (inside && row >= 0 && row < visible_rows() && index < s_count) {
            if (long_press) {
                s_selected = s_files[index];probe_wallpaper_action(); s_view = FILE_VIEW_ACTIONS;
                s_delete_confirm = false; s_message[0] = 0;
                return APP_REDRAW_PAGE;
            }
            if (s_folder == 3 && s_files[index].is_dir) {
                if (strlen(s_files[index].path) >= sizeof(s_dir)) {
                    file_notice(UI_MSG("目录路径过长")); return APP_REDRAW_PAGE;
                }
                ui_snprintf(s_dir, sizeof(s_dir), "%s", s_files[index].path);
                scan_folder(3);
                return APP_REDRAW_PAGE;
            }
            s_selected = s_files[index];
            open_selected(ctx);
            return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    for (int i = 0; !long_press && i < 5; ++i) if (ui_rect_hit(apps_row(i),ev->x0,ev->y0)) {
        extern const app_desc_t app_transfer, app_cloud, app_pindou;
        if (i == 0) {
            s_folder = 3; copy_utf8(s_dir,sizeof(s_dir),"/sdcard"); scan_folder(3);
            return APP_REDRAW_PAGE;
        }
        if (i == 3) ctx->request_app = &app_cloud;
        else if (i == 4) ctx->request_app = &app_pindou;
        else {
            if (i == 1) app_transfer_request_method_picker();
            else app_transfer_request_usb_start();
            ctx->request_app = &app_transfer;
        }
        return APP_REDRAW_NONE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    s_now_ms=ctx->now_ms;
    // 列表左右键共用屏幕分页，确认/编辑页保留原操作。/ List side keys share pagination; dialogs and editors retain their actions.
    if(s_view==FILE_VIEW_LIST && key!=UI_KEY_2){
        int rows=visible_rows();
        if(s_folder>=0 && s_count>rows){int next=s_page+(key==UI_KEY_1?-1:1);if(next>=0&&next*rows<s_count){s_page=next;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
    }
    if (s_view == FILE_VIEW_MOVE) { leave_move(false); return APP_REDRAW_PAGE; }
    if (key == 1) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == FILE_VIEW_RENAME) { s_view = FILE_VIEW_ACTIONS; return APP_REDRAW_PAGE; }
    if (s_view == FILE_VIEW_ACTIONS) {
        s_view = FILE_VIEW_LIST; s_delete_confirm = false; rescan_current(); return APP_REDRAW_PAGE;
    }
    if (s_folder >= 0) {
        if (s_folder == 3 && strcmp(s_dir, "/sdcard")) {
            char *slash = strrchr(s_dir, '/'); if (slash) *slash = 0;
            scan_folder(3);
        } else s_folder = -1;
        return APP_REDRAW_PAGE;
    }
    ui_nav_request(ctx, 0);
    return APP_REDRAW_NONE;
}
static EpdRect file_area_hint(app_ctx_t *ctx){(void)ctx;return s_status_area;}
static bool files_present(app_ctx_t *ctx,app_redraw_t redraw){
    if(redraw==APP_REDRAW_AREA&&s_status_area.height){
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,s_status_area));
        s_status_area=(EpdRect){0};return true;
    }
    // 全页呈现可能由唤醒恢复从AREA提升，必须消费原来的局部刷新标记。
    // A wake recovery can promote AREA to a page/full present; consume the old region token.
    if(redraw==APP_REDRAW_PAGE||redraw==APP_REDRAW_FULL){s_status_area=(EpdRect){0};prepare_thumbnails();}
    if(redraw==APP_REDRAW_PAGE && s_view!=FILE_VIEW_RENAME){
        render(ctx,ctx->fb);
        guard_draw_result(ctx->hl,update_display_navigation_page(ctx->hl,(EpdRect){0},ui_nav_rect()));
        return true;
    }
    if(redraw!=APP_REDRAW_AREA||s_view!=FILE_VIEW_RENAME)return false;
    guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_text_input_area()));
    return true;
}
static void files_on_exit(app_ctx_t *ctx){(void)ctx;clear_thumbnails();ui_nav_notice_clear();s_status_area=(EpdRect){0};}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }

const app_desc_t app_files = {
    .title = UI_MSG("应用"), .detail = UI_MSG("传输、存储卡与数字拼豆"), .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .on_enter = on_enter, .on_exit=files_on_exit, .on_media_lost = on_media_lost, .on_media_ready = on_media_ready,
    .render = render, .on_tick = on_tick, .present = files_present, .area_hint=file_area_hint,
    .on_gesture = on_gesture, .on_key = on_key,
};

void app_files_wallpaper_open(app_ctx_t *ctx,bool rotating){
    clear_thumbnails();picker_preview_clear();s_picker=true;s_picker_rotation=rotating;s_picker_done=false;s_now_ms=ctx->now_ms;
    s_folder=3;s_page=0;s_view=FILE_VIEW_LIST;s_scan_pending=false;s_delete_confirm=false;s_message[0]=0;
    snprintf(s_dir,sizeof(s_dir),"/sdcard/pictures");scan_folder(3);prepare_thumbnails();
}
void app_files_wallpaper_close(void){clear_thumbnails();picker_preview_clear();s_picker=false;s_picker_done=false;s_scan_pending=false;s_status_area=(EpdRect){0};}
void app_files_wallpaper_render(app_ctx_t *ctx,uint8_t *fb){render(ctx,fb);}
app_redraw_t app_files_wallpaper_gesture(app_ctx_t *ctx,const ui_gesture_event_t *event){s_now_ms=ctx->now_ms;app_redraw_t r=picker_gesture(ctx,event);if(!s_picker_done)prepare_thumbnails();return r;}
app_redraw_t app_files_wallpaper_key(app_ctx_t *ctx,int key){
    s_now_ms=ctx->now_ms;if(key==UI_KEY_2){s_picker_done=true;ui_nav_request(ctx,0);return APP_REDRAW_NONE;}
    picker_parent();prepare_thumbnails();return APP_REDRAW_PAGE;
}
app_redraw_t app_files_wallpaper_tick(app_ctx_t *ctx){app_redraw_t r=on_tick(ctx);if(r==APP_REDRAW_PAGE)prepare_thumbnails();if(r==APP_REDRAW_AREA)s_status_area=(EpdRect){0};return r;}
bool app_files_wallpaper_finished(void){return s_picker_done;}

void app_files_wallpaper_media_changed(app_ctx_t *ctx,bool ready){
    (void)ctx;clear_thumbnails();picker_preview_clear();s_view=FILE_VIEW_LIST;s_count=0;
    read_pico_sd_get_info(&s_sd);s_scan_pending=ready;if(!ready)file_notice(UI_MSG("TF 卡尚未挂载"));
}
