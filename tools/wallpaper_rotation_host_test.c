/* SPDX-License-Identifier: Apache-2.0
 * 临时卡目录测试轮播、坏图、无上限与不重复策略。/ Temporary-card tests for rotation, corrupt files, uncapped pools and no repeats.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui_wallpaper_rotation.h"
static void file(const char *name,const char *bytes){char path[288];snprintf(path,sizeof(path),WALLPAPER_CARD_ROOT"/%s",name);FILE *f=fopen(path,"wb");assert(f);fputs(bytes,f);fclose(f);}
static bool valid(const char *p,void *unused){(void)unused;FILE *f=fopen(p,"rb");assert(f);int c=fgetc(f);fclose(f);return c=='G';}
int main(void){
    mkdir("build/wallpaper-tests",0700);mkdir(WALLPAPER_CARD_ROOT,0700);
    assert(wallpaper_card_path_valid(WALLPAPER_CARD_ROOT));assert(wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"/"));
    assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"/../x"));assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"/./x"));assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"//x"));assert(!wallpaper_card_path_valid(WALLPAPER_CARD_ROOT"-outside"));
    char chosen[288],previous[288]={0};
    assert(!wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,NULL,0,valid,NULL,chosen,sizeof(chosen))&&!chosen[0]);
    file("one.PNG","Good");file(".hidden.png","Good");file("note.md","Good");file("empty.jpg","");
    mkdir(WALLPAPER_CARD_ROOT"/nested",0700);mkdir(WALLPAPER_CARD_ROOT"/folder.jpg",0700);
    assert(wallpaper_rotation_count(WALLPAPER_CARD_ROOT)==1);
    assert(wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,NULL,9,valid,NULL,chosen,sizeof(chosen)));strcpy(previous,chosen);
    assert(wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,previous,2,valid,NULL,chosen,sizeof(chosen))&&!strcmp(previous,chosen));
    file("two.JPEG","Good");
    for(unsigned i=0;i<100;++i){assert(wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,previous,i,valid,NULL,chosen,sizeof(chosen)));assert(strcmp(previous,chosen));strcpy(previous,chosen);}
    file("broken.jpg","Broken");unlink(WALLPAPER_CARD_ROOT"/one.PNG");
    strcpy(previous,WALLPAPER_CARD_ROOT"/two.JPEG");assert(wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,previous,0,valid,NULL,chosen,sizeof(chosen))&&!strcmp(previous,chosen));
    for(int i=0;i<80;++i){char n[32];snprintf(n,sizeof(n),"pool-%02d.png",i);file(n,"Good");}
    assert(wallpaper_rotation_count(WALLPAPER_CARD_ROOT)==82);
    bool beyond=false;
    for(unsigned i=0;i<82;++i){assert(wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,NULL,i,valid,NULL,chosen,sizeof(chosen)));if(strstr(chosen,"pool-79.png"))beyond=true;}
    assert(beyond);
    assert(!wallpaper_rotation_pick(WALLPAPER_CARD_ROOT"/missing",previous,0,valid,NULL,chosen,sizeof(chosen))&&!chosen[0]);
    assert(!wallpaper_rotation_pick(WALLPAPER_CARD_ROOT,NULL,0,valid,NULL,chosen,4));
    puts("wallpaper rotation: canonical paths, empty/single/multiple/corrupt images, no repeats and >64 images passed");return 0;
}
