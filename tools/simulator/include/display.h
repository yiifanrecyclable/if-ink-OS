/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epd_highlevel.h"
static inline void display_set_bulk_io(bool b){(void)b;}
static inline int update_display_area_with(void *a,const void *b,int c,EpdRect d){(void)a;(void)b;(void)c;(void)d;return 0;}
static inline int update_display_white(void *a){(void)a;return 0;}
static inline int update_display_with(void *a,const void *b,int c){(void)a;(void)b;(void)c;return 0;}
static inline int update_display_full(void *a){(void)a;return 0;}
static inline int update_display_fast_page(void *a){(void)a;return 0;}
static inline int update_display_image_gray(void *a){(void)a;return 0;}
static inline int update_display_home_clean(void *a){(void)a;return 0;}
static inline int update_display_water_turn(void *a,EpdRect r,int dir){(void)a;(void)r;(void)dir;return 0;}
static inline int update_display_area_diff_with(void *a,const void *b,int c,EpdRect d){(void)a;(void)b;(void)c;(void)d;return 0;}
static inline int update_display_area_full_with(void *a,const void *b,int c,EpdRect d){(void)a;(void)b;(void)c;(void)d;return 0;}
static inline int update_display_mode_diff(void *a,int mode){(void)a;(void)mode;return 0;}
static inline void guard_draw_result(void *a,int b){(void)a;(void)b;}

static inline int update_display_natural_page(void *hl,EpdRect art){(void)hl;(void)art;return 0;}

static inline int update_display_natural_area(void *hl,EpdRect area){(void)hl;(void)area;return 0;}

static inline int update_display_navigation_page(void *hl,EpdRect art,EpdRect nav){(void)hl;(void)art;(void)nav;return 0;}
