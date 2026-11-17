/* SPDX-License-Identifier: Apache-2.0
 * 中文：用真实解析/分页、可记录字体验证内容完整、表头续页、宽表及偏移恢复。
 * English: Exercise production parsing/pagination with a recording font for completeness, headers, wide tables and resume.
 */
#include "markdown_text.h"
#include "book_layout.h"
#include "ttf_font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int html_test_fail_after=-1,layout_test_fail_after=-1;
int test_guide_segments,test_guide_first_y,test_guide_height;
uint8_t test_guide_gray;
static unsigned letters[128],style_flags;
static EpdRect bounds;
static int glyph_count;
int ttf_vertical_advance_px(int px, uint32_t cp) {
    (void)cp;
    assert(!"Markdown must use horizontal layout");
    return px;
}
void ttf_draw_vertical_px(uint8_t *fb, int x, int y, int height, int px, const char *text, int tracking) {
    (void)fb; (void)x; (void)y; (void)height; (void)px; (void)text; (void)tracking;
    assert(!"Markdown must use horizontal drawing");
}
int ttf_ascender_px(int px){return px;}
int ttf_text_left_bearing_px(int px,const char *text){(void)px;(void)text;return 0;}
int ttf_text_width_px(int px,const char *text){int n=0;for(;*text;++text)if(((unsigned char)*text&0xc0)!=0x80)++n;return n*px;}
int ttf_text_width_styled_px(int px,const char *text,unsigned flags){return ttf_text_width_px(px,text)+((flags&1)?1:0);}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags a,uint8_t fg,uint8_t bg){
    (void)fb;(void)a;(void)fg;(void)bg;
    assert(x>=bounds.x && x+ttf_text_width_px(px,text)<=bounds.x+bounds.width);
    assert(y-px>=bounds.y && y<=bounds.y+bounds.height);
    for(;*text;++text){if((unsigned char)*text<128)++letters[(unsigned char)*text];++glyph_count;}
}
void ttf_draw_glyph_styled_px(uint8_t *fb,int x,int y,int px,const char *text,unsigned flags,uint8_t fg,uint8_t bg){
    style_flags|=flags;ttf_draw_text_px(fb,x,y,px,text,EPD_DRAW_ALIGN_LEFT,fg,bg);
}
void ttf_draw_text_px_spaced(uint8_t *fb,int x,int y,int px,const char *text,int tracking,uint8_t fg,uint8_t bg){(void)tracking;ttf_draw_text_px(fb,x,y,px,text,0,fg,bg);}
bool ttf_text_range_px(int px, const char *text, int tracking, int target,
                       size_t start, size_t end, int *first, int *last, int *ink_bottom) {
    (void)px; (void)text; (void)tracking; (void)target;
    (void)start; (void)end; (void)first; (void)last; (void)ink_bottom;
    assert(!"These layout fixtures do not render links");
    return false;
}
void ttf_draw_text_px_fitted(uint8_t *fb,int x,int y,int px,const char *text,int tracking,int target,uint8_t fg,uint8_t bg){(void)target;ttf_draw_text_px_spaced(fb,x,y,px,text,tracking,fg,bg);}
static html_text_t parse(const char *md){html_text_t t={0};assert(markdown_to_blocks(md,strlen(md),&t)==ESP_OK);return t;}
static bool build(const html_text_t *t,int width,int height,int px){
    bounds=(EpdRect){10,20,width,height};uint8_t sizes[]={24,24,24};
    book_layout_set_markdown(true,sizes,t->styles,t->style_count);
    book_layout_set_vertical(true);
    book_layout_set_markdown_tables(t->cells,t->cell_count);
    book_layout_set_spacing(150,50);book_layout_set_first_line_indent(0);
    book_layout_set_chapter_lead(0,0);book_layout_set_typography(0);
    return book_layout_build_blocks(t->utf8,t->len,t->blocks,t->count,bounds,px);
}
static void draw_all(int px){
    memset(letters,0,sizeof(letters));glyph_count=0;uint8_t fb=0;
    for(size_t p=0;p<book_layout_page_count();++p){size_t off=book_layout_page_start_offset(p);
        assert(!p || off>book_layout_page_start_offset(p-1));
        assert(book_layout_page_for_offset(off)==p);
        assert(book_layout_page_image(p)==-1);
        book_layout_draw_page(&fb,p,bounds,px);
    }
}
int main(void){
    html_text_t t=parse("intro\n\n| A | B | C |\n| :--- | :---: | ---: |\n| aaaa | bb | cc |\n| d | eeeeeeeeeeeeeee | f |\n\nafter");
    assert(build(&t,150,110,10));assert(book_layout_page_count()>1);draw_all(10);
    assert(letters['a']==5 && letters['b']==2 && letters['c']==2 && letters['d']==1 && letters['e']==16 && letters['f']==2);
    assert(letters['A']>1 && letters['A']==letters['B'] && letters['B']==letters['C']);
    size_t saved=book_layout_page_start_offset(1);assert(build(&t,150,110,16));
    size_t resumed=book_layout_page_for_offset(saved);assert(book_layout_page_start_offset(resumed)<=saved);draw_all(16);
    assert(letters['a']==5 && letters['e']==16);book_layout_free();html_text_free(&t);
    t=parse("| A | B |\n| --- | --- |\n| a | bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb |");
    assert(build(&t,100,90,10));assert(book_layout_page_count()>5);draw_all(10);
    assert(letters['a']==1 && letters['b']==strlen("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    assert(letters['A']==book_layout_page_count());book_layout_free();html_text_free(&t);
    t=parse("| A | B | C | D | E | F |\n| --- | --- | --- | --- | --- | --- |\n| aaa | bbb | ccc | ddd | eee | fff |\n| | | | | | |");
    assert(build(&t,110,110,10));draw_all(10);for(int c='a';c<='f';++c)assert(letters[c]==3);
    assert(letters['A']>=2);book_layout_free();html_text_free(&t);
    t=parse("| A | B |\n| --- | --- |\n| | |\n| | b |\n\nend\n\n| C | D |\n| --- | --- |\n| c | |\n\nlast");
    assert(build(&t,110,90,10));draw_all(10);assert(letters['b']==1 && letters['c']==1 && letters['l']==1);
    bool success=false;
    for(int n=0;n<80;++n){layout_test_fail_after=n;
        if(build(&t,110,90,10)){draw_all(10);success=true;break;}
        assert(book_layout_page_count()==0);
    }
    assert(success);layout_test_fail_after=-1;
    t.cells[0].offset=t.len+1;assert(!build(&t,110,90,10));assert(!book_layout_page_count());
    book_layout_free();html_text_free(&t);
    t=parse("| H | I |\n| --- | --- |\n| **b** | *i* ==h== |");
    assert(build(&t,110,90,10));style_flags=0;draw_all(10);assert(style_flags==7);book_layout_free();html_text_free(&t);
    // 极短页与长表头也应前进，不能丢失正文或重复页面偏移。/ Tiny pages and long headers must advance without dropping body text or duplicating offsets.
    t=parse("| ABCDEFGHIJKLMNOPQRSTUVWXYZ | B | C |\n| --- | --- | --- |\n| aaaaaaaaaaaaaaaa | bb | cccccccc |\n| | | |\n\nend");
    for (int width=50; width<=200; width+=30) for (int height=36; height<=110; height+=18) {
        assert(build(&t,width,height,10));draw_all(10);
        assert(letters['a']==16 && letters['b']==2 && letters['c']==8 && letters['d']==1);
    }
    book_layout_free();html_text_free(&t);
    t=parse("| |\n| --- |\n| |\n| a |\n\nend");assert(build(&t,100,60,10));draw_all(10);
    assert(letters['a']==1);book_layout_free();html_text_free(&t);
    book_layout_set_markdown(false,NULL,NULL,0);
    puts("PASS: grid/stacked/long-row tables, repeated headers, exact body glyph counts, monotonic resume offsets, styles, mixed flow, bounds and OOM");
}
