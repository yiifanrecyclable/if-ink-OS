"""Exercise root pagination with overflow and the remaining Nutstore help scrolling."""
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'main/apps/app_device_settings.c').read_text()


def function(name):
    start = re.search(r'^static [^\n]+\b'+name+r'\([^;{}]*\)\s*\{', source, re.M)
    assert start, name
    end = source.index('\n}', start.end()) + 2
    return source[start.start():end]


unit = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
typedef struct { int x, y, width, height; } EpdRect;
typedef struct { int opened; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE } app_redraw_t;
typedef enum { UI_GESTURE_TAP, UI_GESTURE_PRESS, UI_GESTURE_MOVE, UI_GESTURE_CANCEL,
               UI_GESTURE_SWIPE_U, UI_GESTURE_SWIPE_D } gesture_t;
typedef struct { gesture_t type; int x0, y0, x, y; } ui_gesture_event_t;
#define UI_NAV_TOP 1136
#define UI_LOCK_WIDTH 684
#define UI_SUBPAGE_RULE_Y 127
#define UI_SECTION_LINE_HEIGHT 2
#define UI_PAGE_Y(y) ((y) - 24)
""" + re.search(r'enum \{\s*SETTINGS_GROUP_TOP\s*=.*?\};', source, re.S)[0] + r"""
#define SETTINGS_ROW_H 80
""" + re.search(r'^#define SETTINGS_LIST_ROW_H .+$', source, re.M)[0] + r"""
typedef enum { SETTINGS_PERSONAL, SETTINGS_DEVICE, SETTINGS_GROUP_COUNT } settings_group_t;
typedef struct { int id; } setting_item_t;
static const setting_item_t personal[] = {{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}};
static const setting_item_t device[] = {{100}};
static const struct { const setting_item_t *items; int count; } settings_groups[] = {
    {personal, sizeof(personal)/sizeof(personal[0])}, {device, sizeof(device)/sizeof(device[0])},
};
static settings_group_t s_main_group;
static int s_main_page;
static bool s_scroll_drag_consumed, s_content_present_pending;
static EpdRect ui_nav_page_arrow_rect(int direction, int y) {
    return (EpdRect){direction < 0 ? 16 : 572, y - 32, 96, 64};
}
static int ui_list_visible_rows(int top, int bottom, int height) { return (bottom - top) / height; }
static bool ui_rect_hit(EpdRect r, int x, int y) {
    return x >= r.x && x < r.x+r.width && y >= r.y && y < r.y+r.height;
}
static app_redraw_t settings_item_open(app_ctx_t *ctx, int id) {
    ctx->opened = id;
    return APP_REDRAW_PAGE;
}
""" + '\n'.join(function(name) for name in (
    'settings_list_rows', 'settings_list_pages', 'settings_list_page',
    'settings_group_rect', 'settings_list_row_rect', 'settings_list_redraw',
    'settings_list_turn', 'settings_tapped', 'settings_main_tap', 'scroll_gesture',
)) + r"""
static app_redraw_t tap(app_ctx_t *ctx, int x, int y) {
    ui_gesture_event_t event = {UI_GESTURE_TAP,x,y,x,y};
    return settings_main_tap(ctx,&event);
}

int main(void) {
    app_ctx_t ctx = {.opened = -1};
    const int rows = settings_list_rows();
    assert(rows < (int)(sizeof(personal)/sizeof(personal[0])));
    assert(settings_list_pages() == 2 && settings_list_page() == 0);
    assert(settings_list_turn(-1) == APP_REDRAW_NONE);
    assert(tap(&ctx,620,SETTINGS_LIST_PAGER_Y) == APP_REDRAW_AREA);
    assert(settings_list_page() == 1 && s_content_present_pending);
    assert(tap(&ctx,620,SETTINGS_LIST_PAGER_Y) == APP_REDRAW_NONE);
    assert(tap(&ctx,400,SETTINGS_LIST_TOP+10) == APP_REDRAW_PAGE && ctx.opened == rows);
    assert(tap(&ctx,400,SETTINGS_LIST_TOP+(rows-1)*SETTINGS_LIST_ROW_H+10) == APP_REDRAW_NONE);
    assert(tap(&ctx,64,SETTINGS_LIST_PAGER_Y) == APP_REDRAW_AREA && settings_list_page() == 0);
    assert(settings_list_turn(1) == APP_REDRAW_AREA && settings_list_page() == 1);
    assert(tap(&ctx,500,SETTINGS_GROUP_TOP+20) == APP_REDRAW_AREA && s_main_group == SETTINGS_DEVICE);
    assert(settings_list_page() == 0 && settings_list_pages() == 1);
    assert(settings_list_turn(1) == APP_REDRAW_NONE);
    assert(tap(&ctx,400,SETTINGS_LIST_TOP+10) == APP_REDRAW_PAGE && ctx.opened == 100);
    assert(tap(&ctx,170,SETTINGS_GROUP_TOP+20) == APP_REDRAW_AREA && s_main_group == SETTINGS_PERSONAL);
    assert(settings_list_page() == 0);

    bool handled = false;
    int scroll = 0;
    ui_gesture_event_t ev = {UI_GESTURE_MOVE,300,700,300,570};
    assert(scroll_gesture(&ev,&scroll,334,&handled) == APP_REDRAW_AREA && handled && scroll == 204);
    ev.type = UI_GESTURE_SWIPE_U;
    assert(scroll_gesture(&ev,&scroll,334,&handled) == APP_REDRAW_NONE && scroll == 204);
    assert(scroll_gesture(&ev,&scroll,334,&handled) == APP_REDRAW_AREA && scroll == 334);
    ev.y0 = UI_PAGE_Y(190)-1;
    assert(scroll_gesture(&ev,&scroll,334,&handled) == APP_REDRAW_NONE && scroll == 334);
}
"""

with tempfile.TemporaryDirectory() as folder:
    c, binary = Path(folder)/'test.c', Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: root overflow paging, row routing, group reset, arrow bounds and Nutstore help scrolling')
