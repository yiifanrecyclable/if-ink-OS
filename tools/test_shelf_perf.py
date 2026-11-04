#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real local recorder: timing, owner isolation, bounded memory, no live I/O, export failures/retry, unique names."""
from pathlib import Path
import os,subprocess,tempfile,csv,io,json
R=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shelf_perf.h"
#include "esp_app_desc.h"
#include "read_pico_sd.h"
int64_t test_us=1000000;
bool test_alloc_fail;
static bool mounted=true,usb;
static esp_app_desc_t descriptor={.version="0.2.0-r9t1",.project_name="Read_Pico_IF"};
const esp_app_desc_t *esp_app_get_description(void){return &descriptor;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *p){memset(p,0,sizeof(*p));p->mounted=mounted;return ESP_OK;}
bool usb_storage_active(void){return usb;}
static void *other(void *unused){(void)unused;for(int i=0;i<4000;++i)shelf_perf_event(SP_RENDER,1,2,3,4);shelf_perf_finish();shelf_perf_poll(true);return NULL;}
static unsigned logs(void){DIR *d=opendir("/sdcard/diagnostics");if(!d)return 0;unsigned n=0;struct dirent *e;while((e=readdir(d)))if(strstr(e->d_name,".csv"))++n;closedir(d);return n;}
/* PRODUCTION_PRESENT_PREFIX */
int main(int argc,char **argv){assert(argc==2);unsigned before=logs();
 if(!strcmp(argv[1],"t2")||!strcmp(argv[1],"t3")||!strcmp(argv[1],"t4")||!strcmp(argv[1],"t5")||!strcmp(argv[1],"t6")||!strcmp(argv[1],"t7"))snprintf(descriptor.version,sizeof(descriptor.version),"0.2.0-r9%s",argv[1]);
 if(!strcmp(argv[1],"formal"))strcpy(descriptor.version,"0.2.0-beta.9");
 if(!strcmp(argv[1],"oom"))test_alloc_fail=true;
 shelf_perf_init();shelf_perf_request(0,123);
 for(int i=0;i<5000;++i){assert(present_prefix(NULL,APP_REDRAW_NONE));assert(present_prefix(NULL,APP_REDRAW_DONE));}
 {SHELF_PERF_SCOPE(scan,SP_SCAN,0,0);test_us+=100;
  {SHELF_PERF_SCOPE(capacity,SP_CAPACITY,0,0);test_us+=700;}
  test_us+=200;}
 if(!strcmp(argv[1],"t5")||!strcmp(argv[1],"t6")||!strcmp(argv[1],"t7"))for(int stage=SP_NAV_SWITCH;stage<=SP_NAV_OUTPUT;++stage){SHELF_PERF_SCOPE(nav,stage,0,1);test_us+=25;}
 if(!strcmp(argv[1],"t6")||!strcmp(argv[1],"t7")){SHELF_PERF_SCOPE(banner,SP_HOME_BANNER,0,1);test_us+=40;}
 if(!strcmp(argv[1],"t7"))for(int stage=SP_COVER_SOURCE;stage<=SP_COVER_PERSIST_WRITE;++stage){SHELF_PERF_SCOPE(cover,stage,1,1);test_us+=60;}
 if(!strcmp(argv[1],"full"))for(int i=0;i<3000;++i)shelf_perf_event(SP_INPUT,i,0,0,0);
 pthread_t task;assert(!pthread_create(&task,NULL,other,NULL));assert(!pthread_join(task,NULL));
 shelf_perf_poll(true); // Active capture must not create files.
 assert(logs()==before);
 shelf_perf_loop(false,false,0);test_us+=50000;shelf_perf_loop(true,false,1);
 if(!strcmp(argv[1],"expiry")){test_us+=300000000;shelf_perf_poll(false);}else shelf_perf_finish();
 shelf_perf_poll(false);assert(logs()==before);
 mounted=false;shelf_perf_poll(true);assert(logs()==before);mounted=true;
 usb=true;shelf_perf_poll(true);assert(logs()==before);usb=false;
 setenv("PICO_TEST_FSYNC_FAIL","1",1);shelf_perf_poll(true);unsetenv("PICO_TEST_FSYNC_FAIL");
 test_us+=5000001;shelf_perf_poll(true);return 0;
}
'''
book=(R/'main/apps/app_book.c').read_text()
# Exercise the production early-return/measurement prefix, including its ordering.
# The rest of present touches hardware; this regression ends at its first draw branch.
prefix=book.split('static bool present(app_ctx_t* ctx, app_redraw_t redraw) {',1)[1].split('    if (s_presented_view',1)[0]
SOURCE=SOURCE.replace('/* PRODUCTION_PRESENT_PREFIX */','typedef void app_ctx_t;typedef int app_redraw_t;enum {APP_REDRAW_NONE,APP_REDRAW_DONE};static int s_view;\nstatic bool present_prefix(app_ctx_t* ctx,app_redraw_t redraw){(void)ctx;'+prefix+'return false;}')
with tempfile.TemporaryDirectory(prefix='shelf-perf-',dir=R/'build') as tmp:
 p=Path(tmp);headers=p/'include';headers.mkdir()
 (headers/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nextern int64_t test_us;static inline int64_t esp_timer_get_time(void){return test_us;}\n')
 (headers/'esp_heap_caps.h').write_text('#pragma once\n#include <stdlib.h>\n#include <stdbool.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nextern bool test_alloc_fail;static inline void *heap_caps_malloc(size_t n,int c){(void)c;return test_alloc_fail?NULL:malloc(n);}\n')
 (p/'test.c').write_text(SOURCE)
 cmd=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-DSHELF_PERF_HOST_TEST','-DPICO_SIM_CARD_PATH="unused"','-include',str(R/'tools/simulator/fs.h'),'-I'+str(headers),'-I'+str(R/'tools/simulator/include'),'-I'+str(R/'main'),'-I'+str(R/'main/apps'),str(p/'test.c'),str(R/'main/shelf_perf.c'),str(R/'tools/simulator/fs.c'),'-o',str(p/'test')]
 subprocess.run(cmd,check=True)
 for mode in ('normal','t2','t3','t4','t5','t6','t7','full','expiry','formal','oom'):
  card=p/mode;card.mkdir();env={**os.environ,'PICO_SIM_CARD_ROOT':str(card),'ASAN_OPTIONS':'detect_leaks=0'}
  subprocess.run([str(p/'test'),mode],env=env,check=True)
  logs=list(card.glob('diagnostics/*.csv'))
  if mode in ('formal','oom'):assert not logs;continue
  version='r9'+mode if mode in ('t2','t3','t4','t5','t6','t7') else 'r9t1'
  assert len(logs)==1;raw=logs[0].read_text();assert raw.startswith('# shelf_perf schema=1 version=0.2.0-'+version+' ');assert raw.endswith('# complete=1\n')
  assert logs[0].name=='shelf-perf-'+version+'-0001.csv'
  rows=list(csv.DictReader(io.StringIO('\n'.join(line for line in raw.splitlines() if not line.startswith('#')))))
  assert len(rows)==(2048 if mode=='full' else 18 if mode=='t7' else 14 if mode=='t6' else 13 if mode=='t5' else 6),(mode,len(rows))
  stages={x['stage']:x for x in rows};assert stages['capacity']['duration_us']=='700' and stages['scan']['duration_us']=='1000';assert stages['scan']['request']=='1'
  if mode in ('t5','t6','t7'):
   for name in ('nav_switch','nav_exit','nav_font','nav_enter','nav_present','nav_render','nav_output'):assert stages[name]['duration_us']=='25'
  if mode in ('t6','t7'):assert stages['home_banner']['duration_us']=='40' and stages['home_banner']['b']=='1'
  if mode=='t7':
   for name in ('cover_source','cover_decode','cover_persist_read','cover_persist_write'):assert stages[name]['duration_us']=='60' and stages[name]['b']=='1'
  assert stages['enter']['b']=='123';assert ("dropped=0" not in raw)==(mode=='full');assert all(int(x['end_us'])>=int(x['start_us']) for x in rows)
  if mode=='normal':
   previous=logs[0].read_bytes();subprocess.run([str(p/'test'),mode],env=env,check=True);assert logs[0].read_bytes()==previous;assert len(list(card.glob('diagnostics/*.csv')))==2
 print('PASS local recorder: real module, exact nested durations, UI owner only, 64KiB bound/drop count, no writes during capture, disabled formal builds/OOM, expiry/manual finish, no-card/USB, fsync retry and non-overwriting names.')
