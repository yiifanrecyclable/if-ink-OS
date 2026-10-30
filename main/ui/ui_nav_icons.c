/* SPDX-License-Identifier: Apache-2.0
 * 中文：官方Lucide灰阶覆盖图标，与固件和宿主导航共用。
 * English: Official Lucide coverage masks shared by firmware and host navigation.
 */
#include "ui_nav_icons.h"
#include "ui_kit.h"
#include "assets/nav_lucide.h"
void ui_nav_lucide_icon(uint8_t *fb,int tab,int cx,int top,uint8_t foreground,uint8_t background){
    if(tab==2){ui_draw_icon(fb,cx,top+20,40,UI_ICON_LAYOUT_GRID,foreground);return;}
    const uint8_t *icons[]={nav_house,nav_book_text,nav_folder,nav_sliders_horizontal};
    if(!fb||tab<0||tab>3)return;
    for(int y=0;y<40;++y)for(int x=0;x<40;++x){
        int p=y*40+x;uint8_t b=icons[tab][p/2];int alpha=p&1?b&15:b>>4;
        if(alpha)epd_draw_pixel(cx-20+x,top+y,(uint8_t)((background*(15-alpha)+foreground*alpha+7)/15),fb);
    }
}
