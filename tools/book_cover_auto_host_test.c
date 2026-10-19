/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover_auto.h"
#include "epdiy.h"

static int glyph_count(const char *text) {
    int count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if ((*p & 0xc0) != 0x80) ++count;
    return count;
}
bool ui_font_has_text(const char *text) { (void)text; return true; }
int ui_font_ascender_px(int px) { return px; }
int ui_font_text_width_px(int px, const char *text) { return glyph_count(text) * px; }
bool ttf_font_ready(void) { return true; }
int ttf_ascender_px(int px) { return px; }
int ttf_text_width_px(int px, const char *text) { return ui_font_text_width_px(px, text); }
static void glyph_rect(uint8_t *fb, int x, int baseline, int px, const char *text,
                       enum EpdFontFlags align, uint8_t fg) {
    int width = ui_font_text_width_px(px, text);
    if (align == EPD_DRAW_ALIGN_CENTER) x -= width / 2;
    else if (align == EPD_DRAW_ALIGN_RIGHT) x -= width;
    for (int g = 0; g < glyph_count(text); ++g)
        for (int y = baseline - px; y < baseline - 2; ++y)
            for (int dx = 0; dx < px - 3; ++dx) {
                int xx = x + g * px + dx;
                if (xx < 0 || xx >= epd_rotated_display_width() ||
                    y < 0 || y >= epd_rotated_display_height()) continue;
                int physical_x = y, physical_y = epd_height() - xx - 1;
                uint8_t *at = fb + (size_t)physical_y * epd_width() / 2 + physical_x / 2;
                if (physical_x & 1) *at = (*at & 0x0f) | (fg << 4);
                else *at = (*at & 0xf0) | fg;
            }
}
void ui_font_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                          enum EpdFontFlags align, uint8_t fg, uint8_t bg, bool black_white) {
    (void)bg; (void)black_white;
    glyph_rect(fb, x, baseline, px, text, align, fg);
}
void ttf_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)bg;
    glyph_rect(fb, x, baseline, px, text, align, fg);
}

enum { W = 176, H = 240, N = W * H };
static void render_checked(const char *identity, const char *title, const char *author,
                           uint8_t out[N]) {
    uint8_t *guard = malloc(N + 32);
    assert(guard);
    memset(guard, 0xa5, N + 32);
    assert(book_auto_cover_render(identity, title, author, W, H, guard + 16));
    for (int i = 0; i < 16; ++i) assert(guard[i] == 0xa5 && guard[N + 16 + i] == 0xa5);
    memcpy(out, guard + 16, N);
    free(guard);
}
int main(void) {
    uint8_t *a = malloc(N), *b = malloc(N);
    assert(a && b);
    // 所有路径采用同一种平底模板，书名和作者之外没有装饰。/ Every path uses the same flat template without decorations outside title and author.
    render_checked("/sdcard/books/a.txt","Title","Author",a);
    render_checked("/sdcard/books/b.txt","Title","Author",b);
    assert(!memcmp(a,b,N));
    bool title_seen=false,author_seen=false;
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){
        uint8_t value=a[y*W+x];
        assert(value==68||value==255);
        if(y<30||(y>=150&&y<196)||y>=211||x<16||x>=W-16)assert(value==68);
        if(value==255){if(y<150)title_seen=true;if(y>=196)author_seen=true;}
    }
    assert(title_seen&&author_seen);
    render_checked("/sdcard/books/long.epub", "夏天、烟火和我的尸体：一个很长很长的中文书名", "三天两觉", a);
    render_checked("/sdcard/books/long.epub", "夏天、烟火和我的尸体：一个很长很长的中文书名", "三天两觉", b);
    assert(!memcmp(a, b, N));
    render_checked("/sdcard/books/english.txt", "The Extremely Long English Title That Must Wrap Cleanly Without Escaping Its Frame", "An Author With a Very Long Name", b);
    assert(memcmp(a, b, N));
    render_checked("/sdcard/books/empty-author.txt", "没有作者的书", "", b);
    for(int y=160;y<H;++y)for(int x=0;x<W;++x)assert(b[y*W+x]==68);
    assert(!book_auto_cover_render("x", "", "", W, H, b));
    assert(!book_auto_cover_render("x", "title", "", 8, H, b));
    free(a); free(b);
    puts("minimal automatic cover: flat dark background, white title/author, no decoration, deterministic rendering and guarded bounds OK");
    return 0;
}
