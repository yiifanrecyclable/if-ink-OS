/* SPDX-License-Identifier: Apache-2.0
 * 中文：识别器使用真实驱动快照。/ English: Feed the recognizer actual driver snapshots.
 */
#pragma once
#include "cst836u.h"
typedef struct {
    int64_t now_ms;
    const cst836u_touch_t *touch;
    bool pressed, released, consumed;
} app_ctx_t;
