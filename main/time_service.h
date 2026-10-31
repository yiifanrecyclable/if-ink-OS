/* SPDX-License-Identifier: Apache-2.0
 * 已连接WiFi的有界后台校时；调用者为UI任务。/ Bounded time calibration over an existing WiFi connection, owned by the UI task.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/// 空闲十秒后尝试；每小时最多成功校准一次，保存上次成功时间并重试PMU同步，不自行联网。
/// Try after ten idle seconds, persist successful calibration and retry PMU sync without starting WiFi.
void app_time_service_poll(int64_t now_ms,bool idle);
/// 前台联网或睡眠优先；只关闭本服务拥有的SNTP。/ Foreground networking and sleep take priority; close only our SNTP instance.
void app_time_service_stop(void);
