/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：验证章节 HTML 解析、边界和分配失败。
 * English: Verify chapter HTML parsing, bounds and allocation failures.
 * 冻结：仅供宿主测试。/ Frozen: Host tests only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "html_text.h"
int html_test_fail_after = -1;
static html_text_t parse(const char* html, const char* expected) {
    html_text_t out = {0};
    assert(html_to_blocks(html, strlen(html), &out) == ESP_OK);
    assert(out.utf8 != NULL && out.len == strlen(expected));
    assert(strcmp(out.utf8, expected) == 0);
    for (size_t i = 0; i < out.count; ++i) {
        assert(out.blocks[i].len > 0);
        assert(out.blocks[i].offset + out.blocks[i].len <= out.len);
        if (i) assert(out.blocks[i].offset == out.blocks[i - 1].offset + out.blocks[i - 1].len + 1);
    }
    return out;
}
int main(void) {
    html_text_t t = parse("<HEAD><title>隐</title><style>x</style></HEAD><div><div></div><H1>标题 <i>甲</i></H1><p>A  B\n C</p><div></div><p>乙<br/>丙</p></div>", "标题 甲\nA B C\n乙\n丙");
    assert(t.count == 4 && t.blocks[0].heading && !t.blocks[1].heading);
    html_text_free(&t);
    t = parse("a<script>if(a < b){x='<p>fake</p>'}</script><style>p{content:'<x>'}</style><!-- hidden > -->b", "ab");
    html_text_free(&t);
    t = parse("<head><script>var x='</head>';</script><title>hidden</title></head><p>visible</p>", "visible");
    html_text_free(&t);
    t = parse("a<script><!-- raw JS without a comment close </script>b", "ab");
    html_text_free(&t);
    t = parse("<script>const s = '<!--';</script><p>ok</p>", "ok");
    html_text_free(&t);
    t = parse("<head><style>p:before{content:'<!-- </head>'}</style><title>hidden</title></head><p>ok</p>", "ok");
    html_text_free(&t);
    t = parse("a<3 and 2 > 1", "a<3 and 2 > 1");
    html_text_free(&t);
    t = parse("<p title='x > y'> &amp; &lt; &gt; &quot; &apos; &nbsp; &#20013; &#x1F600; </p>", "& < > \" ' 中 😀");
    html_text_free(&t);
    t = parse("<h2>two</h2><h3>three</h3><h4>four</h4><li>x</li><tr>y</tr><hr><blockquote>z</blockquote>", "two\nthree\nfour\nx\ny\nz");
    assert(t.count == 6 && t.blocks[0].heading && t.blocks[1].heading && !t.blocks[2].heading);
    html_text_free(&t);
    t = parse("<style>body{margin-bottom: .5em} p{ text-indent:2em; margin-top:8px } .center{text-align:center}</style>"
              "<p class='center'>甲</p><p style='text-align:right; margin-bottom:25%'>乙</p>", "甲\n乙");
    assert(t.count == 2);
    assert(t.blocks[0].align == 1 && t.blocks[0].indent_percent == 200);
    assert(t.blocks[0].margin_before_percent == 50 && t.blocks[0].margin_after_percent == 50);
    assert(t.blocks[1].align == 2 && t.blocks[1].indent_percent == 200);
    assert(t.blocks[1].margin_after_percent == 25);
    html_text_free(&t);
    t = parse("<p style='text-align:left'>Left</p><p style='text-align:start'>Start</p>"
              "<p style='text-align:justify'>Justify</p><p>Default</p>",
              "Left\nStart\nJustify\nDefault");
    assert(t.count == 4 && t.blocks[0].align == HTML_ALIGN_LEFT && t.blocks[1].align == HTML_ALIGN_LEFT);
    assert(t.blocks[2].align == HTML_ALIGN_DEFAULT && t.blocks[3].align == HTML_ALIGN_DEFAULT);
    html_text_free(&t);
    t = parse("<style>p { margin-top:5pt; margin-bottom:5pt; }</style>"
              "<p>First</p><p style='margin-bottom:12PT'>Second</p>", "First\nSecond");
    assert(t.count == 2);
    assert(t.blocks[0].margin_before_percent == 42 && t.blocks[0].margin_after_percent == 42);
    assert(t.blocks[1].margin_before_percent == 42 && t.blocks[1].margin_after_percent == 100);
    html_text_free(&t);
    t = parse("<p>前</p><img src='data:image/png;base64,x' data-src='../图&amp;文.png'/>"
              "<svg:image xlink:href='../svg.png'/><object data='../obj.png'/>",
              "前\n￼\n￼\n￼");
    assert(t.image_count == 3 && !strcmp(t.images[0], "../图&文.png") &&
           !strcmp(t.images[1], "../svg.png") && !strcmp(t.images[2], "../obj.png"));
    html_text_free(&t);
    t = parse("<p>前<a href='#note_5' id='call'><sup><img src='dot.png' alt='[１２]'/></sup></a>后</p>"
              "<p id='note_5'><a href='#call' role='doc-backlink'><img src='data:image/png;base64,AA=='/></a>注释</p>",
              "前￼后\n￼注释");
    assert(t.count == 2 && !t.image_count && t.link_count == 2);
    assert(t.links[0].start == 3 && t.links[0].end == 6 && !strcmp(t.links[0].image_label, "12"));
    // 0.2.3 placed an on-disk image between two newline separators. Its saved offsets still locate the same text.
    assert(html_position_convert(&t.positions, 4, true) == 3);
    assert(html_position_convert(&t.positions, 8, true) == 6);
    assert(html_position_convert(&t.positions, 6, false) == 8);
    assert(!strcmp(t.links[0].anchor, "call") && !strcmp(t.links[1].image_label, "*") && t.links[1].backlink);
    html_text_free(&t);
    t = parse("<p>A<a href='#details'><img src='cover.png' width='240px' height='32' alt='1'/></a>B</p>"
              "<p>C<a href='#note_3'><img src='small.png' width='24' height='24'/> Caption</a>D</p>"
              "<p>E<a href='https://example.invalid/#note_1'><img src='icon.png' width='24' height='24'/></a>F</p>",
              "A\n￼\nB\nC\n￼\nCaptionD\nE\n￼\nF");
    assert(t.image_count == 3 && t.link_count == 2);
    assert(!t.links[0].image_label[0] && !t.links[1].image_label[0]);
    html_text_free(&t);
    html_test_fail_after = 3;
    t = parse("<p>lead</p><p>a<a href='#fn'><img src='note.png'/></a>b</p>", "lead\na*b");
    assert(t.count == 2 && !t.link_count && !t.image_count);
    html_text_free(&t);
    html_test_fail_after = -1;
    const char *short_note = "<p>Body</p><aside epub:type='footnote'><div><p id='note'>First</p></div>"
                             "<p>Second</p></aside><p>Outside</p>";
    size_t anchor = SIZE_MAX;
    assert(html_to_blocks_with_css_anchor(short_note, strlen(short_note), NULL, 0, "note", &anchor, &t) == ESP_OK);
    assert(t.note_kind == HTML_NOTE_EXPLICIT && t.note_start <= anchor && anchor < t.note_end);
    assert(t.note_end - anchor == strlen("First\nSecond") &&
           !memcmp(t.utf8 + anchor, "First\nSecond", t.note_end - anchor));
    html_text_free(&t);
    const char *plain_note = "<p>Body</p><p><a id='note' href='#ref'>[1]</a> Plain note.</p><p>Outside</p>";
    assert(html_to_blocks_with_css_anchor(plain_note, strlen(plain_note), NULL, 0, "note", &anchor, &t) == ESP_OK);
    assert(t.note_kind == HTML_NOTE_PARAGRAPH && t.note_start == anchor);
    assert(t.note_end - anchor == strlen("[1] Plain note."));
    html_text_free(&t);
    const char *unclosed_note = "<p>Body</p><aside role='doc-footnote'><p id='note'>Unclosed</p>";
    assert(html_to_blocks_with_css_anchor(unclosed_note, strlen(unclosed_note), NULL, 0, "note", &anchor, &t) == ESP_OK);
    assert(anchor != SIZE_MAX && t.note_kind == HTML_NOTE_NONE);
    html_text_free(&t);
    const char* external = ".center{text-align:center}";
    assert(html_to_blocks_with_css("<p class='center'>甲</p>", strlen("<p class='center'>甲</p>"),
                                    external, strlen(external), &t) == ESP_OK);
    assert(t.count == 1 && t.blocks[0].align == 1);
    html_text_free(&t);
    t = parse("  <div><div> </div></div><br><hr> ", "");
    assert(t.count == 0);
    html_text_free(&t);
    t = parse("a &unknown; &amp b < 3", "a &unknown; &amp b < 3");
    html_text_free(&t);
    t = parse("&#0; &#xD800; &#x110000;", "� � �");
    html_text_free(&t);
    assert(html_to_blocks(NULL, 0, &t) == ESP_OK && t.count == 0);
    html_text_free(&t);
    assert(html_to_blocks(NULL, 1, &t) == ESP_ERR_INVALID_ARG);
    assert(html_to_blocks("x", HTML_TEXT_MAX_BYTES + 1, &t) == ESP_ERR_INVALID_SIZE);
    assert(html_to_blocks("\xe7\x94", 2, &t) != ESP_OK && !t.utf8 && !t.blocks);
    assert(html_to_blocks("<p title='unterminated", 22, &t) == ESP_ERR_INVALID_RESPONSE);
    char* large = malloc(HTML_TEXT_MAX_BYTES);
    memset(large, 'x', HTML_TEXT_MAX_BYTES);
    assert(html_to_blocks(large, HTML_TEXT_MAX_BYTES, &t) == ESP_OK);
    assert(t.len == HTML_TEXT_MAX_BYTES && t.count == 1 && t.utf8[t.len] == 0);
    html_text_free(&t);
    free(large);
    size_t n = HTML_TEXT_MAX_BLOCKS + 1;
    char* many = malloc(n * 8 + 1);
    for (size_t i = 0; i < n; ++i) memcpy(many + i * 8, "<p>x</p>", 8);
    assert(html_to_blocks(many, (n - 1) * 8, &t) == ESP_OK && t.count == HTML_TEXT_MAX_BLOCKS);
    html_text_free(&t);
    assert(html_to_blocks(many, n * 8, &t) == ESP_ERR_INVALID_SIZE);
    assert(!t.utf8 && !t.blocks && t.count == 0);
    free(many);
    for (int i = 0; i < 3; ++i) {
        html_test_fail_after = i;
        assert(html_to_blocks("<p>test</p>", 11, &t) == ESP_ERR_NO_MEM);
        assert(!t.utf8 && !t.blocks);
    }
    html_test_fail_after = -1;
    html_text_free(&t);
    puts("html_text_host_test: PASS");
}
