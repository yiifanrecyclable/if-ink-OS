/* SPDX-License-Identifier: Apache-2.0
 * 中文：适配kiikoread rc97自然分区和文字柔化；保留if-ink导航、16带节奏与强刷策略。
 * English: Adapt kiikoread rc97 natural regions/text softening, retaining if-ink navigation, 16 bands and full refresh.
 * 来源：wegooo-cell/read-pico-reader @2087afda；派生序列与厂商原表分离。
 * Source: wegooo-cell/read-pico-reader @2087afda; derived sequences never mutate vendor tables.
 * 冻结（用户2026-10-20修订）：仅普通系统页及前后均纯文字水波纹采用；不改VCOM、扫描时序或插图强刷。
 * Frozen (user revision 2026-10-20): use only ordinary system pages and text-to-text ripple; retain VCOM, scan timing and image cleanup.
 */
#include "display_profiles.h"
#include "display_waveform_smooth.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

static uint8_t s_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_data, NULL};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {5, 1, s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_modes};
static bool s_ready;

static void set_action(int phase, int to, int action) {
    // from=15 占该字节最低两位；其余三对不变。/ from=15 occupies only the lowest two bits.
    s_data[phase][to][3] = (uint8_t)((s_data[phase][to][3] & ~3u) | (unsigned)action);
}

const EpdWaveform* display_text_waveform(void) {
    if (s_ready) return &s_waveform;
    const EpdWaveformPhases* original = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
    if (!original || !original->luts || original->phases != E0470_GL16_FRAMES || original->phase_times)
        return &E0470_WAVEFORM;
    memset(s_data,0,sizeof(s_data));
    for(int to=0;to<16;++to)for(int from=0;from<16;++from){
        int first=0;while(first<original->phases&&!e0470_phase_action(original,first,to,from))++first;
        if(to==from)continue;
        int erase_end=first;
        if(to<15)while(erase_end<original->phases&&e0470_phase_action(original,erase_end,to,from)==2)++erase_end;
        for(int f=first;f<original->phases;++f){
            unsigned action=f<erase_end?0:e0470_phase_action(original,f,to,from);
            s_data[f-first][to][from/4]|=(uint8_t)(action<<(6-2*(from%4)));
        }
    }
    uint8_t aligned_data[sizeof(s_data)];memcpy(aligned_data,s_data,sizeof(s_data));
    EpdWaveformPhases aligned={E0470_GL16_FRAMES,(const uint8_t*)aligned_data,NULL};
    original=&aligned;
    for (int to = 1; to < 15; ++to) {
        int first_white = original->phases, last_black = -1;
        for (int f = 0; f < original->phases; ++f) {
            int action = e0470_phase_action(original, f, to, 15);
            if (action == 2 && first_white == original->phases) first_white = f;
            if (action == 1) last_black = f;
        }
        int prefix_black = 0, tail_white = 0;
        for (int f = 0; f < first_white; ++f)
            prefix_black += e0470_phase_action(original, f, to, 15) == 1;
        for (int f = last_black + 1; f < original->phases; ++f)
            tail_white += e0470_phase_action(original, f, to, 15) == 2;
        int moves = tail_white >= 4 && prefix_black >= 6 ? 2 :
                    tail_white >= 2 && prefix_black >= 4 ? 1 : 0;
        if (!moves) continue;
        // 交换动作而非删相；保留起点、末尾回浅及所有保持相，不增加第二次推屏。
        // Swap pulses instead of dropping phases; retain the start, final settling and holds.
        int rank = 0, moved = 0, white = last_black + 1;
        int target_rank = prefix_black - (moves == 2 ? 5 : 3);
        for (int f = 0; f < first_white && moved < moves; ++f) {
            if (e0470_phase_action(original, f, to, 15) != 1) continue;
            if (rank++ != target_rank) continue;
            while (white < original->phases && e0470_phase_action(original, white, to, 15) != 2) ++white;
            if (white == original->phases) break;
            set_action(f, to, 2); set_action(white++, to, 1);
            ++moved; target_rank += 2;
        }
    }
    s_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_ready = true;
    return &s_waveform;
}

static uint8_t s_natural[E0470_GL16_FRAMES][16][4],s_art[E0470_GL16_FRAMES][16][4];
static EpdWaveformPhases s_natural_phases={E0470_GL16_FRAMES,(const uint8_t*)s_natural,NULL};
static EpdWaveformPhases s_art_phases={E0470_GL16_FRAMES,(const uint8_t*)s_art,NULL};
static const EpdWaveformPhases *s_natural_ranges[]={&s_natural_phases},*s_art_ranges[]={&s_art_phases};
static const EpdWaveformMode s_natural_mode={MODE_GL16,1,s_natural_ranges},s_art_mode={MODE_GL16,1,s_art_ranges};
static const EpdWaveformMode *s_natural_modes[]={&s_natural_mode},*s_art_modes[]={&s_art_mode};
static EpdWaveform s_natural_wave={1,1,s_natural_modes,NULL},s_art_wave={1,1,s_art_modes,NULL};
static bool s_natural_ready;
static bool natural_init(void){
    if(s_natural_ready)return true;
    const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
    const EpdWaveformPhases *bw=e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU);
    if(!gl||!bw||!gl->luts||!bw->luts||gl->phases!=E0470_GL16_FRAMES||gl->phase_times||bw->phase_times)return false;
    memcpy(s_natural,gl->luts,sizeof(s_natural));memcpy(s_art,gl->luts,sizeof(s_art));
    for(int to=0;to<16;++to)for(int from=0;from<16;++from)for(int f=0;f<E0470_GL16_FRAMES;++f){
        int action=e0470_phase_action(gl,f,to,from);
        if(to==from)action=0;
        else if(to==0||to==15){
            action=f<bw->phases?e0470_phase_action(bw,f,to,from):to==15&&f<bw->phases+4?2:0;
        }
        int shift=6-2*(from%4);uint8_t mask=(uint8_t)~(3u<<shift);
        s_natural[f][to][from/4]=(uint8_t)((s_natural[f][to][from/4]&mask)|(action<<shift));
        // 图片中间灰保持原标定，只让白目标使用单向擦白。/ Keep calibrated image grays; only white targets use directional erasure.
        if(to==15)s_art[f][to][from/4]=(uint8_t)((s_art[f][to][from/4]&mask)|(action<<shift));
    }
    for(int to=1;to<15;++to)for(int from=0;from<16;++from)if(to!=from)display_soften_gray_pair((uint8_t*)s_natural,gl,to,from);
    s_natural_wave.temp_intervals=s_art_wave.temp_intervals=E0470_WAVEFORM.temp_intervals;
    s_natural_ready=true;return true;
}
const EpdWaveform *display_natural_waveform(void){return natural_init()?&s_natural_wave:&E0470_WAVEFORM;}
const EpdWaveform *display_art_waveform(void){return natural_init()?&s_art_wave:&E0470_WAVEFORM;}
