/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：用户授权完整黑体自动补足标题缺字；内建子集保持菜单外观，用户选字体优先。
 * English: User-authorized full sans automatically covers titles; builtin UI appearance and explicit choices are preserved.
 */
#include "app_font_context.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "book_store.h"
#include "record_store_media.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "book/content_library.h"

static bool s_reading;
static bool s_document;
void app_font_set_document_mode(bool document){s_document=document;}

static const char* bundled_path(char path[TTF_FONT_PATH_MAX]) {
    struct stat st;
    if(snprintf(path,TTF_FONT_PATH_MAX,"%s/%s",app_settings_fonts_dir(),TTF_FONT_DEFAULT_FILE)<TTF_FONT_PATH_MAX&&stat(path,&st)==0&&S_ISREG(st.st_mode))return path;
    if(strcmp(app_settings_fonts_dir(),"/sdcard/fonts")){snprintf(path,TTF_FONT_PATH_MAX,"/sdcard/fonts/%s",TTF_FONT_DEFAULT_FILE);if(stat(path,&st)==0&&S_ISREG(st.st_mode))return path;}
    return NULL;
}

static const char* system_path(char fallback[TTF_FONT_PATH_MAX]) {
    const char* chosen = app_settings_system_font_path();
    if (chosen[0]) return chosen;
    if(bundled_path(fallback))return fallback;
    if (snprintf(fallback, TTF_FONT_PATH_MAX, "%s/Hei.ttf", app_settings_fonts_dir()) >= TTF_FONT_PATH_MAX)
        return TTF_FONT_BUILTIN;
    struct stat st;
    if (stat(fallback, &st) == 0 && S_ISREG(st.st_mode)) return fallback;
    if (!ttf_font_count()) ttf_font_scan();
    const ttf_font_item_t *item = ttf_font_item(0);
    return item ? item->path : TTF_FONT_BUILTIN;
}

static const char *reading_path(char fallback[TTF_FONT_PATH_MAX]) {
    const char *saved = s_document ? content_markdown_font() : app_settings_font_path();
    if (!ttf_font_path_is_builtin(saved)) return saved;
    const char *bundled = bundled_path(fallback);
    return bundled ? bundled : TTF_FONT_BUILTIN;
}

static bool activate(const char *path, bool system, esp_err_t *result) {
    char previous[TTF_FONT_PATH_MAX];
    strlcpy(previous,ttf_font_path(),sizeof(previous));
    bool was_ready = ttf_font_ready();
    esp_err_t err = ttf_font_select_context(system,path);
    if (result) *result = err;
    s_reading = !system;
    ttf_font_set_normalized_sizes(true);
    // 未选字体时由内建思源黑体绘制界面；完整 TTF 为所有动态标题补字。
    // Built-in Source Han Sans draws default UI; the full TTF remains loaded for missing glyphs.
    ui_text_set_system_font(system && app_settings_system_font_path()[0] &&
                            ttf_font_ready() && !ttf_font_is_builtin());
    ui_text_set_system_scale(system);
    // 只有实际生效的字体变化才触发主循环刷新。/ Only a change in the effective face requests a redraw.
    return ttf_font_ready() && (!was_ready || strcmp(previous,ttf_font_path()));
}

bool app_font_activate_system(void) {
    char fallback[TTF_FONT_PATH_MAX];
    return activate(system_path(fallback),true,NULL);
}

bool app_font_activate_reading(void) {
    char fallback[TTF_FONT_PATH_MAX];
    return activate(reading_path(fallback),false,NULL);
}

void app_font_restore_reading(const char *path) {
    (void)activate(path,false,NULL);
}

bool app_font_retry_active(void) {
    static char failed_path[TTF_FONT_PATH_MAX];
    static uint32_t failed_mount;
    static unsigned failed_revision;
    static bool failed_system;
    char fallback[TTF_FONT_PATH_MAX];
    bool system = !s_reading;
    const char *path = system ? system_path(fallback) : reading_path(fallback);
    uint32_t mount = record_store_media_epoch();
    unsigned revision = book_store_revision();
    if (failed_path[0] && failed_mount == mount && failed_revision == revision &&
        failed_system == system && !strcmp(failed_path,path)) return false;

    esp_err_t result;
    bool changed = activate(path,system,&result);
    if (result == ESP_OK) failed_path[0] = 0;
    else {
        // 周期任务等待字体选择或卡上文件变化后再重试失败项。/ Periodic retries wait for a new selection or card change after failure.
        strlcpy(failed_path,path,sizeof(failed_path));
        failed_mount = mount;
        failed_revision = revision;
        failed_system = system;
    }
    return changed;
}
