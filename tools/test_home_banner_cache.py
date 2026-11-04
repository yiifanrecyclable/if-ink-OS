#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise production Banner reuse and invalidation with fault-injected render/filesystem dependencies."""
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1]
src=(R/'main/apps/app_dashboard.c').read_text()
block=src[src.index('static uint8_t *s_quote_bitmap;'):src.index('\nstatic void draw_home_banner')]
unit=r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "content_library.h"
#include "shelf_perf.h"
#define TTF_FONT_PATH_MAX 288
#define ui_snprintf snprintf
#define ESP_OK 0
typedef struct{int unused;}app_ctx_t;
static uint32_t s_reading_end_day=20760,epoch=8;
static bool s_reading_days_valid=true,builtin=true;
static int weight=3,language,renders,failures;
static char active[288]="builtin",system_font[288]="";
static content_banner_config_t config;
static bool missing;
static unsigned file_version;
static unsigned char frame_value=23;
static uint32_t book_index_cache_epoch(void){return epoch;}
static int ttf_get_weight(void){return weight;}
static bool ttf_font_is_builtin(void){return builtin;}
static const char *ttf_font_path(void){return active;}
static int app_settings_language(void){return language;}
static const char *app_settings_system_font_path(void){return system_font;}
void content_banner_load(content_banner_config_t *out){*out=config;}
static int fake_stat(const char *p,struct stat *st){assert(*p);if(missing)return -1;memset(st,0,sizeof(*st));st->st_mode=S_IFREG;st->st_size=111+file_version;st->st_mtime=222+file_version;return 0;}
#define stat(p,s) fake_stat(p,s)
static bool ui_banner_preview_config(const content_banner_config_t *cfg,uint32_t day,bool valid,uint8_t **out,bool *fits){
 (void)cfg;(void)day;(void)valid;++renders;*out=NULL;*fits=true;
 if(failures){--failures;return false;}
 *out=malloc(684*300/2);assert(*out);memset(*out,frame_value,684*300/2);return true;
}
'''+block+r'''
static void rebuild(void){int before=renders;prepare_quote_bitmap();assert(renders==before+1&&s_quote_key_valid);}
static void reuse(void){int before=renders;uint8_t *p=s_quote_bitmap;prepare_quote_bitmap();assert(renders==before&&p==s_quote_bitmap&&s_quote_key_valid);for(unsigned i=0;i<684*300/2;++i)assert(p[i]==frame_value);}
int main(void){
 memset(&config,0,sizeof(config));config.version=3;config.show_date=config.show_text=1;config.quote_px=96;strcpy(config.text,"quote");
 rebuild();reuse();home_exit(NULL);reuse(); /* Keep only the final, immutable 102600-byte bitmap. */
 ++s_reading_end_day;rebuild();reuse();s_reading_days_valid=false;rebuild();
 ++language;rebuild();++weight;rebuild();
 strcpy(system_font,"/sdcard/fonts/system.ttf");rebuild();
 strcpy(active,system_font);builtin=false;rebuild();reuse();
 const unsigned char *bytes=(const unsigned char*)&config;
 (void)bytes;
 config.date_y=70;rebuild();config.text_y=120;rebuild();config.align=1;rebuild();
 config.quote_px=108;rebuild();strcpy(config.text,"new text\nline two");rebuild();
 config.show_date=0;rebuild();config.show_text=0;rebuild();
 config.mode=1;strcpy(config.image,"/sdcard/banner pic/test.png");rebuild();reuse();
 ++file_version;rebuild();reuse();
 ++epoch;rebuild();reuse(); /* Same size/mtime changes retired by media/commit epoch. */
 config.show_text=1;strcpy(config.font,"/sdcard/fonts/quote.ttf");rebuild();reuse();
 missing=true;int n=renders;prepare_quote_bitmap();assert(renders==n+1&&!s_quote_key_valid);prepare_quote_bitmap();assert(renders==n+2&&!s_quote_key_valid);
 missing=false;rebuild();reuse();
 ++epoch;failures=1;n=renders;prepare_quote_bitmap();assert(renders==n+2&&s_quote_bitmap&&!s_quote_key_valid);rebuild();reuse();
 ++epoch;failures=2;n=renders;prepare_quote_bitmap();assert(renders==n+2&&!s_quote_bitmap&&!s_quote_key_valid);rebuild();reuse();
 clear_quote_bitmap();assert(!s_quote_bitmap&&!s_quote_key_valid);rebuild();reuse();
 clear_quote_bitmap();puts("PASS production Banner cache: tab reuse, exact pixels, config/day/language/font/file/epoch changes, missing media, render/allocation fallback and cleanup");
}
'''
# Media callback must discard the image before rebuilding even if metadata matches.
assert '(void)ctx;clear_quote_bitmap();prepare_quote_bitmap();' in src
assert '(void)ctx;clear_quote_bitmap();' in src.split('static void on_media_lost',1)[1]
with tempfile.TemporaryDirectory(prefix='home-banner-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'main/book'),'-I'+str(R/'main'),'-I'+str(R/'tools/html_text_stubs'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
