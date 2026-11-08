/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅本地测试的终端模式，UI 独占显示，网络任务独占 socket。
 * English: Local-only terminal mode, with separate UI display and worker socket ownership.
 */
#include "app_inkbuddy.h"
#include "app_registry.h"
#include "app_transfer_mode.h"
#include "cloud_sync.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inkbuddy_wire.h"
#include "ota_online.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "settings_storage.h"
#include "shelf_perf.h"
#include "time_service.h"
#include "ui_font.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_text_input.h"
#include "usb_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TERMINAL_SETTINGS, TERMINAL_CONNECTING, TERMINAL_RUNNING, TERMINAL_DISCONNECTED, TERMINAL_FAILED };
enum { STATUS_OK, STATUS_OFFLINE, STATUS_SERVER, STATUS_BAD_FRAME };
enum { INTERVAL_MIN = 1, INTERVAL_MAX = 30, INTERVAL_DEFAULT = 5, TRACK_LEFT = 90, TRACK_RIGHT = 594 };
static const char *const tag = "inkbuddy";
static const EpdRect screen = {0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT};
static const EpdRect back_button = {24, 72, 84, 72};
static const EpdRect address_button = {36, 146, 612, 90};
static const EpdRect wifi_button = {36, 248, 612, 90};
static const EpdRect interval_track = {36, 380, 612, 58};
static const EpdRect interval_minus = {48, 442, 84, 64};
static const EpdRect interval_plus = {552, 442, 84, 64};
static const EpdRect discovery_button = {36, 524, 612, 68};
static const EpdRect connect_button = {172, 900, 340, 86};

typedef struct {
    char endpoint[INKBUDDY_ENDPOINT_BYTES];
    uint8_t interval;
} terminal_prefs_t;

typedef struct {
    atomic_bool cancelled, done;
    atomic_uint refs;
    bool discover;
    unsigned peer_count;
    char peers[INKBUDDY_PEERS][INKBUDDY_ENDPOINT_BYTES];
    inkbuddy_request_t request;
    uint8_t *pixels;
    uint32_t epoch;
    inkbuddy_frame_t frame;
    inkbuddy_result_t result;
    int64_t received_us;
} terminal_job_t;

static terminal_prefs_t s_prefs, s_saved;
static const char prefs_namespace[] = "if_inkbuddy";
static const char prefs_key[] = "terminal2";
static bool s_prefs_needs_migration;
static uint8_t *s_resume_pixels;
static int s_resume_state;
static char s_resume_endpoint[INKBUDDY_ENDPOINT_BYTES];
static char s_address[INKBUDDY_ENDPOINT_BYTES];
static ui_text_input_t s_input;
static terminal_job_t *s_job;
static atomic_bool s_worker_busy;
static bool s_own_wifi, s_retry_wifi, s_prefs_writable, s_crc_checked, s_editing, s_wifi_ready, s_shown, s_force_full, s_retried;
static int s_state;
static read_pico_transfer_state_t s_wifi_phase;
static unsigned s_peer_count, s_diffs, s_status;
static uint32_t s_epoch, s_revision;
static uint16_t s_minute;
static char s_peers[INKBUDDY_PEERS][INKBUDDY_ENDPOINT_BYTES];
static int64_t s_next_ms, s_wifi_retry_ms, s_hold_ms = -1;
static unsigned s_largest, s_full_ms, s_diff_ms;
static const char *s_message;

static const char *tr(const char *zh, const char *en) { return ui_i18n_english() ? en : zh; }

static void job_release(terminal_job_t *job) {
    if (atomic_fetch_sub(&job->refs, 1) != 1) return;
    free(job->pixels);
    free(job);
}

static void network_task(void *arg) {
    terminal_job_t *job = arg;
    if (job->discover) job->peer_count = inkbuddy_discover(&job->cancelled, job->peers);
    else job->result = inkbuddy_fetch(&job->request, &job->cancelled, job->pixels,
                                     &job->epoch, &job->frame, &job->received_us);
    atomic_store(&job->done, true);
    job_release(job);
    atomic_store(&s_worker_busy, false);
    vTaskDelete(NULL);
}

static void cancel_job(void) {
    if (!s_job) return;
    atomic_store(&s_job->cancelled, true);
    job_release(s_job);
    s_job = NULL;
}

static bool start_job(bool discover) {
    if (s_job || atomic_exchange(&s_worker_busy, true)) return false;
    terminal_job_t *job = calloc(1, sizeof(*job));
    if (job && !discover) {
        unsigned caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        s_largest = (unsigned)heap_caps_get_largest_free_block(caps);
        job->pixels = heap_caps_malloc(INKBUDDY_FRAME_BYTES, caps);
        ESP_LOGI(tag, "PSRAM largest=%u frame=%u allocated=%d", s_largest,
                 (unsigned)INKBUDDY_FRAME_BYTES, job->pixels != NULL);
        shelf_perf_event(SP_INKBUDDY_MEMORY, (int)s_largest, INKBUDDY_FRAME_BYTES, job->pixels != NULL, 0);
        if (!job->pixels) { free(job); job = NULL; }
        else if (!s_crc_checked) {
            memset(job->pixels, 0xff, INKBUDDY_FRAME_BYTES);
            uint32_t empty = inkbuddy_crc(job->pixels, 0);
            uint32_t sample = inkbuddy_crc((const uint8_t *)"123456789", 9);
            uint32_t white = inkbuddy_crc(job->pixels, INKBUDDY_FRAME_BYTES);
            ESP_LOGI(tag, "CRC empty=%08lx vector=%08lx white=%08lx",
                     (unsigned long)empty, (unsigned long)sample, (unsigned long)white);
            s_crc_checked = empty == 0 && sample == 0xcbf43926u && white == 0xf6dc332fu;
            shelf_perf_event(SP_INKBUDDY_CRC, (int)empty, (int)sample, (int)white, s_crc_checked);
            if (!s_crc_checked) {
                free(job->pixels);
                free(job);
                atomic_store(&s_worker_busy, false);
                s_message = tr("CRC 自检失败", "CRC self-test failed.");
                return false;
            }
        }
    }
    if (!job) {
        atomic_store(&s_worker_busy, false);
        s_message = tr("内存不足，请返回后重试", "Insufficient memory. Go back and retry.");
        return false;
    }
    atomic_init(&job->cancelled, false);
    atomic_init(&job->done, false);
    atomic_init(&job->refs, 2);
    job->discover = discover;
    memcpy(job->request.endpoint, s_prefs.endpoint, sizeof(s_prefs.endpoint));
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
        snprintf(job->request.device, sizeof(job->request.device), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    else memcpy(job->request.device, "000000", 7);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    int battery = pmu ? pmu_battery_percent(pmu) : -1;
    job->request.battery = battery >= 0 && battery <= 100 ? (unsigned)battery : 255;
    job->request.status = s_status;
    job->request.epoch = s_epoch;
    job->request.revision = s_revision;
    if (xTaskCreate(network_task, "inkbuddy_net", 8192, job, 4, NULL) != pdPASS) {
        job_release(job);
        job_release(job);
        atomic_store(&s_worker_busy, false);
        s_message = tr("无法启动网络任务", "Could not start the network task.");
        return false;
    }
    s_job = job;
    return true;
}

static bool decode_prefs(terminal_prefs_t *value, size_t size, bool legacy) {
    if (size != sizeof(*value) || !memchr(value->endpoint, 0, sizeof(value->endpoint))) return false;
    uint32_t ip;
    uint16_t port;
    if ((legacy || value->endpoint[0]) && !inkbuddy_endpoint(value->endpoint, &ip, &port)) return false;
    if (legacy) {
        // 旧版存选项下标，原 60 分钟映射到新上限。/ The old record stores an index. Map 60 minutes to the new maximum.
        static const uint8_t minutes[] = {1, 5, 15, 30, INTERVAL_MAX};
        if (value->interval >= sizeof(minutes) / sizeof(*minutes)) return false;
        value->interval = minutes[value->interval];
    }
    return value->interval >= INTERVAL_MIN && value->interval <= INTERVAL_MAX;
}

static void load_prefs(void) {
    s_prefs = (terminal_prefs_t){.interval = INTERVAL_DEFAULT};
    s_prefs_needs_migration = false;
    nvs_handle_t nvs;
    esp_err_t error = nvs_open(prefs_namespace, NVS_READONLY, &nvs);
    s_prefs_writable = error == ESP_ERR_NVS_NOT_FOUND;
    if (error == ESP_OK) {
        terminal_prefs_t value;
        size_t size = sizeof(value);
        error = nvs_get_blob(nvs, prefs_key, &value, &size);
        bool legacy = error == ESP_ERR_NVS_NOT_FOUND;
        s_prefs_writable = legacy;
        if (legacy) {
            size = sizeof(value);
            error = nvs_get_blob(nvs, "terminal", &value, &size);
        }
        if (error == ESP_OK && decode_prefs(&value, size, legacy)) {
            s_prefs = value;
            s_prefs_writable = true;
            s_prefs_needs_migration = legacy;
        }
        nvs_close(nvs);
    }
    s_saved = s_prefs;
}

static bool save_prefs(void) {
    if (!s_prefs_needs_migration && s_prefs.interval == s_saved.interval &&
        !strcmp(s_prefs.endpoint, s_saved.endpoint)) return true;
    if (!s_prefs_writable ||
        settings_storage_put_blob(prefs_namespace, prefs_key, &s_prefs, sizeof(s_prefs)) != ESP_OK)
        return false;
    s_saved = s_prefs;
    s_prefs_needs_migration = false;
    return true;
}

static void clear_resume_frame(void) {
    free(s_resume_pixels);
    s_resume_pixels = NULL;
    s_resume_state = TERMINAL_SETTINGS;
}

static void reset_screen_state(void) {
    s_shown = false;
    s_revision = s_epoch = 0;
    s_minute = UINT16_MAX;
    s_diffs = s_status = 0;
    s_force_full = true;
    s_retried = false;
    s_hold_ms = -1;
    s_wifi_retry_ms = 0;
}

static void release_wifi(void) {
    if (!s_own_wifi) return;
    s_own_wifi = false;
    app_time_service_stop();
    read_pico_transfer_stop();
    s_wifi_ready = false;
}

void inkbuddy_suspend(void) {
    cancel_job();
    clear_resume_frame();
    reset_screen_state();
    s_state = TERMINAL_SETTINGS;
    s_message = NULL;
    s_editing = false;
    ui_text_input_clear(&s_input);
}

bool inkbuddy_owns_frame(void) {
    return s_state == TERMINAL_RUNNING || s_state == TERMINAL_DISCONNECTED;
}

static bool wifi_ready(read_pico_transfer_status_t *status) {
    read_pico_transfer_get_status(status);
    return status->mode == READ_PICO_TRANSFER_MODE_STA && status->network_ready &&
           status->state == READ_PICO_TRANSFER_READY;
}

static esp_err_t start_saved_wifi(void) {
    // 使用已选配置，避免在 UI 线程同步扫网。/ Use the selected profile to avoid a synchronous UI-thread Wi-Fi scan.
    read_pico_transfer_cfg_t config = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true,
                                      .use_selected_wifi = true};
    esp_err_t error = read_pico_transfer_start(&config);
    if (error == ESP_OK) s_own_wifi = true;
    return error;
}

static bool network_busy(void) {
    cloud_status_t cloud;
    if_update_status_t update;
    cloud_get_status(&cloud);
    if_update_get_status(&update);
    return cloud.busy || update.busy || usb_storage_active();
}

static void connect_wifi(app_ctx_t *ctx) {
    read_pico_transfer_status_t net;
    if (wifi_ready(&net)) return;
    if (s_own_wifi && net.state == READ_PICO_TRANSFER_STARTING) return;
    if (s_own_wifi) release_wifi();
    if (net.state != READ_PICO_TRANSFER_STOPPED && net.state != READ_PICO_TRANSFER_ERROR) {
        s_message = tr("请先结束当前网络任务", "Finish the current network task first.");
        return;
    }
    if (network_busy()) {
        s_message = tr("请先结束同步、升级或 USB 连接", "Finish sync, update or USB first.");
        return;
    }
    if (net.state == READ_PICO_TRANSFER_ERROR && !read_pico_transfer_try_stop_if_idle()) {
        s_message = tr("请先结束文件传输", "Finish file transfer first.");
        return;
    }
    esp_err_t error = start_saved_wifi();
    if (error == ESP_OK) {
        s_retry_wifi = true;
        s_message = tr("正在连接已保存的 WiFi…", "Connecting to saved Wi-Fi…");
    } else if (error == ESP_ERR_NOT_FOUND) {
        extern const app_desc_t app_transfer;
        app_transfer_request_wifi_setup();
        ctx->request_app = &app_transfer;
    } else s_message = tr("无法连接 WiFi，请检查设置", "Could not connect to Wi-Fi.");
}

static const char *error_text(inkbuddy_result_t error) {
    switch (error) {
    case INKBUDDY_OFFLINE: return tr("连接已断开，请检查 WiFi 和电脑", "Disconnected. Check Wi-Fi and the computer.");
    case INKBUDDY_BAD_FRAME: return tr("数据校验失败", "Frame validation failed.");
    case INKBUDDY_FIRMWARE_OLD: return tr("桌面端版本过新，请升级固件", "Update Pico firmware to connect.");
    case INKBUDDY_DESKTOP_OLD: return tr("桌面端版本过旧，请升级 inkbuddy", "Update inkbuddy on the computer.");
    case INKBUDDY_PANEL: return tr("桌面端与本机面板不匹配", "The desktop panel format does not match.");
    default: return tr("服务器无响应或协议无效", "Server unavailable or incompatible.");
    }
}

static enum EpdDrawError display_frame(app_ctx_t *ctx, bool full) {
    enum EpdDrawError error = full
        ? update_display_area_full_with(ctx->hl, &E0470_FULL_WAVEFORM, MODE_GC16, screen)
        : update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, screen);
    if (error != EPD_DRAW_SUCCESS) guard_draw_result(ctx->hl, error);
    return error;
}

static void draw_disconnected(app_ctx_t *ctx) {
    uint8_t map[256];
    for (unsigned i = 0; i < sizeof(map); ++i) {
        unsigned lo = 3 + ((i & 15) * 10 + 7) / 15;
        unsigned hi = 3 + ((i >> 4) * 10 + 7) / 15;
        map[i] = (uint8_t)(hi << 4 | lo);
    }
    for (size_t i = 0; i < INKBUDDY_FRAME_BYTES; ++i) ctx->fb[i] = map[ctx->hl->back_fb[i]];
    char line[96];
    const char *label = tr("连接已断开", "Disconnected");
    if (s_minute == UINT16_MAX) snprintf(line, sizeof(line), "%s", label);
    else snprintf(line, sizeof(line), "%02u:%02u %s", (unsigned)s_minute / 60, (unsigned)s_minute % 60, label);
    epd_fill_rect((EpdRect){112, 563, 460, 90}, UI_GRAY_WHITE, ctx->fb);
    if (ui_font_has_text(line)) {
        int above = 0, below = 0;
        ui_font_measure_line_px(34, line, &above, &below);
        ui_font_draw_text_px(ctx->fb, 342, 608 + (above - below) / 2, 34, line,
                             EPD_DRAW_ALIGN_CENTER, UI_INK_BLACK, UI_INK_WHITE, false);
    } else ui_text_fixed_vc(ctx->fb, 342, 608, 34, line, EPD_DRAW_ALIGN_CENTER, false);
    (void)display_frame(ctx, s_force_full);
}

static app_redraw_t failed(app_ctx_t *ctx, inkbuddy_result_t error) {
    s_status = error == INKBUDDY_OFFLINE ? STATUS_OFFLINE :
               error == INKBUDDY_BAD_FRAME ? STATUS_BAD_FRAME : STATUS_SERVER;
    s_revision = 0;
    s_message = error_text(error);
    s_next_ms = ctx->now_ms + (int64_t)s_prefs.interval * 60000;
    if (!s_shown) {
        s_state = TERMINAL_FAILED;
        return APP_REDRAW_PAGE;
    }
    if (s_state == TERMINAL_DISCONNECTED) return APP_REDRAW_NONE;
    s_state = TERMINAL_DISCONNECTED;
    draw_disconnected(ctx);
    s_force_full = true;
    return APP_REDRAW_DONE;
}

static app_redraw_t accept_frame(app_ctx_t *ctx, terminal_job_t *job) {
    if (job->result == INKBUDDY_UNCHANGED) {
        shelf_perf_event(SP_INKBUDDY_UNCHANGED, 0, 0, 0, 0);
        s_retried = false;
        s_status = STATUS_OK;
        s_next_ms = ctx->now_ms + (int64_t)s_prefs.interval * 60000;
        return APP_REDRAW_NONE;
    }
    if (job->result != INKBUDDY_FRAME) {
        if (job->result == INKBUDDY_BAD_FRAME && !s_retried) {
            s_retried = true;
            s_status = STATUS_BAD_FRAME;
            s_revision = 0;
            s_next_ms = ctx->now_ms;
            return APP_REDRAW_NONE;
        }
        s_retried = false;
        return failed(ctx, job->result);
    }
    bool full = s_force_full || (job->frame.flags & 1) || s_diffs >= 8;
    memcpy(ctx->fb, job->pixels, INKBUDDY_FRAME_BYTES);
    int64_t draw_start = esp_timer_get_time();
    enum EpdDrawError result = display_frame(ctx, full);
    int64_t end = esp_timer_get_time();
    ESP_LOGI(tag, "frame=%lu full=%d receive_to_screen_us=%lld draw_us=%lld result=%d",
             (unsigned long)job->frame.revision, full, (long long)(end - job->received_us),
             (long long)(end - draw_start), result);
    shelf_perf_event(SP_INKBUDDY_PRESENT, full, result, (int)(end - job->received_us), (int)(end - draw_start));
    if (result != EPD_DRAW_SUCCESS) {
        // 物理基准可能只完成部分更新。/ A failed refresh may have changed only part of the panel.
        s_shown = false;
        s_force_full = true;
        s_revision = 0;
        s_state = TERMINAL_FAILED;
        s_message = tr("屏幕刷新失败，请重试", "Display refresh failed. Please retry.");
        return APP_REDRAW_FULL;
    }
    if (full) s_full_ms = (unsigned)((end - job->received_us) / 1000);
    else s_diff_ms = (unsigned)((end - draw_start) / 1000);
    s_diffs = full ? 0 : s_diffs + 1;
    s_epoch = job->epoch;
    s_revision = job->frame.revision;
    s_minute = job->frame.minute;
    s_state = TERMINAL_RUNNING;
    s_status = STATUS_OK;
    s_shown = true;
    s_retried = s_force_full = false;
    s_next_ms = end / 1000 + (int64_t)s_prefs.interval * 60000;
    return APP_REDRAW_DONE;
}

static int interval_from_x(int x) {
    if (x <= TRACK_LEFT) return INTERVAL_MIN;
    if (x >= TRACK_RIGHT) return INTERVAL_MAX;
    int length = TRACK_RIGHT - TRACK_LEFT;
    return INTERVAL_MIN + ((x - TRACK_LEFT) * (INTERVAL_MAX - INTERVAL_MIN) + length / 2) / length;
}

static app_redraw_t change_interval(int minutes) {
    if (minutes < INTERVAL_MIN || minutes > INTERVAL_MAX || minutes == s_prefs.interval)
        return APP_REDRAW_NONE;
    s_prefs.interval = (uint8_t)minutes;
    return APP_REDRAW_PAGE;
}

static EpdRect peer_button(unsigned i) { return (EpdRect){36, 606 + (int)i * 70, 612, 62}; }

static void button(uint8_t *fb, EpdRect area, const char *label, bool active) {
    ui_fill_round_rect(fb, area, 9, active ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    ui_draw_round_rect(fb, area, 9, UI_GRAY_BLACK);
    int px = ui_text_fit_px(UI_TEXT_BODY, label, area.width - 16, area.height - 10);
    ui_text_fixed_vc(fb, area.x + area.width / 2, area.y + area.height / 2, px, label, EPD_DRAW_ALIGN_CENTER, active);
}

static void draw_interval(uint8_t *fb) {
    ui_text_vc(fb, 342, 358, UI_TEXT_BODY, tr("自动抓取间隔", "Automatic refresh interval"), EPD_DRAW_ALIGN_CENTER, false);
    int knob = TRACK_LEFT + ((s_prefs.interval - INTERVAL_MIN) * (TRACK_RIGHT - TRACK_LEFT) +
                           (INTERVAL_MAX - INTERVAL_MIN) / 2) / (INTERVAL_MAX - INTERVAL_MIN);
    ui_fill_round_rect(fb, (EpdRect){TRACK_LEFT, 402, TRACK_RIGHT - TRACK_LEFT, 8}, 4, 0xb0);
    if (knob > TRACK_LEFT) epd_fill_rect((EpdRect){TRACK_LEFT, 402, knob - TRACK_LEFT, 8}, UI_GRAY_BLACK, fb);
    epd_fill_circle(knob, 406, 14, UI_GRAY_BLACK, fb);
    button(fb, interval_minus, "−", false);
    button(fb, interval_plus, "+", false);
    char label[24];
    snprintf(label, sizeof(label), "%u %s", (unsigned)s_prefs.interval, tr("分钟", "min"));
    ui_text_vc(fb, 342, 474, UI_TEXT_BODY, label, EPD_DRAW_ALIGN_CENTER, false);
}

static void draw_key_guide(uint8_t *fb) {
    const char *labels[] = {
        tr("立即刷新", "Refresh now"),
        tr("长按 2 秒退出", "Hold 2s to exit"),
        tr("设置／返回画面", "Settings / Display"),
    };
    for (int i = 0; i < 3; ++i) {
        int x = UI_LOCK_WIDTH * (2 * i + 1) / 6;
        int px = ui_text_fit_px(UI_TEXT_CAPTION, labels[i], UI_LOCK_WIDTH / 3 - 20, 40);
        ui_text_fixed_vc(fb, x, 1125, px, labels[i], EPD_DRAW_ALIGN_CENTER, false);
        epd_draw_line(x, 1153, x, 1181, UI_GRAY_BLACK, fb);
        epd_draw_line(x, 1181, x - 8, 1173, UI_GRAY_BLACK, fb);
        epd_draw_line(x, 1181, x + 8, 1173, UI_GRAY_BLACK, fb);
    }
}

static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    if (inkbuddy_owns_frame()) return;
    epd_fill_rect(screen, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    if (s_editing) {
        ui_text_input_draw(fb, &s_input, tr("服务器地址", "Server address"), "IPv4:port",
                           NULL, tr("完成", "Done"), s_message);
        return;
    }
    ui_nav_close_in(fb, back_button);
    ui_text_vc(fb, 342, 107, UI_TEXT_TITLE, tr("终端模式", "Terminal mode"), EPD_DRAW_ALIGN_CENTER, false);
    if (s_state == TERMINAL_SETTINGS) {
        button(fb, address_button, s_prefs.endpoint[0] ? s_prefs.endpoint : tr("输入服务器 IPv4:端口", "Enter server IPv4:port"), false);
        const char *wifi_label = s_wifi_ready ? tr("WiFi 已连接", "Wi-Fi connected") :
            s_own_wifi && s_wifi_phase == READ_PICO_TRANSFER_STARTING ? tr("正在连接 WiFi…", "Connecting to Wi-Fi…") :
            tr("WiFi 未连接 · 点此连接", "Wi-Fi disconnected · Tap to connect");
        button(fb, wifi_button, wifi_label, false);
        draw_interval(fb);
        button(fb, discovery_button, s_job && s_job->discover ? tr("正在查找…", "Searching…") :
               tr("查找局域网电脑", "Find computers"), false);
        for (unsigned i = 0; i < s_peer_count; ++i) button(fb, peer_button(i), s_peers[i], !strcmp(s_prefs.endpoint, s_peers[i]));
    }
    if (s_state == TERMINAL_CONNECTING)
        ui_text_vc(fb, 342, 519, UI_TEXT_BODY, tr("正在连接 inkbuddy…", "Connecting to inkbuddy…"), EPD_DRAW_ALIGN_CENTER, false);
    if (s_message) {
        int px = ui_text_fit_px(UI_TEXT_BODY, s_message, 608, 60);
        ui_text_fixed_vc(fb, 342, 835, px, s_message, EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_state != TERMINAL_CONNECTING)
        button(fb, connect_button, s_state == TERMINAL_FAILED ? tr("重试", "Retry") : tr("连接", "Connect"), true);
    if (s_largest) {
        char diagnostic[96];
        snprintf(diagnostic, sizeof(diagnostic), "PSRAM %u KiB · Full %u ms · Diff %u ms", s_largest / 1024, s_full_ms, s_diff_ms);
        ui_text_vc(fb, 342, 1032, UI_TEXT_CAPTION, diagnostic, EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_state == TERMINAL_SETTINGS) draw_key_guide(fb);
}

static app_redraw_t begin_connection(void) {
    read_pico_transfer_status_t network;
    if (network_busy()) {
        s_message = tr("请先结束同步、升级或 USB 连接", "Finish sync, update or USB first.");
        return APP_REDRAW_PAGE;
    }
    if (!wifi_ready(&network)) {
        s_state = TERMINAL_SETTINGS;
        s_message = tr("WiFi 未连接，请先连接", "Connect Wi-Fi first.");
        return APP_REDRAW_PAGE;
    }
    uint32_t ip;
    uint16_t port;
    if (!inkbuddy_endpoint(s_prefs.endpoint, &ip, &port)) {
        s_state = TERMINAL_SETTINGS;
        s_message = tr("请输入正确的 IPv4 地址和端口", "Enter a valid IPv4 address and port.");
        return APP_REDRAW_PAGE;
    }
    if (epd_width() != INKBUDDY_WIDTH || epd_height() != INKBUDDY_HEIGHT) {
        s_state = TERMINAL_FAILED;
        s_message = error_text(INKBUDDY_PANEL);
        return APP_REDRAW_PAGE;
    }
    cancel_job();
    clear_resume_frame();
    reset_screen_state();
    s_message = NULL;
    s_state = TERMINAL_CONNECTING;
    s_next_ms = 0;
    if (!save_prefs()) s_message = tr("本次仍可连接，设置未保存", "Connecting. Settings were not saved.");
    return APP_REDRAW_PAGE;
}

static void open_terminal_settings(app_ctx_t *ctx) {
    cancel_job();
    // 设置界面会覆盖两份显示缓冲。/ Settings redraw overwrites both display buffers.
    s_resume_state = s_state;
    memcpy(s_resume_endpoint, s_prefs.endpoint, sizeof(s_resume_endpoint));
    s_resume_pixels = heap_caps_malloc(INKBUDDY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_resume_pixels) memcpy(s_resume_pixels, ctx->hl->back_fb, INKBUDDY_FRAME_BYTES);
    s_revision = 0;
    s_state = TERMINAL_SETTINGS;
    s_message = NULL;
}

static app_redraw_t resume_terminal(app_ctx_t *ctx) {
    if (s_resume_state == TERMINAL_SETTINGS) {
        s_message = tr("尚未连接，请先连接服务器", "No frame yet. Connect to the server first.");
        return APP_REDRAW_PAGE;
    }
    if (!s_resume_pixels || strcmp(s_resume_endpoint, s_prefs.endpoint)) return begin_connection();
    if (!save_prefs()) ESP_LOGW(tag, "Could not save settings before restoring the display");
    cancel_job();
    int state = s_resume_state;
    memcpy(ctx->fb, s_resume_pixels, INKBUDDY_FRAME_BYTES);
    clear_resume_frame();
    if (display_frame(ctx, true) != EPD_DRAW_SUCCESS) {
        reset_screen_state();
        s_state = TERMINAL_FAILED;
        s_message = tr("屏幕刷新失败，请重试", "Display refresh failed. Please retry.");
        return APP_REDRAW_FULL;
    }
    s_state = state;
    s_diffs = 0;
    s_force_full = state == TERMINAL_DISCONNECTED;
    s_next_ms = esp_timer_get_time() / 1000 + (int64_t)s_prefs.interval * 60000;
    return APP_REDRAW_DONE;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    if (event->type == UI_GESTURE_CANCEL) s_hold_ms = -1;
    if (s_editing) {
        ui_input_result_t result = ui_text_input_gesture(&s_input, event);
        if (result == UI_INPUT_NONE) return APP_REDRAW_NONE;
        if (result == UI_INPUT_DONE) {
            uint32_t ip;
            uint16_t port;
            if (!inkbuddy_endpoint(s_address, &ip, &port)) {
                s_message = tr("请输入正确的 IPv4 地址和端口", "Enter a valid IPv4 address and port.");
                return APP_REDRAW_PAGE;
            }
            snprintf(s_prefs.endpoint, sizeof(s_prefs.endpoint), "%s", s_address);
        }
        if (result == UI_INPUT_DONE || result == UI_INPUT_CANCEL) {
            s_editing = false;
            s_message = NULL;
            ui_text_input_clear(&s_input);
        }
        return APP_REDRAW_PAGE;
    }
    if (event->type != UI_GESTURE_TAP || inkbuddy_owns_frame()) return APP_REDRAW_NONE;
    int x = event->x0, y = event->y0;
    if (ui_rect_hit(back_button, x, y)) {
        ctx->request_app = app_at(2);
        return APP_REDRAW_NONE;
    }
    if (s_state != TERMINAL_CONNECTING && ui_rect_hit(connect_button, x, y)) return begin_connection();
    if (s_state != TERMINAL_SETTINGS) return APP_REDRAW_NONE;
    if (ui_rect_hit(address_button, x, y)) {
        memcpy(s_address, s_prefs.endpoint, sizeof(s_address));
        ui_text_input_open(&s_input, s_address, sizeof(s_address), false, false, UI_INPUT_ENGLISH);
        s_editing = true;
        s_message = NULL;
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit(wifi_button, x, y)) {
        connect_wifi(ctx);
        return ctx->request_app ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
    }
    if (ui_rect_hit(interval_track, x, y)) return change_interval(interval_from_x(x));
    if (ui_rect_hit(interval_minus, x, y)) return change_interval(s_prefs.interval - 1);
    if (ui_rect_hit(interval_plus, x, y)) return change_interval(s_prefs.interval + 1);
    if (ui_rect_hit(discovery_button, x, y)) {
        if (s_job) return APP_REDRAW_NONE;
        s_message = NULL;
        if (!s_wifi_ready) s_message = tr("WiFi 未连接，请先连接", "Connect Wi-Fi first.");
        else if (!start_job(true) && !s_message) s_message = tr("请稍后重试", "Try again shortly.");
        return APP_REDRAW_PAGE;
    }
    for (unsigned i = 0; i < s_peer_count; ++i)
        if (ui_rect_hit(peer_button(i), x, y)) {
            memcpy(s_prefs.endpoint, s_peers[i], sizeof(s_prefs.endpoint));
            return APP_REDRAW_PAGE;
        }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if (key == UI_KEY_2 && ctx->touch && ctx->touch->count == 1) s_hold_ms = ctx->now_ms;
    if (key == UI_KEY_1 && inkbuddy_owns_frame() && !s_job) s_next_ms = 0;
    if (key != UI_KEY_3) return APP_REDRAW_NONE;
    if (s_editing) {
        s_editing = false;
        s_message = NULL;
        ui_text_input_clear(&s_input);
    } else if (s_state == TERMINAL_SETTINGS) return resume_terminal(ctx);
    else if (inkbuddy_owns_frame()) open_terminal_settings(ctx);
    else inkbuddy_suspend();
    return APP_REDRAW_PAGE;
}

static app_redraw_t on_tick(app_ctx_t *ctx) {
    bool touching = ctx->touch && ctx->touch->touched;
    if (s_hold_ms >= 0) {
        bool middle = ctx->touch && ui_key_hit_test(ctx->touch->x, ctx->touch->y) == UI_KEY_2;
        bool valid = middle && ((touching && ctx->touch->count == 1) || ctx->released);
        if (!valid) s_hold_ms = -1;
        else if (ctx->now_ms - s_hold_ms >= 2000) {
            inkbuddy_suspend();
            ctx->request_app = app_home_page();
            return APP_REDRAW_NONE;
        } else if (ctx->released) s_hold_ms = -1;
    }
    // 按住实体键时不推屏，避免同步刷新打断退出计时。/ Avoid synchronous panel updates while a key is held.
    if (touching || ctx->released) return APP_REDRAW_NONE;
    if (s_job && atomic_load(&s_job->done)) {
        terminal_job_t *job = s_job;
        s_job = NULL;
        app_redraw_t redraw = APP_REDRAW_NONE;
        if (job->discover) {
            memcpy(s_peers, job->peers, sizeof(s_peers));
            s_peer_count = job->peer_count;
            if (!s_editing) {
                s_message = s_peer_count ? NULL : tr("未发现电脑，可手输地址", "No computers found. Enter the address manually.");
                redraw = APP_REDRAW_PAGE;
            }
        } else redraw = accept_frame(ctx, job);
        job_release(job);
        return redraw;
    }
    read_pico_transfer_status_t net;
    bool ready = wifi_ready(&net);
    if (s_state == TERMINAL_SETTINGS) {
        if (s_editing || (ready == s_wifi_ready && net.state == s_wifi_phase)) return APP_REDRAW_NONE;
        s_wifi_ready = ready;
        s_wifi_phase = net.state;
        if (ready) s_message = NULL;
        else if (s_own_wifi && net.state == READ_PICO_TRANSFER_ERROR)
            s_message = tr("WiFi 连接失败，请检查已选网络", "Wi-Fi connection failed. Check the saved network.");
        return APP_REDRAW_PAGE;
    }
    if (s_state == TERMINAL_FAILED || s_job || ctx->now_ms < s_next_ms || atomic_load(&s_worker_busy))
        return APP_REDRAW_NONE;
    if (!ready) {
        if (s_retry_wifi && ctx->now_ms >= s_wifi_retry_ms &&
            (net.state == READ_PICO_TRANSFER_STOPPED || (s_own_wifi && net.state == READ_PICO_TRANSFER_ERROR))) {
            release_wifi();
            esp_err_t error = start_saved_wifi();
            app_redraw_t redraw = failed(ctx, INKBUDDY_OFFLINE);
            s_wifi_retry_ms = s_next_ms;
            // 给现有 STA 连接器完成重连的时间。/ Give the existing STA connector time to finish its retry.
            if (error == ESP_OK) s_next_ms = ctx->now_ms + 2000;
            return redraw;
        }
        if (s_retry_wifi && net.state == READ_PICO_TRANSFER_STARTING) {
            app_redraw_t redraw = failed(ctx, INKBUDDY_OFFLINE);
            s_next_ms = ctx->now_ms + 2000;
            return redraw;
        }
        return failed(ctx, INKBUDDY_OFFLINE);
    }
    if (!start_job(false)) {
        const char *error = s_message;
        if (inkbuddy_owns_frame()) inkbuddy_suspend();
        s_state = TERMINAL_FAILED;
        s_message = error ? error : tr("内存或任务资源不足，请重试", "Insufficient memory or task resources. Retry.");
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static bool present(app_ctx_t *ctx, app_redraw_t redraw) {
    (void)ctx;
    (void)redraw;
    return inkbuddy_owns_frame();
}

static void on_enter(app_ctx_t *ctx) {
    (void)ctx;
    inkbuddy_suspend();
    load_prefs();
    read_pico_transfer_status_t status;
    s_wifi_ready = wifi_ready(&status);
    s_peer_count = 0;
    ui_nav_notice_clear();
    s_wifi_phase = status.state;
}

static void terminal_exit(app_ctx_t *ctx) {
    (void)ctx;
    inkbuddy_suspend();
    s_retry_wifi = false;
    release_wifi();
}
static esp_err_t prepare_sleep(app_ctx_t *ctx) { terminal_exit(ctx); return ESP_OK; }
static bool prevent_idle(app_ctx_t *ctx) {
    (void)ctx;
    return s_state == TERMINAL_CONNECTING || inkbuddy_owns_frame() || s_job;
}
static bool hide_handle(app_ctx_t *ctx) { (void)ctx; return false; }

const app_desc_t app_inkbuddy = {
    .title = "inkbuddy",
    .owns_keys = true,
    .menu_handle_enabled = hide_handle,
    .on_enter = on_enter,
    .on_exit = terminal_exit,
    .prepare_sleep = prepare_sleep,
    .prevent_idle_sleep = prevent_idle,
    .render = render,
    .present = present,
    .on_gesture = on_gesture,
    .on_key = on_key,
    .on_tick = on_tick,
};
