/* SPDX-License-Identifier: Apache-2.0
 * 中文：验证Markdown内容保真、块契约、资源边界及分配失败。
 * English: Check Markdown preservation, layout block contracts, bounds and allocation failures.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "markdown_text.h"
int html_test_fail_after = -1;
static int md_fail_after = -1;
void *md_test_malloc(size_t n) {
    if(md_fail_after == 0) return NULL;
    if(md_fail_after > 0) --md_fail_after;
    return malloc(n);
}
void *md_test_realloc(void *p, size_t n) {
    if(md_fail_after == 0) return NULL;
    if(md_fail_after > 0) --md_fail_after;
    return realloc(p,n);
}
static void invariant(const html_text_t *t) {
    assert(t->utf8 && strlen(t->utf8) == t->len);
    size_t end = 0;
    for (size_t i = 0; i < t->count; ++i) {
        const blk_t *b = &t->blocks[i];
        assert(b->len && b->offset == end && (b->image == -1 || (size_t)b->image < t->image_count));
        end = b->offset + b->len;
        assert(end <= t->len);
        if (b->table_columns) {
            assert(b->table_id && b->table_cell+b->table_columns <= t->cell_count);
            for (size_t c=b->table_cell; c<b->table_cell+b->table_columns; ++c) {
                assert(t->cells[c].offset>=b->offset && t->cells[c].offset+t->cells[c].len<=end);
                assert(t->cells[c].align<=2);
            }
        }
        if (i + 1 < t->count) { assert(t->utf8[end] == '\n'); ++end; }
    }
    assert(end == t->len);
    end=0;
    for(size_t i=0;i<t->style_count;++i){const text_style_t *run=&t->styles[i];
        assert(run->offset>=end&&run->len&&run->offset+run->len<=t->len&&run->flags&&run->flags<=7);
        assert(((unsigned char)t->utf8[run->offset]&0xc0)!=0x80);
        end=run->offset+run->len;
        assert(end==t->len||((unsigned char)t->utf8[end]&0xc0)!=0x80);
    }
}
static html_text_t parse(const char *s, const char *expected) {
    html_text_t t = {0};
    assert(markdown_to_blocks(s, strlen(s), &t) == ESP_OK);
    invariant(&t);
    if (strcmp(t.utf8, expected)) fprintf(stderr, "expected <%s>, got <%s>\n", expected, t.utf8);
    assert(!strcmp(t.utf8, expected));
    return t;
}
static void reject(const char *s, size_t n, esp_err_t error) {
    html_text_t t = {0};
    assert(markdown_to_blocks(s, n, &t) == error);
    assert(!t.utf8 && !t.blocks && !t.count && !t.len && !t.images);
}
static unsigned flags_at(const html_text_t *t, size_t at) {
    for(size_t i=0;i<t->style_count;++i) if(at>=t->styles[i].offset && at-t->styles[i].offset<t->styles[i].len)return t->styles[i].flags;
    return 0;
}
int main(void) {
    html_text_t styled=parse("**粗体** *斜体* ==高亮 **重点**== ***组合***", "粗体 斜体 高亮 重点 组合");
    assert(flags_at(&styled,0)==TEXT_STYLE_BOLD);
    assert(flags_at(&styled,7)==TEXT_STYLE_ITALIC);
    assert(flags_at(&styled,14)==TEXT_STYLE_HIGHLIGHT);
    assert(flags_at(&styled,21)==(TEXT_STYLE_HIGHLIGHT|TEXT_STYLE_BOLD));
    assert(flags_at(&styled,28)==(TEXT_STYLE_BOLD|TEXT_STYLE_ITALIC));
    html_text_free(&styled);
    styled=parse("`==code==` \\==escaped==\n\n&#61;&#61;entity==\n\n==open", "==code== ==escaped==\n==entity==\n==open");
    assert(!styled.style_count);
    // 转义开标记不参与后续配对；实体不会生成格式。/ Escaped openers and entities must never become formatting.
    html_text_free(&styled);
    styled=parse("==open\n\nclosed==\n\n```\n==code== **literal**\n```", "==open\nclosed==\n==code== **literal**");
    assert(!styled.style_count);html_text_free(&styled);
    styled=parse("==first== and ==second==\n\n# ==Title==", "first and second\nTitle");
    assert(styled.blocks[1].offset==17 && flags_at(&styled,17)==TEXT_STYLE_HIGHLIGHT);html_text_free(&styled);

    html_text_t t = parse("", ""); assert(!t.count); html_text_free(&t);
    t = parse("\xef\xbb\xbf---\r\ntitle: test\r\n---\r\n# 中文\r\n\r\n正文", "中文\n正文");
    assert(t.blocks[0].heading && !t.blocks[1].heading); html_text_free(&t);
    t = parse("# 一\n## 二\n### 三\n#### 四\n##### 五\n###### 六", "一\n二\n三\n四\n五\n六");
    assert(t.count == 6); for(size_t i=0;i<t.count;++i) assert(t.blocks[i].heading && t.blocks[i].heading_level==i+1); html_text_free(&t);
    t = parse("- outer\n  - inner\n\n> quote\n> next\n\n3. third\n4. fourth\n\n- [x] done\n- [ ] todo",
              "- outer\n  - inner\n> quote\nnext\n3. third\n4. fourth\n[x] done\n[ ] todo"); for(size_t i=0;i<t.count;++i) assert(t.blocks[i].literal); html_text_free(&t);
    t = parse("甲\r\n乙\r\n\r\n\r\n丙", "甲\n乙\n丙");
    assert(t.count == 2 && t.blocks[0].len == 7 && t.blocks[1].offset == 8); html_text_free(&t);
    t = parse("a\nb  \nc\\\nd\n\ne", "a\nb\nc\nd\ne");
    assert(t.count == 2 && t.blocks[1].offset == 8); html_text_free(&t);
    t = parse("**first\nsecond**\n\n- item\n  continuation\n- next", "first\nsecond\n- item\ncontinuation\n- next");
    assert(t.count == 3 && t.blocks[1].literal && flags_at(&t, 6) == TEXT_STYLE_BOLD); html_text_free(&t);
    t = parse("heading\ncontinuation\n======", "heading continuation");
    assert(t.count == 1 && t.blocks[0].heading); html_text_free(&t);
    t = parse("before\n![image\nalt](image.png)\nafter",
              "before \n[图片: image alt]\n after");
    assert(t.count == 3 && t.blocks[1].image == 0); html_text_free(&t);
    t = parse("before ![image alt](../image.png) after\n\n![second](https://example.org/a.png)",
              "before \n[图片: image alt]\n after\n[图片: second]"); assert(t.image_count==2);assert(!strcmp(t.images[0],"../image.png"));assert(t.blocks[1].image==0); html_text_free(&t);
    t = parse("```c\n\tcode &amp;\n\nlast\n```\n\n`literal &amp;`",
              "    code &amp;\n\nlast\nliteral &amp;"); html_text_free(&t);
    t = parse("a&nbsp;b &amp; &lt; &#20013; &#x1F600; &#0; &unknown;", "a b & < 中 😀 � &unknown;"); html_text_free(&t);
    t = parse("a  \nb\n\n<table>literal</table>\n\n**bold** and *italic*", "a\nb\n<table>literal</table>\nbold and italic"); html_text_free(&t);
    t = parse("| left | right |\n| --- | --- |\n| A | B |", "left | right\nA | B"); html_text_free(&t);
    t = parse("| **名** | ==值== | 空 |\n| :--- | :---: | ---: |\n| a\\|b | *c* | |", "名 | 值 | 空\na|b | c | ");
    assert(t.cell_count==6 && t.count==2 && t.blocks[0].table_header && !t.blocks[1].table_header);
    assert(t.cells[0].offset==0 && t.cells[0].len==3 && t.cells[1].offset==6 && t.cells[1].len==3);
    assert(t.cells[1].align==1 && t.cells[2].align==2 && t.cells[5].len==0);
    assert(flags_at(&t,t.cells[1].offset)==TEXT_STYLE_HIGHLIGHT);
    assert(flags_at(&t,t.cells[4].offset)==TEXT_STYLE_ITALIC); html_text_free(&t);
    t = parse("| A | B |\n| --- | --- |\n| ==open | closed== |\n| ![alt](x.png) | value |", "A | B\n==open | closed==\n[图片: alt] | value");
    assert(!t.style_count && !t.image_count && t.blocks[2].table_columns==2);html_text_free(&t);
    t = parse("| |\n| --- |\n| |", " \n ");assert(t.count==2 && t.cell_count==2);html_text_free(&t);
    t = parse("| A | B | C |\n| --- | --- | --- |\n| | 2 | 3 |\n| 1 | | 3 |\n| 1 | 2 | |\n| | | |",
              "A | B | C\n | 2 | 3\n1 |  | 3\n1 | 2 | \n |  | "); html_text_free(&t);
    t = parse("---\ntitle: unclosed", "---\ntitle: unclosed"); html_text_free(&t);
    reject("a\0b", 3, ESP_ERR_INVALID_ARG);
    reject("\xc0\xaf", 2, ESP_ERR_INVALID_ARG);
    reject("\xed\xa0\x80", 3, ESP_ERR_INVALID_ARG);
    reject("\xf4\x90\x80\x80", 4, ESP_ERR_INVALID_ARG);
    reject("\xe4\xb8", 2, ESP_ERR_INVALID_ARG);
    reject(NULL, 1, ESP_ERR_INVALID_ARG);
    char *large = malloc(MARKDOWN_INPUT_MAX + 1); assert(large);
    memset(large, 'a', MARKDOWN_INPUT_MAX + 1);
    reject(large, MARKDOWN_INPUT_MAX + 1, ESP_ERR_INVALID_SIZE);
    assert(markdown_to_blocks(large, MARKDOWN_INPUT_MAX, &t) == ESP_OK); invariant(&t); html_text_free(&t);
    memset(large, 0, MARKDOWN_INPUT_MAX + 1);
    for (size_t i = 0; i < MARKDOWN_BLOCK_MAX + 1; ++i) strcat(large, "a\n\n");
    reject(large, strlen(large), ESP_ERR_INVALID_SIZE);
    large[0] = 0; for (unsigned i=0;i<MARKDOWN_IMAGE_MAX+1;++i) strcat(large,"![x](x) ");
    reject(large, strlen(large), ESP_ERR_INVALID_SIZE);
    memset(large, '>', 100); strcpy(large + 100, " x"); reject(large, strlen(large), ESP_ERR_INVALID_SIZE);
    strcpy(large,"![x]("); memset(large+5,'a',MARKDOWN_PATH_MAX+1); strcpy(large+5+MARKDOWN_PATH_MAX+1,")");
    reject(large, strlen(large), ESP_ERR_INVALID_SIZE); free(large);
    bool reached_success = false;
    const char *alloc = "# heading\n\n- item\n\ntext **粗体** ==*高亮*== &amp; text\n\n![image](x)\n\n| **H** | I |\n| --- | --- |\n| ==v== | |";
    for(int n=0;n<64;++n) {
        html_test_fail_after = n;
        esp_err_t e = markdown_to_blocks(alloc, strlen(alloc), &t);
        if(e == ESP_OK) { invariant(&t); html_text_free(&t); reached_success=true; break; }
        assert(e == ESP_ERR_NO_MEM && !t.utf8 && !t.blocks && !t.len);
    }
    assert(reached_success); html_test_fail_after=-1;
    const char *owned[] = {
        "before [link](a&amp;b) after ![image](c&amp;d \"t&amp;x\")",
        "[link][ref]\n\n[ref]: a&amp;b \"title&amp;x\"",
        "[growth](a&amp;b&amp;c&amp;d&amp;e&amp;f&amp;g&amp;h&amp;i&amp;j)",
    };
    for(size_t i=0;i<sizeof(owned)/sizeof(owned[0]);++i) {
        reached_success=false;
        for(int n=0;n<256;++n) {
            md_fail_after=n;
            esp_err_t e=markdown_to_blocks(owned[i],strlen(owned[i]),&t);
            if(e==ESP_OK) { invariant(&t);html_text_free(&t);reached_success=true;break; }
            assert(e==ESP_ERR_NO_MEM && !t.utf8 && !t.blocks);
        }
        assert(reached_success);md_fail_after=-1;
    }
    for(int n=0;n<100;++n) { t=parse("# 标题\n\n正文", "标题\n正文"); html_text_free(&t); }
    puts("Markdown content, block invariants, bounds and OOM checks passed");
}
