#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production cover geometry/drawing versus whole-cover-layer repaint, with real epdiy rotation primitives."""
from pathlib import Path
import re, subprocess, tempfile
R=Path(__file__).resolve().parents[1]
book=(R/'main/apps/app_book.c').read_text()
kit=(R/'main/ui/ui_kit.c').read_text()
epd=(R/'components/epdiy/src/epdiy.c').read_text()
def function(src,name):
 m=re.search(r'^(?:static )?[^\n]*\b'+name+r'\([^;{}]*\)\s*\{',src,re.M);assert m,name
 at=m.end();depth=1
 while depth:
  if src[at]=='{':depth+=1
  elif src[at]=='}':depth-=1
  at+=1
 return src[m.start():at]+'\n'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epdiy.h"
#include "book_cover.h"
#define UI_LOCK_WIDTH 684
#define UI_MARGIN 40
#define UI_GAP 12
#define UI_NAV_PAGER_TOP 1048
#define CONTENT_LIST_TOP 148
#define UI_TEXT_BODY 28
#define UI_GRAY_WHITE 255
#define UI_GRAY_LIGHT 208
#define UI_GRAY_BLACK 0
#define SHELF_PERF_SCOPE(...) ((void)0)
#define _swap_int(a,b) do{int temporary=(a);(a)=(b);(b)=temporary;}while(0)
typedef struct {int x,y;} Coord_xy;
typedef struct {uint8_t *fb;int leaf;} app_ctx_t;
static enum EpdRotation display_rotation;
static int physical_w,physical_h,preset=100,locks,scaled;
static bool tracing;
static EpdRect allowed;
enum {SHELF,BULK};static int s_view=SHELF;
static struct {char name[256];} s_shelf[18];
static struct {uint8_t *gray;unsigned width,height;} s_covers[9];
static int app_settings_system_font_size(void){return preset;}
static int ui_text_line_height(int role){(void)role;return preset/4+8;}
static EpdRect management_row(int i){return (EpdRect){40,148+i*80,604,80};}
int epd_width(void){return physical_w;}int epd_height(void){return physical_h;}
static void ui_text_set_system_scale(bool on){assert(on);++scaled;}
static void lock_draw(void){assert(!locks);++locks;}
static void unlock_draw(void){assert(locks==1);--locks;}
static void copy_text(char *out,size_t cap,const char *s){snprintf(out,cap,"%s",s);}
static void fit_text(char *s,int px,int w){(void)s;(void)px;(void)w;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags f,bool invert){(void)fb;(void)x;(void)y;(void)px;(void)s;(void)f;(void)invert;}
'''
unit+=function(epd,'_rotate')
unit+=function(epd,'epd_draw_pixel').replace('void epd_draw_pixel(', 'static void physical_draw_pixel(')
unit+=r'''
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){
 if(tracing)assert(x>=allowed.x&&x<allowed.x+allowed.width&&y>=allowed.y&&y<allowed.y+allowed.height);
 physical_draw_pixel(x,y,c,fb);
}
'''
for n in ['epd_draw_hline','epd_draw_vline','epd_fill_rect','epd_fill_circle_helper']:unit+=function(epd,n)
for n in ['ui_gray_level','clamp_radius','draw_arc','ui_draw_round_rect','ui_fill_round_rect']:unit+=function(kit,n)
unit+=function((R/'main/ui/ui_image_dither.c').read_text(),'ui_image_dither_gray')
for n in ['shelf_grid_side','shelf_rows','shelf_cell_height','row_rect','shelf_cover_height','shelf_cover_top','shelf_caption_width','shelf_image_rect','draw_shelf_cover','render_shelf_cover']:unit+=function(book,n)
pending=book.split('if (s_view == SHELF && !s_scan_pending && ctx->now_ms >= s_cover_next_ms &&',1)[1].split('// 目录暂借',1)[0]
assert 'render_shelf_cover(ctx, row);' in pending and 'render(ctx, ctx->fb);' not in pending
unit+=r'''
static EpdRect band(int row){EpdRect c=row_rect(row);return (EpdRect){c.x,c.y,c.width,shelf_cover_top(c)-c.y+shelf_cover_height()};}
static void full_cover_layer(uint8_t *fb){
 for(int i=0;i<shelf_rows();++i){epd_fill_rect(band(i),255,fb);draw_shelf_cover(fb,row_rect(i),i,s_shelf[i].name);}
}
int main(void){
 enum {BYTES=684*1216/2};uint8_t *fb=malloc(BYTES),*expected=malloc(BYTES),*before=malloc(BYTES);assert(fb&&expected&&before);
 uint8_t *pixels[9];for(int i=0;i<9;++i){pixels[i]=malloc(BOOK_COVER_W*BOOK_COVER_H);assert(pixels[i]);for(int k=0;k<BOOK_COVER_W*BOOK_COVER_H;++k)pixels[i][k]=(uint8_t)(k*7+i*13);snprintf(s_shelf[i].name,256,"cover%d",i);}
 unsigned cases=0;const int presets[]={100,120,140,160};
 for(int rot=0;rot<4;++rot)for(int f=0;f<4;++f){
  display_rotation=(enum EpdRotation)rot;physical_w=rot%2?1216:684;physical_h=rot%2?684:1216;preset=presets[f];
  for(int i=0;i<9;++i)s_covers[i]=(typeof(s_covers[i])){pixels[i],176,240};
  for(int k=0;k<BYTES;++k)fb[k]=(uint8_t)(k*19+47); /* Canary titles/status/progress/navigation. */
  full_cover_layer(fb);
  const unsigned dims[][2]={{176,60},{60,240},{176,240}};
  for(int pass=0;pass<3;++pass)for(int row=0;row<shelf_rows();++row){
   memcpy(before,fb,BYTES);s_covers[row].width=dims[pass][0];s_covers[row].height=dims[pass][1];
   memcpy(expected,before,BYTES);full_cover_layer(expected);
   app_ctx_t ctx={fb,0};allowed=band(row);tracing=true;render_shelf_cover(&ctx,row);tracing=false;
   assert(!locks&&scaled);assert(!memcmp(fb,expected,BYTES));
   tracing=true;render_shelf_cover(&ctx,row);tracing=false;assert(!memcmp(fb,expected,BYTES));++cases;
  }
 }
 for(int i=0;i<9;++i)free(pixels[i]);free(fb);free(expected);free(before);
 printf("PASS %u actual partial-cover transitions: four physical rotations, all four sizes, 2x2/3x3 cells, wide/tall aspect changes, repeatability; all writes inside image band and full physical framebuffer equals whole-cover-layer reference.\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-partial-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'tools/simulator/include'),'-I'+str(R/'main/book'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
