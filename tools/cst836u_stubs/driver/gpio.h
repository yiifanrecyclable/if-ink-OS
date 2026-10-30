/* SPDX-License-Identifier: Apache-2.0
 * 中文：触控驱动主机测试GPIO替身。/ English: GPIO shim for touch driver host tests.
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
#define GPIO_IS_VALID_GPIO(g) ((g) >= 0 && (g) < 64)
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t gpio, int level);
int gpio_get_level(gpio_num_t gpio);
