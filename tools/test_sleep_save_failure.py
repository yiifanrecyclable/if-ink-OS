#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实锁屏流程在持续保存失败/下电失败时保持浅睡且可按键唤醒。 / Exercise real lock flow through persistent save/handoff failures and key wake."""
from pathlib import Path
import subprocess,tempfile,re
R=Path(__file__).resolve().parents[1]
s=(R/'main/sleep.c').read_text();a=s.index('void enter_lock_and_sleep(');i=s.index('{',a)+1;depth=1
while depth:
 if s[i]=='{':depth+=1
 elif s[i]=='}':depth-=1
 i+=1
function=s[a:i]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef int EpdiyHighlevelState;
typedef int esp_err_t;
typedef void *sc7a20h_handle_t;
typedef int app_wake_source_t;
#define APP_WAKE_TIMEOUT 3
#define APP_WAKE_KEY 1
#define APP_LOCK_LIGHT_SLEEP_MS 600000
#define APP_LOCK_IGNORE_BOOT_MS 500
#define APP_SLEEP_DEEP 1
#define EPD_DRAW_SUCCESS 0
#define ESP_FAIL -1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 2
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
enum EpdDrawError {DRAW_OK};
static bool s_lock_save_confirmed,cache_ok,transfer_ok=true;
static int waits,caches,pmu,resumes,cleans,clears;
static bool transfer_busy_after_handoff;
static uint8_t fb[1],lock_4bpp_bin_start[1];
static bool read_pico_transfer_pause_for_sleep(void){return transfer_ok;}
static void read_pico_transfer_resume_after_sleep(void){resumes++;}
static void app_time_service_stop(void){}
static uint8_t *epd_hl_get_framebuffer(void *hl){(void)hl;return fb;}
static void epd_hl_set_all_white(void *hl){(void)hl;}
static bool draw_wallpaper(uint8_t *p){(void)p;return true;}
static void ui_draw_full_image(uint8_t *p,const uint8_t *q){(void)p;(void)q;}
static enum EpdDrawError update_display_image_gray(void *hl){(void)hl;return DRAW_OK;}
static void guard_draw_result(void *hl,enum EpdDrawError result){(void)hl;(void)result;}
static void app_lock_wait_key_idle(int ms){(void)ms;}
static void display_poweroff(void){}
static app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t a,int ms){(void)a;assert(ms==600000);return ++waits<=2?APP_WAKE_TIMEOUT:APP_WAKE_KEY;}
static int epd_width(void){return 684;}
static int epd_height(void){return 1216;}
static int epd_get_rotation(void){return 3;}
static bool sleep_resume_save(const uint8_t *p,size_t bytes,unsigned rotation){(void)p;(void)bytes;(void)rotation;caches++;return cache_ok;}
static int app_enter_host_sleep(int mode){assert(mode==APP_SLEEP_DEEP);pmu++;if(transfer_busy_after_handoff)transfer_ok=false;return ESP_FAIL;}
static int sleep_resume_clear(void){clears++;return ESP_OK;}
static void app_settings_trace_storage_result(const char *stage,int error){(void)stage;assert(error!=0);}
static void app_settings_trace_storage(const char *stage){assert(stage&&*stage);}
static void read_pico_pmu_suppress_key_until_release(void){}
static void read_pico_pmu_drain_events(void){}
static void display_resume_clean_next(void){cleans++;}
static int64_t esp_timer_get_time(void){return 1000000;}
'''+function+r'''
int main(void){int64_t ignore=0;
 s_lock_save_confirmed=false;enter_lock_and_sleep(NULL,&ignore,NULL,true);assert(waits==3&&!caches&&!pmu&&resumes==1&&cleans==1);
 waits=caches=pmu=0;s_lock_save_confirmed=true;cache_ok=false;enter_lock_and_sleep(NULL,&ignore,NULL,true);assert(waits==3&&caches==1&&!pmu);
 waits=caches=pmu=0;s_lock_save_confirmed=true;cache_ok=true;enter_lock_and_sleep(NULL,&ignore,NULL,true);assert(waits==3&&caches==1&&pmu==1);
 assert(clears==1);
 waits=caches=pmu=0;s_lock_save_confirmed=true;transfer_busy_after_handoff=true;enter_lock_and_sleep(NULL,&ignore,NULL,true);assert(waits==3&&pmu==1&&clears==2);
 waits=caches=pmu=0;transfer_ok=false;enter_lock_and_sleep(NULL,&ignore,NULL,true);assert(!waits&&!pmu);
 puts("PASS: persistent save/cache/handoff failure retains RAM in light sleep, timeouts stay locked, key wake works, active transfers are protected");}
'''
with tempfile.TemporaryDirectory(dir=R/'build',prefix='sleep-failure-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
