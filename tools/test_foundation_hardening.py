#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real cache/draft/reference modules + checked settings, isolated card, failure and fresh-process cases."""
from pathlib import Path
import os,subprocess,tempfile,sys,struct,zlib
R=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "file_references.h"
#include "content_library.h"
#include "book_index_cache.h"
#include "beads_png.h"
#include "read_pico_sd.h"
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *p){memset(p,0,sizeof(*p));p->mounted=true;return ESP_OK;}
static void put(const char *p){FILE *f=fopen(p,"wb");assert(f);assert(fputs("changed",f)>=0);assert(!fclose(f));}
static const char *old="/sdcard/fonts/shared.ttf",*next="/sdcard/fonts/new.ttf";
static void fonts(const char *p){assert(app_settings_set_system_font_path(p)==ESP_OK);assert(app_settings_set_font_path(p)==ESP_OK);}
static void eq(const char *a,const char *b){assert(!strcmp(a,b));}
static void cache_write(const char *path,book_index_cache_header_t *h){char tmp[288];FILE *f=book_index_cache_open_write(path,h,tmp,sizeof(tmp));assert(f);assert(fputs("old index",f)>=0);assert(book_index_cache_finish_write(f,tmp,path,true));}
int main(int argc,char **argv){assert(argc==2);const char *mode=argv[1];
if(!strcmp(mode,"cache-write")){
 put("/sdcard/books/test.txt");char p[288];book_index_cache_header_t h;
 assert(book_index_cache_prepare("/sdcard/books/test.txt","txt",1,p,sizeof(p),&h));cache_write(p,&h);
 FILE *f=book_index_cache_open_read(p,&h);assert(f);fclose(f);
 uint32_t global_epoch=book_index_cache_epoch();
 book_index_cache_invalidate_file(h.source_size,h.modified);
 assert(book_index_cache_epoch()==global_epoch && !book_index_cache_open_read(p,&h));
 unsigned unchanged=0;
 for(unsigned i=1;i<=32;++i)unchanged+=book_index_cache_file_epoch(h.source_size+i,h.modified)==global_epoch;
 assert(unchanged>=24);
 assert(book_index_cache_prepare("/sdcard/books/test.txt","txt",1,p,sizeof(p),&h));cache_write(p,&h);
 book_index_cache_invalidate_all();assert(!book_index_cache_open_read(p,&h));
 assert(book_index_cache_prepare("/sdcard/books/test.txt","txt",1,p,sizeof(p),&h));cache_write(p,&h);return 0;
}
if(!strcmp(mode,"cache-read")){char p[288];book_index_cache_header_t h;assert(book_index_cache_prepare("/sdcard/books/test.txt","txt",1,p,sizeof(p),&h));assert(!book_index_cache_open_read(p,&h));return 0;}
if(!strncmp(mode,"beads",5)){
 cyber_beads_t *b=calloc(1,sizeof(*b));assert(b);cyber_beads_reset(b);
 if(!strcmp(mode,"beads-write")){assert(cyber_beads_place(b,0));assert(beads_board_save(b));}
 else if(!strcmp(mode,"beads-draft")){assert(beads_board_load(b));assert(b->cells[0]==0&&cyber_beads_dirty(b)&&cyber_beads_can_save(b));cyber_beads_saved(b);assert(beads_board_save(b));}
 else if(!strcmp(mode,"beads-exported")){assert(beads_board_load(b));assert(!cyber_beads_dirty(b)&&!cyber_beads_can_save(b));assert(cyber_beads_place(b,1));assert(beads_board_save(b));}
 else if(!strcmp(mode,"beads-modified")){assert(beads_board_load(b));assert(cyber_beads_dirty(b)&&cyber_beads_can_save(b));assert(b->cells[0]==0&&b->cells[1]==0);setenv("PICO_TEST_FSYNC_FAIL","1",1);b->cells[2]=1;assert(!beads_board_save(b));unsetenv("PICO_TEST_FSYNC_FAIL");assert(beads_board_load(b));assert(b->cells[2]==-1);}
 else if(!strcmp(mode,"beads-copy")){assert(beads_board_load(b));assert(beads_board_save(b));}
 else if(!strcmp(mode,"beads-legacy")){assert(beads_board_load(b));assert(b->cells[0]==2&&cyber_beads_can_save(b));assert(beads_board_save(b));}
 free(b);return 0;
}
app_settings_init();bool done=false;
if(!strcmp(mode,"refs-fail")){
 put(old);fonts(old);assert(file_references_begin(old,next,&done)==ESP_OK&&!done);assert(!rename(old,next));
 setenv("PICO_TEST_NVS_SET_FAIL","font",1);assert(file_references_finish()!=ESP_OK);eq(app_settings_system_font_path(),next);eq(app_settings_font_path(),old);
 return 0;
}
if(!strcmp(mode,"refs-retry")){
 eq(app_settings_system_font_path(),next);eq(app_settings_font_path(),old);assert(file_references_begin(old,next,&done)==ESP_OK&&done);eq(app_settings_font_path(),next);
 assert(file_references_finish()==ESP_OK);return 0;
}
if(!strcmp(mode,"refs-corrupt")){
 const char *chosen=app_settings_font_path();char before[288];snprintf(before,sizeof(before),"%s",chosen);
 assert(file_references_begin("/sdcard/fonts/renamed","/sdcard/fonts/another",&done)!=ESP_OK);
 eq(app_settings_font_path(),before);struct stat st;assert(!stat("/sdcard/fonts/renamed/b.ttf",&st));return 0;
}
if(!strcmp(mode,"refs-delete")){
 fonts(next);assert(file_references_begin(next,NULL,&done)==ESP_OK&&!done);assert(!unlink(next));assert(file_references_finish()==ESP_OK);eq(app_settings_system_font_path(),"");eq(app_settings_font_path(),"");
 put(old);fonts(old);assert(file_references_begin(old,next,&done)==ESP_OK&&!done); // rename never happens
 assert(file_references_finish()==ESP_OK);eq(app_settings_font_path(),old);
 setenv("PICO_TEST_FSYNC_FAIL","1",1);assert(file_references_begin(old,next,&done)!=ESP_OK);unsetenv("PICO_TEST_FSYNC_FAIL");struct stat st;assert(!stat(old,&st));
 assert(file_references_begin(old,next,&done)==ESP_OK);assert(!rename(old,next));assert(app_settings_set_font_path("builtin")==ESP_OK);assert(file_references_finish()==ESP_OK);eq(app_settings_font_path(),"builtin");eq(app_settings_system_font_path(),next);
 // a folder move repairs both nested references; a partial delete only clears absent files
 assert(!mkdir("/sdcard/fonts/folder",0775));put("/sdcard/fonts/folder/a.ttf");put("/sdcard/fonts/folder/b.ttf");
 assert(app_settings_set_system_font_path("/sdcard/fonts/folder/a.ttf")==ESP_OK);assert(app_settings_set_font_path("/sdcard/fonts/folder/b.ttf")==ESP_OK);
 assert(file_references_begin("/sdcard/fonts/folder","/sdcard/fonts/renamed",&done)==ESP_OK);assert(!rename("/sdcard/fonts/folder","/sdcard/fonts/renamed"));assert(file_references_finish()==ESP_OK);
 eq(app_settings_system_font_path(),"/sdcard/fonts/renamed/a.ttf");eq(app_settings_font_path(),"/sdcard/fonts/renamed/b.ttf");
 assert(file_references_begin("/sdcard/fonts/renamed",NULL,&done)==ESP_OK);assert(!unlink("/sdcard/fonts/renamed/a.ttf"));assert(file_references_finish()==ESP_OK);eq(app_settings_system_font_path(),"");eq(app_settings_font_path(),"/sdcard/fonts/renamed/b.ttf");
 return 0;
}
if(!strcmp(mode,"refs-font-move")){
 const char *source="/sdcard/fonts/reset.ttf",*target="/sdcard/fonts/moved.ttf";
 put(source);fonts(source);
 assert(content_markdown_font_set(source)==ESP_OK&&content_home_quote_font_set(source)==ESP_OK);
 assert(file_references_font_in_use(source));
 assert(file_references_begin_reset_fonts(source,target,&done)==ESP_OK&&!done);
 assert(file_references_finish()==ESP_OK); // Cancelled physical move preserves all preferences.
 eq(app_settings_system_font_path(),source);eq(content_markdown_font(),source);
 assert(file_references_begin_reset_fonts(source,target,&done)==ESP_OK&&!done);assert(!rename(source,target));
 setenv("PICO_TEST_NVS_SET_FAIL","md_font",1);assert(file_references_finish()!=ESP_OK);
 eq(app_settings_system_font_path(),"");eq(app_settings_font_path(),"");eq(content_markdown_font(),source);eq(content_home_quote_font(),"");
 return 0;
}
if(!strcmp(mode,"refs-font-retry")){
 const char *source="/sdcard/fonts/reset.ttf",*target="/sdcard/fonts/moved.ttf";
 assert(file_references_begin_reset_fonts(source,target,&done)==ESP_OK&&done);
 eq(content_markdown_font(),"builtin");assert(!file_references_font_in_use(source));
 struct stat st;assert(stat(source,&st)&&!stat(target,&st));return 0;
}
assert(!"unknown mode");return 1;}
'''
with tempfile.TemporaryDirectory(prefix='foundation-',dir=R/'build') as tmp:
 p=Path(tmp);card=p/'card';(p/'build/simulator').mkdir(parents=True)
 for folder in ('books','fonts','pictures/pindou'):(card/folder).mkdir(parents=True)
 (p/'test.c').write_text(SOURCE)
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DPICO_HOST_SIMULATOR','-DPICO_SIM_CARD_PATH="unused"','-include',str(R/'tools/simulator/fs.h')]
 for inc in ['tools/simulator/include','main','main/book','main/apps','main/ui','main/cloud','components/read_pico_transfer/include']:flags+=['-I'+str(R/inc)]
 sources=['main/file_references.c','main/book/content_library.c','main/book/record_store.c','main/book/book_index_cache.c','main/apps/beads_png.c','main/apps/cyber_beads.c','main/settings.c','main/wallpaper_preferences.c','main/ui/ui_i18n.c','tools/simulator/fs.c','tools/simulator/nvs.c']
 subprocess.run(flags+[str(p/'test.c')]+[str(R/s) for s in sources]+['-lz']+(['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections'])+['-o',str(p/'test')],check=True)
 env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'PICO_TEST_FATFS_RENAME':'1','ASAN_OPTIONS':'detect_leaks=0'}
 def run(mode):subprocess.run([str(p/'test'),mode],cwd=p,env=env,check=True,timeout=30)
 run('cache-write');source=card/'books/test.txt';stamp=source.stat().st_mtime_ns;source.write_bytes(b'another');os.utime(source,ns=(stamp,stamp));run('cache-read')
 for mode in ('beads-write','beads-draft','beads-exported','beads-modified'):run(mode)
 # Deliberate latest-slot truncation: previous exported baseline must survive.
 run('beads-copy');(card/'pictures/pindou/.board-b').write_bytes(b'corrupt');run('beads-modified')
 for f in (card/'pictures/pindou').iterdir():f.unlink()
 legacy=b'IFBEADS1'+struct.pack('>I',7)+bytes([0,17,27,0])+bytes([2]+[255]*458)
 (card/'pictures/pindou/.board-a').write_bytes(legacy+struct.pack('>I',zlib.crc32(legacy)));run('beads-legacy')
 for mode in ('refs-fail','refs-retry','refs-delete','refs-font-move','refs-font-retry'):run(mode)
 (card/'.readpico/settings/file-refs.pending').write_bytes(b'broken intent');run('refs-corrupt')
print('PASS F1/F2/F3: real modules, private card, fresh processes, legacy drafts, export baseline, partial setters, retry and failed fsync.')
