/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：验证真实水波纹组件的 16 带、37 相、四向映射和失败后旧帧基准。
 * English: Exercise the real water-turn engine's 16 bands, 37 phases, four directions and failure baseline.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "e0470_page_turn.h"
#include "e0470_epaper_waveform.h"

#define WIDTH 1216
#define HEIGHT 684
#define FB_BYTES ((WIDTH * HEIGHT) / 2)
static uint8_t front[FB_BYTES], back[FB_BYTES], diff[WIDTH * HEIGHT];
static bool dirty_lines[HEIGHT];
static uint8_t dirty_columns[WIDTH / 2];
static const EpdWaveformPhases gl = {.phases = 37, .luts = (const uint8_t*)"x"};
const EpdWaveform E0470_WAVEFORM = {.id = 1};
const EpdWaveform E0470_APPLY_WAVEFORM = {.id = 2};
static enum EpdRotation rotation;
static e0470_turn_dir_t direction;
static bool check_direction = true;
static const uint8_t* const* staged_luts;
static const int8_t* staged_lines;
static const int *staged_x0, *staged_x1;
static const int8_t* staged_bands;
static int staged_count;
static int scans, fail_at, differences, powerons;
static bool region_test, nav_test, nav_changed;
static EpdRect expected_art;
static const EpdWaveform soft_wave={.id=3},art_wave={.id=4};
static const EpdWaveformPhases soft_phases={.phases=37,.luts=(const uint8_t*)"s"},art_phases={.phases=37,.luts=(const uint8_t*)"a"};
static int64_t now_us;
int water_test_alloc_fail;

int epd_width(void) { return WIDTH; }
int epd_height(void) { return HEIGHT; }
enum EpdRotation epd_get_rotation(void) { return rotation; }
EpdRect epd_full_screen(void) { return (EpdRect){0, 0, WIDTH, HEIGHT}; }
void epd_poweron(void) { ++powerons; }
int64_t esp_timer_get_time(void) { return now_us; }
void esp_rom_delay_us(uint32_t us) { now_us += us; }
const EpdWaveformPhases* e0470_waveform_phases(const EpdWaveform* waveform, int mode) {
    assert(mode == MODE_GL16);
    if(waveform==&soft_wave)return &soft_phases;
    if(waveform==&art_wave)return &art_phases;
    assert(waveform==&E0470_WAVEFORM);return &gl;
}
void epd_build_1ppB_lut_1k(uint8_t* lut, const EpdWaveformPhases* phases, int frame) {
    assert((phases==&gl||phases==&soft_phases||phases==&art_phases)&&frame>=0&&frame<37);
    memset(lut,frame+(phases==&soft_phases?100:phases==&art_phases?150:0),1024);
}
EpdRect epd_difference_image_cropped(const uint8_t* to, const uint8_t* from, EpdRect area,
                                      uint8_t* difference, bool* lines, uint8_t* columns) {
    assert(to == front && from == back && difference == diff && lines == dirty_lines && columns == dirty_columns);
    assert(area.x >= 0 && area.y >= 0 && area.x + area.width <= WIDTH &&
           area.y + area.height <= HEIGHT && area.width > 0 && area.height > 0);
    ++differences;
    return area;
}
void epd_clear_phase_luts(void) {
    staged_luts = NULL;
    staged_lines = NULL;
    staged_x0 = staged_x1 = NULL;
    staged_bands = NULL;
    staged_count = 0;
}
void epd_set_line_phase_luts(const uint8_t* const* luts, const int8_t* lines) {
    epd_clear_phase_luts();
    staged_luts = luts;
    staged_lines = lines;
}
void epd_set_col_phase_luts(const uint8_t* const* luts, const int* x0, const int* x1,
                            const int8_t* phases, int count) {
    epd_clear_phase_luts();
    staged_luts = luts;
    staged_x0 = x0;
    staged_x1 = x1;
    staged_bands = phases;
    staged_count = count;
}
static void physical_to_logical(int px, int py, int* lx, int* ly) {
    switch (rotation) {
        case EPD_ROT_LANDSCAPE: *lx = px; *ly = py; break;
        case EPD_ROT_PORTRAIT: *lx = py; *ly = WIDTH - px - 1; break;
        case EPD_ROT_INVERTED_LANDSCAPE: *lx = WIDTH - px - 1; *ly = HEIGHT - py - 1; break;
        case EPD_ROT_INVERTED_PORTRAIT: *lx = HEIGHT - py - 1; *ly = px; break;
    }
}
enum EpdDrawError epd_draw_base(EpdRect area, const uint8_t* data, EpdRect crop,
                                 enum EpdDrawMode mode, int temperature, const bool* lines,
                                 const uint8_t* columns, const EpdWaveform* waveform) {
    assert(area.width == WIDTH && crop.height == HEIGHT && data == diff);
    assert(mode == (MODE_PACKING_1PPB_DIFFERENCE | MODE_DU) && temperature == 25);
    assert(lines == dirty_lines && columns == dirty_columns && waveform == &E0470_APPLY_WAVEFORM);
    assert(staged_luts && (staged_lines != NULL) != (staged_bands != NULL));
    int tick = scans++;
    if(nav_test){
        assert(staged_count>=1&&staged_count<=5);
        int next=0;
        for(int b=0;b<staged_count;++b){
            assert(staged_x0[b]==next&&!(staged_x0[b]&15)&&staged_x1[b]>next&&!(staged_x1[b]&15));
            next=staged_x1[b];
            for(int x=staged_x0[b];x<staged_x1[b];++x){
                int expected=x>=1136?(nav_changed?2:-1):
                    x>=expected_art.y&&x<expected_art.y+expected_art.height?1:0;
                assert(staged_bands[b]==expected);
                if(expected>=0)assert(staged_luts[expected][0]==tick+(expected==0?100:expected==1?150:0));
            }
        }
        assert(next==1216);
        for(int y=0;y<HEIGHT;++y)assert(lines[y]);
        return fail_at==scans?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;
    }
    if(region_test){
        assert(staged_count==3&&staged_bands[0]==0&&staged_bands[1]==1&&staged_bands[2]==0);
        assert(staged_x0[0]==0&&staged_x1[2]==1216&&staged_x0[1]==128&&staged_x1[1]==1056);
        assert(staged_luts[0][0]==tick&&staged_luts[1][0]==tick);
        for(int y=0;y<HEIGHT;++y)assert(lines[y]);
        return fail_at==scans?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;
    }
    int active = 0;
    bool phase_seen[37] = {0};
    int px = -1, py = -1;
    if (staged_lines) {
        for (int y = 0; y < HEIGHT; ++y) {
            int p = staged_lines[y];
            if (p < 0) continue;
            assert(p < 37 && staged_luts[p][0] == p);
            if (!phase_seen[p]) { phase_seen[p] = true; ++active; }
            if (px < 0) { px = WIDTH / 2; py = y; }
        }
    } else {
        assert(staged_count == 16);
        for (int b = 0; b < staged_count; ++b) {
            int p = staged_bands[b];
            if (p < 0) continue;
            assert(p < 37 && staged_luts[p][0] == p);
            assert(staged_x0[b] >= 0 && staged_x1[b] <= WIDTH && staged_x1[b] > staged_x0[b]);
            if (!phase_seen[p]) { phase_seen[p] = true; ++active; }
            if (px < 0) { px = (staged_x0[b] + staged_x1[b]) / 2; py = HEIGHT / 2; }
        }
    }
    int expected = 0;
    for (int b = 0; b < 16; ++b) if (tick - b >= 0 && tick - b < 37) ++expected;
    assert(active == expected);
    if (tick == 0 && check_direction) {
        int lx, ly;
        physical_to_logical(px, py, &lx, &ly);
        int coord = direction <= E0470_TURN_RTL ? lx : ly;
        int span = direction <= E0470_TURN_RTL ?
            ((rotation & 1) ? HEIGHT : WIDTH) : ((rotation & 1) ? WIDTH : HEIGHT);
        if (direction == E0470_TURN_LTR || direction == E0470_TURN_TTB) assert(coord < span / 4);
        else assert(coord > span * 3 / 4);
    }
    now_us += 7000;
    return fail_at == scans ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
}

static void paint_nav(uint8_t *fb,int active){
    for(int y=1136;y<1216;++y)for(int x=0;x<684;++x){
        int px=y,py=683-x,gray=x/171==active?15:8;
        size_t offset=((size_t)py*WIDTH+px)/2;
        int shift=(px&1)?4:0;
        fb[offset]=(uint8_t)((fb[offset]&~(15u<<shift))|(gray<<shift));
    }
}

int main(void) {
    EpdiyHighlevelState hl = {front, back, diff, dirty_lines, dirty_columns};
    for (int rot = 0; rot < 4; ++rot) for (int dir = 0; dir < 4; ++dir) {
        rotation = (enum EpdRotation)rot;
        direction = (e0470_turn_dir_t)dir;
        memset(front, 0x24, sizeof(front));
        memset(back, 0xFF, sizeof(back));
        scans = differences = powerons = fail_at = 0;
        now_us = 0;
        EpdRect logical = (rot & 1) ? (EpdRect){0, 0, HEIGHT, WIDTH} :
                                      (EpdRect){0, 0, WIDTH, HEIGHT};
        assert(e0470_page_turn(&hl, logical, direction) == EPD_DRAW_SUCCESS);
        assert(scans == 52 && differences == 1 && powerons == 1);
        assert(!memcmp(back, front, sizeof(back)) && now_us >= 1092000);
        // 真实阅读页保留页眉和底栏；裁剪区同样必须覆盖全部 16 带。
        // The actual reader leaves header and footer outside the crop; all 16 bands must still run.
        EpdRect reader = {0, 160, logical.width, logical.height - 288};
        memset(back, 0xFF, sizeof(back));
        scans = differences = 0;
        check_direction = false;
        assert(e0470_page_turn(&hl, reader, direction) == EPD_DRAW_SUCCESS);
        assert(scans == 52 && differences == 1);
        // 全屏阅读保留状态栏时从第 80 行开始，水波纹仍要覆盖整个正文。
        // Full-screen reading keeps the status row at y=80 while animating the remaining body.
        EpdRect fullscreen_reader = {0, 80, logical.width, logical.height - 80};
        memset(back, 0xFF, sizeof(back));
        scans = differences = 0;
        assert(e0470_page_turn(&hl, fullscreen_reader, direction) == EPD_DRAW_SUCCESS);
        assert(scans == 52 && differences == 1);
        check_direction = true;
        memset(back, 0xFF, sizeof(back));
        scans = differences = 0;
        fail_at = 13;
        assert(e0470_page_turn(&hl, logical, direction) == EPD_DRAW_OTHER_ERROR);
        assert(scans == 13 && differences == 1);
        for (size_t i = 0; i < sizeof(back); ++i) assert(back[i] == 0xFF);
    }
    region_test=true;rotation=EPD_ROT_INVERTED_PORTRAIT;
    for(int fail=0;fail<2;++fail){
        scans=differences=0;fail_at=fail?13:0;memset(back,0xff,sizeof(back));
        assert(e0470_update_regions(&hl,(EpdRect){0,0,684,1216},(EpdRect){0,128,684,928},&E0470_WAVEFORM,&E0470_WAVEFORM,MODE_GL16)==(fail?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS));
        assert(scans==(fail?13:37)&&differences==1);
        if(fail){for(size_t i=0;i<sizeof(back);++i)assert(back[i]==0xff);}else assert(!memcmp(front,back,sizeof(back)));
    }
    region_test=false;nav_test=true;rotation=EPD_ROT_INVERTED_PORTRAIT;
    EpdRect arts[]={{0,128,684,928},{0,32,684,1088},{0,0,684,1216}};
    // 四主导航全部16种迁移：底栏独立原表、无变化保持，正文/图片同37相。/ All16 root transitions use the original footer table or hold, sharing37 phases with body/art.
    for(int a=0;a<3;++a)for(int from=0;from<4;++from)for(int to=0;to<4;++to){
        expected_art=arts[a];nav_changed=from!=to;
        memset(back,0xff,sizeof(back));memset(front,0x24,sizeof(front));paint_nav(back,from);paint_nav(front,to);
        scans=differences=fail_at=0;
        assert(e0470_update_navigation_regions(&hl,(EpdRect){0,0,684,1216},expected_art,(EpdRect){0,1136,684,80},&soft_wave,&art_wave)==EPD_DRAW_SUCCESS);
        assert(scans==37&&differences==1&&!memcmp(front,back,sizeof(back)));
    }
    expected_art=arts[0];nav_changed=true;
    memset(front,0x24,sizeof(front));memset(back,0xff,sizeof(back));scans=differences=0;fail_at=13;
    assert(e0470_update_navigation_regions(&hl,(EpdRect){0,0,684,1216},expected_art,(EpdRect){0,1136,684,80},&soft_wave,&art_wave)==EPD_DRAW_OTHER_ERROR);
    assert(scans==13&&differences==1);for(size_t i=0;i<sizeof(back);++i)assert(back[i]==0xff);
    scans=differences=fail_at=0;water_test_alloc_fail=1;
    assert(e0470_update_navigation_regions(&hl,(EpdRect){0,0,684,1216},expected_art,(EpdRect){0,1136,684,80},&soft_wave,&art_wave)==EPD_DRAW_NO_PHASES_AVAILABLE);
    assert(!scans&&!differences);water_test_alloc_fail=0;
    EpdRect invalid_nav[]={{0,1135,684,81},{0,1136,684,64},{1,1136,683,80},{0,0,0,0}};
    for(size_t i=0;i<sizeof(invalid_nav)/sizeof(invalid_nav[0]);++i){
        assert(e0470_update_navigation_regions(&hl,(EpdRect){0,0,684,1216},expected_art,invalid_nav[i],&soft_wave,&art_wave)==EPD_DRAW_INVALID_CROP);
        assert(!scans&&!differences);
    }
    for(size_t i=0;i<sizeof(back);++i)assert(back[i]==0xff);
    nav_test=false;
    e0470_page_turn_release();
    memset(back, 0xFF, sizeof(back));
    scans = differences = 0;
    fail_at = 0;
    water_test_alloc_fail = 1;
    assert(e0470_page_turn(&hl, (EpdRect){0, 0, HEIGHT, WIDTH}, E0470_TURN_LTR) ==
           EPD_DRAW_NO_PHASES_AVAILABLE);
    assert(scans == 0 && differences == 0);
    for (size_t i = 0; i < sizeof(back); ++i) assert(back[i] == 0xFF);
    puts("water turn: 4 rotations × 4 directions, 52 ticks, single diff, 37 phases, 48 calibrated navigation cases and failure baseline passed");
}
