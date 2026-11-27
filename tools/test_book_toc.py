"""Exercise the firmware's real directory layout and title normalization on the host."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/apps/book_toc.c"

STUBS = {
    "epdiy.h": """#pragma once
#include <stdint.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT=0, EPD_DRAW_ALIGN_CENTER=1, EPD_DRAW_ALIGN_RIGHT=2};
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb);
void epd_fill_circle(int x,int y,int radius,uint8_t color,uint8_t *fb);
void epd_draw_circle(int x,int y,int radius,uint8_t color,uint8_t *fb);
""",
    "book_source.h": """#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef enum {BOOK_KIND_TXT, BOOK_KIND_EPUB, BOOK_KIND_MARKDOWN} book_kind_t;
book_kind_t book_kind(void);
size_t book_navigation_count(void);
esp_err_t book_navigation_title(size_t position, char *out, size_t cap);
size_t book_navigation_chapter(size_t position);
""",
    "book_markdown.h": """#pragma once
#include <stdbool.h>
#include <stddef.h>
size_t book_markdown_navigation_depth(size_t position);
bool book_markdown_navigation_children(size_t position);
bool book_markdown_navigation_expanded(size_t position);
""",
    "settings.h": """#pragma once
#include <stdint.h>
uint8_t app_settings_system_font_size(void);
""",
    "ui_kit.h": """#pragma once
#include <stdbool.h>
#include "epdiy.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_ITEM_LINE 0xbb
#define UI_TEXT_BODY 28
#define UI_TEXT_CAPTION 22
#define UI_PAGE_Y(y) ((y) - 24)
#define UI_MSG(text) (text)
#define ui_snprintf snprintf
int ui_text_effective_px(int px);
void ui_clear_page(uint8_t *fb);
void ui_fill_round_rect(uint8_t *fb, EpdRect r, int radius, uint8_t gray);
void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverse);
void ui_text_title_line(uint8_t *fb,EpdRect r,int px,const char *text,uint8_t background);
int ui_text_fixed_width_px(int px,const char *text);
bool ui_rect_hit(EpdRect r,int x,int y);
""",
}

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "book_toc.c"
static book_kind_t kind = BOOK_KIND_EPUB;
static uint8_t system_size = 120;
static int titles, skip_labels, jump_summary, pager_labels, start_labels, zero_labels, outline_markers, borders;
static size_t nav_shift,seen_sources[8];
static int seen_count;
book_kind_t book_kind(void){return kind;}
size_t book_navigation_count(void){return 7;}
size_t book_markdown_navigation_depth(size_t position){return position==1;}
bool book_markdown_navigation_children(size_t position){return position==0;}
bool book_markdown_navigation_expanded(size_t position){return position==0;}
uint8_t app_settings_system_font_size(void){return system_size;}
int ui_text_effective_px(int px){return px;}
void epd_fill_rect(EpdRect rect,uint8_t color,uint8_t *fb){
    (void)fb;
    if(rect.height==1&&color==UI_GRAY_ITEM_LINE)++borders;
}
void epd_fill_circle(int x,int y,int radius,uint8_t color,uint8_t *fb){(void)x;(void)y;(void)radius;(void)color;(void)fb;}
void epd_draw_circle(int x,int y,int radius,uint8_t color,uint8_t *fb){(void)x;(void)y;(void)radius;(void)color;(void)fb;}
void ui_clear_page(uint8_t *fb){(void)fb;}
void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)r;(void)radius;(void)gray;}
void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverse){
    (void)fb;(void)x;(void)y;(void)px;(void)align;(void)inverse;
    assert(!strchr(text,'\n') && !strchr(text,'\r') && !strchr(text,'\t'));
    if(!strcmp(text,"«10")||!strcmp(text,"10»"))++skip_labels;
    if(!strcmp(text,"‹")||!strcmp(text,"›"))++pager_labels;
    if(!strcmp(text,"00"))++zero_labels;
    if(!strcmp(text,"−"))++outline_markers;
    if(strstr(text,"第 99～109 节")) jump_summary++;
}
void ui_text_title_line(uint8_t *fb,EpdRect r,int px,const char *text,uint8_t background){
    (void)fb;(void)background;
    assert(px==UI_TEXT_BODY && r.y>=TOC_ROW_TOP && r.width>0);
    assert(!strchr(text,'\n') && !strchr(text,'\r') && !strchr(text,'\t'));
    if(kind==BOOK_KIND_MARKDOWN && !titles) assert(r.x==TOC_LEFT+79);
    if(kind==BOOK_KIND_MARKDOWN && titles==1) assert(r.x==TOC_LEFT+79+24);
    if(!strcmp(text,"封面/首页")){assert(r.y==TOC_ROW_TOP);++start_labels;}
    ++titles;
}
int ui_text_fixed_width_px(int px,const char *text){int width=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)width+=((unsigned char)*text<128?px/2:px);return width;}
bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
esp_err_t book_navigation_title(size_t position,char *out,size_t cap){
    size_t index=book_navigation_chapter(position);
    if(seen_count<8)seen_sources[seen_count++]=index;
    const char *name=index==0?"第一章\n重叠\t标题":"普通章节";
    snprintf(out,cap,"%s",name);
    return ESP_OK;
}
size_t book_navigation_chapter(size_t position){return position+nav_shift;}
int main(void){
    const uint8_t sizes[] = {100,120,140,160};
    const int expected_rows[] = {11,11,10,10};
    for (int i=0;i<4;++i) {
        system_size=sizes[i];
        int rows=expected_rows[i];
        assert(BOOK_TOC_ROWS==rows);
        EpdRect last=book_toc_row_rect(rows-1);
        assert(last.y+last.height<=TOC_ROW_BOTTOM);
        assert(book_toc_hit(100,last.y+last.height-1,0,40)==rows-2);
        assert(book_toc_hit(100,TOC_ROW_TOP+5,1,40)==rows-1);
        assert(book_toc_pages(rows)==1 && book_toc_pages(rows+1)==2);
    }
    system_size=120;
    assert(book_toc_pages(0)==1);
    assert(book_toc_hit(58,110,0,8)==-1);
    assert(book_toc_entry_count()==8);
    assert(book_toc_hit(100,TOC_ROW_TOP-1,0,8)==-1);
    int first_y=book_toc_row_rect(0).y+5;
    int second_y=book_toc_row_rect(1).y+5;
    assert(book_toc_hit(100,first_y,0,1)==BOOK_TOC_START);
    assert(book_toc_hit(100,second_y,0,8)==0);
    assert(book_toc_hit(100,first_y,1,12)==10);
    assert(book_toc_hit(100,second_y,1,12)==-1);
    assert(book_toc_hit(600,1060,0,8)==BOOK_TOC_SKIP_NEXT);
    assert(book_toc_hit(98,1060,0,80)==BOOK_TOC_SKIP_PREV);
    assert(book_toc_hit(190,1060,0,80)==BOOK_TOC_PREV);
    assert(book_toc_hit(480,1060,0,80)==BOOK_TOC_NEXT);
    assert(book_toc_hit(320,1060,0,80)==BOOK_TOC_JUMP);
    assert(book_toc_jump_hit(120,880)==BOOK_TOC_JUMP_CANCEL);
    assert(book_toc_jump_hit(480,880)==BOOK_TOC_JUMP_CONFIRM);
    assert(book_toc_jump_hit(342,640)==BOOK_TOC_JUMP_TRACK);
    assert(book_toc_jump_percent(92)==0 && book_toc_jump_percent(592)==100);
    assert(book_toc_page_from_percent(0,24)==0);
    assert(book_toc_page_from_percent(4,24)==0);
    assert(book_toc_page_from_percent(60,24)==14);
    assert(book_toc_page_from_percent(100,24)==23);
    char title[160]; one_line(title,sizeof(title),"  第一章\n 重叠\t标题  ");
    assert(strcmp(title,"第一章 重叠 标题")==0);
    char long_title[160]; memset(long_title,'A',sizeof(long_title)-1);long_title[159]=0;
    fit_line(long_title,30,100);assert(strstr(long_title,"…")!=NULL);
    uint8_t fb=0;
    book_toc_render(&fb,12,1,0,"");
    assert(titles==11 && skip_labels==2 && pager_labels==2);
    assert(borders==16 && start_labels==1 && zero_labels==1);
    book_toc_render(&fb,12,1,1,"");
    assert(start_labels==1 && zero_labels==1);
    book_toc_render_jump(&fb,168,60);
    assert(jump_summary==1);
    kind=BOOK_KIND_TXT;
    assert(book_toc_entry_count()==7);
    assert(book_toc_hit(342,first_y,0,8)==0);
    nav_shift=1;seen_count=0;book_toc_render(&fb,4,1,0,"");
    assert(seen_count==4&&seen_sources[0]==1&&seen_sources[3]==4);
    assert(start_labels==1);
    kind=BOOK_KIND_MARKDOWN;
    assert(book_toc_hit(342,first_y,0,8)==0);
    titles=0;book_toc_render(&fb,2,SIZE_MAX,0,"");
    assert(titles==2 && outline_markers==1 && start_labels==1);
    puts("book_toc: pages, taps, overflow and single-line rendering passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    for name, content in STUBS.items():
        (directory / name).write_text(content)
    harness = directory / "test.c"
    harness.write_text(HARNESS)
    output = directory / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(directory),
                    "-I", str(SOURCE.parent), str(harness), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
