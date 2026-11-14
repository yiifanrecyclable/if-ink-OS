"""书架差分反馈与状态栏实际函数回归。/ Actual shelf feedback and status-bar regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, name):
    text = (root / path).read_text()
    match = re.search(r"^(?:static )?[^\n]+\b" + name + r"\([^;{}]*?\)\s*\{", text, re.M)
    assert match, name
    start, at, depth, quote, escape = match.start(), match.end(), 1, None, False
    while depth:
        c = text[at]
        if quote:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        at += 1
    return text[start:at]


shelf = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_DONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SHELF, TOC, READING, EDIT, SEARCH, DOCUMENTS};
enum {READER_PANEL_NONE, READER_PANEL_FONT_PICKER};
enum EpdRotation { EPD_ROT_INVERTED_PORTRAIT, EPD_ROT_LANDSCAPE };
enum EpdDrawMode {MODE_GL16,MODE_DU,MODE_GC16};
enum EpdDrawError {EPD_DRAW_SUCCESS};
#define APP_PAGE_REFRESH_MODE MODE_GL16
#define UI_LOCK_WIDTH 684
#define SHELF_PERF_SCOPE(...) ((void)0)
#define shelf_perf_begin(...) 0
#define shelf_perf_end(span) ((void)*(span))
#define shelf_perf_event(event,view,visible,mask,err) ((void)(visible))
#define operation_log_event(...) ((void)0)
typedef int shelf_perf_span_t;
static EpdRect ui_text_input_area(void){return (EpdRect){0,226,684,990};}
static const int E0470_WAVEFORM=0,E0470_FULL_WAVEFORM=1,E0470_FOLLOW_WAVEFORM=2;
static const char *TAG="test";
static int s_view,s_presented_view,s_reader_panel;
static bool s_reader_cleanup,s_reader_image_refresh_pending,s_toolbar,s_clear_confirm;
static bool s_reader_fullscreen,s_water_turn_pending,s_reader_footer_pending;
static enum EpdDrawMode s_mode;
static EpdRect s_area,s_du_area;
static unsigned s_du_count;
static int64_t s_du_ms;
static int s_water_turn_dir;
static bool system_scale, landscape, wide_panel;
static enum EpdRotation rotation;
static int s_visible_count, s_cover_pending_mask;
static bool s_scan_pending;
static int64_t s_cover_next_ms;
static unsigned pushes;
static int route;
static enum EpdDrawMode pushed_mode;
static EpdRect pushed_area;
enum {DIFF=1,AREA,FULL,WHOLE,FAST,WATER,READER,NAV};
static void ui_text_set_system_scale(bool enabled){system_scale=enabled;}
static void ttf_name_preview_pause(void){}
static bool reader_landscape(void){return landscape;}
static bool wide_panel_open(void){return wide_panel;}
static enum EpdRotation epd_get_rotation(void){return rotation;}
static void epd_set_rotation(enum EpdRotation value){rotation=value;}
static void reader_set_landscape_rotation(void){epd_set_rotation(EPD_ROT_LANDSCAPE);}
static EpdRect landscape_reader_area(void){return (EpdRect){0,56,1216,564};}
static EpdRect ui_nav_rect(void){return (EpdRect){0,1136,684,80};}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;}
static bool kick_prep(void){return false;}
static int64_t esp_timer_get_time(void){return 1000000;}
static void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI test_log
static void reader_prefetch_end(bool started){assert(!started);}
static void guard_draw_result(void *hl,int error){(void)hl;assert(error==EPD_DRAW_SUCCESS);}
static EpdRect reader_area(void){return (EpdRect){36,176,612,856};}
static EpdRect progress_rect(void){return (EpdRect){36,1040,612,42};}
static EpdRect reader_fullscreen_progress_area(void){return (EpdRect){0,1200,684,4};}
static unsigned percent(unsigned page){return page;}
static unsigned s_page;
static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static enum EpdDrawError record(int kind,enum EpdDrawMode mode,EpdRect area){++pushes;route=kind;pushed_mode=mode;pushed_area=area;return EPD_DRAW_SUCCESS;}
static enum EpdDrawError update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;(void)wave;return record(DIFF,mode,area);}
static enum EpdDrawError update_display_area_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;(void)wave;return record(AREA,mode,area);}
static enum EpdDrawError update_display_with(void *hl,const int *wave,enum EpdDrawMode mode){(void)hl;(void)wave;return record(WHOLE,mode,(EpdRect){0});}
static enum EpdDrawError update_display_full(void *hl){(void)hl;return record(FULL,MODE_GC16,(EpdRect){0});}
static enum EpdDrawError update_display_fast_page(void *hl){(void)hl;return record(FAST,MODE_GL16,(EpdRect){0});}
static enum EpdDrawError update_display_mode_diff(void *hl,enum EpdDrawMode mode){(void)hl;return record(READER,mode,(EpdRect){0});}
static enum EpdDrawError update_display_water_turn(void *hl,EpdRect area,int dir,bool wide){(void)hl;(void)dir;assert(wide==landscape);return record(WATER,MODE_GL16,area);}
static enum EpdDrawError update_display_natural_area(void *hl,EpdRect area){(void)hl;return record(DIFF,MODE_GL16,area);}
static enum EpdDrawError update_display_navigation_page(void *hl,EpdRect body,EpdRect nav){(void)hl;(void)body;return record(NAV,MODE_GL16,nav);}
'''
shelf += function("main/apps/app_book.c", "paint_control") + "\n"
shelf += function("main/apps/app_book.c", "present") + r'''
int main(void){
 uint8_t fb=0;app_ctx_t ctx={.fb=&fb};s_view=s_presented_view=SHELF;
 // Shelf covers no longer paint a pressed frame. Check live feedback on the pager buttons.
 for(int repeat=0;repeat<80;++repeat){
   EpdRect button={repeat%2?16:568,1032,100,64};
   assert(paint_control(&ctx,button)==APP_REDRAW_AREA);
   unsigned before=pushes;
   assert(present(&ctx,APP_REDRAW_AREA));
   assert(pushes==before+1&&route==AREA&&pushed_mode==MODE_DU&&system_scale);
   assert(pushed_area.x==button.x&&pushed_area.y==button.y);
   assert(pushed_area.width==button.width&&pushed_area.height==button.height);
 }
 s_view=READING;
 assert(present(&ctx,APP_REDRAW_AREA)&&route==READER&&!system_scale);
 s_view=SHELF;
 assert(present(&ctx,APP_REDRAW_PAGE)&&route==NAV&&system_scale);
 assert(present(&ctx,APP_REDRAW_FULL)&&route==FULL&&pushed_mode==MODE_GC16);
 s_view=READING;
 landscape=true;
 s_reader_image_refresh_pending=true;
 assert(present(&ctx,APP_REDRAW_PAGE)&&route==WHOLE&&pushed_mode==MODE_GC16&&!system_scale);
 s_reader_image_refresh_pending=s_reader_cleanup=true;
 assert(present(&ctx,APP_REDRAW_PAGE)&&route==FULL&&pushed_mode==MODE_GC16);
 puts("PASS: 80 shelf pager feedbacks stay local, page transitions select the correct refresh, and landscape illustrations keep scheduled cleanup");
}
'''

nav = (root / "main/ui/ui_nav.c").read_text()
nav_header = (root / "main/ui/ui_nav.h").read_text()
kit_header = (root / "main/ui/ui_kit.h").read_text()
defines = "\n".join(re.findall(
    r"^#define (?:UI_NAV_NETWORK_ICON_PX|UI_STATUS_(?:SIGNATURE_PX|CENTER_Y|HEIGHT)) .+$",
    "\n".join((nav, nav_header, kit_header)), re.M
))
status = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
enum {UI_ICON_WIFI,UI_ICON_ZAP};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_TEXT_TITLE 34
#define UI_GRAY_SECTION_LINE 0x58
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
#define READ_PICO_TRANSFER_MODE_STA 1
typedef struct {bool time_synced;time_t unix_sec;} pmu_snapshot_t;
typedef struct {bool network_ready;int mode;} read_pico_transfer_status_t;
static pmu_snapshot_t pmu;
static int pct;
static bool wifi,s_system_ttf,charging;
static int gauge_fills,bolt_triangles,battery_clears,dividers;
static char s_status_notice[192];
static const char *ui_message(const char *text){return text;}
static int ui_text_effective_px(int px){return px;}
static void app_settings_format_status_clock(time_t now,char *out,size_t n){(void)now;snprintf(out,n,"12:34");}
static void app_settings_format_status_clock_with_date(time_t now,char *out,size_t n){(void)now;snprintf(out,n,"2026-11-14 12:34");}
static char signature[96];
static const pmu_snapshot_t *read_pico_pmu_get(void){return &pmu;}
static int pmu_battery_percent(const pmu_snapshot_t *snapshot){(void)snapshot;return pct;}
static bool pmu_battery_charging(const pmu_snapshot_t *snapshot){(void)snapshot;return charging;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *status){*status=(read_pico_transfer_status_t){wifi,READ_PICO_TRANSFER_MODE_STA};}
static const char *app_settings_status_signature(void){return signature;}
static int letters(const char *s){int n=0;for(;*s;++s)if(((unsigned char)*s&0xc0)!=0x80)++n;return n;}
static bool ui_font_has_text(const char *s){(void)s;return true;}
static int ui_font_text_width_px(int px,const char *s){return letters(s)*px/2;}
static int ttf_text_width_px(int px,const char *s){return letters(s)*px*3/4;}
static void ui_font_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*3/4;*below=1;}
static void ttf_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*2/3;*below=px/5;}
typedef struct {int left,right,center,px,align;char text[96];} text_t;
static text_t texts[4];static int text_count,network_left,offset;
static void draw_text(uint8_t *fb,int x,int baseline,int px,const char *s,enum EpdFontFlags align,int fg,int bg){
 (void)fb;(void)fg;(void)bg;assert(text_count<4);
 int above,below,width=s_system_ttf?ttf_text_width_px(px,s):ui_font_text_width_px(px,s);
 if(s_system_ttf)ttf_measure_line_px(px,s,&above,&below);else ui_font_measure_line_px(px,s,&above,&below);
 text_t *t=&texts[text_count++];t->left=x-(align==EPD_DRAW_ALIGN_RIGHT?width:align==EPD_DRAW_ALIGN_CENTER?width/2:0);
 t->right=t->left+width;t->center=baseline+(below-above)/2;t->px=px;t->align=align;snprintf(t->text,sizeof(t->text),"%s",s);
}
static void ui_font_draw_text_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a,int fg,int bg,bool bw){(void)bw;draw_text(f,x,y,px,s,a,fg,bg);}
static void ttf_draw_text_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a,int fg,int bg){draw_text(f,x,y,px,s,a,fg,bg);}
''' + defines + r'''
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)fb;(void)gray;assert(y==UI_STATUS_CENTER_Y);if(icon==UI_ICON_ZAP)return;if(x-px/2<network_left)network_left=x-px/2;}
static void ui_nav_wifi_icon(uint8_t *fb,int x,int y,int px,int gray){ui_draw_icon(fb,x,y,px,UI_ICON_WIFI,gray);}
static void ui_draw_round_rect(uint8_t *fb,EpdRect box,int radius,int gray){(void)fb;(void)radius;(void)gray;assert(box.y+box.height/2==UI_STATUS_CENTER_Y);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect box,int radius,int gray){
 (void)fb;(void)radius;
 if(box.x==606+offset&&box.y==UI_STATUS_CENTER_Y-11&&box.width==40&&box.height==22){assert(gray==UI_GRAY_WHITE);++battery_clears;}
 if(box.x==610+offset&&box.y==UI_STATUS_CENTER_Y-7&&box.height==14){assert(gray==UI_GRAY_BLACK);++gauge_fills;}
}
static void epd_fill_triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint8_t gray,uint8_t *fb){
 (void)fb;assert(gray==UI_GRAY_BLACK);int xs[]={x0,x1,x2},ys[]={y0,y1,y2};
 for(int i=0;i<3;++i){assert(xs[i]>606+offset&&xs[i]<646+offset&&ys[i]>UI_STATUS_CENTER_Y-11&&ys[i]<UI_STATUS_CENTER_Y+11);}++bolt_triangles;
}
'''
status += r"""
static void epd_fill_rect(EpdRect rect,int gray,uint8_t *fb){
 (void)fb;assert(rect.x==608&&rect.y==UI_STATUS_CENTER_Y-9&&rect.width==1&&rect.height==18);
 assert(gray==UI_GRAY_SECTION_LINE);++dividers;
}
"""
for path, name in (
    ("main/ui/ui_kit.c", "ui_text_fixed_width_px"),
    ("main/ui/ui_kit.c", "ui_text_fixed_vc_gray"),
    ("main/ui/ui_kit.c", "ui_text_fixed_vc"),
    ("main/ui/ui_nav.c", "ui_nav_battery_bolt"),
    ("main/ui/ui_nav.c", "status_text"),
    ("main/ui/ui_nav.c", "draw_status"),
    ("main/ui/ui_nav.c", "ui_nav_status"),
    ("main/ui/ui_nav.c", "ui_nav_reader_status"),
):
    status += function(path, name) + "\n"
status += r'''
int main(void){
 uint8_t fb=0;const int percentages[]={0,9,78,100,-1};
 for(int font=0;font<2;++font)for(int mask=0;mask<2;++mask)for(unsigned p=0;p<5;++p)for(int charge=0;charge<2;++charge){
   s_system_ttf=font;wifi=mask&1;pct=percentages[p];charging=charge;gauge_fills=bolt_triangles=battery_clears=0;text_count=0;network_left=1000;
   snprintf(signature,sizeof(signature),"时间统一 · 正在阅读我的收藏书籍，保持热爱 ABCabc");
   ui_nav_status(&fb);assert(text_count==2||text_count==3);
   assert(battery_clears==1);
   assert(bolt_triangles==(charging?4:0));
   assert(gauge_fills==(!charging&&pct>0?1:0));
   char expected[8];if(pct>=0)snprintf(expected,sizeof(expected),"%u%%",(unsigned)pct);else strcpy(expected,"--%");
   assert(!strcmp(texts[text_count-1].text,expected));
   assert(texts[0].px==UI_STATUS_SIGNATURE_PX&&texts[text_count-1].px==UI_STATUS_SIGNATURE_PX);
   for(int i=0;i<text_count;++i)assert(abs(texts[i].center-UI_STATUS_CENTER_Y)<=1);
   if(text_count==3){
     assert(texts[1].px==UI_STATUS_SIGNATURE_PX);
     assert(texts[0].right+12<=texts[1].left);
     assert(texts[1].right+12<=(mask?network_left:texts[2].left));
     assert(texts[1].align==EPD_DRAW_ALIGN_CENTER);
     assert(texts[1].left+texts[1].right>=683&&texts[1].left+texts[1].right<=685);
     size_t length=strlen(texts[1].text);assert(length&&((unsigned char)texts[1].text[length-1]&0xc0)!=0xc0);
   }else assert(texts[0].right+12<=(mask?network_left:texts[1].left));
   assert(texts[text_count-1].right==595);
 }
 signature[0]=0;text_count=0;ui_nav_status(&fb);assert(text_count==2);
 wifi=true;charging=true;pct=100;offset=UI_LOCK_HEIGHT-UI_LOCK_WIDTH;
 strcpy(signature,"横屏签名");
 text_count=0;network_left=1216;
 ui_nav_reader_status(&fb,"书籍名称",true);
 assert(text_count==4&&dividers==1);
 assert(!strcmp(texts[0].text,"2026-11-14 12:34"));
 assert(texts[1].align==EPD_DRAW_ALIGN_RIGHT&&texts[1].right==594);
 assert(texts[2].align==EPD_DRAW_ALIGN_LEFT&&texts[2].left==622);
 assert(texts[0].right+12<=texts[1].left&&texts[2].right+12<=network_left);
 assert(texts[3].right==595+offset);
 for(int i=0;i<text_count;++i)assert(texts[i].px==UI_STATUS_SIGNATURE_PX&&abs(texts[i].center-UI_STATUS_CENTER_Y)<=1);
 signature[0]=0;text_count=0;
 ui_nav_reader_status(&fb,"书籍名称",true);
 assert(text_count==3&&dividers==1&&texts[1].align==EPD_DRAW_ALIGN_CENTER);
 assert(abs(texts[1].left+texts[1].right-1216)<=1);
 puts("PASS: larger status text shares the icon centerline with builtin/custom metrics; WiFi, 100% and long UTF-8 signatures do not overlap; charging uses an empty battery/solid bolt at every percentage, unplugging restores the gauge");
}
'''

with tempfile.TemporaryDirectory() as folder:
    for name, unit in (("shelf", shelf), ("status", status)):
        source, binary = Path(folder) / (name + ".c"), Path(folder) / name
        source.write_text(unit)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
