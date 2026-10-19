/* SPDX-License-Identifier: Apache-2.0
 * 仅扫描指定目录，选择不设图片数量上限，不保留打开的卡句柄。/ Scan only the selected directory, with no image-count cap or retained card handles.
 */
#include "ui_wallpaper_rotation.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static bool image_path(const char *directory, const char *name, char path[288]) {
    if (name[0] == '.') return false;
    const char *ext = strrchr(name, '.');
    if (!ext || (strcasecmp(ext, ".png") && strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg"))) return false;
    size_t n = strlen(directory);
    if (snprintf(path, 288, "%s%s%s", directory, n && directory[n-1] == '/' ? "" : "/", name) >= 288) return false;
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 2 * 1024 * 1024;
}
unsigned wallpaper_rotation_count(const char *directory) {
    if (!wallpaper_card_path_valid(directory)) return 0;
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    unsigned count = 0; struct dirent *entry; char path[288];
    while ((entry = readdir(dir))) if (image_path(directory, entry->d_name, path)) ++count;
    closedir(dir); return count;
}
bool wallpaper_rotation_pick(const char *directory, const char *previous, uint32_t random,
                             bool (*accept)(const char *, void *), void *context,
                             char *selected, size_t capacity) {
    if (!selected || !capacity || !accept) return false;
    selected[0] = 0;
    if(!wallpaper_card_path_valid(directory))return false;
    DIR *pool=opendir(directory);if(!pool)return false;
    unsigned count=0;struct dirent *item;char candidate[288],fallback[288]={0};
    while((item=readdir(pool)))if(image_path(directory,item->d_name,candidate)){
        if(previous&&!strcmp(candidate,previous))snprintf(fallback,sizeof(fallback),"%s",candidate);else ++count;
    }
    closedir(pool);
    unsigned start=count?random%count:0;
    // 从随机起点环形遍历，至多试每张一次；上一张仅在其他图片都不可用时回退。
    // Walk once from a random start; fall back to the previous image only when all alternatives fail.
    for (unsigned pass = 0; pass < 2; ++pass) {
        DIR *dir = opendir(directory);
        if (!dir) return false;
        struct dirent *entry; unsigned i = 0; char path[288];
        while ((entry = readdir(dir))) {
            if (!image_path(directory, entry->d_name, path)) continue;
            if (previous && !strcmp(path, previous)) continue;
            unsigned index = i++;
            if ((pass == 0 && index < start) || (pass == 1 && index >= start)) continue;
            if (strlen(path) < capacity && accept(path, context)) {
                snprintf(selected, capacity, "%s", path); closedir(dir); return true;
            }
        }
        closedir(dir);
    }
    if (fallback[0] && strlen(fallback) < capacity && accept(fallback, context)) {
        snprintf(selected, capacity, "%s", fallback); return true;
    }
    return false;
}
