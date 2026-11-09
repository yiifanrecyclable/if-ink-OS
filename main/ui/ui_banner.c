/* SPDX-License-Identifier: Apache-2.0
 * 中文：格言字号可调、默认96px，按实际字宽换行；保存/选字库/改字号时校验，不在绘制中打开字体。
 * English: Use adjustable type with a 96px default, wrap by real advances, and validate on save/font/size selection, without opening fonts during paint.
 */
#include "ui_banner.h"
#include "ui_kit.h"
#include "ui_bitmap.h"
#include "ui_wallpaper.h"
#include "ttf_font.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "ui_lunar.h"
#include "esp_heap_caps.h"
#ifdef IFINK_BANNER_LAYER_TEST
#include "book/document_store.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "shelf_perf.h"
#include "ui_font.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef ESP_PLATFORM
#include "esp_app_desc.h"
#endif
#endif
#define BANNER_TEXT_WIDTH (UI_LOCK_WIDTH-2*UI_BANNER_QUOTE_INSET)
static const char *const longest_date="2088年12月30日 · 星期三 · 癸亥猪年闰腊月三十";
static int width(const char *text,bool custom,int px){return custom?ttf_text_width_px(px,text):ui_text_fixed_width_px(px,text);}
static int ink_height(int px,const char *text,bool custom){if(!text[0])text="国Ag";int a=0,b=0;if(custom)ttf_measure_line_px(px,text,&a,&b);else ui_text_fixed_measure_px(px,text,&a,&b);return a+b;}
ui_banner_positions_t ui_banner_positions_size(const char *date,int quote_px){
    // 二分求最大字号，避免每次首页绘制逐字号栅格化整行日期。/ Binary-search size instead of rasterizing every intervening size on each home draw.
    int lo=12,hi=96;
    while(lo<hi){int mid=(lo+hi+1)/2;
        if(ui_text_fixed_width_px(mid,date)<=612&&ui_text_fixed_width_px(mid,longest_date)<=612)lo=mid;
        else hi=mid-1;
    }
    int px=lo;
    int dh=ink_height(px,date,false);
    int qh=ink_height(quote_px,"国Ag我将宇宙随身携带",false);if(qh<1)qh=quote_px;
    int total=dh+qh+UI_BANNER_GAP;
    int top=UI_PAGE_Y(205)-total/2,limit=UI_BANNER_BOTTOM-total-qh-UI_BANNER_GAP;
    if(top>limit)top=limit;
    if(top<UI_STATUS_HEIGHT+2)top=UI_STATUS_HEIGHT+2;
    return (ui_banner_positions_t){.date_px=px,.date_top=top,.quote_top=top+dh+UI_BANNER_GAP,.quote_height=qh,.quote_px=quote_px,.quote_align=content_home_quote_alignment(),.quote_valign=content_home_quote_valign()};
}
static bool quote_layout_size(const char *text,bool custom,int px,ui_banner_quote_t *out){
    memset(out,0,sizeof(*out));if(!content_home_quote_valid(text))return false;const char *next=text;
    for(int row=0;row<UI_BANNER_QUOTE_LINES&&(*next||row<out->count);++row){size_t used=0;out->count=row+1;
        while(*next&&*next!='\n'){
            unsigned char ch=(unsigned char)*next;size_t bytes=ch<0x80?1:ch<0xe0?2:ch<0xf0?3:4;
            memcpy(out->lines[row]+used,next,bytes);out->lines[row][used+bytes]=0;
            if(width(out->lines[row],custom,px)>BANNER_TEXT_WIDTH){out->lines[row][used]=0;break;}
            used+=bytes;next+=bytes;
        }
        if(*next=='\n'){++next;if(row==UI_BANNER_QUOTE_LINES-1){out->overflow=true;break;}out->count=row+2;}
    }
    out->overflow|=*next!=0;return !out->overflow;
}
static int quote_top(const ui_banner_quote_t *q,bool custom,ui_banner_positions_t p){
    int height=0;
    for(int i=0;i<q->count;++i)height+=ink_height(p.quote_px,q->lines[i],custom)+(i?UI_BANNER_GAP:0);
    if(p.free_y){
        int maximum=UI_BANNER_BOTTOM-height,minimum=UI_STATUS_HEIGHT+2;
        if(maximum<minimum)return minimum;
        return p.quote_top<minimum?minimum:p.quote_top>maximum?maximum:p.quote_top;
    }
    int spare=UI_BANNER_BOTTOM-p.quote_top-height;
    if(spare<0)spare=0;
    return p.quote_top+(p.quote_valign==CONTENT_QUOTE_VBOTTOM?spare:p.quote_valign==CONTENT_QUOTE_VCENTER?spare/2:0);
}
static bool fits_active(const char *text,bool custom,ui_banner_positions_t p){
    ui_banner_quote_t q;if(!quote_layout_size(text,custom,p.quote_px,&q))return false;
    int y=quote_top(&q,custom,p);
    for(int i=0;i<q.count;++i){int h=ink_height(p.quote_px,q.lines[i],custom);
        if(y+h>UI_BANNER_BOTTOM)return false;
        y+=h+UI_BANNER_GAP;}
    return true;
}
bool ui_banner_quote_fits_size(const char *text,const char *path,int px){
    if(!content_home_quote_size_valid((unsigned)px))return false;
    ui_banner_positions_t p=ui_banner_positions_size(longest_date,px);
    if(!path||!path[0])return fits_active(text,false,p);
    char saved[TTF_FONT_PATH_MAX];strlcpy(saved,ttf_font_path(),sizeof(saved));int weight=ttf_get_weight();
    bool normalized=ttf_font_set_normalized_sizes(false);
    bool custom=ttf_font_open(path)==ESP_OK&&ttf_font_has_text(text),fit=false;
    if(custom)fit=fits_active(text,true,p);
    if(ttf_font_open(saved)!=ESP_OK)ttf_font_open_builtin();
    ttf_set_weight(weight);
    ttf_font_set_normalized_sizes(normalized);
    return custom?fit:fits_active(text,false,p);
}
void ui_banner_quote_draw(uint8_t *fb,const char *text,bool custom,ui_banner_positions_t p){
    ui_banner_quote_t q;quote_layout_size(text,custom,p.quote_px,&q);
    if(q.overflow){char *last=q.lines[UI_BANNER_QUOTE_LINES-1];size_t n=strlen(last);
        while(n&&width(last,custom,p.quote_px)+width("…",custom,p.quote_px)>BANNER_TEXT_WIDTH){do{--n;}while(n&&((unsigned char)last[n]&0xc0)==0x80);last[n]=0;}
        strlcat(last,"…",sizeof(q.lines[UI_BANNER_QUOTE_LINES-1]));if(last[0]&&q.count<UI_BANNER_QUOTE_LINES)q.count=UI_BANNER_QUOTE_LINES;
    }
    int y=quote_top(&q,custom,p);
    for(int row=0;row<q.count;++row){int above=0,below=0;
        const char *measure=q.lines[row][0]?q.lines[row]:"国Ag";
        if(custom)ttf_measure_line_px(p.quote_px,measure,&above,&below);
        else ui_text_fixed_measure_px(p.quote_px,measure,&above,&below);
        if(y+above+below>UI_BANNER_BOTTOM)break;
        enum EpdFontFlags align=p.quote_align==CONTENT_QUOTE_LEFT?EPD_DRAW_ALIGN_LEFT:p.quote_align==CONTENT_QUOTE_RIGHT?EPD_DRAW_ALIGN_RIGHT:EPD_DRAW_ALIGN_CENTER;
        int x=p.quote_align==CONTENT_QUOTE_LEFT?UI_BANNER_QUOTE_INSET:p.quote_align==CONTENT_QUOTE_RIGHT?UI_LOCK_WIDTH-UI_BANNER_QUOTE_INSET:UI_LOCK_WIDTH/2;
        if(p.quote_align==CONTENT_QUOTE_JUSTIFY)ui_text_justify_ink_top(fb,UI_BANNER_QUOTE_INSET,y,p.quote_px,q.lines[row],BANNER_TEXT_WIDTH,custom);
        else if(custom)ttf_draw_text_px(fb,x,y+above,p.quote_px,q.lines[row],align,0,15);
        else ui_text_fixed_ink_top(fb,x,y,p.quote_px,q.lines[row],align);
        y+=above+below+UI_BANNER_GAP;
    }
}

ui_banner_positions_t ui_banner_positions(const char *date){return ui_banner_positions_size(date,content_home_quote_size());}
bool ui_banner_quote_layout(const char *text,bool custom,ui_banner_quote_t *out){return quote_layout_size(text,custom,content_home_quote_size(),out);}
bool ui_banner_quote_fits_font(const char *text,const char *path){return ui_banner_quote_fits_size(text,path,content_home_quote_size());}
void ui_banner_date_line(uint32_t day,bool valid,char *line,size_t cap){
    static const char *const weekdays[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
    struct tm date;char lunar[80];time_t seconds=(time_t)((uint64_t)day*86400);
    strlcpy(line,ui_i18n_english()?"Set date & time":"请设置日期与时间",cap);
    if(valid&&gmtime_r(&seconds,&date)){
        if(ui_i18n_english()){
            static const char *const months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            static const char *const days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
            snprintf(line,cap,"%s %d, %d · %s",months[date.tm_mon],date.tm_mday,date.tm_year+1900,days[date.tm_wday]);return;
        }
        bool have=ui_lunar_label((int32_t)day,lunar,sizeof(lunar));
        snprintf(line,cap,"%d年%d月%d日 · %s%s%s",date.tm_year+1900,date.tm_mon+1,date.tm_mday,weekdays[date.tm_wday],have?" · ":"",have?lunar:"");}
}
uint16_t ui_banner_default_date_y(int quote_px){
    ui_banner_positions_t p=ui_banner_positions_size(longest_date,quote_px);
    int y=p.date_top-UI_STATUS_HEIGHT;return (uint16_t)(y<4?4:y>CONTENT_BANNER_DATE_MAX?CONTENT_BANNER_DATE_MAX:y);
}
static void banner_date_draw(uint8_t *page,const char *date,ui_banner_positions_t p,bool outline){
    if(!outline){ui_text_fixed_ink_top(page,342,p.date_top,p.date_px,date,EPD_DRAW_ALIGN_CENTER);return;}
    int a=0,b=0;ui_text_fixed_measure_px(p.date_px,date,&a,&b);
    int center=p.date_top+(a+b)/2;
    for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy)if(dx*dx+dy*dy<=4)
        ui_text_fixed_vc_gray(page,342+dx,center+dy,p.date_px,date,EPD_DRAW_ALIGN_CENTER,UI_INK_WHITE,UI_INK_WHITE);
    ui_text_fixed_vc_gray(page,342,center,p.date_px,date,EPD_DRAW_ALIGN_CENTER,UI_INK_BLACK,UI_INK_WHITE);
}
// 先从白底文字层生成描边，再叠到日期与背景之上；复用输出缓存避免额外整屏分配。
// Outline a white text layer, then composite above date/background, reusing the output cache.
static int layer_gray(const uint8_t *layer,int x,int y){return (layer[y*342+x/2]>>(x%2?4:0))&15;}
static void banner_text_overlay(uint8_t *page,const uint8_t *layer){
    for(int y=0;y<300;++y)for(int x=0;x<684;){
        if(layer_gray(layer,x,y)==15){++x;continue;}
        int start=x;while(x<684&&layer_gray(layer,x,y)<15)++x;
        int left=start<2?0:start-2,right=x+2>684?684:x+2,top=y<2?0:y-2,bottom=y+3>300?300:y+3;
        epd_fill_rect((EpdRect){left,UI_STATUS_HEIGHT+top,right-left,bottom-top},UI_GRAY_WHITE,page);
    }
    for(int y=0;y<300;++y)for(int x=0;x<684;++x){int shade=layer_gray(layer,x,y);
        if(shade<15)epd_draw_pixel(x,UI_STATUS_HEIGHT+y,(uint8_t)(shade*17),page);
    }
}
uint16_t ui_banner_text_y(const content_banner_config_t *cfg){
    ui_banner_positions_t p=ui_banner_positions_size(longest_date,cfg->quote_px);p.quote_valign=cfg->valign;
    if(cfg->text_y!=CONTENT_BANNER_TEXT_AUTO){p.quote_top=UI_STATUS_HEIGHT+cfg->text_y;p.free_y=true;}
    char saved[TTF_FONT_PATH_MAX];strlcpy(saved,ttf_font_path(),sizeof(saved));int weight=ttf_get_weight();
    bool normalized=cfg->font[0]?ttf_font_set_normalized_sizes(false):false;
    bool custom=cfg->font[0]&&ttf_font_open(cfg->font)==ESP_OK&&ttf_font_has_text(cfg->text);
    ui_banner_quote_t q;quote_layout_size(cfg->text,custom,cfg->quote_px,&q);int y=quote_top(&q,custom,p);
    if(cfg->font[0]){if(ttf_font_open(saved)!=ESP_OK)ttf_font_open_builtin();ttf_set_weight(weight);ttf_font_set_normalized_sizes(normalized);}
    return (uint16_t)(y-UI_STATUS_HEIGHT);
}
static ui_banner_positions_t banner_positions(const content_banner_config_t *cfg, const char *date) {
    ui_banner_positions_t p = ui_banner_positions_size(date, cfg->quote_px);
    p.quote_align = cfg->align;
    p.quote_valign = cfg->valign;
    if (cfg->text_y != CONTENT_BANNER_TEXT_AUTO) {
        p.quote_top = UI_STATUS_HEIGHT + cfg->text_y;
        p.free_y = true;
    }
    if (cfg->date_y != CONTENT_BANNER_DATE_AUTO) p.date_top = UI_STATUS_HEIGHT + cfg->date_y;
    return p;
}

static bool banner_draw_quote_layer(uint8_t *page, const content_banner_config_t *cfg,
                                   ui_banner_positions_t p, bool *fits) {
    bool custom = false, restored = true;
    if (cfg->font[0]) {
        char saved[TTF_FONT_PATH_MAX];
        strlcpy(saved, ttf_font_path(), sizeof(saved));
        int weight = ttf_get_weight();
        bool normalized = ttf_font_set_normalized_sizes(false);
        custom = ttf_font_open(cfg->font) == ESP_OK && ttf_font_has_text(cfg->text);
        if (custom) {
            ui_banner_quote_draw(page, cfg->text, true, p);
            *fits = fits_active(cfg->text, true, p);
        }
        restored = ttf_font_open(saved) == ESP_OK;
        if (!restored) ttf_font_open_builtin();
        ttf_set_weight(weight);
        ttf_font_set_normalized_sizes(normalized);
    }
    if (!custom) {
        ui_banner_quote_draw(page, cfg->text, false, p);
        *fits = fits_active(cfg->text, false, p);
    }
    return restored && (custom || !cfg->font[0]);
}

#ifdef IFINK_BANNER_LAYER_TEST
enum {
    BANNER_HEIGHT = 300,
    BANNER_BITMAP_BYTES = UI_LOCK_WIDTH * BANNER_HEIGHT / 2,
    BANNER_MASK_BYTES = UI_LOCK_WIDTH * BANNER_HEIGHT / 8,
    BANNER_PAGE_BYTES = UI_LOCK_WIDTH * UI_LOCK_HEIGHT / 2,
    BANNER_BACKGROUND = 1,
    BANNER_TEXT = 2,
    BANNER_IMAGE_HASH_LIMIT = 2 * 1024 * 1024,
    BANNER_FONT_HASH_LIMIT = 16 * 1024 * 1024,
};
static const EpdRect banner_area = {0, UI_STATUS_HEIGHT, UI_LOCK_WIDTH, BANNER_HEIGHT};
static const char banner_background_cache[] = "/sdcard/.readpico/banner-background-v1.bin";
static const char banner_text_cache[] = "/sdcard/.readpico/banner-text-v1.bin";

typedef struct {
    char key[65];
    uint32_t crc;
} banner_cache_header_t;

static bool banner_builtin_covers_quote(const char *text) {
    char glyphs[CONTENT_HOME_QUOTE_BYTES];
    size_t count = 0;
    for (; *text; ++text) {
        if (*text != '\n') glyphs[count++] = *text;
    }
    glyphs[count] = 0;
    return ui_font_has_text(glyphs) && ui_font_has_text("国Ag…");
}

static bool banner_layer_key(int kind, const content_banner_config_t *cfg,
                             ui_banner_positions_t p, char key[65]) {
    SHELF_PERF_SCOPE(perf, SP_BANNER_LAYER_KEY, kind, 0);
    struct {
        uint8_t build[32];
        char source_hash[65], text[sizeof(cfg->text)];
        int32_t kind, align, valign, top, px, weight, language;
        uint8_t free_y, custom_font, system_font;
    } input;
    memset(&input, 0, sizeof(input));
#ifdef ESP_PLATFORM
    memcpy(input.build, esp_app_get_description()->app_elf_sha256, sizeof(input.build));
#endif
    input.kind = kind;
    const char *source = cfg->image;
    if (kind == BANNER_TEXT) {
        strlcpy(input.text, cfg->text, sizeof(input.text));
        input.align = p.quote_align;
        input.valign = p.quote_valign;
        input.top = p.quote_top;
        input.px = p.quote_px;
        input.free_y = p.free_y;
        input.weight = ttf_get_weight();
        input.language = app_settings_language();
        input.custom_font = cfg->font[0] != 0;
        input.system_font = app_settings_system_font_path()[0] && !ttf_font_is_builtin();
        // 只核验参与格言绘制的外部字体。/ Verify only the external face used by the quote.
        source = cfg->font;
        if (!input.custom_font && !ttf_font_is_builtin() &&
            (input.system_font || !banner_builtin_covers_quote(cfg->text))) {
            source = ttf_font_path();
        }
    }
    // 体积过大的资源继续正常渲染，避免为了验证缓存多读大文件。
    // Large assets remain renderable without an extra read just to validate a cache.
    off_t limit = kind == BANNER_BACKGROUND ? BANNER_IMAGE_HASH_LIMIT : BANNER_FONT_HASH_LIMIT;
    struct stat st;
    if (source[0] && (stat(source, &st) || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > limit ||
        document_store_hash_file(source, input.source_hash, NULL) != ESP_OK)) return false;
    perf.b = document_store_hash_bytes(&input, sizeof(input), key);
    return perf.b != 0;
}

static bool banner_layer_read(int kind, const char *path, const char key[65],
                              uint8_t *layer, size_t size) {
    SHELF_PERF_SCOPE(perf, SP_BANNER_LAYER_READ, kind, 0);
    struct stat st;
    banner_cache_header_t header;
    if (stat(path, &st) || !S_ISREG(st.st_mode) ||
        st.st_size != (off_t)(sizeof(header) + size)) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fread(&header, sizeof(header), 1, file) == 1 &&
              !memcmp(header.key, key, sizeof(header.key)) &&
              fread(layer, 1, size, file) == size && fgetc(file) == EOF && !ferror(file) &&
              header.crc == crc32(0, layer, size);
    fclose(file);
    perf.b = ok;
    return ok;
}

static void banner_layer_write(int kind, const char *path, const char key[65],
                               const uint8_t *layer, size_t size) {
    SHELF_PERF_SCOPE(perf, SP_BANNER_LAYER_WRITE, kind, 0);
    char temp[sizeof(banner_background_cache) + sizeof(".tmp")];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) return;
    FILE *file = fopen(temp, "wb");
    if (!file) return;
    banner_cache_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.key, key, sizeof(header.key));
    header.crc = crc32(0, layer, size);
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
              fwrite(layer, 1, size, file) == size && !fflush(file) && !fsync(fileno(file));
    if (fclose(file)) ok = false;
    // FAT 可能拒绝覆盖，只有派生缓存允许删除重建。/ FAT may refuse replacement. Only the derived cache may be removed.
    if (ok && (rename(temp, path) == 0 ||
        (errno == EEXIST && unlink(path) == 0 && rename(temp, path) == 0))) {
        perf.b = 1;
        return;
    }
    unlink(temp);
}

static void banner_text_mask(const uint8_t *glyph, uint8_t *mask) {
    memset(mask, 0, BANNER_MASK_BYTES);
    // 掩码包含原有两像素描边，覆盖处保留文字或纯白。/ The mask includes the original two-pixel white outline.
    for (int y = 0; y < BANNER_HEIGHT; ++y) {
        for (int x = 0; x < UI_LOCK_WIDTH;) {
            if (layer_gray(glyph, x, y) == 15) {
                ++x;
                continue;
            }
            int start = x;
            while (x < UI_LOCK_WIDTH && layer_gray(glyph, x, y) < 15) ++x;
            int left = start < 2 ? 0 : start - 2;
            int right = x + 2 > UI_LOCK_WIDTH ? UI_LOCK_WIDTH : x + 2;
            for (int row = y - 2; row <= y + 2; ++row) {
                if (row < 0 || row >= BANNER_HEIGHT) continue;
                for (int col = left; col < right; ++col) {
                    size_t pixel = (size_t)row * UI_LOCK_WIDTH + col;
                    mask[pixel / 8] |= 1u << (pixel % 8);
                }
            }
        }
    }
}

static bool banner_background_layer(uint8_t *page, uint8_t *image,
                                    const content_banner_config_t *cfg,
                                    ui_banner_positions_t p, int *hit_bits) {
    if (!cfg->mode) {
        memset(image, 255, BANNER_BITMAP_BYTES);
        return true;
    }
    char key[65];
    bool keyed = banner_layer_key(BANNER_BACKGROUND, cfg, p, key);
    if (keyed && banner_layer_read(BANNER_BACKGROUND, banner_background_cache,
                                  key, image, BANNER_BITMAP_BYTES)) {
        *hit_bits |= BANNER_BACKGROUND;
        return true;
    }
    {
        SHELF_PERF_SCOPE(perf, SP_BANNER_LAYER_RENDER, BANNER_BACKGROUND, 0);
        memset(page, 255, BANNER_PAGE_BYTES);
        if (!ui_wallpaper_draw(page, cfg->image, banner_area)) return false;
        ui_bitmap_capture(page, banner_area, image);
        perf.b = 1;
    }
    if (keyed) banner_layer_write(BANNER_BACKGROUND, banner_background_cache, key, image, BANNER_BITMAP_BYTES);
    return true;
}

static void banner_quote_layer(uint8_t *page, uint8_t *glyph, const content_banner_config_t *cfg,
                               ui_banner_positions_t p, int *hit_bits) {
    size_t size = BANNER_BITMAP_BYTES + BANNER_MASK_BYTES;
    char key[65];
    bool keyed = banner_layer_key(BANNER_TEXT, cfg, p, key);
    if (keyed && banner_layer_read(BANNER_TEXT, banner_text_cache, key, glyph, size)) {
        *hit_bits |= BANNER_TEXT;
        return;
    }
    bool fits;
    bool rendered;
    {
        SHELF_PERF_SCOPE(perf, SP_BANNER_LAYER_RENDER, BANNER_TEXT, 0);
        memset(page, 255, BANNER_PAGE_BYTES);
        rendered = banner_draw_quote_layer(page, cfg, p, &fits);
        ui_bitmap_capture(page, banner_area, glyph);
        banner_text_mask(glyph, glyph + BANNER_BITMAP_BYTES);
        perf.b = rendered;
    }
    if (keyed && rendered) banner_layer_write(BANNER_TEXT, banner_text_cache, key, glyph, size);
}

static void banner_compose(uint8_t *page, uint8_t *image, const uint8_t *glyph,
                           const content_banner_config_t *cfg, const char *date,
                           ui_banner_positions_t p) {
    SHELF_PERF_SCOPE(perf, SP_BANNER_LAYER_COMPOSE, cfg->show_date, cfg->show_text);
    if (cfg->show_date) {
        ui_bitmap_draw(page, banner_area, image);
        banner_date_draw(page, date, p, true);
        ui_bitmap_capture(page, banner_area, image);
    }
    if (!glyph) return;
    const uint8_t *mask = glyph + BANNER_BITMAP_BYTES;
    for (size_t i = 0; i < BANNER_MASK_BYTES; ++i) {
        unsigned coverage = mask[i];
        if (!coverage) continue;
        for (size_t byte = 4 * i; coverage; ++byte, coverage >>= 2) {
            unsigned nibbles = (coverage & 1 ? 0x0f : 0) | (coverage & 2 ? 0xf0 : 0);
            image[byte] = (image[byte] & ~nibbles) | (glyph[byte] & nibbles);
        }
    }
}

bool ui_banner_home_layers(const content_banner_config_t *cfg, uint32_t day, bool valid,
                           uint8_t **out, int *hit_bits) {
    *out = NULL;
    *hit_bits = 0;
    read_pico_sd_info_t sd = {0};
    if (!content_banner_valid(cfg) || cfg->version != 3 ||
        read_pico_sd_get_info(&sd) != ESP_OK || !sd.mounted) return false;
    const int caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    uint8_t *page = heap_caps_malloc(BANNER_PAGE_BYTES, caps);
    uint8_t *image = heap_caps_malloc(BANNER_BITMAP_BYTES, caps);
    uint8_t *glyph = cfg->show_text ? heap_caps_malloc(BANNER_BITMAP_BYTES + BANNER_MASK_BYTES, caps) : NULL;
    bool ok = page && image && (!cfg->show_text || glyph);
    if (ok) {
        char date[160];
        ui_banner_date_line(day, valid, date, sizeof(date));
        ui_banner_positions_t p = banner_positions(cfg, date);
        ok = banner_background_layer(page, image, cfg, p, hit_bits);
        if (ok) {
            if (glyph) banner_quote_layer(page, glyph, cfg, p, hit_bits);
            banner_compose(page, image, glyph, cfg, date, p);
        }
    }
    free(page);
    free(glyph);
    if (!ok) {
        free(image);
        return false;
    }
    *out = image;
    return true;
}
#endif

bool ui_banner_preview_config(const content_banner_config_t *cfg,uint32_t day,bool valid,uint8_t **out,bool *fits){
    *out=NULL;*fits=false;if(!content_banner_valid(cfg)||cfg->version!=3)return false;
    uint8_t *page=heap_caps_malloc(UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *image=heap_caps_malloc(UI_LOCK_WIDTH*300/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!page||!image){free(page);free(image);return false;}
    memset(page,255,UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
    char date[160];ui_banner_date_line(day,valid,date,sizeof(date));
    ui_banner_positions_t p=banner_positions(cfg,date);
    *fits=true;
    if(cfg->show_text){
        (void)banner_draw_quote_layer(page,cfg,p,fits);
        ui_bitmap_capture(page,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300},image);
    }
    memset(page,255,UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
    if(cfg->mode&&!ui_wallpaper_draw(page,cfg->image,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300})){free(page);free(image);return false;}
    if(cfg->show_date)banner_date_draw(page,date,p,true);
    if(cfg->show_text)banner_text_overlay(page,image);
    ui_bitmap_capture(page,(EpdRect){0,UI_STATUS_HEIGHT,UI_LOCK_WIDTH,300},image);
    free(page);*out=image;return true;
}
bool ui_banner_preview(const char *text,const char *path,int px,content_quote_align_t align,content_quote_valign_t valign,uint32_t day,bool valid,uint8_t **out,bool *fits){
    content_banner_config_t cfg;content_banner_load(&cfg);cfg.mode=0;cfg.show_text=1;cfg.show_date=1;cfg.text_y=CONTENT_BANNER_TEXT_AUTO;
    if(!content_home_quote_valid(text)||!content_home_quote_font_valid(path?path:"")||!content_home_quote_size_valid((unsigned)px)||!content_home_quote_alignment_valid((unsigned)align)||!content_home_quote_valign_valid((unsigned)valign)){*out=NULL;*fits=false;return false;}
    strlcpy(cfg.text,text,sizeof(cfg.text));strlcpy(cfg.font,path?path:"",sizeof(cfg.font));cfg.quote_px=px;cfg.align=align;cfg.valign=valign;
    return ui_banner_preview_config(&cfg,day,valid,out,fits);
}
