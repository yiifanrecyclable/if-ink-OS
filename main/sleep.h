/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏、浅睡等待、软睡/关机下电。
 *
 * Lock, light-sleep wait, and soft-sleep / power-off rail drop.
 */

#pragma once

#include <stdint.h>

#include "epd_highlevel.h"
#include "esp_err.h"
#include "sc7a20h.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WAKE_NONE = 0,
    APP_WAKE_KEY,
    APP_WAKE_PICKUP,
    APP_WAKE_TIMEOUT,
} app_wake_source_t;

/// 当前页未确认保存时仅浅睡保留RAM，不进入深睡。/ Keep RAM in light sleep when page saves are unconfirmed.
void app_lock_set_save_confirmed(bool confirmed);
void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc,
    bool reader_background
);

/// 等电源键松开，避免进睡瞬间被同一下按住立刻唤醒。
/// Wait for the power key to release so the same press does not wake immediately.
void app_lock_wait_key_idle(int timeout_ms);
/// ESP 浅睡，按键或拿起唤醒。acc 为空则只等按键。
/// ESP light sleep; wake on key or pickup. Key only when acc is NULL.
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc);
/// ESP 浅睡至按键、拿起或超时；超时以毫秒计，0 表示不设期限。
/// ESP light sleep until key, pickup, or timeout; 0 means no deadline.
app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t acc, uint32_t timeout_ms);
app_wake_source_t app_last_wake_source(void);
/// 电源交接失败时返回错误，保留挂载及阅读状态；成功断电不返回。
/// Return an error on a failed handoff, retaining the mount and reader state; successful power-off never returns.
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode);
/// 仅在用户确认丢弃未保存数据后允许忽略同步错误；活动传输仍不打断。/ Ignore sync errors only after explicit discard confirmation, never interrupt active transfers.
esp_err_t app_enter_host_sleep_ex(app_sleep_mode_t mode,bool discard_unsaved);
/// 将当前系统时间写入常供电 PMU RTC；主控断电后由 PMU 继续走时。
/// Sync system time to the always-on PMU RTC so it keeps ticking while the host is off.
esp_err_t app_sync_time_to_pmu(void);
/// 让PMU重启；拒绝或未实际重启则返回错误，避免无限等待。/ Ask the PMU to restart; return on rejection or a missing reset instead of hanging.
esp_err_t app_restart_host(void);
esp_err_t app_restart_host_ex(bool discard_unsaved);

#ifdef __cplusplus
}
#endif
