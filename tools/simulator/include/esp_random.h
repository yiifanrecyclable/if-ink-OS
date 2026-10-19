/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdlib.h>
static inline uint32_t esp_random(void){return arc4random();}
