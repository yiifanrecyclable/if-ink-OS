/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "epd_highlevel.h"

/// 按原比例居中裁切并铺满区域；预览与锁屏共用。
/// Center-crop an SD image at its original aspect ratio to cover preview and lock screen.
bool ui_wallpaper_draw(uint8_t *framebuffer, const char *path, EpdRect area);

/// 居中填满圆角区域并裁切四角，用于设置页头像。
/// Center-crop into a rounded area for the settings avatar.
bool ui_wallpaper_draw_rounded(uint8_t *framebuffer, const char *path, EpdRect area, int radius);

/// 每次真实休眠仅调用一次，成功绘制后记住轮播选择。/ Call once per actual sleep and remember rotation only after a successful draw.
bool ui_wallpaper_draw_lock(uint8_t *framebuffer);
/// 验证目录至少有一张可解码图片；不会保存或推进轮播。/ Validate at least one decodable image without persisting or advancing rotation.
bool ui_wallpaper_check_directory(uint8_t *framebuffer, const char *directory);
