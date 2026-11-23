/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用伪字体验证布局完整性、偏移和测宽复杂度。
 * English: Verify layout completeness, offsets and measurement complexity using a fake font.
 * 冻结：只用于宿主测试。/ Frozen: Host testing only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"
int layout_test_fail_after = -1;
static bool small_image_dims(void *ctx, int image, int *width, int *height) {
    (void)ctx;
    if (image < 0 || image > 1) return false;
    *width = image ? 96 : 48; *height = 48;
    return true;
}
static char drawn[20000];
static size_t measured_codepoints;
static size_t measure_calls;
static int first_draw_px, last_draw_px, first_draw_x, last_draw_x;
static int last_tracking_px;
static int fitted_target;
static bool fitted_word_spaces;
static int test_cjk_advance;
static int test_opener_bearing;
int test_guide_segments;
int test_guide_first_y;
int test_guide_height;
uint8_t test_guide_gray;
int ttf_text_width_px(int px, const char* text) {
    // 模拟全角空格比汉字窄的真实字体，缩进仍应以汉字宽度为准。/ Model a narrow ideographic space; indentation must still follow CJK glyph width.
    if(!strcmp(text,"　"))return px/2;
    int n = 0, width = 0;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80) {
        n++;
        width += (unsigned char)*text >= 0x80 && test_cjk_advance ? test_cjk_advance :
                 *text == 'i' ? px / 2 : *text == 'W' ? px + px / 2 : px;
    }
    measured_codepoints += (size_t)n;
    measure_calls++;
    return width;
}
int ttf_ascender_px(int px) { return px; }
int ttf_text_left_bearing_px(int px, const char* text) {
    (void)px; (void)text; return test_opener_bearing;
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)y; (void)px; (void)align;
    assert(fg <= 15 && bg <= 15);
    if (!drawn[0]) { first_draw_px = px; first_draw_x = x; }
    last_draw_px = px;
    last_draw_x = x;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg) {
    last_tracking_px = tracking_px;
    ttf_draw_text_px(fb, x, y, px, text, EPD_DRAW_ALIGN_LEFT, fg, bg);
}
bool ttf_text_range_px(int px, const char *text, int tracking, int target, bool word_spaces,
                       size_t start, size_t end, int *first, int *last, int *ink_bottom) {
    (void)px; (void)text; (void)tracking; (void)target; (void)word_spaces;
    (void)start; (void)end; (void)first; (void)last; (void)ink_bottom;
    assert(!"These layout fixtures do not render links");
    return false;
}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, bool word_spaces, uint8_t fg, uint8_t bg) {
    fitted_target = target_width;
    fitted_word_spaces = word_spaces;
    ttf_draw_text_px_spaced(fb, x, y, px, text, tracking_px, fg, bg);
}
int ttf_text_width_styled_px(int px,const char *text,unsigned flags){return ttf_text_width_px(px,text)+(flags&1?1:0)+(flags&2?px/5:0);}
static unsigned rendered_styles;
int ttf_vertical_advance_px(int px, uint32_t cp) {
    return cp < 128 ? px / 2 : px;
}
void ttf_draw_vertical_px(uint8_t *fb, int x, int y, int height, int px, const char *text, int tracking) {
    (void)fb; (void)y; (void)height; (void)px; (void)tracking;
    if (!drawn[0]) first_draw_x = x;
    last_draw_x = x;
    strcat(drawn, text);
}
void ttf_draw_glyph_styled_px(uint8_t *fb,int x,int y,int px,const char *text,unsigned flags,uint8_t fg,uint8_t bg){
    rendered_styles|=flags;ttf_draw_text_px(fb,x,y,px,text,EPD_DRAW_ALIGN_LEFT,fg,bg);
}
static void assert_slice_text(uint8_t *fb,size_t page,EpdRect rect,int px) {
    drawn[0]=0;book_layout_draw_page(fb,page,rect,px);
    char *expected=strdup(drawn);assert(expected);drawn[0]=0;
    book_layout_cursor_t cursor={0};int status;unsigned units=0;
    do{status=book_layout_draw_slice(fb,page,rect,px,&cursor);assert(status>=0&&++units<20000);}while(status==0);
    assert(!strcmp(drawn,expected));free(expected);
}

static void check_english_alignment(uint8_t *fb) {
    const char text[] = "red sun sky";
    const size_t len = sizeof(text) - 1;
    const EpdRect rect = {0, 0, 74, 15};
    blk_t block = {.len = len, .image = -1};
    book_layout_set_first_line_indent(0);
    book_layout_set_english_justification(true);
    assert(book_layout_build_blocks(text, len, &block, 1, rect, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 7);
    drawn[0] = 0;
    book_layout_draw_page(fb, 0, rect, 10);
    assert(!strcmp(drawn, "red sun") && fitted_target == rect.width && fitted_word_spaces);
    drawn[0] = 0; fitted_target = 0;
    book_layout_draw_page(fb, 1, rect, 10);
    assert(!strcmp(drawn, "sky") && !fitted_target);

    block.align = HTML_ALIGN_LEFT;
    assert(book_layout_build_blocks(text, len, &block, 1, rect, 10));
    drawn[0] = 0;
    book_layout_draw_page(fb, 0, rect, 10);
    assert(!strcmp(drawn, "red sun") && !fitted_target);
    block.align = HTML_ALIGN_DEFAULT;

    book_layout_set_markdown(true, NULL, NULL, 0);
    assert(book_layout_build_blocks(text, len, &block, 1, rect, 10));
    drawn[0] = 0;
    book_layout_draw_page(fb, 0, rect, 10);
    assert(!strcmp(drawn, "red sun") && last_draw_x == rect.width - 10);
    drawn[0] = 0;
    book_layout_draw_page(fb, 1, rect, 10);
    assert(!strcmp(drawn, "sky") && last_draw_x == 20);
    book_layout_set_english_justification(false);
    assert(book_layout_build_blocks(text, len, &block, 1, rect, 10));
    drawn[0] = 0;
    book_layout_draw_page(fb, 0, rect, 10);
    assert(last_draw_x == 60);
    book_layout_set_markdown(false, NULL, NULL, 0);
    book_layout_set_first_line_indent(2);
}

int main(void) {
    EpdRect r = {0, 0, 20, 30};
    const char text[] = "甲乙丙丁戊己庚辛壬癸";
    assert(book_layout_build(text, strlen(text), r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 12);
    assert(book_layout_page_for_offset(11) == 0);
    assert(book_layout_page_for_offset(12) == 1);
    assert(book_layout_page_for_offset(999) == 2);
    uint8_t fb = 0;
    for (size_t i = 0; i < book_layout_page_count(); i++) {
        assert(book_layout_page_start_offset(i) % 3 == 0);
        book_layout_draw_page(&fb, i, r, 10);
    }
    assert(strcmp(drawn, text) == 0);
    const char mixed[] = "Wi甲iiW";
    assert(book_layout_build(mixed, strlen(mixed), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 5);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, mixed));
    book_layout_set_first_line_indent(0);
    const char *long_word = "abcdefghij";
    EpdRect narrow = {0, 0, 40, 15};
    assert(book_layout_build(long_word, strlen(long_word), narrow, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3 &&
           book_layout_page_start_offset(2) == 7);
    const char *wrapped[] = {"abc-", "defg", "hij"};
    for (size_t i = 0; i < 3; ++i) {
        drawn[0] = 0;
        book_layout_draw_page(&fb, i, narrow, 10);
        assert(!strcmp(drawn, wrapped[i]));
        assert_slice_text(&fb,i,narrow,10);
    }
    assert(book_layout_build("abcd）", strlen("abcd）"), narrow, 10));
    drawn[0]=0;book_layout_draw_page(&fb,0,narrow,10);
    assert(!strcmp(drawn,"abcd"));
    const char *authored = "ab-cd ef";
    assert(book_layout_build(authored, strlen(authored), narrow, 10));
    drawn[0]=0;book_layout_draw_page(&fb,0,narrow,10);
    assert(!strcmp(drawn,"ab-"));
    drawn[0]=0;book_layout_draw_page(&fb,1,narrow,10);
    assert(drawn[0]!='-');

    EpdRect fit_word = {0,0,70,15};
    const char *mixed_word = "甲乙abcdef";
    assert(book_layout_build(mixed_word,strlen(mixed_word),fit_word,10));
    drawn[0]=0;book_layout_draw_page(&fb,0,fit_word,10);
    assert(!strcmp(drawn,"甲乙abc-") && book_layout_page_start_offset(1)==9);
    const char *unbroken[] = {"甲model42","甲can't", "甲ab-cdef", "甲www.test"};
    EpdRect token_width={0,0,90,15};
    for(size_t i=0;i<sizeof(unbroken)/sizeof(*unbroken);++i){
        assert(book_layout_build(unbroken[i],strlen(unbroken[i]),token_width,10));
        drawn[0]=0;
        for(size_t page=0;page<book_layout_page_count();++page)book_layout_draw_page(&fb,page,token_width,10);
        assert(!strcmp(drawn,unbroken[i]));
    }
    const char *heading_text = "abcdefgh";
    blk_t heading_block={.offset=0,.len=8,.heading=true,.image=-1};
    EpdRect title_box={0,0,100,30};
    assert(book_layout_build_blocks(heading_text,8,&heading_block,1,title_box,10));
    drawn[0]=0;book_layout_draw_page(&fb,0,title_box,10);
    assert(!strchr(drawn,'-')&&book_layout_page_start_offset(1)>3);
    heading_block.heading=false;heading_block.literal=true;
    assert(book_layout_build_blocks(heading_text,8,&heading_block,1,narrow,10));
    drawn[0]=0;book_layout_draw_page(&fb,0,narrow,10);
    assert(!strcmp(drawn,"abcd"));

    book_layout_set_hyphenation(false);
    assert(book_layout_build(mixed_word,strlen(mixed_word),fit_word,10));
    assert(book_layout_page_start_offset(1)==strlen("甲乙"));
    drawn[0]=0;book_layout_draw_page(&fb,0,fit_word,10);
    assert(!strcmp(drawn,"甲乙"));
    drawn[0]=0;book_layout_draw_page(&fb,1,fit_word,10);
    assert(!strcmp(drawn,"abcdef"));
    const char *compound = "甲乙丙abcd-ef";
    assert(book_layout_build(compound,strlen(compound),fit_word,10));
    assert(book_layout_page_start_offset(1)==strlen("甲乙丙"));
    drawn[0]=0;book_layout_draw_page(&fb,1,fit_word,10);
    assert(!strcmp(drawn,"abcd-ef"));
    assert(book_layout_build(long_word,strlen(long_word),narrow,10));
    assert(book_layout_page_start_offset(1)==4);
    drawn[0]=0;book_layout_draw_page(&fb,0,narrow,10);
    assert(!strcmp(drawn,"abcd"));
    book_layout_set_hyphenation(true);
    book_layout_set_first_line_indent(2);
    check_english_alignment(&fb);

    assert(book_layout_build("", 0, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_build(NULL, 0, r, 10));
    assert(!book_layout_build(NULL, 1, r, 10));
    assert(book_layout_page_count() == 0);
    assert(!book_layout_build("\xe7\x94", 2, r, 10));
    assert(!book_layout_build("x\0y", 3, r, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,5,30}, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,20,5}, 10));
    char* many = malloc(4097);
    memset(many, 'x', 4097);
    r = (EpdRect){0,0,10,15};
    assert(book_layout_build(many, 4096, r, 10));
    assert(book_layout_page_count() == 4096);
    assert(!book_layout_build(many, 4097, r, 10));
    assert(book_layout_page_count() == 0);
    free(many);
    assert(book_layout_build("a\r\n\r\nb", 6, (EpdRect){0,0,20,100}, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, (EpdRect){0,0,20,100}, 10);
    assert(strcmp(drawn, "ab") == 0);
    char long_line[1024];
    memset(long_line, 'z', sizeof(long_line));
    r = (EpdRect){0,0,10000,30};
    measured_codepoints = measure_calls = 0;
    assert(book_layout_build(long_line, sizeof(long_line), r, 10));
    printf("long-line build: %zu calls, %zu measured codepoints\n", measure_calls, measured_codepoints);
    fflush(stdout);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 11);
    assert(drawn[0] == 0);
    book_layout_draw_page(&fb, 99, r, 10);
    assert(drawn[0] == 0);
    measured_codepoints = measure_calls = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(strlen(drawn) == sizeof(long_line));
    assert(!memcmp(drawn, long_line, sizeof(long_line)));
    assert(book_layout_page_start_offset(99) == sizeof(long_line));
    assert(!book_layout_build("\xed\xa0\x80", 3, r, 10));
    assert(!book_layout_build("\xf4\x90\x80\x80", 4, r, 10));
    assert(!book_layout_build("\xc0\xaf", 2, r, 10));
    book_layout_free();
    assert(book_layout_page_count() == 0);
    const char styled[] = "Title\nbody";
    blk_t blocks[] = {{.offset = 0, .len = 5, .heading = true, .image = -1},
                      {.offset = 6, .len = 4, .heading = false, .image = -1}};
    r = (EpdRect){0, 0, 100, 45};
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, "Titlebody") && first_draw_px == 18 && last_draw_px == 10);
    const char illustrated[] = "IMG\nAA\nIMG\nBB";
    blk_t illustrated_blocks[] = {
        {.offset = 0, .len = 3, .image = 0},
        {.offset = 4, .len = 2, .image = -1},
        {.offset = 7, .len = 3, .image = 1},
        {.offset = 11, .len = 2, .image = -1},
    };
    EpdRect illustrated_rect = {0, 0, 100, 60};
    assert(book_layout_build_blocks(illustrated, strlen(illustrated), illustrated_blocks, 4,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 4);
    assert(book_layout_page_image(0) == 0 && book_layout_page_image(2) == 1);
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(illustrated, strlen(illustrated), illustrated_blocks, 4,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 4);
    assert(book_layout_page_image(0) == -1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, illustrated_rect, 10);
    assert(!strcmp(drawn, "AABB"));
    assert(book_layout_build_blocks("IMG", 3, illustrated_blocks, 1, illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) == 0);
    book_layout_set_images_visible(true);
    // 真实 HTML 的换行、分页空白、全角及零宽占位不应在插图前后产生额外页面。
    // Real HTML line/page-break whitespace and fullwidth/zero-width spacers must not create image-adjacent pages.
    const char *image_html = "<p>甲</p><p>&#10;&#12;&nbsp;</p><p>　</p>"
        "<br/><img src='image.png'/><br/><p>　&#x200b;&#xfeff;</p><p id='after'>乙<br/>丙</p>";
    html_text_t parsed = {0};size_t image_anchor = 0;
    assert(html_to_blocks_with_css_anchor(image_html, strlen(image_html), NULL, 0,
                                          "after", &image_anchor, &parsed) == ESP_OK);
    assert(parsed.count == 6 && parsed.image_count == 1);
    size_t after_image = parsed.blocks[4].offset;
    assert(image_anchor == after_image);
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_image(1) == 0);
    assert(book_layout_page_start_offset(2) == after_image && book_layout_page_for_offset(image_anchor) == 2);
    drawn[0] = 0;
    for(size_t p=0;p<3;p++)book_layout_draw_page(&fb,p,illustrated_rect,10);
    assert(!strcmp(drawn,"甲乙丙"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) < 0);
    drawn[0]=0;book_layout_draw_page(&fb,0,illustrated_rect,10);assert(!strcmp(drawn,"甲乙丙"));
    book_layout_free();html_text_free(&parsed);
    const char *only_image = "<p>　&#x200b;</p><img src='image.png'/><p>　</p>";
    assert(html_to_blocks(only_image,strlen(only_image),&parsed)==ESP_OK);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,illustrated_rect,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==0);
    book_layout_set_images_visible(true);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,illustrated_rect,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==0);
    book_layout_free();html_text_free(&parsed);
    // 小注图和多个插图与正文同页，未知尺寸仍沿用安全回退。
    // Small note icons and multiple images flow with text; unknown dimensions retain fallback.
    const char *notes="<p>AA</p><img src='note.png'/><p>BB</p><img src='other.png'/><p>CC</p>";
    assert(html_to_blocks(notes,strlen(notes),&parsed)==ESP_OK);
    book_layout_set_image_dims(small_image_dims,NULL);
    EpdRect mixed_rect={0,0,200,200};
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,mixed_rect,10));
    assert(book_layout_page_count()==1 && book_layout_page_image(0)==-1);
    assert(book_layout_page_image_count(0)==2);
    int img,y,w,h;
    assert(book_layout_page_image_at(0,0,&img,&y,&w,&h,NULL)&&img==0&&w==48&&h==48&&y>0);
    assert(book_layout_page_image_at(0,1,&img,&y,&w,&h,NULL)&&img==1&&w==96&&h==48);
    assert(!book_layout_page_image_at(0,2,NULL,NULL,NULL,NULL,NULL));
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_rect,10);assert(!strcmp(drawn,"AABBCC"));
    uint32_t generation=book_layout_generation();
    mixed_rect.height=90;
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,mixed_rect,10));
    assert(book_layout_generation()!=generation && book_layout_page_count()>1);
    int placed=0;drawn[0]=0;
    for(size_t p=0;p<book_layout_page_count();++p){placed+=book_layout_page_image_count(p);book_layout_draw_page(&fb,p,mixed_rect,10);}
    assert(placed==2 && !strcmp(drawn,"AABBCC"));
    for(int fail=0;fail<4;++fail){
        layout_test_fail_after=fail;
        assert(!book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,(EpdRect){0,0,200,200},10));
        assert(book_layout_page_count()==0 && book_layout_page_image_count(0)==0);
    }
    layout_test_fail_after=-1;
    book_layout_free();html_text_free(&parsed);book_layout_set_image_dims(NULL,NULL);
    const char *text_spacer="<p>甲</p><p>　</p><p>乙</p>";
    assert(html_to_blocks(text_spacer,strlen(text_spacer),&parsed)==ESP_OK);
    const EpdRect text_spacer_rect={0,0,100,20};
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,text_spacer_rect,10));
    assert(book_layout_page_count()==2 && book_layout_page_start_offset(1)==parsed.blocks[2].offset);
    drawn[0]=0;
    book_layout_draw_page(&fb,0,text_spacer_rect,10);
    book_layout_draw_page(&fb,1,text_spacer_rect,10);
    assert(!strcmp(drawn,"甲乙"));
    book_layout_free();html_text_free(&parsed);
    EpdRect balanced = book_layout_balanced_rect((EpdRect){36, 0, 612, 100}, 48, 0);
    assert(balanced.x == 54 && balanced.width == 576);
    assert(balanced.x == 684 - balanced.x - balanced.width);
    balanced = book_layout_balanced_rect((EpdRect){36, 0, 612, 100}, 48, 2);
    assert(balanced.x == 43 && balanced.width == 598);
    r = (EpdRect){0, 0, 100, 100};
    for (unsigned em = 0; em <= 3; ++em) {
        book_layout_set_first_line_indent(em);
        assert(book_layout_build("甲乙\n丙丁", strlen("甲乙\n丙丁"), r, 10));
        drawn[0] = 0;
        book_layout_draw_page(&fb, 0, r, 10);
        assert(first_draw_x == (int)em * 10);
        // 原文的段首全角/半角空格不得额外叠加一个缩进档位。
        // Source leading spaces must not add another visual indent stop.
        const char *preindented = "　甲乙\n  丙丁";
        assert(book_layout_build(preindented, strlen(preindented), r, 10));
        drawn[0] = 0;
        book_layout_draw_page(&fb, 0, r, 10);
        assert(first_draw_x == (int)em * 10);
        assert(!strcmp(drawn, "甲乙丙丁"));
    }
    // 模拟字号 48、真实字宽 33 的字体；开引号留白不得额外增加半格。
    // Simulate a 48-high font with 33-wide glyphs; an opener's bearing must not add half an indent cell.
    r = (EpdRect){36, 0, 612, 180};
    test_cjk_advance = 33;
    for (unsigned em = 0; em <= 3; ++em) {
        for (int tracking = -4; tracking <= 4; tracking += 2) {
            book_layout_set_first_line_indent(em);
            book_layout_set_typography(tracking);
            const char *cases[] = {"甲乙丙丁", "　甲乙丙丁", "\xef\xbb\xbf\xe2\x80\x8b甲乙丙丁",
                                    "　“甲乙丙丁”", "（甲乙丙丁）", "「甲乙丙丁」"};
            for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
                test_opener_bearing = i >= 3 ? 20 : 0;
                assert(book_layout_build(cases[i], strlen(cases[i]), r, 48));
                drawn[0] = 0;
                book_layout_draw_page(&fb, 0, r, 48);
                int shift = em ? test_opener_bearing : 0;
                assert(first_draw_x + shift == r.x + (int)em * (33 + tracking));
                assert(book_layout_page_start_offset(0) == 0);
            }
        }
    }
    test_cjk_advance = test_opener_bearing = 0;
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    const char punct[] = "甲乙，丙";
    r = (EpdRect){0, 0, 20, 15};
    assert(book_layout_build(punct, strlen(punct), r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3);
    book_layout_set_first_line_indent(0);
    r = (EpdRect){0, 0, 40, 15};
    assert(book_layout_build("甲乙丙丁，戊", strlen("甲乙丙丁，戊"), r, 10));
    assert(book_layout_page_start_offset(1) == 9);
    book_layout_set_first_line_indent(2);
    const char opener[] = "甲（乙丙";
    r.width = 20;
    assert(book_layout_build(opener, strlen(opener), r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3);
    const char aligned[] = "甲乙";
    blk_t aligned_block = {.offset = 0, .len = strlen(aligned), .image = -1,
                           .align = 1, .indent_percent = 100};
    r = (EpdRect){10, 0, 100, 30};
    assert(book_layout_build_blocks(aligned, strlen(aligned), &aligned_block, 1, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_x == 50 && last_draw_x == 50);
    r = (EpdRect){0, 0, 100, 45};
    book_layout_set_chapter_lead(6, 20);
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_page_start_offset(0) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "body"));
    book_layout_set_chapter_lead(0, 0);
    r.width = 20;
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 6);
    assert(book_layout_page_start_offset(5) == 6);
    drawn[0] = 0;
    for (size_t i = 0; i < book_layout_page_count(); ++i) book_layout_draw_page(&fb, i, r, 10);
    assert(!strcmp(drawn, "Titlebody"));
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, (EpdRect){0,0,100,20}, 10));
    blocks[1].offset = 5;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 0);
    blocks[1].offset = 6;
    blocks[1].len = SIZE_MAX;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(!book_layout_build_blocks(styled, strlen(styled), NULL, 2, r, 10));
    blk_t split_utf8[] = {{.offset = 0, .len = 1, .heading = true, .image = -1},
                          {.offset = 2, .len = 2, .heading = false, .image = -1}};
    assert(!book_layout_build_blocks("甲\nx", 5, split_utf8, 2, r, 10));
    assert(book_layout_build("body", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_px == 10);
    book_layout_set_typography(0);
    r = (EpdRect){0, 0, 35, 100};
    assert(book_layout_build("abcd", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "abcd") && first_draw_x == 0 && last_draw_x == 0);
    book_layout_set_first_line_indent(0);
    book_layout_set_typography(2);
    r = (EpdRect){0, 0, 25, 15};
    assert(book_layout_build("abcd", 4, r, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 2);
    drawn[0] = 0; last_tracking_px = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "ab") && last_tracking_px == 2);
    book_layout_set_typography(-2);
    r.width = 28;
    assert(book_layout_build("abcd", 4, r, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 3);
    drawn[0] = 0; last_tracking_px = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "abc") && last_tracking_px == -2);
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    r = (EpdRect){10, 20, 400, 120};
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    book_layout_set_reading_line(0);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments == 0);
    book_layout_set_reading_line(1);
    book_layout_draw_page(&fb, 0, r, 40);
    int dashed_segments = test_guide_segments;
    assert(dashed_segments > 0);
    assert(test_guide_first_y == 70 && test_guide_height == 2 && test_guide_gray == 0x50);
    book_layout_set_reading_line_offset(-8);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 62);
    book_layout_set_reading_line_offset(8);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 78);
    book_layout_set_reading_line_offset(0);
    book_layout_set_reading_line(2);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments > dashed_segments);
    assert(test_guide_first_y == 70 && test_guide_height == 2 && test_guide_gray == 0x50);
    book_layout_set_spacing(110, 50);
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 62);
    book_layout_set_spacing(200, 50);
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 80);
    book_layout_set_spacing(150, 50);
    book_layout_set_reading_line(0);
    // 右标点可在安全留白内悬挂，下一页必须仍从正文开始。/ Hang a closer safely without starting the next page with punctuation.
    book_layout_set_first_line_indent(0);
    r = (EpdRect){40, 0, 40, 15};
    const char hanging[] = "甲乙丙丁，戊己庚辛";
    assert(book_layout_build(hanging, strlen(hanging), r, 10));
    assert(book_layout_page_start_offset(1) == strlen("甲乙丙丁，"));
    drawn[0] = 0; fitted_target = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "甲乙丙丁，") && fitted_target == 43);
    // 居中块的首行不应带入普通段落缩进。/ A centered block must not inherit first-line body indent.
    blk_t centered = {.offset = 0, .len = strlen("甲乙丙丁戊己"), .align = 1, .indent_percent = 200, .image = -1};
    r = (EpdRect){40, 0, 30, 15};
    assert(book_layout_build_blocks("甲乙丙丁戊己", centered.len, &centered, 1, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    int first_center_x = first_draw_x;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 1, r, 10);
    assert(first_center_x == first_draw_x);
    book_layout_free();
    // Markdown 代码缩进不能变成正文首行缩进。/ Markdown code indentation must survive body indentation settings.
    const char *code = "  code";
    blk_t literal = {.offset=0,.len=strlen(code),.literal=true,.image=-1};
    r=(EpdRect){40,0,300,100};book_layout_set_first_line_indent(3);
    assert(book_layout_build_blocks(code,literal.len,&literal,1,r,10));
    drawn[0]=0;book_layout_draw_page(&fb,0,r,10);
    assert(!strcmp(drawn,code));assert(first_draw_x==r.x);
    book_layout_free();
    // 分层标题、样式跨页连续与坏样式拒绝，不改变普通图书。/ Tiered headings, cross-page styles and invalid runs keep ordinary books intact.
    const char *md="AB\nCD\nEF\nGH";
    blk_t md_blocks[]={{.offset=0,.len=2,.heading=true,.heading_level=1,.image=-1},
        {.offset=3,.len=2,.heading=true,.heading_level=2,.image=-1},
        {.offset=6,.len=2,.heading=true,.heading_level=3,.image=-1},
        {.offset=9,.len=2,.image=-1}};
    uint8_t headings[]={72,64,56};text_style_t run={9,2,7};
    book_layout_set_markdown(true,headings,&run,1);r=(EpdRect){0,0,612,1000};
    assert(book_layout_build_blocks(md,strlen(md),md_blocks,4,r,48));drawn[0]=0;rendered_styles=0;
    book_layout_draw_page(&fb,0,r,48);assert(first_draw_px==72 && last_draw_px==48 && rendered_styles==7);
    assert(!strcmp(drawn,"ABCDEFGH"));assert_slice_text(&fb,0,r,48);run.len=999;
    assert(!book_layout_build_blocks(md,strlen(md),md_blocks,4,r,48));
    const char *across="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN";
    run=(text_style_t){0,strlen(across),7};r=(EpdRect){0,0,388,160};
    book_layout_set_first_line_indent(0);book_layout_set_spacing(150,50);
    assert(book_layout_build(across,strlen(across),r,48));assert(book_layout_page_count()>1);
    drawn[0]=0;rendered_styles=0;test_guide_segments=0;
    for(size_t page=0;page<book_layout_page_count();++page)book_layout_draw_page(&fb,page,r,48);
    assert(strchr(drawn,'-')&&rendered_styles==7&&test_guide_segments>0);
    size_t source_chars=0;
    for(size_t i=0;drawn[i];++i)if(drawn[i]!='-'){
        assert(source_chars<strlen(across)&&drawn[i]==across[source_chars]);
        ++source_chars;
    }
    assert(source_chars==strlen(across));
    book_layout_set_markdown(false,NULL,NULL,0);r=(EpdRect){0,0,612,1000};
    assert(book_layout_build_blocks(md,strlen(md),md_blocks,4,r,48));drawn[0]=0;
    book_layout_draw_page(&fb,0,r,48);assert(first_draw_px==56);
    book_layout_free();
    // 段内换行使用行距，段间空行使用段距，两种边界共用分页与绘制规则。
    // Line breaks take line spacing and paragraph boundaries add paragraph spacing in both pagination and drawing.
    const char *line_breaks = "甲乙丙\n次行\n新段";
    blk_t paragraphs[] = {{.offset=0,.len=16,.image=-1},{.offset=17,.len=6,.image=-1}};
    book_layout_set_markdown(true,NULL,NULL,0);
    r = (EpdRect){0,0,50,80};
    assert(book_layout_build_blocks(line_breaks,strlen(line_breaks),paragraphs,2,r,10));
    book_layout_cursor_t cursor = {0};
    drawn[0] = 0;
    assert(book_layout_draw_slice(&fb,0,r,10,&cursor) == 0);
    assert(cursor.offset == 10 && cursor.used == 15 && last_draw_x == 20);
    assert(book_layout_draw_slice(&fb,0,r,10,&cursor) == 0);
    assert(cursor.offset == 17 && cursor.used == 37);
    assert(book_layout_draw_slice(&fb,0,r,10,&cursor) == 1);
    assert(!strcmp(drawn,"甲乙丙次行新段"));
    r.height = 44;
    assert(book_layout_build_blocks(line_breaks,strlen(line_breaks),paragraphs,2,r,10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 17);
    assert(book_layout_page_for_offset(10) == 0 && book_layout_page_for_offset(17) == 1);
    assert_slice_text(&fb,0,r,10);
    book_layout_free();
    // 重排后旧游标不能恢复；空白、文字和样式使用同一续画实现。
    // A cursor cannot resume across reflow; text and styles share one implementation.
    book_layout_set_markdown(false,NULL,NULL,0);
    const char paragraph_html[] = "<style>p{margin-top:3em;margin-bottom:5pt}</style><p>Alpha</p><p>Bravo</p>";
    const char paragraph_txt[] = "Alpha\r\n \r\n　\r\nBravo";
    html_text_t ebook = {0};
    assert(html_to_blocks(paragraph_html,strlen(paragraph_html),&ebook)==ESP_OK);
    assert(ebook.count==2 && ebook.blocks[0].margin_after_percent==42);
    book_layout_set_first_line_indent(0);
    const unsigned paragraph_steps[] = {0,31,75};
    for(size_t i=0;i<sizeof(paragraph_steps)/sizeof(*paragraph_steps);++i){
        unsigned gap=15*paragraph_steps[i]/100;
        book_layout_set_spacing(150,paragraph_steps[i]);
        r=(EpdRect){0,0,150,30+(int)gap};
        assert(book_layout_build_blocks(ebook.utf8,ebook.len,ebook.blocks,ebook.count,r,10));
        assert(book_layout_page_count()==1);
        book_layout_cursor_t epub_cursor={0};
        assert(book_layout_draw_slice(&fb,0,r,10,&epub_cursor)==0);
        assert(epub_cursor.offset==ebook.blocks[1].offset && epub_cursor.used==15+gap);

        assert(book_layout_build(paragraph_txt,strlen(paragraph_txt),r,10));
        assert(book_layout_page_count()==1);
        book_layout_cursor_t txt_cursor={0};
        drawn[0]=0;
        assert(book_layout_draw_slice(&fb,0,r,10,&txt_cursor)==0);
        assert(txt_cursor.offset==(size_t)(strstr(paragraph_txt,"Bravo")-paragraph_txt));
        assert(txt_cursor.used==epub_cursor.used);
        assert(book_layout_draw_slice(&fb,0,r,10,&txt_cursor)==1 && !strcmp(drawn,"AlphaBravo"));
    }
    book_layout_free();
    html_text_free(&ebook);
    book_layout_set_spacing(150,50);
    r=(EpdRect){0,0,300,300};
    assert(book_layout_build("one\ntwo\nthree",13,r,20));assert_slice_text(&fb,0,r,20);
    book_layout_cursor_t stale={0};assert(book_layout_draw_slice(&fb,0,r,20,&stale)==0);
    assert(book_layout_build("other",5,r,20));assert(book_layout_draw_slice(&fb,0,r,20,&stale)==-1);
    book_layout_free();
    book_layout_set_vertical(true);
    book_layout_set_spacing(110, 0);
    r = (EpdRect){0,0,45,40};
    const char *vertical = "甲乙丙丁戊己庚辛壬癸ABC123";
    assert(book_layout_build(vertical, strlen(vertical), r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_x > last_draw_x && !strcmp(drawn, vertical));
    book_layout_cursor_t vertical_cursor = {0};
    drawn[0] = 0;
    assert(book_layout_draw_slice(&fb, 0, r, 10, &vertical_cursor) == 0);
    assert(vertical_cursor.offset == strlen("甲乙丙丁"));
    r.width = 22;
    assert(book_layout_build(vertical, strlen(vertical), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_for_offset(strlen("甲乙丙丁戊己庚辛")) == 1);
    assert(book_layout_draw_slice(&fb, 0, r, 10, &vertical_cursor) == -1);
    drawn[0] = 0;
    for (size_t page = 0; page < book_layout_page_count(); ++page)
        book_layout_draw_page(&fb, page, r, 10);
    assert(!strcmp(drawn, vertical));

    const char *vertical_illustrated = "甲乙\n\xef\xbf\xbc\n丙丁";
    const blk_t illustrations[] = {
        {.offset=0,.len=6,.image=-1}, {.offset=7,.len=3,.image=0},
        {.offset=11,.len=6,.image=-1}
    };
    book_layout_set_images_visible(true);
    assert(book_layout_build_blocks(vertical_illustrated, strlen(vertical_illustrated), illustrations, 3, r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_image(1) == 0);
    assert(book_layout_page_for_offset(7) == 1 && book_layout_page_for_offset(11) == 2);
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(vertical_illustrated, strlen(vertical_illustrated), illustrations, 3, r, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) < 0);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "甲乙丙丁"));
    book_layout_set_images_visible(true);
    book_layout_set_vertical(false);
    book_layout_free();
    book_layout_set_two_columns(true);
    book_layout_set_spacing(110, 0);
    r = (EpdRect){0,0,116,22};
    const char *two_columns = "甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥";
    assert(book_layout_build(two_columns, strlen(two_columns), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 16 * 3);
    assert(book_layout_page_for_offset(12 * 3) == 0);
    book_layout_cursor_t spread = {0};
    drawn[0] = 0;
    while (book_layout_draw_slice(&fb, 0, r, 10, &spread) == 0) {}
    assert(spread.column == 1 && first_draw_x < last_draw_x);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, two_columns));

    assert(book_layout_build_blocks(vertical_illustrated, strlen(vertical_illustrated),
                                    illustrations, 3, r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_image(1) == 0);
    assert(book_layout_page_for_offset(11) == 2);

    // 双栏共享图和文字的流式排版，跨栏后按图底继续画文字。
    // Images share the text flow, and text resumes below an image that moved across columns.
    assert(html_to_blocks(notes, strlen(notes), &parsed) == ESP_OK);
    book_layout_set_image_dims(small_image_dims, NULL);
    book_layout_set_first_line_indent(0);
    EpdRect image_spread = {0,0,200,90};
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count, image_spread, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image_count(0) == 2);
    unsigned image_column;
    assert(book_layout_page_image_at(0,0,&img,&y,&w,&h,&image_column));
    assert(img == 0 && image_column == 0 && y == 11 && w == 48 && h == 48);
    assert(book_layout_page_image_at(0,1,&img,&y,&w,&h,&image_column));
    assert(img == 1 && image_column == 1 && y == 0 && w == 82 && h == 41);
    drawn[0] = 0;
    spread = (book_layout_cursor_t){0};
    int status;
    while ((status = book_layout_draw_slice(&fb, 0, image_spread, 10, &spread)) == 0) {}
    assert(status == 1 && spread.column == 1 && spread.used == 57);
    assert(last_draw_x == book_layout_column_rect(image_spread, 1).x && !strcmp(drawn, "AABBCC"));

    image_spread.height = 60;
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count, image_spread, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_image_at(0,1,&img,&y,&w,&h,&image_column));
    assert(img == 1 && image_column == 1 && y == 11);
    assert(book_layout_page_start_offset(1) == parsed.blocks[4].offset);
    drawn[0] = 0;
    book_layout_draw_page(&fb,0,image_spread,10);
    book_layout_draw_page(&fb,1,image_spread,10);
    assert(!strcmp(drawn, "AABBCC"));

    image_spread.height = 41;
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count, image_spread, 10));
    assert(book_layout_page_count() == 5);
    assert(book_layout_page_image(1) == 0 && !book_layout_page_image_count(1));
    assert(book_layout_page_image(3) == 1 && !book_layout_page_image_count(3));
    book_layout_free();
    html_text_free(&parsed);
    book_layout_set_image_dims(NULL, NULL);

    book_layout_set_vertical(true);
    r.height = 40;
    assert(book_layout_build(two_columns, strlen(two_columns), r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_x > last_draw_x && !strcmp(drawn, two_columns));
    book_layout_set_two_columns(false);
    book_layout_set_vertical(false);
    book_layout_free();
    puts("book_layout_host_test: PASS");
}
