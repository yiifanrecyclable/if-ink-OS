#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""实际电源函数的有限交接与保存门控。 / Real power functions: bounded handoff and persistence gates."""
from pathlib import Path
import re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
def function(path,name):
 s=(R/path).read_text();m=re.search(r'^(?:static )?(?:void|bool|esp_err_t|EpdRect|ui_power_action_t) '+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 a=m.start();i=s.index('{',a)+1;depth=1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[a:i]+'\n'
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define APP_SLEEP_OFF 2
#define APP_SLEEP_DEEP 1
#define PMU_ACTION_HOST_NORMAL_RESTART 4
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define TAG "test"
#define UI_MSG(x) (x)
#define APP_REDRAW_PAGE 1
#define OPLOG_SLEEP 1
typedef int app_sleep_mode_t;
typedef struct {int64_t now_ms;bool pressed,released;unsigned char *fb;void *hl;} app_ctx_t;
typedef struct {esp_err_t (*prepare_sleep)(app_ctx_t*);void (*on_before_lock)(app_ctx_t*);} app_desc_t;
static app_desc_t current;
static int prepares,pmu_calls,radio_resumes,presents,final_frames,clears;
static int64_t now;
static bool radio_ok=true;
static int flush_error,prepare_error,clear_error,pmu_result;
static jmp_buf cut;
static bool read_pico_transfer_pause_for_sleep(void){return radio_ok;}
static void read_pico_transfer_resume_after_sleep(void){radio_resumes++;}
static esp_err_t read_pico_sd_flush(void){return flush_error;}
static void app_time_service_stop(void){}
static esp_err_t app_sync_time_to_pmu(void){return ESP_OK;}
static void display_poweroff(void){}
static void app_settings_trace_storage(const char *phase){(void)phase;}
static void app_settings_trace_storage_result(const char *phase,int error){(void)phase;(void)error;}
static int send(void){pmu_calls++;if(pmu_result==2)longjmp(cut,1);return pmu_result==0?ESP_OK:ESP_FAIL;}
static esp_err_t read_pico_pmu_power_off(void){return send();}
static esp_err_t read_pico_pmu_report_sleep(void){return send();}
static esp_err_t read_pico_pmu_action(int a,int b,int c){(void)a;(void)b;(void)c;return send();}
static void vTaskDelay(int ms){now+=ms;}
static void read_pico_pmu_suppress_key_until_release(void){}
static void display_resume_clean_next(void){}
static void sleep_resume_set_target(unsigned tab,const char *path){(void)tab;(void)path;}
static esp_err_t sleep_resume_clear(void){clears++;return clear_error;}
static const app_desc_t *app_at(unsigned tab){return tab==0?&current:NULL;}
static void storage_notice(const char *message){assert(message&&*message);}
static void operation_log_event(int a,int b,int c,int d,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static void operation_log_finish(void){}
static void app_present(app_ctx_t *ctx,const app_desc_t *app,int mode){(void)ctx;(void)app;(void)mode;presents++;}
static void ui_power_final_draw(unsigned char *fb,bool restart){(void)fb;(void)restart;final_frames++;}
static int update_display_full(void *hl){(void)hl;return 0;}
static void guard_draw_result(void *hl,int result){(void)hl;(void)result;}
static void app_lock_wait_key_idle(int ms){(void)ms;}
static void epd_poweroff(void){}
static int64_t esp_timer_get_time(void){return now*1000;}
static esp_err_t prepare(app_ctx_t *ctx){(void)ctx;prepares++;return prepare_error;}
'''
for name in ['app_enter_host_sleep_ex','app_restart_host_ex','app_enter_host_sleep','app_restart_host']:code+=function('main/sleep.c',name)
for name in ['prepare_lock_resume','run_power_action']:code+=function('main/app/app_loop.c',name)
code+=r'''
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int type,x0,y0;} ui_gesture_event_t;
enum {UI_GESTURE_TAP=1};
typedef enum {UI_POWER_ACTION_NONE,UI_POWER_ACTION_CANCEL,UI_POWER_ACTION_SHUTDOWN,UI_POWER_ACTION_RESTART,UI_POWER_ACTION_RETRY,UI_POWER_ACTION_DISCARD} ui_power_action_t;
static bool s_unsaved,s_restarting;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
'''
ui_source=(R/'main/ui/ui_power_dialog.c').read_text()
code+='\n'.join(line for line in ui_source.splitlines() if line.startswith('static const EpdRect k_'))+'\n'
for name in ['unsaved_button','ui_power_dialog_set_unsaved','ui_power_dialog_handle']:code+=function('main/ui/ui_power_dialog.c',name)
code+=r'''
static void reset(void){prepares=pmu_calls=radio_resumes=presents=final_frames=clears=0;now=0;radio_ok=true;flush_error=prepare_error=clear_error=pmu_result=0;current.prepare_sleep=prepare;}
int main(void){app_ctx_t ctx={0};
 ui_gesture_event_t tap={.type=UI_GESTURE_TAP,.x0=200,.y0=630};
 ui_power_dialog_set_unsaved(false,false);assert(ui_power_dialog_handle(&tap)!=UI_POWER_ACTION_DISCARD);
 ui_power_dialog_set_unsaved(true,false);assert(ui_power_dialog_handle(&tap)==UI_POWER_ACTION_RETRY);
 tap.x0=340;assert(ui_power_dialog_handle(&tap)==UI_POWER_ACTION_DISCARD);
 tap.x0=500;assert(ui_power_dialog_handle(&tap)==UI_POWER_ACTION_CANCEL);
 tap.y0=440;assert(ui_power_dialog_handle(&tap)==UI_POWER_ACTION_NONE);
 tap.y0=300;assert(ui_power_dialog_handle(&tap)==UI_POWER_ACTION_CANCEL);
 ui_power_dialog_set_unsaved(false,true);tap.x0=224;tap.y0=566;assert(ui_power_dialog_handle(&tap)==UI_POWER_ACTION_RESTART);
 reset();prepare_error=ESP_FAIL;run_power_action(&ctx,&current,false,false);assert(prepares==1&&!pmu_calls&&!clears&&!final_frames&&!presents);
 reset();clear_error=ESP_FAIL;run_power_action(&ctx,&current,false,false);assert(prepares==1&&clears==1&&!pmu_calls&&!final_frames&&!presents);
 reset();flush_error=ESP_FAIL;run_power_action(&ctx,&current,false,false);assert(!pmu_calls&&!radio_resumes&&!presents);
 reset();radio_ok=false;assert(app_enter_host_sleep(APP_SLEEP_OFF)==ESP_ERR_INVALID_STATE&&!pmu_calls);
 reset();pmu_result=1;assert(app_enter_host_sleep(APP_SLEEP_OFF)==ESP_FAIL&&pmu_calls==1&&radio_resumes==1);
 reset();assert(app_enter_host_sleep(APP_SLEEP_OFF)==ESP_ERR_TIMEOUT&&now==5000&&pmu_calls==1&&radio_resumes==1);
 reset();pmu_result=1;assert(app_enter_host_sleep(APP_SLEEP_DEEP)==ESP_FAIL&&pmu_calls==3&&now==400);
 reset();assert(app_restart_host()==ESP_ERR_TIMEOUT&&pmu_calls==1&&radio_resumes==1&&now==5000);
 reset();pmu_result=1;assert(app_restart_host()==ESP_FAIL&&pmu_calls==1&&radio_resumes==1);
 reset();run_power_action(&ctx,&current,true,false);assert(prepares==1&&clears==1&&final_frames==1&&presents==1&&!ctx.pressed&&!ctx.released);
 reset();pmu_result=2;if(!setjmp(cut)){run_power_action(&ctx,&current,false,false);assert(!"powered-off path returned");}assert(final_frames==1&&pmu_calls==1&&!presents);
 reset();prepare_error=clear_error=flush_error=ESP_FAIL;assert(run_power_action(&ctx,&current,false,true));assert(pmu_calls==1&&presents==1&&radio_resumes==1);
 reset();radio_ok=false;assert(run_power_action(&ctx,&current,true,true));assert(!pmu_calls&&presents==1);
 puts("PASS: save/token/sync failure gates, PMU rejection/ACK timeout, restart timeout and same-page fallback without closing reader or unmounting SD");
}
'''
with tempfile.TemporaryDirectory(prefix='power-handoff-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
