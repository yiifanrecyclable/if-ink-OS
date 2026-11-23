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
#include "font_path_policy.h"
#include "book/content_library.h"

static bool s_reading;
static bool s_english;
static char s_english_path[TTF_FONT_PATH_MAX];
typedef esp_err_t (*font_setter_t)(const char *);
static esp_err_t set_english_reading(const char *path) { return content_english_font_set(false, path); }
static const font_setter_t choices[2][2] = {
    [APP_FONT_READING] = {app_settings_set_font_path, set_english_reading},
    [APP_FONT_SYSTEM] = {app_settings_set_system_font_path, content_english_system_font_set},
};

static bool s_system_english_cached;
void app_font_set_english_mode(bool english){
    s_english = english;
    s_english_path[0] = 0;
    if (english) strlcpy(s_english_path, content_english_font(false), sizeof(s_english_path));
}

static const char* bundled_path(char path[TTF_FONT_PATH_MAX]) {
    struct stat st;
    if(snprintf(path,TTF_FONT_PATH_MAX,"%s/%s",app_settings_fonts_dir(),TTF_FONT_DEFAULT_FILE)<TTF_FONT_PATH_MAX&&stat(path,&st)==0&&S_ISREG(st.st_mode))return path;
    if(strcmp(app_settings_fonts_dir(),"/sdcard/fonts")){snprintf(path,TTF_FONT_PATH_MAX,"/sdcard/fonts/%s",TTF_FONT_DEFAULT_FILE);if(stat(path,&st)==0&&S_ISREG(st.st_mode))return path;}
    return NULL;
}

typedef struct {
    char path[TTF_FONT_PATH_MAX];
    uint32_t epoch;
    unsigned revision;
    uint8_t coverage;
} face_coverage_t;

static uint8_t saved_face_coverage(face_coverage_t *checked, const char *path) {
    uint32_t next_epoch = record_store_media_epoch();
    unsigned next_revision = book_store_revision();
    if (checked->epoch != next_epoch || checked->revision != next_revision || strcmp(checked->path, path)) {
        checked->epoch = next_epoch;
        checked->revision = next_revision;
        strlcpy(checked->path, path, sizeof(checked->path));
        checked->coverage = ttf_font_path_coverage(path);
    }
    return checked->coverage;
}

static bool system_face_valid(face_coverage_t *checked, const char *path) {
    return app_font_path_valid(path) && (saved_face_coverage(checked, path) & TTF_COVERAGE_CHINESE);
}

esp_err_t app_font_save_choice(app_font_role_t role, bool english, const char *path) {
    if ((unsigned)role > APP_FONT_SYSTEM || !path || (*path && !app_font_path_valid(path))) return ESP_ERR_INVALID_ARG;
    static face_coverage_t checked;
    if (role == APP_FONT_SYSTEM && *path && !system_face_valid(&checked, path)) return ESP_ERR_NOT_SUPPORTED;
    const char *stored = role == APP_FONT_SYSTEM && english && !*path ? TTF_FONT_BUILTIN : path;
    esp_err_t err = choices[role][english](stored);
    if (err == ESP_OK && role == APP_FONT_SYSTEM) s_system_english_cached = false;
    return err;
}

const char *app_font_system_choice(void) {
    static face_coverage_t checked, english_checked;
    const char *chosen = app_settings_system_font_path();
    if (app_settings_language() == APP_LANGUAGE_EN) {
        static char english[TTF_FONT_PATH_MAX];
        static unsigned revision;
        static uint32_t media;
        unsigned fresh_revision = book_store_revision();
        uint32_t fresh_media = record_store_media_epoch();
        if (!s_system_english_cached || revision != fresh_revision || media != fresh_media) {
            english[0] = 0;
            (void)content_english_system_font_checked(english, sizeof(english));
            s_system_english_cached = true;
            revision = fresh_revision;
            media = fresh_media;
        }
        if (!strcmp(english, TTF_FONT_BUILTIN)) return "";
        if (system_face_valid(&english_checked, english)) return english;
    }
    return system_face_valid(&checked, chosen) ? chosen : "";
}

static const char* system_path(char fallback[TTF_FONT_PATH_MAX]) {
    static face_coverage_t bundled_checked, legacy_checked, first_checked;
    const char* chosen = app_font_system_choice();
    if (chosen[0]) return chosen;
    if (bundled_path(fallback) && system_face_valid(&bundled_checked, fallback)) return fallback;
    int length = snprintf(fallback, TTF_FONT_PATH_MAX, "%s/Hei.ttf", app_settings_fonts_dir());
    if (length > 0 && length < TTF_FONT_PATH_MAX && system_face_valid(&legacy_checked, fallback)) return fallback;
    if (app_settings_language() == APP_LANGUAGE_EN) return TTF_FONT_BUILTIN;
    if (!ttf_font_count()) ttf_font_scan();
    const ttf_font_item_t *eligible = ttf_font_picker_item(0, TTF_COVERAGE_CHINESE);
    if (eligible) return eligible->path;
    const ttf_font_item_t *first = ttf_font_item(0);
    return first && system_face_valid(&first_checked, first->path) ? first->path : TTF_FONT_BUILTIN;
}

static const char *compatible_reading_path(char fallback[TTF_FONT_PATH_MAX]) {
    static face_coverage_t checked;
    const char *saved = app_settings_font_path();
    // Preserve legacy preferences, using a Chinese face for mixed text when a saved face is Latin-only.
    if (app_font_path_valid(saved) &&
        (s_english || saved_face_coverage(&checked, saved) != TTF_COVERAGE_LATIN)) return saved;
    const char *bundled = bundled_path(fallback);
    return bundled ? bundled : TTF_FONT_BUILTIN;
}

const char *app_font_reading_choice(void) {
    return s_english && s_english_path[0] ? s_english_path : app_settings_font_path();
}
static const char *reading_path(char fallback[TTF_FONT_PATH_MAX]) {
    static face_coverage_t checked;
    return s_english && app_font_path_valid(s_english_path) &&
        (saved_face_coverage(&checked, s_english_path) & TTF_COVERAGE_LATIN) ?
        s_english_path : compatible_reading_path(fallback);
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
    ui_text_set_system_font(system && app_font_system_choice()[0] &&
                            ttf_font_ready() && !ttf_font_is_builtin());
    ui_text_set_system_scale(system);
    // 只有实际生效的字体变化才触发主循环刷新。/ Only a change in the effective face requests a redraw.
    return ttf_font_ready() && (!was_ready || strcmp(previous,ttf_font_path()));
}

bool app_font_activate_system(void) {
    char fallback[TTF_FONT_PATH_MAX];
    return activate(system_path(fallback),true,NULL);
}

bool app_font_reading_active(void) { return s_reading; }

bool app_font_activate_reading(void) {
    char fallback[TTF_FONT_PATH_MAX];
    esp_err_t result;
    bool changed = activate(reading_path(fallback), false, &result);
    if (result != ESP_OK && s_english)
        changed = activate(compatible_reading_path(fallback), false, &result) || changed;
    if (result != ESP_OK && (!ttf_font_ready() || (!s_english && !ttf_font_has_text("中文")))) {
        const char *path = bundled_path(fallback);
        changed = activate(path ? path : TTF_FONT_BUILTIN, false, NULL) || changed;
    }
    return changed;
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
