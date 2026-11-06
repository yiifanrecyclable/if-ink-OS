#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reproduce global app_present -> reader custom present -> idle/prefetch header state leak."""
from pathlib import Path
import subprocess,tempfile,sys
R=Path(__file__).resolve().parents[1]
base=(R/'tools/test_navigation_perf.py').read_text().split('unit+=function(home,')[0]
ns={'__file__':str(R/'tools/test_navigation_perf.py')};exec(base,ns)
function=ns['function'];unit=ns['unit'];book=(Path(sys.argv[1]) if len(sys.argv)>1 else R/'main/apps/app_book.c').read_text();kit=(R/'main/ui/ui_kit.c').read_text()
unit=unit.replace("static void ui_text_set_system_scale(bool b){assert(b);step('S',0);}","static bool s_system_text_scale;static void ui_text_set_system_scale(bool b){s_system_text_scale=b;}")
unit=unit.replace('static bool active=true,s_home_force_full_once;', 'static bool active=true;').replace('static EpdRect s_area;', '')
unit+=r'''
#define UI_TEXT_TITLE 32
#define UI_TEXT_BODY 28
#define UI_TEXT_CAPTION 22
#define UI_PAGE_Y(y) (y)
#define UI_GRAY_LIGHT 170
#define EPD_DRAW_ALIGN_CENTER 0
static unsigned preset=100;
static unsigned app_settings_system_font_size(void){return preset;}
static int s_view;enum{SHELF,READING};
static int header_px,physical_header,prepared_header;
static bool fullscreen,cleanup,prepare;
static char s_book_title[128]="A readable title",s_title[128]="Chapter";
static void copy_text(char *out,size_t cap,const char *text){snprintf(out,cap,"%s",text);}
static void ui_nav_back(uint8_t *f,int x,int y){(void)f;(void)x;(void)y;}
static void ui_hairline(uint8_t *f,int y,int x,int w,int gray){(void)f;(void)y;(void)x;(void)w;(void)gray;}
static int ttf_text_width_px(int px,const char *text){return px*(int)strlen(text)/2;}
static void ui_text_vc(uint8_t *f,int x,int y,int px,const char *s,int align,bool inv){(void)f;(void)x;(void)y;(void)s;(void)align;(void)inv;header_px=ui_text_effective_px(px);}
'''.replace('static void ui_text_vc','static int ui_text_effective_px(int px);\nstatic void ui_text_vc')
unit+=function(kit,'int ui_text_effective_px(int px) {')
unit+=function(book,'static void fit_text(char* text, int px, int width) {')
unit+=function(book,'static void draw_reader_header(uint8_t *fb) {')
prefix=book.split('static bool present(app_ctx_t* ctx, app_redraw_t redraw) {',1)[1].split('    if (s_presented_view',1)[0]
unit+='static bool book_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;'+prefix+r'''
 if(prepare&&!fullscreen){draw_reader_header(ctx->fb);prepared_header=header_px;}
 if(cleanup&&!fullscreen)physical_header=prepared_header;
 return true;
}
'''
unit+=function((R/'main/app/app_loop.c').read_text(),'void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {')
unit+=r'''
int main(void){
 app_ctx_t ctx={0};app_desc_t book={.id=1,.present=book_present};
 preset=140;s_view=READING;physical_header=prepared_header=32;prepare=true;cleanup=true;
 ui_text_set_system_scale(false);app_present(&ctx,&book,APP_REDRAW_AREA);
 fprintf(stderr,"Periodic cleanup header: expected=32 actual=%d prepared=%d\n",physical_header,prepared_header);
 assert(physical_header==32);
 for(unsigned size=80;size<=160;size+=20)for(int full=0;full<2;++full){
  preset=size;s_view=READING;fullscreen=full;prepare=false;cleanup=false;
  ui_text_set_system_scale(false);draw_reader_header(ctx.fb);physical_header=prepared_header=header_px;assert(header_px==32);
  for(int turn=1;turn<=12;++turn){
   // Idle calls precede background preparation and must preserve reading scale too.
   app_present(&ctx,&book,APP_REDRAW_NONE);assert(!s_system_text_scale);
   app_present(&ctx,&book,APP_REDRAW_DONE);assert(!s_system_text_scale);
   draw_reader_header(ctx.fb);prepared_header=header_px;assert(prepared_header==32);
   cleanup=turn%3==0;prepare=true;app_present(&ctx,&book,APP_REDRAW_AREA);
   assert(header_px==32&&physical_header==32&&!s_system_text_scale);
  }
 }
 s_view=SHELF;app_present(&ctx,&book,APP_REDRAW_NONE);assert(s_system_text_scale);
 puts("PASS real app_present + reader present prefix + real header/size calculation: idle/NONE/DONE, cached preparation, repeated periodic cleanup, five system scales, fullscreen; shelf restores system scaling");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-type-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-DSHELF_PERF_HOST_TEST','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(R/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
