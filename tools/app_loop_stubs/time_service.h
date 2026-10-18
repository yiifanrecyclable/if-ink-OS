/* SPDX-License-Identifier: Apache-2.0
 * 调度测试不启动网络校时。/ Scheduler tests do not start network time synchronization.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline void app_time_service_poll(int64_t ms,bool idle){(void)ms;(void)idle;}
