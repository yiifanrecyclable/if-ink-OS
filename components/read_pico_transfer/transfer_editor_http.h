/* SPDX-License-Identifier: Apache-2.0
 * 中文：单任务HTTP与UI任务之间的双引用请求；超时不提前释放正在使用的资源。
 * English: Two-reference HTTP/UI jobs; a timeout never releases resources still in use.
 */
#define EDITOR_BODY_MAX 3072
#define EDITOR_WAIT_MS 15000
struct editor_job {
    SemaphoreHandle_t done;
    unsigned refs;
    bool cancelled,finished;
    char operation[16],body[EDITOR_BODY_MAX];
    read_pico_transfer_editor_reply_t reply;
};
static struct editor_job *s_editor_pending,*s_editor_active;
static bool editor_cancelled(void *context){
    struct editor_job *job=context;
    portENTER_CRITICAL(&s_lock);bool value=job->cancelled||s_stopping;portEXIT_CRITICAL(&s_lock);
    return value;
}
static void editor_release(struct editor_job *job){
    portENTER_CRITICAL(&s_lock);
    bool destroy=--job->refs==0;
    if(destroy&&s_editor_active==job){
        s_editor_active=NULL;s_upload_active=false;
    }
    portEXIT_CRITICAL(&s_lock);
    if(destroy){free(job->reply.data);vSemaphoreDelete(job->done);free(job);}
}
static void editor_cancel_pending(void){
    portENTER_CRITICAL(&s_lock);
    struct editor_job *job=s_editor_pending;s_editor_pending=NULL;
    if(job){job->cancelled=true;job->finished=true;job->reply.status=503;}
    if(s_editor_active)s_editor_active->cancelled=true;
    portEXIT_CRITICAL(&s_lock);
    if(job){xSemaphoreGive(job->done);editor_release(job);}
}
void read_pico_transfer_editor_poll(void){
    portENTER_CRITICAL(&s_lock);struct editor_job *job=s_editor_pending;s_editor_pending=NULL;portEXIT_CRITICAL(&s_lock);
    if(!job)return;
    read_pico_transfer_editor_reply_t reply={.status=503};
    if(!editor_cancelled(job)&&s_cfg.editor_cb){
        if(s_cfg.editor_cb(job->operation,job->body,&reply,editor_cancelled,job)!=ESP_OK&&!reply.status)reply.status=500;
    }
    portENTER_CRITICAL(&s_lock);
    job->reply=reply;job->finished=true;
    if(reply.changed){++s_status.changed_count;++s_status.done_count;}
    portEXIT_CRITICAL(&s_lock);
    xSemaphoreGive(job->done);editor_release(job);
}
static esp_err_t editor_handler(httpd_req_t *req){
    if(!management_request(req))return wifi_response(req,"403 Forbidden","{\"error\":\"请从设备显示的地址打开管理页面\"}");
    if(s_cfg.is_flash||!s_cfg.editor_cb)return wifi_response(req,"503 Service Unavailable","{\"error\":\"TF 卡或编辑服务未就绪\"}");
    if(req->content_len>=EDITOR_BODY_MAX)return respond_error(req,400);
    struct editor_job *job=calloc(1,sizeof(*job));
    if(!job)return respond_error(req,500);
    job->done=xSemaphoreCreateBinary();job->refs=1;job->reply.status=500;
    if(!job->done){free(job);return respond_error(req,500);}
    strlcpy(job->operation,"get",sizeof(job->operation));
    portENTER_CRITICAL(&s_lock);
    bool admitted=admission_begin(s_stopping,&s_upload_active);
    if(admitted)s_editor_active=job;
    portEXIT_CRITICAL(&s_lock);
    if(!admitted){vSemaphoreDelete(job->done);free(job);return wifi_response(req,"409 Conflict","{\"error\":\"设备正忙，请稍后重试\"}");}
    if(req->method==HTTP_POST){
        char query[64]={0};
        if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
            httpd_query_key_value(query,"op",job->operation,sizeof(job->operation))!=ESP_OK||
            (strcmp(job->operation,"preview")&&strcmp(job->operation,"banner")&&strcmp(job->operation,"wallpaper")&&strcmp(job->operation,"font-match")&&strcmp(job->operation,"font-release"))||
            !req->content_len||!recv_small_body(req,job->body,sizeof(job->body))){editor_release(job);return respond_error(req,400);}
    }
    portENTER_CRITICAL(&s_lock);bool queued=!s_stopping&&!job->cancelled;
    if(queued){++job->refs;s_editor_pending=job;}portEXIT_CRITICAL(&s_lock);
    if(!queued){editor_release(job);return wifi_response(req,"503 Service Unavailable","{\"error\":\"编辑服务正在关闭，请重新连接\"}");}
    if(xSemaphoreTake(job->done,pdMS_TO_TICKS(EDITOR_WAIT_MS))!=pdTRUE){
        portENTER_CRITICAL(&s_lock);job->cancelled=true;portEXIT_CRITICAL(&s_lock);
        editor_release(job);
        return wifi_response(req,"504 Gateway Timeout","{\"error\":\"设备尚未确认结果，请重新读取设置核实\",\"uncertain\":true}");
    }
    char status[40];int code=job->reply.status?job->reply.status:500;
    snprintf(status,sizeof(status),"%d %s",code,code==200?"OK":code==409?"Conflict":code==422?"Unprocessable Content":code==503?"Service Unavailable":"Error");
    httpd_resp_set_status(req,status);
    httpd_resp_set_type(req,job->reply.pixels?"application/octet-stream":"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    esp_err_t result=job->reply.data?httpd_resp_send(req,(char*)job->reply.data,job->reply.size):
        httpd_resp_sendstr(req,"{\"error\":\"操作未完成，请重试\"}");
    editor_release(job);return result;
}
