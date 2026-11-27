/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "app.h"

typedef enum {
    STANDARD_FONT_HOME = 1, STANDARD_FONT_SETTINGS,
} standard_font_owner_t;

void ui_standard_font_open(standard_font_owner_t owner);
bool ui_standard_font_visible(standard_font_owner_t owner);
void ui_standard_font_draw(uint8_t *fb);
app_redraw_t ui_standard_font_tick(app_ctx_t *ctx);
app_redraw_t ui_standard_font_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev);
app_redraw_t ui_standard_font_back(void);
void ui_standard_font_leave(standard_font_owner_t owner);
void ui_standard_font_interrupt(standard_font_owner_t owner);
void ui_standard_font_media_changed(standard_font_owner_t owner, bool ready);
void ui_standard_font_wifi_finished(bool returned);
