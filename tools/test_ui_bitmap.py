# SPDX-License-Identifier: Apache-2.0
"""Check cached logical images against physical framebuffer rotation, without a real device."""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ui_bitmap.h"
static enum EpdRotation rotation;
static int width,height;
int epd_width(void){return width;}
int epd_height(void){return height;}
enum EpdRotation epd_get_rotation(void){return rotation;}
uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){
    assert(x>=0&&x<w&&y>=0&&y<h);
    uint8_t b=fb[(size_t)y*((w+1)/2)+x/2];return x&1?b&0xf0:b<<4;
}
static void physical(int *x,int *y){
    int a=*x,b=*y;
    if(rotation==EPD_ROT_PORTRAIT){*x=width-1-b;*y=a;}
    if(rotation==EPD_ROT_INVERTED_PORTRAIT){*x=b;*y=height-1-a;}
    if(rotation==EPD_ROT_INVERTED_LANDSCAPE){*x=width-1-a;*y=height-1-b;}
}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){
    physical(&x,&y);assert(x>=0&&x<width&&y>=0&&y<height);
    size_t at=(size_t)y*width/2+x/2;
    fb[at]=x&1?(fb[at]&15)|(c&240):(fb[at]&240)|(c>>4);
}
void epd_draw_rotated_image(EpdRect r,const uint8_t *image,uint8_t *fb){
    for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x)
        epd_draw_pixel(r.x+x,r.y+y,epd_get_pixel(x,y,r.width,r.height,image),fb);
}
static uint8_t logical(const uint8_t *fb,int x,int y){physical(&x,&y);return epd_get_pixel(x,y,width,height,fb);}
static void bitmap_roundtrip(void){
    size_t bytes=684*1216/2;
    uint8_t *page=malloc(bytes),*target=malloc(bytes),*before=malloc(bytes),*image=malloc(684*300/2);
    assert(page&&target&&before&&image);
    for(int rot=0;rot<4;++rot){
        rotation=(enum EpdRotation)rot;
        width=rot%2?1216:684;height=rot%2?684:1216;
        memset(page,255,bytes);memset(target,0xa5,bytes);memcpy(before,target,bytes);
        for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
            epd_draw_pixel(x,y,((x/9+y/5)%16)<<4,page);
        ui_bitmap_capture(page,(EpdRect){0,80,684,300},image);
        ui_bitmap_draw(target,(EpdRect){0,230,684,300},image);
        for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
            if(y>=230&&y<530)assert(logical(target,x,y)==logical(page,x,y-150));
            else assert(logical(target,x,y)==logical(before,x,y));
        }
    }
    free(page);free(target);free(before);free(image);
    puts("PASS: cached banner round-trip in all four physical rotations; status/navigation and other pixels preserved.");
}
'''
production=(ROOT/'main/apps/app_files.c').read_text()
def function(name):
    match=re.search(r'^static [^\n]*\b'+name+r'\([^;{}]*\)\s*\{',production,re.M)
    assert match,name
    end=match.end();depth=1
    while depth:
        if production[end]=='{':depth+=1
        elif production[end]=='}':depth-=1
        end+=1
    return production[match.start():end]+'\n'
SOURCE+=r'''
#define UI_LOCK_WIDTH 684
#define UI_NAV_TOP 1136
#define UI_PAGE_Y(y) ((y)-24)
#define UI_TEXT_BODY 30
#define UI_TEXT_CAPTION 24
#define UI_MSG(s) (s)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(bytes,caps) malloc(bytes)
#define FILE_VIEW_WALLPAPER_PREVIEW 10
typedef struct {uint8_t *fb;} app_ctx_t;
static uint8_t *s_picker_preview;
static bool s_picker_preview_ok,s_picker_eligible,s_picker_rotation;
static int s_view;
static struct {char path[288],name[128];} s_selected={"/sdcard/pictures/pattern.png","pattern.png"};
static bool ui_wallpaper_eligible(const char *path){(void)path;return true;}
static bool ui_wallpaper_draw(uint8_t *fb,const char *path,EpdRect r){
    (void)path;
    for(int y=0;y<r.height;y++)for(int x=0;x<r.width;x++)
        epd_draw_pixel(r.x+x,r.y+y,((x/7+y/11)%16)<<4,fb);
    return true;
}
static void ui_clear_page(uint8_t *fb){memset(fb,255,684*1216/2);}
static void ui_nav_status(uint8_t *fb){(void)fb;}
static void picker_header(uint8_t *fb){(void)fb;}
static void ui_nav_draw(uint8_t *fb,int tab){(void)fb;(void)tab;}
static void fit_name(char *name,int px,int width){(void)name;(void)px;(void)width;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool invert){
    (void)fb;(void)x;(void)y;(void)px;(void)text;(void)align;(void)invert;
}
'''
SOURCE+=function('picker_preview_rect')+function('picker_button_rect')
SOURCE+=r'''
static void picker_button(uint8_t *fb,const char *text,bool enabled){
    (void)text;assert(enabled);EpdRect r=picker_button_rect();
    for(int y=0;y<r.height;y++)for(int x=0;x<r.width;x++)
        if(x<2||y<2||x>=r.width-2||y>=r.height-2)epd_draw_pixel(r.x+x,r.y+y,0,fb);
}
'''
SOURCE+=function('picker_preview_clear')+function('picker_preview_open')+function('picker_render_preview')
SOURCE+=r'''
int main(void){
    bitmap_roundtrip();
    size_t bytes=684*1216/2;
    uint8_t *page=malloc(bytes),*expected=malloc(bytes);assert(page&&expected);
    for(int rot=0;rot<4;rot++){
        rotation=(enum EpdRotation)rot;width=rot%2?1216:684;height=rot%2?684:1216;
        app_ctx_t ctx={.fb=page};memset(page,0xa5,bytes);
        picker_preview_open(&ctx);assert(s_picker_preview_ok&&s_picker_eligible);
        picker_render_preview(page);
        ui_clear_page(expected);ui_wallpaper_draw(expected,s_selected.path,picker_preview_rect());
        picker_button(expected,"use",true);
        // Compare the entire physical page: both image edges and the button/background must match.
        if(memcmp(page,expected,bytes)){
            fprintf(stderr,"Wallpaper preview corrupted physical rotation %d\n",rot);
            assert(!"wallpaper preview differs from direct rendering");
        }
        EpdRect image=picker_preview_rect(),button=picker_button_rect();
        for(int y=button.y+3;y<button.y+button.height-3;y++)for(int x=button.x+3;x<button.x+button.width-3;x++)
            assert(logical(page,x,y)==0xf0);
        for(int y=image.y;y<image.y+image.height;y++)
            assert(logical(page,image.x,y)==logical(expected,image.x,y));
        picker_render_preview(page);assert(!memcmp(page,expected,bytes));
        picker_preview_clear();
    }
    free(page);free(expected);
    puts("PASS: actual wallpaper picker capture/render in all four rotations; image edges, button and page background intact.");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='bitmap-rotation-') as tmp:
    work=Path(tmp);source=work/'test.c';binary=work/'test';source.write_text(SOURCE)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(ROOT/'tools/simulator/include'),'-I'+str(ROOT/'main/ui'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
