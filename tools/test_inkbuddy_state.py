#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise production frame disposition, terminal keys, and settings migration."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
app = (ROOT / "main/apps/app_inkbuddy.c").read_text()

def function(name):
    start = re.search(rf"^\w[^\n]*\b{name}\([^\n]*\) \{{", app, re.M).start()
    newline = app.index("\n", start)
    end = newline if app[start:newline].endswith("}") else app.index("\n}", start) + 2
    return app[start:end]

hold = function("on_tick").split("    // 按住实体键", 1)[0] + "    return APP_REDRAW_NONE;\n}"
source = r'''
#include "inkbuddy_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef enum { APP_REDRAW_NONE, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_DONE } app_redraw_t;
enum EpdDrawError { EPD_DRAW_SUCCESS, TEST_FAILURE };
typedef struct { bool touched; int count; uint16_t x, y; } touch_t;
typedef struct { uint8_t *back_fb; } hl_t;
typedef struct {
    uint8_t *fb;
    hl_t *hl;
    int64_t now_ms;
    const touch_t *touch;
    bool released;
    const void *request_app;
} app_ctx_t;
typedef struct {
    char endpoint[INKBUDDY_ENDPOINT_BYTES];
    uint8_t interval;
} terminal_prefs_t;
typedef struct {
    inkbuddy_result_t result;
    uint8_t *pixels;
    uint32_t epoch;
    inkbuddy_frame_t frame;
    int64_t received_us;
} terminal_job_t;
enum { TERMINAL_SETTINGS, TERMINAL_CONNECTING, TERMINAL_RUNNING, TERMINAL_DISCONNECTED, TERMINAL_FAILED };
enum { STATUS_OK, STATUS_OFFLINE, STATUS_SERVER, STATUS_BAD_FRAME };
enum { UI_KEY_1 = 7, UI_KEY_2, UI_KEY_3 };
enum { INTERVAL_MIN = 1, INTERVAL_MAX = 30, INTERVAL_DEFAULT = 5, TRACK_LEFT = 90, TRACK_RIGHT = 594 };
enum { ESP_OK, ESP_ERR_NVS_NOT_FOUND, TEST_NVS_ERROR, NVS_READONLY };
typedef int esp_err_t;
typedef int nvs_handle_t;
static int s_state = TERMINAL_CONNECTING, s_status;
static terminal_prefs_t s_prefs = {.interval = INTERVAL_DEFAULT}, s_saved;
static const char prefs_namespace[] = "if_inkbuddy";
static const char prefs_key[] = "terminal2";
static bool s_prefs_writable, s_prefs_needs_migration;
static uint8_t *s_resume_pixels;
static int s_resume_state, s_input;
static char s_address[INKBUDDY_ENDPOINT_BYTES];
static char s_peers[INKBUDDY_PEERS][INKBUDDY_ENDPOINT_BYTES];
static unsigned s_peer_count;
static bool s_wifi_ready;
static char s_resume_endpoint[INKBUDDY_ENDPOINT_BYTES];
static bool s_editing, memory_failure;
static terminal_job_t *s_job;
static terminal_prefs_t legacy_record, current_record;
static int legacy_status = ESP_ERR_NVS_NOT_FOUND, current_status = ESP_ERR_NVS_NOT_FOUND;
static int write_status, writes, reconnects;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static void *heap_caps_malloc(size_t size, unsigned caps){
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return memory_failure ? NULL : malloc(size);
}
bool inkbuddy_endpoint(const char *text, uint32_t *ip, uint16_t *port){
    (void)ip; (void)port;
    return !strcmp(text, "192.168.1.1:8791") || !strcmp(text, "192.168.1.2:8791");
}
static int nvs_open(const char *name, int mode, nvs_handle_t *handle){
    assert(!strcmp(name, prefs_namespace) && mode == NVS_READONLY);
    *handle = 42;
    return ESP_OK;
}
static int nvs_get_blob(nvs_handle_t handle, const char *key, void *record, size_t *size){
    assert(handle == 42 && *size == sizeof(terminal_prefs_t));
    if (!strcmp(key, "terminal")) {
        if (legacy_status == ESP_OK) memcpy(record, &legacy_record, *size);
        return legacy_status;
    }
    assert(!strcmp(key, "terminal2"));
    if (current_status == ESP_OK) memcpy(record, &current_record, *size);
    return current_status;
}
static void nvs_close(nvs_handle_t handle){ assert(handle == 42); }
static int settings_storage_put_blob(const char *name, const char *key, const void *record, size_t size){
    assert(!strcmp(name, prefs_namespace) && !strcmp(key, "terminal2") && size == sizeof(current_record));
    ++writes;
    if (write_status == ESP_OK) {
        memcpy(&current_record, record, size);
        current_status = ESP_OK;
    }
    return write_status;
}
static bool s_shown, s_retried, s_force_full = true;
static int64_t s_wifi_retry_ms;
static unsigned s_full_ms, s_diff_ms, s_diffs;
static int64_t s_next_ms, s_hold_ms = -1;
static uint16_t s_minute;
static uint32_t s_epoch, s_revision;
static const char *s_message, *tag;
static enum EpdDrawError draw_error;
static unsigned pushes, grey, cancellations;
static bool last_full;
typedef struct { int x, y, width, height; } EpdRect;
enum { UI_GESTURE_CANCEL, UI_GESTURE_MOVE, UI_GESTURE_TAP };
typedef struct { int type, x0, y0; } ui_gesture_event_t;
enum { UI_INPUT_NONE, UI_INPUT_DONE, UI_INPUT_CANCEL, UI_INPUT_ENGLISH };
typedef int ui_input_result_t;
static ui_input_result_t ui_text_input_gesture(int *input, const ui_gesture_event_t *event) {
    (void)input; (void)event;
    return UI_INPUT_NONE;
}
static void ui_text_input_open(int *input, char *value, size_t size, bool secret, bool multiline, int language) {
    (void)input; (void)value; (void)size; (void)secret; (void)multiline; (void)language;
}
static bool ui_rect_hit(EpdRect area, int x, int y) {
    return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
}
static const void *app_at(int index) { assert(index == 2); return &grey; }
static void connect_wifi(app_ctx_t *ctx) { (void)ctx; }
static bool start_job(bool discover) { (void)discover; return false; }
#define ESP_LOGI(...) ((void)tag)
#define ESP_LOGW(...) ((void)tag)
#define shelf_perf_event(...) ((void)0)
static const char *tr(const char *zh, const char *en){ (void)en; return zh; }
static const char *error_text(inkbuddy_result_t error){ (void)error; return "failure"; }
static int64_t esp_timer_get_time(void){ return 123000; }
static enum EpdDrawError display_frame(app_ctx_t *ctx, bool full){
    (void)ctx;
    pushes++;
    last_full = full;
    return draw_error;
}
static void draw_disconnected(app_ctx_t *ctx){ (void)ctx; grey++; }
static void cancel_job(void){ if (s_job) { ++cancellations; s_job = NULL; } }
static void ui_text_input_clear(int *input){ (void)input; }
static app_redraw_t begin_connection(void){ ++reconnects; return APP_REDRAW_PAGE; }
static int ui_key_hit_test(uint16_t x, uint16_t y){ (void)y; return x; }
static const void *app_home_page(void){ return &pushes; }
'''
names = (
    "decode_prefs", "load_prefs", "save_prefs", "clear_resume_frame", "reset_screen_state",
    "inkbuddy_suspend", "inkbuddy_owns_frame", "failed", "accept_frame", "interval_from_x",
    "change_interval", "peer_button", "open_terminal_settings", "resume_terminal", "on_gesture", "on_key",
)
source += "\n".join(re.findall(r"^static const EpdRect (?!screen\b)[^\n]+;", app, re.M)) + "\n"
source += "\n".join(map(function, names)) + "\n" + hold + r'''
static void test_prefs(void) {
    static const unsigned mapped[] = {1, 5, 15, 30, 30};
    load_prefs();
    assert(s_prefs.interval == 5 && s_prefs_writable && !s_prefs_needs_migration);
    assert(save_prefs() && !writes);
    strcpy(legacy_record.endpoint, "192.168.1.1:8791");
    legacy_status = ESP_OK;
    for (unsigned i = 0; i < 5; ++i) {
        legacy_record.interval = i;
        load_prefs();
        assert(s_prefs_needs_migration && s_prefs.interval == mapped[i]);
        assert(!strcmp(s_prefs.endpoint, legacy_record.endpoint));
    }
    legacy_record.interval = 1;
    load_prefs();
    write_status = TEST_NVS_ERROR;
    assert(!save_prefs() && s_prefs_needs_migration && current_status == ESP_ERR_NVS_NOT_FOUND);
    write_status = ESP_OK;
    assert(save_prefs() && !s_prefs_needs_migration && current_record.interval == 5);
    assert(legacy_record.interval == 1);
    assert(change_interval(1) == APP_REDRAW_PAGE && save_prefs());
    load_prefs();
    assert(s_prefs.interval == 1 && !s_prefs_needs_migration);
    assert(change_interval(0) == APP_REDRAW_NONE && change_interval(1) == APP_REDRAW_NONE);
    assert(change_interval(30) == APP_REDRAW_PAGE && change_interval(31) == APP_REDRAW_NONE);
    assert(interval_from_x(36) == 1 && interval_from_x(TRACK_LEFT) == 1);
    assert(interval_from_x(342) == 16);
    assert(interval_from_x(TRACK_RIGHT) == 30 && interval_from_x(648) == 30);
    for (int x = TRACK_LEFT + 1; x <= TRACK_RIGHT; ++x)
        assert(interval_from_x(x) >= interval_from_x(x - 1));
    current_record.interval = 31;
    load_prefs();
    assert(s_prefs.interval == 5 && !s_prefs_writable && !s_prefs_needs_migration);
    assert(change_interval(2) == APP_REDRAW_PAGE && !save_prefs());
    assert(current_record.interval == 31);
    current_record.interval = 5;
    load_prefs();
}

static void test_tap_controls(app_ctx_t *ctx) {
    inkbuddy_suspend();
    assert(s_prefs.interval == 5);
    ui_gesture_event_t event = {.type = UI_GESTURE_MOVE, .x0 = TRACK_RIGHT, .y0 = 405};
    assert(on_gesture(ctx, &event) == APP_REDRAW_NONE && s_prefs.interval == 5);
    event.type = UI_GESTURE_TAP;
    assert(on_gesture(ctx, &event) == APP_REDRAW_PAGE && s_prefs.interval == 30);
    event.y0 = 474;
    assert(on_gesture(ctx, &event) == APP_REDRAW_NONE && s_prefs.interval == 30);
    event.x0 = TRACK_LEFT;
    assert(on_gesture(ctx, &event) == APP_REDRAW_PAGE && s_prefs.interval == 29);
    event.y0 = 405;
    assert(on_gesture(ctx, &event) == APP_REDRAW_PAGE && s_prefs.interval == 1);
    event.y0 = 474;
    assert(on_gesture(ctx, &event) == APP_REDRAW_NONE && s_prefs.interval == 1);
    event.x0 = TRACK_RIGHT;
    assert(on_gesture(ctx, &event) == APP_REDRAW_PAGE && s_prefs.interval == 2);
    event.x0 = 342;
    event.y0 = 108;
    assert(on_gesture(ctx, &event) == APP_REDRAW_NONE && !ctx->request_app);
    event.x0 = 66;
    assert(on_gesture(ctx, &event) == APP_REDRAW_NONE && ctx->request_app == app_at(2));
    ctx->request_app = NULL;
    load_prefs();
}

static void test_right_key(app_ctx_t *ctx, terminal_job_t *job) {
    unsigned before = pushes;
    unsigned cancelled = cancellations;
    inkbuddy_suspend();
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE);
    assert(s_state == TERMINAL_SETTINGS && s_message && !ctx->request_app && pushes == before);

    s_state = TERMINAL_RUNNING;
    s_shown = true;
    s_epoch = 41;
    s_revision = 5;
    s_job = job;
    memset(ctx->hl->back_fb, 0xe2, INKBUDDY_FRAME_BYTES);
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE);
    assert(s_state == TERMINAL_SETTINGS && !s_job && cancellations == cancelled + 1 && !s_revision);
    assert(s_resume_pixels && pushes == before);
    memset(ctx->hl->back_fb, 0xff, INKBUDDY_FRAME_BYTES);
    assert(change_interval(7) == APP_REDRAW_PAGE);
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_DONE);
    assert(s_state == TERMINAL_RUNNING && last_full && pushes == before + 1);
    assert(!s_resume_pixels && !ctx->request_app && !s_revision && s_epoch == 41);
    assert(!memcmp(ctx->fb, job->pixels, INKBUDDY_FRAME_BYTES));
    assert(s_next_ms == 123 + 7 * 60000 && current_record.interval == 7);
    on_key(ctx, UI_KEY_1);
    assert(!s_next_ms);

    s_state = TERMINAL_DISCONNECTED;
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE);
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_DONE && s_state == TERMINAL_DISCONNECTED && s_force_full);
    assert(!s_resume_pixels && !ctx->request_app);

    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE);
    s_editing = true;
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE && !s_editing && s_resume_pixels);
    strcpy(s_prefs.endpoint, "192.168.1.2:8791");
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE && reconnects == 1 && !ctx->request_app);
    inkbuddy_suspend();
    assert(!s_resume_pixels);
    memory_failure = true;
    s_state = TERMINAL_RUNNING;
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE && !s_resume_pixels);
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE && reconnects == 2 && !ctx->request_app);
    memory_failure = false;
    inkbuddy_suspend();
    s_state = TERMINAL_RUNNING;
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_PAGE && s_resume_pixels);
    draw_error = TEST_FAILURE;
    assert(on_key(ctx, UI_KEY_3) == APP_REDRAW_FULL && s_state == TERMINAL_FAILED);
    assert(!s_resume_pixels && !s_revision && !ctx->request_app);
    draw_error = EPD_DRAW_SUCCESS;
    inkbuddy_suspend();
}

int main(void) {
    test_prefs();
    static uint8_t front[INKBUDDY_FRAME_BYTES], pixels[INKBUDDY_FRAME_BYTES], back[INKBUDDY_FRAME_BYTES];
    hl_t hl = {.back_fb = back};
    s_state = TERMINAL_CONNECTING;
    memset(pixels, 0xe2, sizeof(pixels));
    app_ctx_t ctx = {.fb = front, .hl = &hl};
    terminal_job_t job = {.result = INKBUDDY_FRAME, .epoch = 41, .frame = {.revision=5, .minute=0}, .pixels=pixels};
    assert(accept_frame(&ctx, &job) == APP_REDRAW_DONE);
    assert(pushes == 1 && last_full && s_shown && s_revision == 5 && s_epoch == 41);
    assert(!memcmp(front, pixels, sizeof(front)));

    job.result = INKBUDDY_UNCHANGED;
    assert(accept_frame(&ctx, &job) == APP_REDRAW_NONE && pushes == 1 && !grey);
    job.result = INKBUDDY_BAD_FRAME;
    assert(accept_frame(&ctx, &job) == APP_REDRAW_NONE && pushes == 1 && !grey && !s_revision);
    assert(accept_frame(&ctx, &job) == APP_REDRAW_DONE && pushes == 1 && grey == 1);
    job.result = INKBUDDY_OFFLINE;
    assert(accept_frame(&ctx, &job) == APP_REDRAW_NONE && grey == 1);
    job.result = INKBUDDY_FRAME;
    assert(accept_frame(&ctx, &job) == APP_REDRAW_DONE && last_full && s_revision == 5);
    for (unsigned i = 0; i < 8; ++i) {
        ++job.frame.revision;
        assert(accept_frame(&ctx, &job) == APP_REDRAW_DONE && !last_full);
    }
    ++job.frame.revision;
    assert(accept_frame(&ctx, &job) == APP_REDRAW_DONE && last_full);
    draw_error = TEST_FAILURE;
    ++job.frame.revision;
    assert(accept_frame(&ctx, &job) == APP_REDRAW_FULL && !s_revision && s_force_full);

    draw_error = EPD_DRAW_SUCCESS;
    test_tap_controls(&ctx);
    test_right_key(&ctx, &job);
    cancellations = 0;
    s_job = &job;
    touch_t touch = {.touched=true,.count=1,.x=UI_KEY_2};
    ctx.touch = &touch;
    ctx.now_ms = 2099;
    s_hold_ms = 100;
    on_tick(&ctx);
    assert(!ctx.request_app && !cancellations);
    ctx.now_ms = 2100;
    on_tick(&ctx);
    assert(ctx.request_app == app_home_page() && cancellations == 1 && !s_resume_pixels);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="inkbuddy-state-") as directory:
    c = Path(directory) / "test.c"
    binary = c.with_suffix("")
    c.write_text(source)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(ROOT / "main/apps"), str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: 304 zero refresh, rejected-frame rev=0, disconnect idempotence, recovery, full-refresh budget, 2-second exit, KEY3 round-trip, minute bounds and legacy settings migration.")
