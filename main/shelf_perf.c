/* SPDX-License-Identifier: Apache-2.0
 * r9t系列测量底座：五分钟、2048条，按启动编号导出CSV；不改缓存/绘制/刷新决策。
 * Five-minute, 2048-record baseline; numbered CSV per boot, without changing caching/drawing/refresh decisions.
 */
#include "shelf_perf.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "read_pico_sd.h"
#include "usb_storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static TaskHandle_t s_owner;
static bool owner(void){return xTaskGetCurrentTaskHandle()==s_owner;}
#else
static _Thread_local int s_thread;
static const void *s_owner;
static bool owner(void){return &s_thread==s_owner;}
#endif
#define PERF_COUNT 2048u
#define PERF_WINDOW INT64_C(300000000)
typedef struct {uint32_t start_us,end_us,request;uint16_t stage,kind;int32_t a,b,c,d;} record_t;
_Static_assert(sizeof(record_t)==32,"Local timing records must stay 32 bytes");
static record_t *s_records;
static unsigned s_count,s_dropped;
static int64_t s_start,s_last_loop,s_retry;
static uint32_t s_request;
static bool s_active,s_pending;
static const char *const names[]={"unknown","boot","enter","reuse","scan","roots","capacity","enumerate","documents","render","cover_load","cache_probe","cache_write","index_probe","index_write","present","epd","frame","loop_gap","input","media","scan_wait","nav_switch","nav_exit","nav_font","nav_enter","nav_present","nav_render","nav_output","home_banner","cover_source","cover_decode","cover_persist_read","cover_persist_write",
    "wake_boot","wake_settings","wake_board","wake_pmu","wake_sd_wait",
    "wake_restore","wake_resume_result","wake_splash","wake_font",
    "wake_ui_prepare","wake_first_frame","wake_ui_ready",
    "wake_clear","wake_output","wake_light",
    "wake_tone","wake_maintenance","wake_assets","wake_font_retry",
    "wake_page_enter","wake_media_poll","wake_home_recents","wake_home_history",
    "wake_home_scan","wake_home_cover_cache",
    "banner_layer_key","banner_layer_read","banner_layer_render",
    "banner_layer_write","banner_layer_compose","wake_notice"
#ifdef IFINK_INKBUDDY
    , "inkbuddy_memory", "inkbuddy_crc", "inkbuddy_present", "inkbuddy_unchanged"
#endif
    , "doc_store", "doc_verify_us", "doc_titles", "doc_progress", "doc_order", "doc_reuse"
};
_Static_assert(sizeof(names)/sizeof(names[0])==SP_STAGE_COUNT,"Timing stage names must match the exported stage IDs");
static bool enabled(void){return owner()&&s_active;}
static void record(int kind,int stage,int64_t start,int64_t end,uint32_t request,int a,int b,int c,int d){
    if(s_count==PERF_COUNT){++s_dropped;return;}
    s_records[s_count++]=(record_t){(uint32_t)(start-s_start),(uint32_t)(end-s_start),request,(uint16_t)stage,(uint16_t)kind,a,b,c,d};
}
void shelf_perf_init(void){
    if(s_records)return;
    const char *v=esp_app_get_description()->version;
    if(strncmp(v,"0.2.0-r9t",9) && strcmp(v,"0.2.1-beta-r1t2") &&
       strcmp(v,"0.2.1-beta-r1t3") && strcmp(v,"0.2.1-beta-r1t4") &&
       strcmp(v,"0.2.1-beta-r1t5") && strcmp(v,"0.2.1-beta-r1t6") &&
       strcmp(v,"0.2.2-beta-r1t1")
#ifdef IFINK_INKBUDDY
       && strcmp(v,"0.2.1-beta-r2t2")
#endif
    )return;
#ifdef ESP_PLATFORM
    s_owner=xTaskGetCurrentTaskHandle();
#else
    s_owner=&s_thread;
#endif
    s_records=heap_caps_malloc(sizeof(record_t)*PERF_COUNT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_records)return; // 诊断分配失败不影响产品路径。/ Capture allocation must never gate the product.
    s_count=s_dropped=s_request=0;s_start=esp_timer_get_time();s_last_loop=s_retry=0;s_pending=false;s_active=true;
    shelf_perf_event(SP_BOOT,PERF_COUNT,(int)sizeof(record_t),(int)(PERF_WINDOW/1000000),0);
}
void shelf_perf_event(shelf_perf_stage_t stage,int a,int b,int c,int d){
    if(!enabled())return;
    int64_t now=esp_timer_get_time();record(0,stage,now,now,s_request,a,b,c,d);
}
void shelf_perf_request(int view,int epoch){if(enabled()){++s_request;shelf_perf_event(SP_ENTER,view,epoch,0,0);}}
shelf_perf_span_t shelf_perf_begin(shelf_perf_stage_t stage,int a,int b){
    if(!enabled())return(shelf_perf_span_t){0};
    return(shelf_perf_span_t){esp_timer_get_time(),s_request,stage,a,b};
}
void shelf_perf_end(shelf_perf_span_t *span){
    if(!span||!span->stage||!enabled())return;
    record(1,span->stage,span->start,esp_timer_get_time(),span->request,span->a,span->b,0,0);span->stage=0;
}
void shelf_perf_loop(bool pressed,bool released,int points){
    if(!enabled())return;
    int64_t now=esp_timer_get_time();
    if(s_last_loop&&now-s_last_loop>20000)record(1,SP_LOOP_GAP,s_last_loop,now,s_request,0,0,0,0);
    s_last_loop=now;
    if(pressed||released)shelf_perf_event(SP_INPUT,pressed,released,points,0);
}
void shelf_perf_finish(void){if(enabled()){s_active=false;s_pending=true;s_retry=0;}}
void shelf_perf_poll(bool safe){
    if(!owner())return;
    if(s_active&&esp_timer_get_time()-s_start>=PERF_WINDOW)shelf_perf_finish();
    if(!safe||!s_pending||esp_timer_get_time()<s_retry||usb_storage_active())return;
    read_pico_sd_info_t card={0};if(read_pico_sd_get_info(&card)!=0||!card.mounted)return;
    s_retry=esp_timer_get_time()+5000000;
    if(mkdir("/sdcard/diagnostics",0700)&&errno!=EEXIST)return;
    char path[144];int fd=-1;
    const char *version=esp_app_get_description()->version,*suffix=strrchr(version,'-')+1;
    for(unsigned i=1;i<10000;++i){
        snprintf(path,sizeof(path),"/sdcard/diagnostics/shelf-perf-%s-%04u.csv",suffix,i);
        fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd>=0||errno!=EEXIST)break;
    }
    if(fd<0)return;
    FILE *f=fdopen(fd,"wb");if(!f){close(fd);unlink(path);return;}
    char identity[65]="host";
#ifdef ESP_PLATFORM
    const uint8_t *elf=esp_app_get_description()->app_elf_sha256;
    for(unsigned i=0;i<32;++i)snprintf(identity+i*2,3,"%02x",elf[i]);
#endif
    bool ok=fprintf(f,"# shelf_perf schema=1 version=%s elf_sha256=%s window_ms=300000 elapsed_us=%lld records=%u dropped=%u\n# source=local measurement build; version and ELF identify tested behavior; no text, filenames or credentials; export occurs after capture\n# kind=span uses explicit start/end, nested spans overlap; loop_gap is an observation, not additive work\nstart_us,end_us,duration_us,kind,stage,request,a,b,c,d\n",version,identity,(long long)(esp_timer_get_time()-s_start),s_count,s_dropped)>0;
    for(unsigned i=0;i<s_count&&ok;++i){
        const record_t *r=&s_records[i];
        ok=fprintf(f,"%lu,%lu,%lu,%s,%s,%lu,%ld,%ld,%ld,%ld\n",(unsigned long)r->start_us,(unsigned long)r->end_us,(unsigned long)(r->end_us-r->start_us),r->kind?"span":"event",r->stage<sizeof(names)/sizeof(names[0])?names[r->stage]:"unknown",(unsigned long)r->request,(long)r->a,(long)r->b,(long)r->c,(long)r->d)>0;
    }
    if(ok)ok=fputs("# complete=1\n",f)>=0;
    if(ferror(f)||fflush(f))ok=false;
    if(ok&&fsync(fileno(f)))ok=false;
    if(fclose(f))ok=false;
    if(!ok){unlink(path);return;}
    free(s_records);s_records=NULL;s_pending=false;
}
