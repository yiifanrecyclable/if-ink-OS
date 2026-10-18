/* SPDX-License-Identifier: Apache-2.0
 * 模拟器不联网校时。/ Simulator previews never synchronize network time.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline void app_time_service_stop(void){}
static inline void app_time_service_poll(int64_t ms,bool idle){(void)ms;(void)idle;}
