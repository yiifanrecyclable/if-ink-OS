/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行生产HTTP/UI桥接，注入超时、停止及并发处理；不访问设备。
 * English: Exercise the production HTTP/UI bridge under timeout/stop/concurrency without hardware.
 */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "read_pico_transfer_editor.h"
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define HTTP_GET 0
#define HTTP_POST 1
#define portENTER_CRITICAL(p) pthread_mutex_lock(p)
#define portEXIT_CRITICAL(p) pthread_mutex_unlock(p)
static pthread_mutex_t s_lock=PTHREAD_MUTEX_INITIALIZER;
static bool s_stopping,s_upload_active,stop_during_body;
enum {READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING,READ_PICO_TRANSFER_STARTING};
static struct {int state;bool network_ready;unsigned changed_count,done_count;} s_status;
static struct {bool is_flash;read_pico_transfer_editor_fn editor_cb;} s_cfg;
typedef struct semaphore {pthread_mutex_t lock;pthread_cond_t cv;bool signalled;} *SemaphoreHandle_t;
static unsigned sem_created,sem_deleted;
static SemaphoreHandle_t xSemaphoreCreateBinary(void){SemaphoreHandle_t s=calloc(1,sizeof(*s));assert(s);pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->cv,NULL);++sem_created;return s;}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned ms){
 (void)ms;struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=80000000;if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}
 pthread_mutex_lock(&s->lock);int err=0;while(!s->signalled&&!err)err=pthread_cond_timedwait(&s->cv,&s->lock,&until);bool signalled=s->signalled;pthread_mutex_unlock(&s->lock);return signalled?pdTRUE:0;
}
static int xSemaphoreGive(SemaphoreHandle_t s){pthread_mutex_lock(&s->lock);s->signalled=true;pthread_cond_signal(&s->cv);pthread_mutex_unlock(&s->lock);return 1;}
static void vSemaphoreDelete(SemaphoreHandle_t s){pthread_cond_destroy(&s->cv);pthread_mutex_destroy(&s->lock);++sem_deleted;free(s);}

typedef struct {int method,content_len,code;const char *query,*body;char result[256];} httpd_req_t;
static bool management_request(httpd_req_t *r){(void)r;return true;}
static int wifi_response(httpd_req_t *r,const char *status,const char *body){r->code=atoi(status);snprintf(r->result,sizeof(r->result),"%s",body);return 0;}
static int respond_error(httpd_req_t *r,int code){r->code=code;return 0;}
static bool admission_begin(bool stopping,bool *active){if(stopping||*active)return false;*active=true;return true;}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t cap){snprintf(out,cap,"%s",r->query?r->query:"");return 0;}
static int httpd_query_key_value(const char *query,const char *key,char *out,size_t cap){assert(!strcmp(key,"op"));if(strncmp(query,"op=",3))return -1;snprintf(out,cap,"%s",query+3);return 0;}
static bool recv_small_body(httpd_req_t *r,char *out,size_t cap){snprintf(out,cap,"%s",r->body?r->body:"");if(stop_during_body)s_stopping=true;return true;}
static int httpd_resp_set_status(httpd_req_t *r,const char *s){r->code=atoi(s);return 0;}
static int httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;return 0;}
static int httpd_resp_send(httpd_req_t *r,const char *data,size_t n){assert(n<sizeof(r->result));memcpy(r->result,data,n);r->result[n]=0;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *data){return httpd_resp_send(r,data,strlen(data));}
#include "../components/read_pico_transfer/transfer_editor_http.h"
static pthread_mutex_t callback_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callback_cv=PTHREAD_COND_INITIALIZER;
static bool slow,entered,resume_callback;static unsigned calls,commits;
static esp_err_t dispatch(const char *op,const char *body,read_pico_transfer_editor_reply_t *r,read_pico_transfer_cancel_fn cancelled,void *context){
 (void)body;++calls;assert(!strcmp(op,"get")||!strcmp(op,"banner"));
 pthread_mutex_lock(&callback_lock);entered=true;pthread_cond_signal(&callback_cv);while(slow&&!resume_callback)pthread_cond_wait(&callback_cv,&callback_lock);pthread_mutex_unlock(&callback_lock);
 r->status=cancelled(context)?503:200;r->data=(uint8_t*)strdup("{\"ok\":true}");r->size=strlen((char*)r->data);
 if(r->status==200&&!strcmp(op,"banner")){r->changed=true;++commits;}return ESP_OK;
}
static void *request_thread(void *r){editor_handler(r);return NULL;}
static void *ui_thread(void *ignored){(void)ignored;read_pico_transfer_editor_poll();return NULL;}
static void wait_pending(void){for(unsigned i=0;i<1000;++i){pthread_mutex_lock(&s_lock);bool exists=s_editor_pending!=NULL;pthread_mutex_unlock(&s_lock);if(exists)return;usleep(1000);}assert(!"pending request not published");}
static void reset(void){assert(!s_editor_active&&!s_editor_pending&&!s_upload_active);s_stopping=stop_during_body=slow=entered=resume_callback=false;s_cfg.editor_cb=dispatch;s_status.state=READ_PICO_TRANSFER_READY;s_status.network_ready=true;}
int main(void){
 pthread_t http,ui;httpd_req_t r={.method=HTTP_GET};reset();pthread_create(&http,NULL,request_thread,&r);wait_pending();read_pico_transfer_editor_poll();pthread_join(http,NULL);assert(r.code==200&&!s_upload_active&&calls==1&&s_status.state==READ_PICO_TRANSFER_READY);
 reset();r=(httpd_req_t){.method=HTTP_GET};pthread_create(&http,NULL,request_thread,&r);wait_pending();editor_cancel_pending();pthread_join(http,NULL);assert(r.code==503&&calls==1&&!s_upload_active);
 reset();r=(httpd_req_t){.method=HTTP_GET};pthread_create(&http,NULL,request_thread,&r);wait_pending();pthread_join(http,NULL);assert(r.code==504&&s_upload_active);
 httpd_req_t busy={.method=HTTP_GET};editor_handler(&busy);assert(busy.code==409&&s_upload_active);read_pico_transfer_editor_poll();assert(!s_upload_active&&calls==1);
 reset();slow=true;r=(httpd_req_t){.method=HTTP_POST,.content_len=2,.query="op=banner",.body="{}"};pthread_create(&http,NULL,request_thread,&r);wait_pending();pthread_create(&ui,NULL,ui_thread,NULL);
 pthread_mutex_lock(&callback_lock);while(!entered)pthread_cond_wait(&callback_cv,&callback_lock);pthread_mutex_unlock(&callback_lock);
 pthread_join(http,NULL);assert(r.code==504&&s_upload_active);busy=(httpd_req_t){.method=HTTP_GET};editor_handler(&busy);assert(busy.code==409);
 pthread_mutex_lock(&callback_lock);resume_callback=true;pthread_cond_signal(&callback_cv);pthread_mutex_unlock(&callback_lock);pthread_join(ui,NULL);assert(!s_upload_active&&!commits);
 reset();stop_during_body=true;r=(httpd_req_t){.method=HTTP_POST,.content_len=2,.query="op=banner",.body="{}"};editor_handler(&r);assert(r.code==503&&!s_editor_pending&&!s_upload_active);
 reset();r=(httpd_req_t){.method=HTTP_POST,.content_len=2,.query="op=banner",.body="{}"};pthread_create(&http,NULL,request_thread,&r);wait_pending();read_pico_transfer_editor_poll();pthread_join(http,NULL);assert(r.code==200&&commits==1&&s_status.changed_count==1);
 assert(sem_created==sem_deleted);puts("PASS: production HTTP/UI bridge, normal commit, pending stop, pending/processing timeout, busy retention, stop-before-enqueue and balanced job/semaphore ownership");
}
