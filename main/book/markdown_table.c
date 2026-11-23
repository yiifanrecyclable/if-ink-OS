/* SPDX-License-Identifier: Apache-2.0
 * 中文：保留原文字节位置，窄表按列绘制，宽表按列名展开；只缓存单行游标与有界绘制记录。
 * English: Preserve text offsets; grid narrow tables and stack wide tables using row cursors and bounded draw records.
 * 冻结：不修改章节、不刷新屏幕；字体访问由调用方串行化；失败由调用方释放整次布局。
 * Frozen: Never mutate chapters or present; caller serializes font access and frees the entire layout on failure.
 */
#include "markdown_table.h"
#include "book_hyphen.h"
#include "esp_heap_caps.h"
#include "ttf_font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define GRID_COLUMNS 8u
#define OP_MAX 65536u
#define SPAN_MAX 16384u

typedef struct {
    size_t source, len;
    int16_t x, y, width, height, natural;
    uint8_t align, borders, flags, background, marker;
    uint16_t column;
    bool hyphen;
} cell_op_t;
typedef struct {
    size_t page, start, limit, next, first, count;
    int y_end;
} span_t;
typedef struct { size_t at, end; bool empty; } cursor_t;
typedef struct { size_t source, len, next; int width; bool hyphen; } line_t;
typedef struct {
    const blk_t *rows;
    size_t count, after, page;
    int64_t *used;
    markdown_table_page_fn next_page;
    void *ctx;
    bool repeat_header;
    int header_height;
} flow_t;
static cell_op_t *s_ops;
static span_t *s_spans;
static size_t s_op_count, s_op_cap, s_span_count, s_span_cap;
static const char *s_text;
static const markdown_cell_t *s_cells;
static const text_style_t *s_styles;
static size_t s_style_count;
static EpdRect s_rect;
static int s_px, s_band, s_pad, s_tracking;
static bool s_hyphenate;
void markdown_table_free(void) {
    free(s_ops); free(s_spans); s_ops=NULL; s_spans=NULL;
    s_op_count=s_op_cap=s_span_count=s_span_cap=0;
    s_text=NULL; s_cells=NULL; s_styles=NULL; s_style_count=0;
}
void markdown_table_configure(const char *text, const markdown_cell_t *cells,
    const text_style_t *styles, size_t style_count, EpdRect rect, int px, unsigned percent, int tracking, bool hyphenate) {
    s_text=text; s_cells=cells; s_styles=styles; s_style_count=style_count;
    s_rect=rect; s_px=px; s_tracking=tracking; s_hyphenate=hyphenate; s_pad=px/8+3;
    s_band=(int)((unsigned)px*percent/100)+s_pad;
}
static unsigned style_at(size_t at) {
    size_t lo=0,hi=s_style_count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(s_styles[mid].offset<=at)lo=mid+1;else hi=mid;}
    return lo && at-s_styles[lo-1].offset<s_styles[lo-1].len ? s_styles[lo-1].flags : 0;
}
static size_t cp_size(size_t at) {
    unsigned char c=(unsigned char)s_text[at];
    return c<0x80?1:c<0xe0?2:c<0xf0?3:4;
}
static unsigned cp_value(size_t at, size_t n) {
    const unsigned char *p=(const unsigned char *)s_text+at;
    if (n==1) return p[0];
    unsigned cp=p[0]&(0x7f>>n);
    for (size_t i=1;i<n;++i) cp=(cp<<6)|(p[i]&63);
    return cp;
}
static bool closing_mark(unsigned cp) {
    return cp==',' || cp=='.' || cp==':' || cp==';' || cp=='!' || cp=='?' || cp==')' ||
        cp==']' || cp==0x3001 || cp==0x3002 || cp==0xff0c || cp==0xff1a || cp==0xff1b ||
        cp==0xff01 || cp==0xff1f || cp==0xff09 || cp==0x201d || cp==0x300b;
}
static bool opening_mark(unsigned cp) {
    return cp=='(' || cp=='[' || cp==0xff08 || cp==0x201c || cp==0x300a;
}
static bool wrap(size_t at,size_t end,int available,unsigned flags,line_t *line) {
    *line=(line_t){.source=at,.next=at}; int glyphs=0;
    size_t last_start=at, word_start=at, split_at=0;
    int last_width=0, word_width=0, split_width=0, dash_width=0;
    unsigned last_cp=0, dash_style=UINT32_MAX;
    bool previous_joiner=false, split_draws_dash=false;
    book_hyphen_range_t range={0};
    while(at<end && s_text[at]!='\n' && s_text[at]!='\r') {
        size_t n=cp_size(at); char glyph[5]; memcpy(glyph,s_text+at,n);glyph[n]=0;
        int width=ttf_text_width_styled_px(s_px,glyph,flags|style_at(at));
        int advance=width+(glyphs?s_tracking:0);
        if(line->width+advance<0)return false;
        unsigned cp=cp_value(at,n);
        bool previous_part=book_word_part(last_cp);
        bool joiner=(previous_part||previous_joiner)&&book_word_joiner(cp,s_text+at+n,s_text+end);
        bool continuation=(previous_part||previous_joiner)&&(book_word_part(cp)||joiner);
        if(s_hyphenate&&continuation&&previous_joiner&&last_cp=='-' && book_word_part(cp)){
            split_at=at;split_width=line->width;split_draws_dash=false;
        }else if(continuation&&book_hyphen_fits_range(range,s_text+at)){
            unsigned style=flags|style_at(at-1);
            if(style!=dash_style){
                dash_width=ttf_text_width_styled_px(s_px,"-",style);
                dash_style=style;
            }
            int with_dash=line->width+s_tracking+dash_width;
            if(with_dash<=available){split_at=at;split_width=with_dash;split_draws_dash=true;}
        }
        if(line->width+advance>available) {
            if(!glyphs)return false;
            bool closing=closing_mark(cp)||(cp=='-'&&previous_part);
            bool splits_word=continuation||(closing&&previous_part);
            if(splits_word&&split_at>line->source){
                at=split_at;line->width=split_width;line->hyphen=split_draws_dash;break;
            }
            if(splits_word&&word_start>line->source){
                at=word_start;line->width=word_width;break;
            }
            if(splits_word&&!continuation)break;
            // 单元格不悬挂到邻列：将前一个字带到次行，避免标点独占行首。
            // Never hang into an adjacent column; move the preceding glyph to avoid a closing mark at line start.
            if (glyphs>1 && (closing || opening_mark(last_cp))) {
                at=last_start; line->width=last_width;
            }
            break;
        }
        if(!continuation){
            word_start=at;word_width=line->width;split_at=0;
            range=s_hyphenate&&book_latin_letter(cp)&&(at>line->source||!book_word_before(s_text,s_text+at))
                ?book_hyphen_range(s_text+at,s_text+end):(book_hyphen_range_t){0};
        }
        last_start=at;last_width=line->width;last_cp=cp;previous_joiner=joiner;
        line->width+=advance;++glyphs;at+=n;
    }
    line->len=at-line->source;
    if(at<end && (s_text[at]=='\n'||s_text[at]=='\r'))++at;
    line->next=at;
    return at>line->source || at==end;
}
static bool push_op(flow_t *f,cell_op_t op) {
    if(s_op_count==OP_MAX)return false;
    if(s_op_count==s_op_cap){size_t cap=s_op_cap?s_op_cap*2:64;
        cell_op_t *p=heap_caps_realloc(s_ops,cap*sizeof(*p),CAPS);if(!p)return false;s_ops=p;s_op_cap=cap;}
    s_ops[s_op_count++]=op;
    span_t *span=&s_spans[s_span_count-1];++span->count;span->y_end=(int)*f->used+op.height;
    return true;
}
static bool push_span(flow_t *f) {
    if(s_span_count==SPAN_MAX)return false;
    if(s_span_count==s_span_cap){size_t cap=s_span_cap?s_span_cap*2:16;
        span_t *p=heap_caps_realloc(s_spans,cap*sizeof(*p),CAPS);if(!p)return false;s_spans=p;s_span_cap=cap;}
    s_spans[s_span_count++]=(span_t){.page=f->page,.start=f->rows[0].offset,.limit=f->after,
        .next=f->after,.first=s_op_count,.y_end=(int)*f->used};
    return true;
}
static bool grid_row(flow_t *f,const blk_t *row,bool repeating);
static bool new_page(flow_t *f,size_t anchor,bool header) {
    s_spans[s_span_count-1].next=anchor;
    if(!f->next_page(f->ctx,anchor))return false;
    ++f->page;*f->used=0;
    if(!push_span(f))return false;
    return !header || !f->repeat_header || grid_row(f,&f->rows[0],true);
}
static int row_height(const blk_t *row) {
    int max=1,cell_width=s_rect.width/(int)row->table_columns;
    for(unsigned c=0;c<row->table_columns;++c){const markdown_cell_t *cell=&s_cells[row->table_cell+c];
        size_t at=cell->offset,end=at+cell->len;int lines=0;
        do {line_t line;if(!wrap(at,end,cell_width-2*s_pad,row->table_header?TEXT_STYLE_BOLD:0,&line))return -1;
            at=line.next;++lines;if(lines>s_rect.height/s_band+1)return s_rect.height+s_band;
        }while(at<end);
        if(lines>max)max=lines;
    }
    return max*s_band;
}
static bool grid_row(flow_t *f,const blk_t *row,bool repeating) {
    cursor_t cursors[GRID_COLUMNS];line_t lines[GRID_COLUMNS];
    unsigned cols=row->table_columns;int width=s_rect.width/(int)cols;
    int full_height=row_height(row);if(full_height<0)return false;
    if(!repeating && *f->used && *f->used+full_height>s_rect.height &&
       full_height+(f->repeat_header&&!row->table_header?f->header_height:0)<=s_rect.height)
        if(!new_page(f,row->offset,!row->table_header))return false;
    for(unsigned c=0;c<cols;++c){const markdown_cell_t *cell=&s_cells[row->table_cell+c];
        cursors[c]=(cursor_t){cell->offset,cell->offset+cell->len,!cell->len};}
    bool first=true;
    for(;;){size_t anchor=SIZE_MAX;bool pending=false;
        for(unsigned c=0;c<cols;++c)if(cursors[c].at<cursors[c].end||cursors[c].empty){
            if(cursors[c].at<anchor)anchor=cursors[c].at;
            pending=true;}
        if(!pending)break;
        if(*f->used+s_band>s_rect.height){
            if(repeating)return false;
            // 续页前封闭当前行片段。/ Close the current row fragment before continuing on another page.
            if (!first && s_op_count >= cols)
                for (size_t c=s_op_count-cols; c<s_op_count; ++c) s_ops[c].borders |= 2;
            if(!new_page(f,first?row->offset:anchor,!row->table_header))return false;
            first=true;
        }
        bool last=true;
        for(unsigned c=0;c<cols;++c){
            if(!wrap(cursors[c].at,cursors[c].end,width-2*s_pad,row->table_header?TEXT_STYLE_BOLD:0,&lines[c]))return false;
            cursors[c].at=lines[c].next;cursors[c].empty=false;
            if(cursors[c].at<cursors[c].end)last=false;
        }
        for(unsigned c=0;c<cols;++c){int cell_width=c+1==cols?s_rect.width-(int)c*width:width;
            cell_op_t op={.source=lines[c].source,.len=lines[c].len,.x=(int)c*width,.y=(int)*f->used,
                .width=cell_width,.height=s_band,.natural=lines[c].width,.hyphen=lines[c].hyphen,.align=s_cells[row->table_cell+c].align,
                .flags=row->table_header?TEXT_STYLE_BOLD:0,.background=row->table_header?13:15,
                .borders=(first?1:0)|(last?2:0)|4|8};
            if(!push_op(f,op))return false;
        }
        *f->used+=s_band;first=false;
    }
    return true;
}
// 宽表每个字段重复短列名，长内容保持正文大小；截断仅作用于重复列名，原表头仍完整显示。
// Wide tables repeat short field labels at body size; only repeated labels truncate, while original headers remain complete.
static bool stacked_value(flow_t *f,const markdown_cell_t *cell,const markdown_cell_t *label,unsigned column,bool header) {
    size_t at=cell->offset,end=at+cell->len;bool first=true,empty=!cell->len;
    while(at<end||empty){
        int label_lines=label?1:0;line_t label_line[2]={{0}};
        if(label && label->len){
            if(!wrap(label->offset,label->offset+label->len,s_rect.width-2*s_pad,TEXT_STYLE_BOLD,&label_line[0]))return false;
            if(label_line[0].next<label->offset+label->len){
                if(!wrap(label_line[0].next,label->offset+label->len,s_rect.width-2*s_pad-s_px,TEXT_STYLE_BOLD,&label_line[1]))return false;
                label_lines=2;
            }
        }
        if ((label_lines+1)*s_band>s_rect.height) {label=NULL;label_lines=0;}
        int required=(first?label_lines+1:1)*s_band;
        if(*f->used+required>s_rect.height){
            if(!new_page(f,at,false))return false;
            first=true;
        }
        if(first && label){
            for(int l=0;l<label_lines;++l){line_t *line=&label_line[l];
                cell_op_t op={.source=line->source,.len=line->len,.x=0,.y=(int)*f->used,
                    .width=s_rect.width,.height=s_band,.natural=line->width,.hyphen=line->hyphen,.flags=TEXT_STYLE_BOLD,
                    .background=13,.borders=l?0:1,.marker=!label->len?1:
                        l==label_lines-1&&line->next<label->offset+label->len?2:0,.column=column};
                if(!push_op(f,op))return false;
                *f->used+=s_band;
            }
        }
        line_t line;if(!wrap(at,end,s_rect.width-2*s_pad,header?TEXT_STYLE_BOLD:0,&line))return false;
        cell_op_t op={.source=line.source,.len=line.len,.x=0,.y=(int)*f->used,.width=s_rect.width,
            .height=s_band,.natural=line.width,.hyphen=line.hyphen,.flags=header?TEXT_STYLE_BOLD:0,.background=header?13:15,
            .borders=(header&&first?1:0)|(line.next==end?2:0)};
        if(!push_op(f,op))return false;
        *f->used+=s_band;at=line.next;empty=false;first=false;
    }
    return true;
}
bool markdown_table_build(const blk_t *rows,size_t count,size_t after,size_t page,
    int64_t *used,markdown_table_page_fn next_page,void *ctx) {
    if(!count||s_band>s_rect.height||!s_cells)return false;
    flow_t f={rows,count,after,page,used,next_page,ctx,false,0};
    if(!push_span(&f))return false;
    int advance=ttf_text_width_px(s_px,"一");if(advance<s_px)advance=s_px;
    bool grid=rows[0].table_columns<=GRID_COLUMNS &&
        s_rect.width/(int)rows[0].table_columns>=advance*2+2*s_pad;
    if(grid){
        f.header_height=row_height(rows);
        if(f.header_height<0)return false;
        f.repeat_header=rows[0].table_header && count>1 && f.header_height+2*s_band<=s_rect.height;
        for(size_t r=0;r<count;++r)if(!grid_row(&f,&rows[r],false))return false;
    }else{
        // 短列名直接随内容显示；过长列名另完整显示一次，避免截断造成信息丢失。
        // Pair short labels directly with values; show oversized headers once in full to avoid losing information.
        bool show_header=count==1 || !rows[0].table_header;
        for (unsigned c=0; !show_header && c<rows[0].table_columns; ++c) {
            const markdown_cell_t *cell=&s_cells[rows[0].table_cell+c];
            line_t first,second;
            if (!wrap(cell->offset,cell->offset+cell->len,s_rect.width-2*s_pad,TEXT_STYLE_BOLD,&first)) return false;
            if (first.next<cell->offset+cell->len) {
                if (!wrap(first.next,cell->offset+cell->len,s_rect.width-2*s_pad-s_px,TEXT_STYLE_BOLD,&second)) return false;
                if (second.next<cell->offset+cell->len) show_header=true;
            }
        }
        for(size_t r=show_header?0:1;r<count;++r)for(unsigned c=0;c<rows[r].table_columns;++c){
            const markdown_cell_t *cell=&s_cells[rows[r].table_cell+c];
            const markdown_cell_t *label=r && rows[0].table_header?&s_cells[rows[0].table_cell+c]:NULL;
            if(!stacked_value(&f,cell,label,c+1,rows[r].table_header))return false;
        }
    }
    *used+=s_px/2;s_spans[s_span_count-1].y_end=(int)*used;
    return true;
}
static void draw_op(uint8_t *fb,const cell_op_t *op,EpdRect rect) {
    EpdRect cell={rect.x+op->x,rect.y+op->y,op->width,op->height};
    if(op->background!=15)epd_fill_rect(cell,op->background<<4,fb);
    size_t at=op->source,end=at+op->len;int x=cell.x+s_pad;
    int available=cell.width-2*s_pad;
    if(op->align==1)x+=(available-op->natural)/2;
    else if(op->align==2)x+=available-op->natural;
    int baseline=cell.y+(cell.height-s_px)/2+ttf_ascender_px(s_px);
    // 所有背景先绘制，斜体笔画不会被邻字高亮覆盖。/ Paint backgrounds first so adjacent highlights preserve italic ink.
    for(int pass=0;pass<2;++pass){int cursor=x;
        for(at=op->source;at<end;){size_t n=cp_size(at);char glyph[5];memcpy(glyph,s_text+at,n);glyph[n]=0;
            unsigned flags=op->flags|style_at(at);int width=ttf_text_width_styled_px(s_px,glyph,flags);
            uint8_t bg=flags&TEXT_STYLE_HIGHLIGHT?11:op->background;
            if(!pass && bg!=op->background)epd_fill_rect((EpdRect){cursor,cell.y,width,cell.height},bg<<4,fb);
            if(pass)ttf_draw_glyph_styled_px(fb,cursor,baseline,s_px,glyph,flags,0,bg);
            cursor+=width+(at+n<end||op->hyphen?s_tracking:0);at+=n;
        }
        if(op->hyphen){
            unsigned flags=op->flags|style_at(end-1);
            uint8_t bg=flags&TEXT_STYLE_HIGHLIGHT?11:op->background;
            if(!pass&&bg!=op->background)
                epd_fill_rect((EpdRect){cursor,cell.y,ttf_text_width_styled_px(s_px,"-",flags),cell.height},bg<<4,fb);
            if(pass)ttf_draw_glyph_styled_px(fb,cursor,baseline,s_px,"-",flags,0,bg);
        }
    }
    if(op->marker){char marker[24];if(op->marker==1)snprintf(marker,sizeof(marker),"列 %u",op->column);
        else snprintf(marker,sizeof(marker),"…");
        ttf_draw_text_px(fb,op->marker==1?x:cell.x+cell.width-s_pad-s_px,baseline,s_px,marker,EPD_DRAW_ALIGN_LEFT,0,op->background);}
    if(op->borders&1)epd_fill_rect((EpdRect){cell.x,cell.y,cell.width,1},0x80,fb);
    if(op->borders&2)epd_fill_rect((EpdRect){cell.x,cell.y+cell.height-1,cell.width,1},0x80,fb);
    if(op->borders&4)epd_fill_rect((EpdRect){cell.x,cell.y,1,cell.height},0x80,fb);
    if(op->borders&8)epd_fill_rect((EpdRect){cell.x+cell.width-1,cell.y,1,cell.height},0x80,fb);
}
bool markdown_table_draw(uint8_t *fb,size_t page,size_t offset,EpdRect rect,size_t *next,int64_t *used) {
    size_t lo=0,hi=s_span_count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(s_spans[mid].page<page)lo=mid+1;else hi=mid;}
    for(size_t i=lo;i<s_span_count&&s_spans[i].page==page;++i){const span_t *span=&s_spans[i];
        if(offset<span->start||offset>=span->limit)continue;
        for(size_t j=span->first;j<span->first+span->count;++j)draw_op(fb,&s_ops[j],rect);
        *next=span->next;*used=span->y_end;return true;
    }
    return false;
}
