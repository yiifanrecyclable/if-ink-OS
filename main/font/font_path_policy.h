/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <string.h>
#include <strings.h>

#define APP_FONT_DIRECTORY "/sdcard/fonts"
#define APP_FONT_BUNDLED_NAME "PicoSans-Medium.ttf"

static inline bool app_font_path_valid(const char *path) {
    if (!path || strncasecmp(path, APP_FONT_DIRECTORY "/", sizeof(APP_FONT_DIRECTORY))) return false;
    const char *name = path + sizeof(APP_FONT_DIRECTORY);
    size_t n = strlen(name);
    return n > 4 && n < 146 && !strpbrk(name, "/\\:") && !strcasecmp(name + n - 4, ".ttf");
}

static inline bool app_font_path_protected(const char *path) {
    if (!path) return false;
    if (!strcasecmp(path, APP_FONT_DIRECTORY "/" APP_FONT_BUNDLED_NAME)) return true;
    // FAT may also expose the bundled long name through a generated 8.3 alias.
    if (strncasecmp(path, APP_FONT_DIRECTORY "/PICOSA~", sizeof(APP_FONT_DIRECTORY "/PICOSA~") - 1))
        return false;
    const char *tail = path + sizeof(APP_FONT_DIRECTORY "/PICOSA~") - 1;
    return tail[0] >= '1' && tail[0] <= '9' && !strcasecmp(tail + 1, ".ttf");
}
