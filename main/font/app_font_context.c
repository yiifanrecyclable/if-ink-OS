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

static bool activate(const char* path,bool system) {
    bool changed=!ttf_font_ready()||strcmp(ttf_font_path(),path)!=0;
    return ttf_font_select_context(system,path)==ESP_OK&&changed;
}

bool app_font_activate_system(void) {
    s_reading = false;
    const char* chosen = app_settings_system_font_path();
    char fallback[TTF_FONT_PATH_MAX];
    const char* path = system_path(fallback);
    bool changed = activate(path,true);
    // 未选字体时由内建思源黑体绘制界面；完整 TTF 为所有动态标题补字。
    // Built-in Source Han Sans draws default UI; the full TTF remains loaded for missing glyphs.
    ui_text_set_system_font(chosen[0] && ttf_font_ready() && !ttf_font_is_builtin());
    ui_text_set_system_scale(true);
    return changed;
}

bool app_font_activate_reading(void) {
    s_reading = true;
    const char* saved = s_document?content_markdown_font():app_settings_font_path();
    if(!saved[0])saved=app_settings_font_path();
    char fallback[TTF_FONT_PATH_MAX];const char *path=saved;
    if(ttf_font_path_is_builtin(saved)){const char *bundled=bundled_path(fallback);path=bundled?bundled:TTF_FONT_BUILTIN;}
    bool changed = activate(path,false);
    ui_text_set_system_font(false);
    ui_text_set_system_scale(false);
    return changed;
}

bool app_font_retry_active(void) {
    return s_reading ? app_font_activate_reading() : app_font_activate_system();
}
