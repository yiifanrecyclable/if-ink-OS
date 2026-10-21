/* SPDX-License-Identifier: Apache-2.0
 * 中文：卡就绪时释放内置可选素材，不改变用户配置。
 * English: Install optional bundled artwork when media is ready; never alter preferences.
 */
#pragma once
#include "esp_err.h"
esp_err_t bundled_assets_install(void);
