/* SPDX-License-Identifier: Apache-2.0
 * 中文：把硬件像素出口替换为684×1216的4bpp宿主帧。
 * English: Replace hardware pixels with a 684x1216 4bpp host framebuffer.
 */
#include "epdiy.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#define WIDTH 684
#define HEIGHT 1216
static enum EpdRotation s_rotation = EPD_ROT_LANDSCAPE;

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    int input_x = x;
    switch (s_rotation) {
        case EPD_ROT_PORTRAIT: x = WIDTH - 1 - y; y = input_x; break;
        case EPD_ROT_INVERTED_PORTRAIT: x = y; y = HEIGHT - 1 - input_x; break;
        case EPD_ROT_INVERTED_LANDSCAPE: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
        default: break;
    }
    if (!fb || x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    size_t offset = (size_t)y * (WIDTH / 2) + (size_t)x / 2;
    // epdiy偶数列在低半字节。/ epdiy stores even columns in the low nibble.
    uint8_t gray = color >> 4;
    fb[offset] = x & 1 ? (fb[offset] & 0x0f) | (gray << 4)
                      : (fb[offset] & 0xf0) | gray;
}
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb) {
    int left = rect.x < 0 ? 0 : rect.x;
    int top = rect.y < 0 ? 0 : rect.y;
    int w = epd_rotated_display_width(), h = epd_rotated_display_height();
    int right = rect.x + rect.width < w ? rect.x + rect.width : w;
    int bottom = rect.y + rect.height < h ? rect.y + rect.height : h;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) epd_draw_pixel(x, y, color, fb);
}
int epd_width(void){return WIDTH;}
int epd_height(void){return HEIGHT;}
int epd_rotated_display_width(void){return s_rotation & 1 ? HEIGHT : WIDTH;}
int epd_rotated_display_height(void){return s_rotation & 1 ? WIDTH : HEIGHT;}
enum EpdRotation epd_get_rotation(void){return s_rotation;}
void epd_set_rotation(enum EpdRotation rotation){s_rotation = rotation;}
uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){
    if(x<0||y<0||x>=w||y>=h)return 0xf0;
    uint8_t b=fb[(size_t)y*w/2+x/2];return(x&1)?(b&0xf0):(b<<4);
}
void epd_draw_line(int x0,int y0,int x1,int y1,uint8_t c,uint8_t *fb){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){epd_draw_pixel(x0,y0,c,fb);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
void epd_draw_rect(EpdRect r,uint8_t c,uint8_t *f){
    epd_draw_line(r.x,r.y,r.x+r.width-1,r.y,c,f);epd_draw_line(r.x,r.y,r.x,r.y+r.height-1,c,f);
    epd_draw_line(r.x+r.width-1,r.y,r.x+r.width-1,r.y+r.height-1,c,f);epd_draw_line(r.x,r.y+r.height-1,r.x+r.width-1,r.y+r.height-1,c,f);
}
void epd_fill_circle(int x,int y,int r,uint8_t c,uint8_t *fb){for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx)if(dx*dx+dy*dy<=r*r)epd_draw_pixel(x+dx,y+dy,c,fb);}
void epd_draw_circle(int x,int y,int r,uint8_t c,uint8_t *fb){
    int px=x+r,py=y;for(int i=1;i<=r*8+8;++i){double a=i*6.28318530718/(r*8+8);int nx=x+(int)lround(r*cos(a)),ny=y+(int)lround(r*sin(a));epd_draw_line(px,py,nx,ny,c,fb);px=nx;py=ny;}
}
void epd_fill_circle_helper(int x,int y,int r,int corners,int delta,uint8_t c,uint8_t *fb){
    for(int dx=0;dx<=r;++dx){int h=(int)sqrt(r*r-dx*dx);if(corners&1)epd_fill_rect((EpdRect){x+dx,y-h,1,2*h+1+delta},c,fb);if(corners&2)epd_fill_rect((EpdRect){x-dx,y-h,1,2*h+1+delta},c,fb);}
}
void epd_fill_triangle(int ax,int ay,int bx,int by,int cx,int cy,uint8_t c,uint8_t *fb){
    int left=ax<bx?ax:bx;if(cx<left)left=cx;int right=ax>bx?ax:bx;if(cx>right)right=cx;
    int top=ay<by?ay:by;if(cy<top)top=cy;int bottom=ay>by?ay:by;if(cy>bottom)bottom=cy;
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
        int a=(bx-ax)*(y-ay)-(by-ay)*(x-ax),b=(cx-bx)*(y-by)-(cy-by)*(x-bx),d=(ax-cx)*(y-cy)-(ay-cy)*(x-cx);
        if((a>=0&&b>=0&&d>=0)||(a<=0&&b<=0&&d<=0))epd_draw_pixel(x,y,c,fb);
    }
}

void epd_draw_rotated_image(EpdRect r,const uint8_t *image,uint8_t *fb){
 for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x){uint8_t b=image[(size_t)y*((r.width+1)/2)+x/2];epd_draw_pixel(r.x+x,r.y+y,(x&1)?b&0xf0:b<<4,fb);}
}
