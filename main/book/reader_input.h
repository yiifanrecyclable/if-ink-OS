/* SPDX-License-Identifier: Apache-2.0
 * 阅读快捷操作及双指识别；不绘制、不驱动屏幕。/ Reader shortcuts and two-contact recognition, without drawing or panel driving.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    READER_SWIPE_LEFT, READER_SWIPE_RIGHT, READER_SWIPE_UP, READER_SWIPE_DOWN,
    READER_HOLD, READER_TWO_TAP, READER_PINCH_IN, READER_PINCH_OUT,
    READER_SHORTCUT_COUNT
} reader_shortcut_t;
typedef enum {
    READER_ACTION_NONE, READER_ACTION_PREV, READER_ACTION_NEXT, READER_ACTION_TOC,
    READER_ACTION_BOOKMARKS, READER_ACTION_PANEL, READER_ACTION_FULLSCREEN,
    READER_ACTION_SMALLER, READER_ACTION_LARGER, READER_ACTION_BOOKMARK,
    READER_ACTION_REFRESH, READER_ACTION_COUNT
} reader_action_t;
enum { READER_STORED_SHORTCUT_COUNT = 12 };
typedef enum { READER_TAP_COLUMNS, READER_TAP_ROWS } reader_tap_layout_t;
typedef struct { int x,y,width,height; } reader_tap_region_t;
typedef struct {
    uint8_t version, actions[READER_STORED_SHORTCUT_COUNT], hide_help;
} reader_input_preferences_t;
const char *reader_shortcut_name(unsigned slot);
const char *reader_action_name(unsigned action);
reader_action_t reader_input_action(unsigned slot);
/// 旧预留槽保存基础区域，维持备份结构长度；v1默认三列。/ Version 2 uses a retired slot without changing backup length; v1 defaults to columns.
reader_tap_layout_t reader_input_tap_layout(void);
esp_err_t reader_input_set_tap_layout(reader_tap_layout_t layout);
reader_tap_region_t reader_input_tap_region(reader_tap_layout_t layout,unsigned zone,int width,int height);
esp_err_t reader_input_assign(unsigned slot, unsigned action);
bool reader_input_preferences_valid(const reader_input_preferences_t *prefs);
void reader_input_export(reader_input_preferences_t *prefs);
esp_err_t reader_input_import(const reader_input_preferences_t *prefs);
bool reader_input_needs_help(const char *path);
esp_err_t reader_input_ack_help(const char *path, bool hide_all);

// 双指结束后只产生一次动作，单指残留期间不提交。/ Emit once after both contacts lift, never on a remaining finger.
typedef struct {
    bool active, blocked;
    int x0, y0, x, y, span0, span;
    int64_t started;
    int movement;
} reader_multi_t;
int reader_multi_feed(reader_multi_t *state, bool enabled, bool down, unsigned count,
                      int ax, int ay, int bx, int by, int64_t now);
