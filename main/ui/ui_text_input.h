/* SPDX-License-Identifier: Apache-2.0
 * 全系统共用的底部键盘；页面只负责校验及提交。/ Shared bottom keyboard; pages validate and commit.
 */
#pragma once
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_pinyin.h"

typedef enum { UI_INPUT_CHINESE, UI_INPUT_ENGLISH, UI_INPUT_SYMBOLS } ui_input_mode_t;
typedef enum { UI_INPUT_NONE, UI_INPUT_CHANGED, UI_INPUT_CANCEL, UI_INPUT_DONE } ui_input_result_t;
typedef struct {
    char *text;
    size_t capacity, cursor;
    bool chinese_allowed, secret, revealed, uppercase;
    uint8_t max_lines; ///< 默认单行，Banner可启用最多四行。/ Single line by default; Banner opts into up to four lines.
    ui_input_mode_t mode, letters_mode;
    char pinyin[33], notice[96];
    size_t candidate_page, candidate_count;
    char candidates[6][UI_PINYIN_WORD_BYTES];
    const char *suffix;
    unsigned symbol_page;
} ui_text_input_t;

// 所有页面使用同一几何，最后一排到屏幕底部仅留16px。/ Same geometry everywhere; 16px below the last row.
#define UI_INPUT_KEYBOARD_TOP 686
#define UI_INPUT_KEYS_TOP 766
#define UI_INPUT_KEYS_STEP 84
#define UI_INPUT_FOOTER_TOP 1104
void ui_text_input_open(ui_text_input_t *input, char *text, size_t capacity,
                        bool chinese_allowed, bool secret, ui_input_mode_t initial);
void ui_text_input_clear(ui_text_input_t *input);
void ui_text_input_draw(uint8_t *fb, const ui_text_input_t *input, const char *title,
                        const char *hint, const char *suffix, const char *done, const char *notice);
ui_input_result_t ui_text_input_gesture(ui_text_input_t *input, const ui_gesture_event_t *event);
EpdRect ui_text_input_area(void);
EpdRect ui_text_input_key_rect(int key);
// 测试及候选服务使用；不会绕过字节上限。/ Shared insertion obeys the caller's byte capacity.
bool ui_text_input_insert(ui_text_input_t *input, const char *utf8);
