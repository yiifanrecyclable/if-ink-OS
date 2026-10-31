/* SPDX-License-Identifier: Apache-2.0
 * 固定容量PSRAM环形记录；采样时不碰文件系统。/ Bounded PSRAM ring; capture never accesses the filesystem.
 */
#include "operation_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "read_pico_sd.h"
#include "usb_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CAPACITY 16384u
#define DURATION_US INT64_C(120000000)
typedef struct { uint32_t us; uint16_t type; int16_t a,b,c,d; int32_t e; } record_t;
static record_t *s_records;
static unsigned s_count,s_next,s_dropped;
static int64_t s_start,s_end,s_last_sample,s_last_emit;
static bool s_active,s_pending;
static int64_t s_retry;
static int s_last_x,s_last_y,s_last_count=-1;
static char s_status[160];
static int16_t small(int n){return n<-32768?-32768:n>32767?32767:(int16_t)n;}
bool operation_log_active(void){return s_active;}
bool operation_log_pending(void){return s_pending;}
unsigned operation_log_remaining(void){int64_t left=DURATION_US-(esp_timer_get_time()-s_start);return s_active&&left>0?(unsigned)((left+999999)/1000000):0;}
const char *operation_log_status(void){return s_status;}
esp_err_t operation_log_start(void){
    if(s_active||s_pending||usb_storage_active())return ESP_ERR_INVALID_STATE;
    read_pico_sd_info_t card;read_pico_sd_get_info(&card);if(!card.mounted)return ESP_ERR_INVALID_STATE;
    s_records=heap_caps_malloc(sizeof(record_t)*CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_records)return ESP_ERR_NO_MEM;
    s_count=s_next=s_dropped=0;s_start=esp_timer_get_time();s_last_sample=s_last_emit=0;s_last_count=-1;
    s_status[0]=0;s_retry=0;s_active=true;return ESP_OK;
}
void operation_log_event(operation_log_event_t type,int a,int b,int c,int d,int e){
    if(!s_active||type<OPLOG_SAMPLE||type>OPLOG_CANCEL)return;
    int64_t now=esp_timer_get_time();if(now-s_start>=DURATION_US){s_end=s_start+DURATION_US;s_active=false;s_pending=true;return;}
    s_records[s_next]=(record_t){(uint32_t)(now-s_start),(uint16_t)type,small(a),small(b),small(c),small(d),e};
    s_next=(s_next+1)%CAPACITY;if(s_count<CAPACITY)++s_count;else ++s_dropped;
}
void operation_log_sample(const cst836u_touch_t *t,esp_err_t error,bool pressed,bool released,bool body){
    if(!s_active)return;
    int64_t now=esp_timer_get_time();
    if(s_last_sample&&now-s_last_sample>20000)operation_log_event(OPLOG_GAP,0,0,0,0,(int)((now-s_last_sample)/1000));
    s_last_sample=now;
    int count=error==ESP_OK&&t->touched?t->count:0;
    int x=body?t->x:-1,y=body?t->y:-1;
    if(count!=s_last_count||pressed||released||error!=ESP_OK||
       (count&&body&&(abs(x-s_last_x)>=2||abs(y-s_last_y)>=2))||now-s_last_emit>=50000){
        operation_log_event(OPLOG_SAMPLE,count,x,y,(pressed?1:0)|(released?2:0)|(body?4:0),error);
        if(body)for(unsigned i=0;i<t->count&&i<CST836U_MAX_POINTS;++i){
            const cst836u_point_t *p=&t->points[i];
            operation_log_event(OPLOG_POINT,(int)i,p->x,p->y,(p->id<<4)|p->event,p->active);
        }
        s_last_count=count;s_last_x=x;s_last_y=y;s_last_emit=now;
    }
}
void operation_log_finish(void){if(s_active){s_end=esp_timer_get_time();s_active=false;s_pending=true;}s_retry=0;operation_log_poll(true);}
void operation_log_poll(bool safe){
    if(s_active&&esp_timer_get_time()-s_start>=DURATION_US){s_end=s_start+DURATION_US;s_active=false;s_pending=true;}
    if(!safe||!s_pending||esp_timer_get_time()<s_retry)return;
    if(usb_storage_active()){snprintf(s_status,sizeof(s_status),"Waiting for USB to stop");return;}
    s_retry=esp_timer_get_time()+5000000;
    read_pico_sd_info_t card;read_pico_sd_get_info(&card);if(!card.mounted){snprintf(s_status,sizeof(s_status),"Waiting for TF card");return;}
    const char *root="/sdcard/diagnostics";mkdir(root,0700);
    char path[120],temp[128];FILE *f=NULL;
    for(unsigned i=1;i<10000;++i){snprintf(path,sizeof(path),"%s/operation-%04u.csv",root,i);if(access(path,F_OK)==0)continue;snprintf(temp,sizeof(temp),"%s.part",path);f=fopen(temp,"wb");break;}
    bool ok=f!=NULL;
    if(f){
        const char *names[]={"unknown","sample","gap","gesture","turn","present","multi","page","sleep","clock","read","point","cancel"};
        fprintf(f,"# if-ink OS operation log schema=1 version=%s duration_ms=%lld dropped=%u utc=%lld\n",esp_app_get_description()->version,(long long)((s_end-s_start)/1000),s_dropped,(long long)time(NULL));
        fputs("# sample:count,x,y,flags(press=1 release=2 reader=4),error; gap:e=ms; gesture:type,x0,y0,multi_flags,hold_ms\n# turn:direction,page,chapter,source(0=gesture 1=key),percent; present:a=phase(0=start 1=end),b=redraw,c=view,d=wave,e=duration_ms\n# multi:active,blocked,count,enabled,action; page:a=app_index; sleep:a=1; clock:a=stage,b=result; read:a=error,b=count,e=I2C_us; point:a=slot,b=x,c=y,d=id/event,e=active; cancel:a=reason(1=error 2=page_refresh 3=interrupt)\n# Coordinates are redacted outside reader body. No text, filenames, credentials or SSIDs.\n",f);
        fputs("us,event,a,b,c,d,e\n",f);
        unsigned first=s_count==CAPACITY?s_next:0;
        for(unsigned i=0;i<s_count;++i){record_t *r=&s_records[(first+i)%CAPACITY];if(fprintf(f,"%lu,%s,%d,%d,%d,%d,%ld\n",(unsigned long)r->us,names[r->type],r->a,r->b,r->c,r->d,(long)r->e)<0)ok=false;}
        if(ferror(f)||fflush(f)||fsync(fileno(f)))ok=false;
        if(fclose(f))ok=false;
        if(ok&&rename(temp,path))ok=false;
        if(!ok)unlink(temp);
    }
    if(ok){snprintf(s_status,sizeof(s_status),"%s",path);s_pending=false;free(s_records);s_records=NULL;}
    else snprintf(s_status,sizeof(s_status),"Save failed; open this page to retry");
}
