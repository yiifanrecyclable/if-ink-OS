/* SPDX-License-Identifier: Apache-2.0
 * 中文：选定子树的有界递归枚举与随机选择；只保留路径，候选失效逐项跳过。
 * English: Bounded subtree enumeration and random selection; retain paths only and skip unusable candidates.
 * 冻结：不允许卡根目录，不跟随符号链接；触及保护上限时失败，不能拿截断清单轮播。
 * Frozen: reject the card root and symlinks; limits fail rather than rotating a silently truncated pool.
 */
#include "ui_wallpaper_rotation.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define lstat stat
#endif
#define MAX_DEPTH 12
#define MAX_NODES 4096
#define MAX_IMAGES 1024

typedef struct {char path[288];} candidate_t;
typedef struct {candidate_t *items;unsigned count,capacity,nodes;wallpaper_rotation_status_t status;} pool_t;
bool wallpaper_rotation_directory_valid(const char *path){
    if(!wallpaper_card_path_valid(path))return false;
    size_t n=strlen(path),root=strlen(WALLPAPER_CARD_ROOT);
    while(n>root&&path[n-1]=='/')--n;
    return n>root;
}
static bool image_name(const char *name){
    const char *ext=strrchr(name,'.');
    return ext&&(!strcasecmp(ext,".png")||!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg"));
}
static bool add(pool_t *pool,const char *path){
    if(pool->count==MAX_IMAGES){pool->status=WALLPAPER_ROTATION_LIMIT;return false;}
    if(pool->count==pool->capacity){
        unsigned capacity=pool->capacity?pool->capacity*2:32;
#ifdef ESP_PLATFORM
        void *next=heap_caps_realloc(pool->items,capacity*sizeof(candidate_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        void *next=realloc(pool->items,capacity*sizeof(candidate_t));
#endif
        if(!next){pool->status=WALLPAPER_ROTATION_MEMORY;return false;}
        pool->items=next;pool->capacity=capacity;
    }
    snprintf(pool->items[pool->count++].path,288,"%s",path);return true;
}
static void walk(pool_t *pool,const char *directory,unsigned depth){
    if(depth>MAX_DEPTH){pool->status=WALLPAPER_ROTATION_LIMIT;return;}
    DIR *dir=opendir(directory);if(!dir){pool->status=WALLPAPER_ROTATION_IO;return;}
    while(pool->status==WALLPAPER_ROTATION_OK){
        errno=0;struct dirent *entry=readdir(dir);
        if(!entry){if(errno)pool->status=WALLPAPER_ROTATION_IO;break;}
        if(entry->d_name[0]=='.')continue;
        if(++pool->nodes>MAX_NODES){pool->status=WALLPAPER_ROTATION_LIMIT;break;}
        char path[288];if(snprintf(path,sizeof(path),"%s%s%s",directory,directory[strlen(directory)-1]=='/'?"":"/",entry->d_name)>=(int)sizeof(path)){pool->status=WALLPAPER_ROTATION_LIMIT;break;}
        struct stat st;if(lstat(path,&st)){pool->status=WALLPAPER_ROTATION_IO;break;}
        if(S_ISDIR(st.st_mode))walk(pool,path,depth+1);
        else if(S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=2*1024*1024&&image_name(entry->d_name))add(pool,path);
#ifdef ESP_PLATFORM
        if(pool->nodes%32==0)vTaskDelay(1);
#endif
    }
    if(closedir(dir)&&pool->status==WALLPAPER_ROTATION_OK)pool->status=WALLPAPER_ROTATION_IO;
}
static pool_t collect(const char *directory){
    pool_t pool={0};
    if(!wallpaper_rotation_directory_valid(directory)){pool.status=WALLPAPER_ROTATION_INVALID;return pool;}
    walk(&pool,directory,0);return pool;
}
unsigned wallpaper_rotation_count(const char *directory){
    pool_t pool=collect(directory);unsigned n=pool.status==WALLPAPER_ROTATION_OK?pool.count:0;free(pool.items);return n;
}
bool wallpaper_rotation_pick_checked(const char *directory,const char *previous,uint32_t random,
    bool (*accept)(const char *,void *),void *context,char *selected,size_t capacity,wallpaper_rotation_status_t *status){
    if(status)*status=WALLPAPER_ROTATION_INVALID;
    if(!selected||!capacity||!accept)return false;
    selected[0]=0;pool_t pool=collect(directory);
    if(pool.status!=WALLPAPER_ROTATION_OK){if(status)*status=pool.status;free(pool.items);return false;}
    unsigned count=pool.count,previous_index=count;
    for(unsigned i=0;i<count;i++)if(previous&&!strcmp(pool.items[i].path,previous)){previous_index=i;break;}
    // 上一次图片放末尾；其他候选从随机起点依次尝试。/ Put the previous image last, trying alternatives from a random start.
    if(previous_index<count){candidate_t swap=pool.items[count-1];pool.items[count-1]=pool.items[previous_index];pool.items[previous_index]=swap;--count;}
    unsigned start=count?random%count:0;bool ok=false;
    for(unsigned i=0;i<count&&!ok;i++){
        const char *path=pool.items[(start+i)%count].path;
        if(strlen(path)<capacity&&accept(path,context)){snprintf(selected,capacity,"%s",path);ok=true;}
    }
    if(!ok&&previous_index<pool.count){const char *path=pool.items[pool.count-1].path;if(strlen(path)<capacity&&accept(path,context)){snprintf(selected,capacity,"%s",path);ok=true;}}
    if(status)*status=ok?WALLPAPER_ROTATION_OK:WALLPAPER_ROTATION_EMPTY;
    free(pool.items);return ok;
}
bool wallpaper_rotation_pick(const char *directory,const char *previous,uint32_t random,
    bool (*accept)(const char *,void *),void *context,char *selected,size_t capacity){
    return wallpaper_rotation_pick_checked(directory,previous,random,accept,context,selected,capacity,NULL);
}
