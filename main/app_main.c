/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 只做开机装配：拉起板级硬件、读设置、定 VCOM、放开机图、开字体；
 * 未标定则先拦住进设定页。然后把控制权交给 app_loop。
 *
 * Boot wiring only: board init, settings, PMU RTC restore, VCOM, splash, fonts. If VCOM
 * is unset, the factory page blocks first. Then control goes to app_loop.
 */

#include <stdint.h>
#include <sys/time.h>

#include "app_loop.h"
#include "sleep_resume.h"
#include "app_content_open.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "read_pico_sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_registry.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "esp_log.h"
#include "ota_update.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_init.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "settings.h"
#include "shelf_perf.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_font.h"
#include "ui_kit.h"
#include "vcom_setup.h"
#ifdef IFINK_WAKE_TIMING_TEST
#include "esp_timer.h"
#define WAKE_SCOPE SHELF_PERF_SCOPE
#else
#define WAKE_SCOPE(...) ((void)0)
#endif

#ifdef IFINK_WAKE_TONE_TEST
#include "read_pico_buzzer.h"
#endif

static const char* TAG = "read_pico";

static bool is_key_resume_boot(bool pmu_ready) {
    if (!pmu_ready || !sleep_resume_pending()) return false;
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    enum { PMU_WAKE_REASON_KEY = 2 };
    return pmu && pmu->status_ok && pmu->wake_reason == PMU_WAKE_REASON_KEY;
}

#ifdef IFINK_WAKE_TONE_TEST
static void mark_key_wake(void) {
    // 提示音用于录像与诊断对时。/ The tone aligns diagnostic recordings with the video.
    enum { WAKE_TONE_HZ = 2700, WAKE_TONE_MS = 80 };
    SHELF_PERF_SCOPE(tone, SP_WAKE_TONE, WAKE_TONE_MS, 0);
    tone.b = read_pico_buzzer_tone(WAKE_TONE_HZ, WAKE_TONE_MS);
}
#endif

static void present_wake_notice(EpdiyHighlevelState *hl, uint8_t *fb) {
    SHELF_PERF_SCOPE(notice, SP_WAKE_NOTICE, 0, 0);
    const char *text = ui_i18n_english() ? "Starting…" : "启动中";
    const int size = 36;
    int above = 0, below = 0;
    ui_font_measure_line_px(size, text, &above, &below);
    if (above + below <= 0) return;
    const EpdRect area = {UI_LOCK_WIDTH / 2 - 120, UI_LOCK_HEIGHT / 2 - 40, 240, 80};
    epd_fill_rect(area, UI_GRAY_WHITE, fb);
    ui_font_draw_text_px(fb, UI_LOCK_WIDTH / 2, UI_LOCK_HEIGHT / 2 + (above - below) / 2,
                         size, text, EPD_DRAW_ALIGN_CENTER, UI_INK_BLACK, UI_INK_WHITE, false);
    // 此处已有校验过的壁纸参考；即使局刷失败，完整首帧也会重新清屏。
    // The validated wallpaper is our baseline. The first complete frame clears even after a failed hint.
    notice.a = 1;
    notice.b = update_display_area_diff_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
}

// 主机上电会丢失系统时间；PMU 在主机断电后继续走时。先取实时值再恢复，
// 不使用启动时缓存的快照，以免将已经经过的开机耗时丢掉。
static void restore_time_from_pmu(void) {
    if (read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0) != ESP_OK) return;
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (!pmu || !pmu->time_synced || pmu->unix_sec < 1704067200 || pmu->unix_sec > 4102444799U) return;
    struct timeval tv = {.tv_sec = (time_t)pmu->unix_sec, .tv_usec = 0};
    if (settimeofday(&tv, NULL) == 0) ESP_LOGI(TAG, "system clock restored from PMU");
}

extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");
extern const uint8_t lock_4bpp_bin_end[] asm("_binary_lock_4bpp_bin_end");
extern const uint8_t loading_4bpp_bin_start[] asm("_binary_loading_4bpp_bin_start");
extern const uint8_t loading_4bpp_bin_end[] asm("_binary_loading_4bpp_bin_end");

// 已标定则开机读一次喂给 epdiy。未标定先用板级默认出开机图，随后拦住进标定页。
// Load VCOM once when set. Otherwise keep the board default for the splash,
// then gate into the setup page.
static bool resolve_vcom_at_boot(void) {
    int mv = 0;
    for (int i = 0; i < 3; i++) {
        if (read_pico_pmu_vcom_get(&mv) == ESP_OK) {
            epd_set_vcom((uint16_t)mv);
            ESP_LOGI(TAG, "panel VCOM loaded from PMU");
            return true;
        }
    }
    ESP_LOGW(TAG, "panel VCOM unset, keeping board default until setup");
    return false;
}

// 开机图和锁屏图是预先转好的 4bpp 全屏位图，尺寸对不上说明资源和面板不匹配。
// Splash and lock images are pre-baked 4bpp full-frames. A size mismatch
// means the assets do not match the panel.
static bool images_match_panel(void) {
    size_t lock_size = (size_t)(lock_4bpp_bin_end - lock_4bpp_bin_start);
    size_t loading_size = (size_t)(loading_4bpp_bin_end - loading_4bpp_bin_start);
    size_t expected = (size_t)epd_width() * epd_height() / 2;
    if (lock_size == expected && loading_size == expected) return true;
    ESP_LOGE(
        TAG, "Image size mismatch: lock %u loading %u expected %u",
        (unsigned)lock_size, (unsigned)loading_size, (unsigned)expected
    );
    return false;
}

void app_main(void) {
    const esp_reset_reason_t reset_reason = esp_reset_reason();
#ifdef IFINK_WAKE_TIMING_TEST
    shelf_perf_init();
    shelf_perf_event(SP_WAKE_BOOT, reset_reason, (int)esp_timer_get_time(), 0, 0);
#endif
    {
        WAKE_SCOPE(settings, SP_WAKE_SETTINGS, 0, 0);
        app_settings_init();
        sleep_resume_init(esp_app_get_description()->app_elf_sha256,
                          reset_reason == ESP_RST_POWERON || reset_reason == ESP_RST_DEEPSLEEP);
    }
    read_pico_handle_t hw;
    {
        WAKE_SCOPE(board, SP_WAKE_BOARD, 0, 0);
        if (read_pico_init(&hw) != ESP_OK) return;
    }
    const bool key_wake = is_key_resume_boot(hw.pmu_ready);
#ifdef IFINK_WAKE_TONE_TEST
    if (key_wake) mark_key_wake();
#endif
    bool vcom_ok;
    {
        WAKE_SCOPE(pmu, SP_WAKE_PMU, hw.pmu_ready, 0);
        if (hw.pmu_ready) restore_time_from_pmu();
        vcom_ok = resolve_vcom_at_boot();
    }
    pmu_selftest_bind(hw.sensor);

    if (!images_match_panel()) return;

    EpdiyHighlevelState hl = hw.hl;
    uint8_t* framebuffer = hw.framebuffer;

    sleep_resume_target_t resume={0};
    bool restored=false;
    const bool resume_pending = vcom_ok && sleep_resume_pending();
    if(resume_pending){
        read_pico_sd_info_t card={0};
        {
            WAKE_SCOPE(sd_wait, SP_WAKE_SD_WAIT, 0, 0);
            for(unsigned wait=0;wait<150&&read_pico_sd_get_info(&card)==ESP_ERR_NOT_FINISHED;++wait)vTaskDelay(pdMS_TO_TICKS(20));
        }
        {
            WAKE_SCOPE(restore, SP_WAKE_RESTORE, card.mounted, 0);
            restored=card.present&&card.mounted&&sleep_resume_restore(framebuffer,hl.back_fb,(size_t)epd_width()*epd_height()/2,(unsigned)epd_get_rotation(),&resume);
        }
    }
#ifdef IFINK_WAKE_TIMING_TEST
    const pmu_snapshot_t *boot_pmu = hw.pmu_ready ? read_pico_pmu_get() : NULL;
    shelf_perf_event(SP_WAKE_RESUME_RESULT, resume_pending, restored, restored ? (int)resume.tab : -1,
                     boot_pmu && boot_pmu->status_ok ? boot_pmu->wake_reason : -1);
#endif
    if (restored) {
        if (key_wake && hw.touch_ready) present_wake_notice(&hl, framebuffer);
        display_resume_clean_next();
    } else {
        WAKE_SCOPE(splash, SP_WAKE_SPLASH, 0, 0);
        epd_poweron();read_pico_i2c_census_take();epd_clear();epd_hl_set_all_white(&hl);
        ui_draw_full_image(framebuffer,loading_4bpp_bin_start);update_display_from_white(&hl);
    }
    const app_desc_t *first=app_home_page();
    bool reader_resume=restored&&resume.path[0];
    {
        WAKE_SCOPE(font, SP_WAKE_FONT, reader_resume, 0);
        if(reader_resume){
            (void)ttf_font_open_builtin();
            if(app_book_request_open(resume.path))first=app_at(1);
            else {reader_resume=false;app_font_activate_system();}
        }else{
            app_font_activate_system();if(restored)first=app_at(resume.tab);
        }
    }

    if (!hw.touch_ready) {
        ESP_LOGE(TAG, "No touch controller, UI cannot run");
        return;
    }

    if (hw.pmu_ready && !vcom_ok) {
        vcom_setup_run(&hl, framebuffer, hw.touch);
    }

    // 硬件、显示、触摸与必要的出厂设置均成功后确认新槽。
    // Confirm a new slot only after hardware, display, touch, and required setup succeed.
    pico_ota_confirm_running();

    app_loop_run(&(app_loop_config_t){
        .hl = &hl,
        .fb = framebuffer,
        .acc = hw.sensor,
        .tp = hw.touch,
        .sensor_ready = hw.sensor_ready,
        // 正式固件始终进入产品首页；工厂自检不再进入用户导航。
        // Production firmware always enters the product home; factory diagnostics stay outside user navigation.
        .first_app = first,
        .reader_resume = reader_resume,
    });
}
