/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STANDARD_FONT_NO_CARD, STANDARD_FONT_MISSING, STANDARD_FONT_PRESENT, STANDARD_FONT_CONFLICT,
} standard_font_file_t;

typedef enum {
    STANDARD_FONT_IDLE, STANDARD_FONT_DOWNLOADING, STANDARD_FONT_INSTALLED,
    STANDARD_FONT_CANCELLED, STANDARD_FONT_MEDIA_ERROR, STANDARD_FONT_NETWORK_ERROR,
    STANDARD_FONT_INVALID, STANDARD_FONT_WRITE_ERROR,
} standard_font_state_t;

typedef struct {
    standard_font_state_t state;
    unsigned percent;
    bool busy;
} standard_font_status_t;

standard_font_file_t standard_font_file(void);
bool standard_font_wifi_ready(void);
void standard_font_start(void);
void standard_font_cancel(void);
void standard_font_stop(void);
standard_font_status_t standard_font_status(void);
