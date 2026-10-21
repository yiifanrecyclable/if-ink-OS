/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实标题绘制函数保留完整文本字体来源、UTF8边界与灰底。/ Real title renderer retains the complete sample's face, UTF-8 boundaries and row gray.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_kit.h"
static int drawn_ttf,drawn_builtin;static char shown[160];static uint8_t drawn_background;
bool ui_font_has_text(const char *s){return !strstr(s,"缺");}
int ttf_text_width_px(int px,const char *s){(void)px;return (int)strlen(s)*5;}
int ui_font_text_width_px(int px,const char *s){return ttf_text_width_px(px,s);}
void ttf_measure_line_px(int px,const char *s,int *above,int *below){(void)px;(void)s;*above=7;*below=2;}
void ui_font_measure_line_px(int px,const char *s,int *above,int *below){ttf_measure_line_px(px,s,above,below);}
static void record(int x,int baseline,int px,const char *s,uint8_t bg){assert(x==10&&baseline==29&&px==10);snprintf(shown,sizeof(shown),"%s",s);drawn_background=bg;}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,uint8_t fg,uint8_t bg){(void)fb;(void)align;(void)fg;++drawn_ttf;record(x,y,px,s,bg);}
void ui_font_draw_text_px(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,uint8_t fg,uint8_t bg,bool bw){(void)fb;(void)align;(void)fg;assert(!bw);++drawn_builtin;record(x,y,px,s,bg);}
int main(void){
 uint8_t fb[1];EpdRect r={10,20,12,14};
 ui_text_set_system_font(false);ui_text_title_line(fb,r,10,"AB缺",0xc0);
 assert(drawn_ttf==1&&!drawn_builtin&&!strcmp(shown,"AB")&&drawn_background==12);
 ui_text_title_line(fb,r,10,"AB",0xf0);assert(drawn_builtin==1&&drawn_background==15);
 ui_text_set_system_font(true);ui_text_title_line(fb,r,10,"AB",0xc0);assert(drawn_ttf==2);
 puts("PASS: truncated missing suffix cannot switch face; explicit system face, UTF-8 truncation, centered baseline and gray AA background preserved");
}
