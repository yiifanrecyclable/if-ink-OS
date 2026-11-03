/* SPDX-License-Identifier: Apache-2.0
 * 轮播文件夹枚举和无连续重复选择，不依赖显示驱动。/ Folder enumeration and nonrepeating selection, independent of display drivers.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifndef WALLPAPER_CARD_ROOT
#define WALLPAPER_CARD_ROOT "/sdcard"
#endif

/// 只接受卡内规范路径，允许根目录和末尾斜线。/ Accept canonical card paths, including the root and a trailing slash.
static inline bool wallpaper_card_path_valid(const char *path) {
    if (!path) return false;
    size_t root = strlen(WALLPAPER_CARD_ROOT), len = strnlen(path, 288);
    if (len >= 288 || strncmp(path, WALLPAPER_CARD_ROOT, root) || (path[root] && path[root] != '/')) return false;
    for (const char *part = path + root; *part;) {
        if (*part++ != '/') return false;
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n) return !end;
        if ((n == 1 && part[0] == '.') || (n == 2 && !strncmp(part, "..", 2))) return false;
        for (size_t i = 0; i < n; ++i) if ((unsigned char)part[i] < 32 || part[i] == '\\' || part[i] == ':') return false;
        part += n;
    }
    return true;
}
/// 递归统计选定子树的图片候选；根目录/扫描异常返回0。/ Recursively count subtree candidates; root or scan errors return zero.
unsigned wallpaper_rotation_count(const char *directory);
/// 验证/绘制回调成功才选定；优先排除上一张，损坏文件逐个跳过。/ Commit a choice only after acceptance; prefer a different image and skip broken files.
bool wallpaper_rotation_pick(const char *directory, const char *previous, uint32_t random,
                             bool (*accept)(const char *, void *), void *context,
                             char *selected, size_t capacity);

/// 卡根目录只能浏览，不能作为轮播目标。/ Browse the card root, but never select it for rotation.
bool wallpaper_rotation_directory_valid(const char *directory);
typedef enum {WALLPAPER_ROTATION_OK,WALLPAPER_ROTATION_EMPTY,WALLPAPER_ROTATION_INVALID,WALLPAPER_ROTATION_IO,WALLPAPER_ROTATION_LIMIT,WALLPAPER_ROTATION_MEMORY} wallpaper_rotation_status_t;
/// 区分无合格图片与扫描异常，失败绝不使用截断结果。/ Distinguish no usable images from scan failures; never use truncated results.
bool wallpaper_rotation_pick_checked(const char *directory,const char *previous,uint32_t random,
    bool (*accept)(const char *,void *),void *context,char *selected,size_t capacity,wallpaper_rotation_status_t *status);
