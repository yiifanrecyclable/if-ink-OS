/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实网络由Python只读客户端执行；原生共用页面读取快照并输出用户命令。
 * English: Python owns real read-only networking; production UI consumes snapshots and emits user commands.
 */
#include "cloud_sync.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static cloud_status_t preview_cloud;
static char preview_account[129];
static cloud_entry_t preview_entries[CLOUD_ENTRY_MAX];
int simulator_cloud_command,simulator_cloud_index=-1,simulator_cloud_page;
static bool initializing;
void cloud_config_load(cloud_config_t *c){memset(c,0,sizeof(*c));snprintf(c->folder,sizeof(c->folder),"%s",preview_cloud.folder[0]?preview_cloud.folder:CLOUD_DEFAULT_FOLDER);snprintf(c->account,sizeof(c->account),"%s",preview_account);if(preview_cloud.configured)strcpy(c->password,"*");}
esp_err_t cloud_config_load_checked(cloud_config_t *c){if(!c)return ESP_ERR_INVALID_ARG;cloud_config_load(c);return ESP_OK;}
bool cloud_config_valid(const cloud_config_t *c,bool secret){
    return c&&memchr(c->folder,0,sizeof(c->folder))&&memchr(c->account,0,sizeof(c->account))&&memchr(c->password,0,sizeof(c->password))&&
        cloud_relative_path(c->folder)&&cloud_utf8_valid(c->account,strlen(c->account))&&!strpbrk(c->account,":\r\n")&&
        !strpbrk(c->password,"\r\n")&&(!secret||(c->account[0]&&c->password[0]));
}
esp_err_t cloud_config_save(const cloud_config_t *c){(void)c;simulator_cloud_command=5;return ESP_ERR_NOT_SUPPORTED;}
static cloud_config_t backup_input,backup_output;
static bool backup_read,backup_restored;
static esp_err_t backup_input_error;
// 仅在用户操作备份时，通过私有stdin管道接收密码；不落入快照或参数。/ Receive secrets only for backup via private stdin, never snapshots or argv.
esp_err_t cloud_config_backup_export(cloud_config_t *out){
    if(preview_cloud.busy)return ESP_ERR_INVALID_STATE;
    if(!backup_read){
        backup_read=true;strcpy(backup_input.folder,CLOUD_DEFAULT_FOLDER);
        if(getenv("PICO_SIM_CLOUD_BACKUP_INPUT")){
            char text[2049];size_t n=fread(text,1,2048,stdin);
            text[n]=0;
            cJSON *j=!ferror(stdin)&&fgetc(stdin)==EOF?cJSON_ParseWithLengthOpts(text,n+1,NULL,1):NULL;
            const cJSON *f=cJSON_GetObjectItemCaseSensitive(j,"folder"),*a=cJSON_GetObjectItemCaseSensitive(j,"account"),*p=cJSON_GetObjectItemCaseSensitive(j,"password");
            if(cJSON_IsString(f)&&strlen(f->valuestring)<sizeof(backup_input.folder)&&cJSON_IsString(a)&&strlen(a->valuestring)<sizeof(backup_input.account)&&cJSON_IsString(p)&&strlen(p->valuestring)<sizeof(backup_input.password)){
                strcpy(backup_input.folder,f->valuestring);strcpy(backup_input.account,a->valuestring);strcpy(backup_input.password,p->valuestring);
            }else backup_input_error=ESP_ERR_INVALID_RESPONSE;
            cJSON_Delete(j);memset(text,0,sizeof(text));
        }else if(preview_cloud.configured)backup_input_error=ESP_ERR_NOT_SUPPORTED;
    }
    if(backup_input_error)return backup_input_error;
    *out=backup_input;return cloud_config_valid(out,false)?ESP_OK:ESP_ERR_INVALID_RESPONSE;
}
esp_err_t cloud_config_backup_restore(const cloud_config_t *c){
    if(preview_cloud.busy)return ESP_ERR_INVALID_STATE;
    if(!cloud_config_valid(c,false))return ESP_ERR_INVALID_ARG;
    backup_output=*c;backup_restored=true;return ESP_OK;
}
const cloud_config_t *simulator_cloud_backup_restored(void){return backup_restored?&backup_output:NULL;}
bool simulator_cloud_backup_used(void){return backup_read||backup_restored;}
esp_err_t cloud_config_forget(void){simulator_cloud_command=6;return ESP_ERR_NOT_SUPPORTED;}
void cloud_get_status(cloud_status_t *out){*out=preview_cloud;}
bool cloud_get_entry(size_t i,cloud_entry_t *out){if(i>=preview_cloud.count)return false;*out=preview_entries[i];return true;}
esp_err_t cloud_refresh(void){if(!initializing)simulator_cloud_command=1;return ESP_OK;}
esp_err_t cloud_toggle(size_t i){if(i<preview_cloud.count&&preview_entries[i].selectable){preview_entries[i].selected=!preview_entries[i].selected;simulator_cloud_command=2;simulator_cloud_index=i;return ESP_OK;}return ESP_ERR_INVALID_ARG;}
esp_err_t cloud_download_selected(void){simulator_cloud_command=3;return ESP_OK;}
void cloud_cancel(void){if(!initializing)simulator_cloud_command=4;}
void cloud_close(void){}
#define app_cloud simulator_descriptor_cloud
#include "../../main/apps/app_cloud.c"
#include "replay.h"
extern int simulator_route;
static const char *string(const cJSON *o,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static uint64_t number(const cJSON *o,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsNumber(v)&&v->valuedouble>=0?(uint64_t)v->valuedouble:0;}
static bool boolean(const cJSON *o,const char *key){return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,key));}
void simulator_cloud_load(const char *file){
    preview_account[0]=0;
    memset(&preview_cloud,0,sizeof(preview_cloud));memset(preview_entries,0,sizeof(preview_entries));snprintf(preview_cloud.folder,sizeof(preview_cloud.folder),"%s",CLOUD_DEFAULT_FOLDER);
    FILE *f=fopen(file,"rb");char *data=malloc(256*1024+1);cJSON *snapshot=NULL;if(f&&data){size_t n=fread(data,1,256*1024,f);data[n]=0;snapshot=cJSON_ParseWithLengthOpts(data,n+1,NULL,1);}if(f)fclose(f);free(data);
    if(snapshot){snprintf(preview_account,sizeof(preview_account),"%s",string(snapshot,"account"));preview_cloud.revision=number(snapshot,"revision");preview_cloud.busy=boolean(snapshot,"busy");preview_cloud.configured=boolean(snapshot,"configured");preview_cloud.cancel_requested=boolean(snapshot,"cancel_requested");preview_cloud.done=number(snapshot,"done");preview_cloud.total=number(snapshot,"total");preview_cloud.failed=number(snapshot,"failed");preview_cloud.bytes_done=number(snapshot,"bytes_done");preview_cloud.bytes_total=number(snapshot,"bytes_total");snprintf(preview_cloud.folder,sizeof(preview_cloud.folder),"%s",string(snapshot,"folder"));snprintf(preview_cloud.message,sizeof(preview_cloud.message),"%s",string(snapshot,"message"));snprintf(preview_cloud.indicator,sizeof(preview_cloud.indicator),"%s",string(snapshot,"indicator"));const cJSON *entries=cJSON_GetObjectItemCaseSensitive(snapshot,"entries");
        if(cJSON_IsArray(entries))for(const cJSON *i=entries->child;i&&preview_cloud.count<CLOUD_ENTRY_MAX;i=i->next){cloud_entry_t *out=&preview_entries[preview_cloud.count++];snprintf(out->name,sizeof(out->name),"%s",string(i,"name"));snprintf(out->filename,sizeof(out->filename),"%s",string(i,"filename"));snprintf(out->error,sizeof(out->error),"%s",string(i,"error"));out->bytes=number(i,"bytes");out->date_ms=number(i,"date_ms");out->selected=boolean(i,"selected");out->selectable=boolean(i,"selectable");out->date_fallback=boolean(i,"date_fallback");}
    }else strcpy(preview_cloud.message,"请配置坚果云账号和应用密码");cJSON_Delete(snapshot);
}
void simulator_cloud(uint8_t *fb,const char *file,int page,const char *events){
    simulator_cloud_load(file);
    initializing=true;app_ctx_t ctx={.fb=fb,.now_ms=1000};enter(&ctx);initializing=false;simulator_cloud_command=0;simulator_cloud_index=-1;s_cloud_page=page<0?0:(unsigned)page;
    simulator_replay(&ctx,gesture,key,events);render(&ctx,fb);simulator_cloud_page=s_cloud_page;if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
}
