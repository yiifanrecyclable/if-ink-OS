/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "epd_highlevel.h"
typedef enum {E0470_TURN_LTR,E0470_TURN_RTL,E0470_TURN_TTB,E0470_TURN_BTT} e0470_turn_dir_t;
static inline void e0470_page_turn_release(void){}
