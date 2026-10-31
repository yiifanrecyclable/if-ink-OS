/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef void* cst836u_handle_t;
#define CST836U_MAX_POINTS 2
typedef struct {bool active;uint16_t x,y;uint8_t id,event;} cst836u_point_t;
typedef struct {bool touched;uint8_t count;uint16_t x,y;cst836u_point_t points[2];uint8_t raw[15];} cst836u_touch_t;
typedef struct {int unused;} cst836u_info_t;

