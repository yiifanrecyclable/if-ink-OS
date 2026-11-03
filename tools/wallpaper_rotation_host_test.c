/* SPDX-License-Identifier: Apache-2.0
 * 临时卡目录测试递归轮播、坏图、大于64张和不重复策略。/ Temporary-card tests for rotation, corrupt files, large pools and no repeats.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui_wallpaper_rotation.h"
#define POOL WALLPAPER_CARD_ROOT "/pictures"
static void file(const char *name,const char *bytes){char path[288];snprintf(path,sizeof(path),POOL"/%s",name);FILE *f=fopen(path,"wb");assert(f);fputs(bytes,f);fclose(f);}
static bool valid(const char *p,void *unused){(void)unused;FILE *f=fopen(p,"rb");assert(f);int c=fgetc(f);fclose(f);return c=='G';}
int main(void){
    mkdir("build/wallpaper-tests",0700);mkdir(WALLPAPER_CARD_ROOT,0700);
    assert(wallpaper_card_path_valid(WALLPAPER_CARD_ROOT));assert(wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"/"));
    assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"/../x"));assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"/./x"));assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"//x"));assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"-outside"));
    mkdir(POOL,0700);
    assert(!wallpaper_rotation_directory_valid(WALLPAPER_CARD_ROOT));assert(!wallpaper_rotation_directory_valid(WALLPAPER_CARD_ROOT"/"));assert(wallpaper_rotation_directory_valid(POOL));
    char chosen[288],previous[288]={0};
    assert(!wallpaper_rotation_pick(POOL,NULL,0,valid,NULL,chosen,sizeof(chosen))&&!chosen[0]);
    file("one.PNG","Good");file(".hidden.png","Good");file("note.md","Good");file("empty.jpg","");
    mkdir(POOL"/nested",0700);mkdir(POOL"/folder.jpg",0700);
    assert(wallpaper_rotation_count(POOL)==1);
    assert(wallpaper_rotation_pick(POOL,NULL,9,valid,NULL,chosen,sizeof(chosen)));strcpy(previous,chosen);
    assert(wallpaper_rotation_pick(POOL,previous,2,valid,NULL,chosen,sizeof(chosen))&&!strcmp(previous,chosen));
    file("two.JPEG","Good");
    for(unsigned i=0;i<100;++i){assert(wallpaper_rotation_pick(POOL,previous,i,valid,NULL,chosen,sizeof(chosen)));assert(strcmp(previous,chosen));strcpy(previous,chosen);}
    file("broken.jpg","Broken");unlink(POOL"/one.PNG");
    strcpy(previous,POOL"/two.JPEG");assert(wallpaper_rotation_pick(POOL,previous,0,valid,NULL,chosen,sizeof(chosen))&&!strcmp(previous,chosen));
    for(int i=0;i<80;++i){char n[32];snprintf(n,sizeof(n),"pool-%02d.png",i);file(n,"Good");}
    assert(wallpaper_rotation_count(POOL)==82);
    bool beyond=false;
    for(unsigned i=0;i<82;++i){assert(wallpaper_rotation_pick(POOL,NULL,i,valid,NULL,chosen,sizeof(chosen)));if(strstr(chosen,"pool-79.png"))beyond=true;}
    assert(beyond);
    FILE *nested=fopen(POOL"/nested/child.png","wb");assert(nested);fputs("Good",nested);fclose(nested);
    assert(wallpaper_rotation_count(POOL)==83);
    assert(!wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,NULL,0,valid,NULL,chosen,sizeof(chosen)));
    char deep[288]=POOL;for(int i=0;i<14;i++){strcat(deep,"/d");assert(!mkdir(deep,0700));}
    wallpaper_rotation_status_t status;assert(!wallpaper_rotation_pick_checked(POOL,NULL,0,valid,NULL,chosen,sizeof(chosen),&status)&&status==WALLPAPER_ROTATION_LIMIT);
    // 移除深目录，其他测试继续验证缺失目录和短缓冲。/ Remove the deep branch before testing missing directories and short buffers.
    for(int i=0;i<14;i++){assert(!rmdir(deep));*strrchr(deep,'/')=0;}
    assert(!wallpaper_rotation_pick(POOL"/missing",previous,0,valid,NULL,chosen,sizeof(chosen))&&!chosen[0]);
    assert(!wallpaper_rotation_pick(POOL,NULL,0,valid,NULL,chosen,4));
    puts("wallpaper rotation: canonical paths, empty/single/multiple/corrupt images, no repeats and >64 images passed");return 0;
}
