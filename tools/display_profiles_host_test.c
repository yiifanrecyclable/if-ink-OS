/* SPDX-License-Identifier: Apache-2.0
 * 中文：使用真实标定表验证派生序列边界、灰阶保留和原表不变。/ Exercise derived profiles against actual vendor data, calibration and immutable originals.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display_profiles.h"
#include "e0470_epaper_waveform.h"
int main(void){
 e0470_waveform_init();
 const EpdWaveformPhases *base=e0470_waveform_phases(&E0470_WAVEFORM,MODE_GL16);
 uint8_t original[E0470_GL16_FRAMES*64];memcpy(original,base->luts,sizeof(original));
 const EpdWaveformPhases *text=e0470_waveform_phases(display_text_waveform(),MODE_GL16);
 const EpdWaveformPhases *soft=e0470_waveform_phases(display_natural_waveform(),MODE_GL16);
 const EpdWaveformPhases *art=e0470_waveform_phases(display_art_waveform(),MODE_GL16);
 assert(text->phases==37&&soft->phases==37&&art->phases==37);
 int changed=0;
 for(int to=0;to<16;++to)for(int from=0;from<16;++from){
  int originals[4]={0},derived[4]={0};
  for(int f=0;f<37;++f){
   int b=e0470_phase_action(base,f,to,from),s=e0470_phase_action(soft,f,to,from),t=e0470_phase_action(text,f,to,from);
   ++originals[b];++derived[t];changed+=t!=b;
   if(to==from){assert(t==0&&s==0);}
   if(to==15&&to!=from)assert(s==0||s==2);
   if(to==0&&to!=from)assert(s==0||s==1);
   if(to!=15)assert(e0470_phase_action(art,f,to,from)==b);
  }
  if(to!=from)assert(derived[1]==originals[1]);
 }
 assert(changed>0&&!memcmp(original,base->luts,sizeof(original)));
 assert(display_text_waveform()==display_text_waveform());
 puts("PASS: real 37-phase profiles, unchanged calibration, held equal grays, directional endpoints, preserved art grays and text black pulse counts");
}
