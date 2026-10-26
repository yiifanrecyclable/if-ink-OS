/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立HTTPS更新任务，完整校验前绝不切换启动槽；不依赖TF卡、不写当前运行槽。
 * English: Independent HTTPS updater; never switch boot slots before full validation, use TF storage or write the running slot.
 */
#include "ui/ui_i18n.h"

#include "ota_online.h"
#include "time_service.h"
#include "ota_online_policy.h"
#include "cloud_sync.h"
#include "usb_storage.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "read_pico_transfer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static SemaphoreHandle_t s_lock,s_finished;
static TaskHandle_t s_task;
static bool s_owned_wifi;
static atomic_bool s_cancel;
static if_update_status_t s_status;
static if_update_candidate_t s_candidate;
static void phase(if_update_state_t state,const char *message){
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.state=state;
    ui_snprintf(s_status.message,sizeof(s_status.message),"%s",message);++s_status.revision;
    xSemaphoreGive(s_lock);
}
static void progress(uint32_t received,uint32_t total){
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.received=received;s_status.total=total;
    ++s_status.revision;xSemaphoreGive(s_lock);
}
void if_update_get_status(if_update_status_t *out){
    if(!out)return;
    if(s_lock)xSemaphoreTake(s_lock,portMAX_DELAY);
    *out=s_status;
    if(!out->current[0])ui_snprintf(out->current,sizeof(out->current),"%s",esp_app_get_description()->version);
    if(s_lock)xSemaphoreGive(s_lock);
}
static bool init(void){
    if(!s_lock)s_lock=xSemaphoreCreateMutex();
    if(!s_finished)s_finished=xSemaphoreCreateBinary();
    return s_lock&&s_finished;
}
typedef struct {char location[2048];bool invalid,encoded;} http_headers_t;
static esp_err_t header_event(esp_http_client_event_t *event){
    http_headers_t *h=event->user_data;
    if(event->event_id!=HTTP_EVENT_ON_HEADER||!h)return ESP_OK;
    if(!strcasecmp(event->header_key,"Location")){
        if(strlen(event->header_value)>=sizeof(h->location))h->invalid=true;
        else strcpy(h->location,event->header_value);
    }
    if(!strcasecmp(event->header_key,"Content-Encoding")&&strcasecmp(event->header_value,"identity"))h->encoded=true;
    return ESP_OK;
}
static void http_dispose(esp_http_client_handle_t http){if(http){esp_http_client_close(http);esp_http_client_cleanup(http);}}
static esp_err_t http_open(const char *initial,bool asset,uint32_t limit,esp_http_client_handle_t *out){
    *out=NULL;char *url=malloc(2048);http_headers_t *headers=calloc(1,sizeof(*headers));
    if(!url||!headers){free(url);free(headers);return ESP_ERR_NO_MEM;}
    ui_snprintf(url,2048,"%s",initial);esp_err_t err=ESP_FAIL;
    for(int redirects=0;redirects<=3&&!atomic_load(&s_cancel);++redirects){
        memset(headers,0,sizeof(*headers));
        esp_http_client_config_t config={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
            .disable_auto_redirect=true,.timeout_ms=15000,.buffer_size=4096,.buffer_size_tx=1024,
            .event_handler=header_event,.user_data=headers};
        esp_http_client_handle_t http=esp_http_client_init(&config);
        if(!http){err=ESP_ERR_NO_MEM;break;}
        esp_http_client_set_header(http,"User-Agent","if-ink-OS-updater/1");
        esp_http_client_set_header(http,"Accept-Encoding","identity");
        esp_http_client_set_header(http,"Accept",asset&&!strstr(initial,"/git/blobs/")?"application/octet-stream":"application/vnd.github.raw+json");
        esp_http_client_set_header(http,"Cache-Control","no-cache");
        err=esp_http_client_open(http,0);
        int64_t length=err==ESP_OK?esp_http_client_fetch_headers(http):-1;
        int code=esp_http_client_get_status_code(http);
        if(err==ESP_OK&&length>=0&&!headers->invalid){
            if(code==200&&!headers->encoded&&(uint64_t)length<=limit){
                // 回调所用内存需活到关闭HTTP连接。读取阶段不再接收头事件。
                // Header userdata must outlive the handle; remove it before releasing the header buffer.
                esp_http_client_set_user_data(http,NULL);
                esp_http_client_set_timeout_ms(http,5000);*out=http;break;
            }
            if(asset&&(code==301||code==302||code==303||code==307||code==308)&&if_update_redirect_allowed(headers->location)){
                strcpy(url,headers->location);http_dispose(http);continue;
            }
            if(code==403||code==429)phase(IF_UPDATE_FAILED,ui_message(UI_MSG("GitHub访问受限，请稍后重试")));
            err=ESP_ERR_INVALID_RESPONSE;
        }else if(err==ESP_OK)err=ESP_FAIL;
        http_dispose(http);break;
    }
    if(!*out&&err==ESP_OK)err=ESP_FAIL;
    free(headers);free(url);return *out?ESP_OK:err;
}
static esp_err_t check_manifest(void){
    esp_http_client_handle_t http=NULL;esp_err_t err=http_open(IF_OTA_FEED_URL,false,IF_OTA_MANIFEST_LIMIT,&http);
    char *json=err==ESP_OK?malloc(IF_OTA_MANIFEST_LIMIT+1):NULL;
    if(err==ESP_OK&&!json)err=ESP_ERR_NO_MEM;
    size_t total=0;int64_t deadline=esp_timer_get_time()+30000000;
    while(err==ESP_OK&&!atomic_load(&s_cancel)){
        if(esp_timer_get_time()>deadline){err=ESP_ERR_TIMEOUT;break;}
        int n=esp_http_client_read(http,json+total,(int)(IF_OTA_MANIFEST_LIMIT-total));
        if(n<0){err=ESP_FAIL;break;}
        if(!n){if(!esp_http_client_is_complete_data_received(http))err=ESP_ERR_INVALID_SIZE;break;}
        total+=(size_t)n;
        if(total==IF_OTA_MANIFEST_LIMIT){err=ESP_ERR_INVALID_SIZE;break;}
    }
    http_dispose(http);
    if(err==ESP_OK&&!atomic_load(&s_cancel)){
        json[total]=0;
        if(!if_update_parse_manifest(json,total,&s_candidate))err=ESP_ERR_INVALID_RESPONSE;
        else{
            xSemaphoreTake(s_lock,portMAX_DELAY);ui_snprintf(s_status.version,sizeof(s_status.version),"%s",s_candidate.version);
            s_status.total=s_candidate.size;xSemaphoreGive(s_lock);
            phase(s_candidate.available&&if_update_newer(s_candidate.version,esp_app_get_description()->version)?IF_UPDATE_AVAILABLE:IF_UPDATE_CURRENT,
                  !s_candidate.available?ui_message(UI_MSG("暂未发布在线升级版本")):if_update_newer(s_candidate.version,esp_app_get_description()->version)?ui_message(UI_MSG("发现新版本，请确认后安装")):ui_message(UI_MSG("当前无需升级")));
        }
    }
    free(json);return err;
}
static bool valid_layout(const esp_partition_t *target){
    const esp_partition_t *running=esp_ota_get_running_partition();
    const esp_partition_t *data=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,NULL);
    return running&&target&&data&&running->address!=target->address&&
        running->size==IF_OTA_MAX_BYTES&&target->size==IF_OTA_MAX_BYTES&&
        ((running->address==0x10000&&target->address==0x910000)||(running->address==0x910000&&target->address==0x10000))&&
        data->address==0xd10000&&data->size==0x2000;
}
static bool valid_prefix(const uint8_t *bytes){
    esp_image_header_t image;esp_image_segment_header_t segment;esp_app_desc_t desc;
    memcpy(&image,bytes,sizeof(image));memcpy(&segment,bytes+sizeof(image),sizeof(segment));
    memcpy(&desc,bytes+sizeof(image)+sizeof(segment),sizeof(desc));
    return image.magic==ESP_IMAGE_HEADER_MAGIC&&image.chip_id==ESP_CHIP_ID_ESP32S3&&
        segment.data_len>=sizeof(desc)&&desc.magic_word==ESP_APP_DESC_MAGIC_WORD&&
        memchr(desc.project_name,0,sizeof(desc.project_name))&&memchr(desc.version,0,sizeof(desc.version))&&
        !strcmp(desc.project_name,IF_OTA_PROJECT)&&!strcmp(desc.version,s_candidate.version);
}
static esp_err_t install_image(void){
    const esp_partition_t *target=esp_ota_get_next_update_partition(NULL);
    if(!valid_layout(target)){
        phase(IF_UPDATE_FAILED,ui_message(UI_MSG("固件分区不兼容，请使用电脑刷机")));return ESP_ERR_INVALID_STATE;
    }
    esp_http_client_handle_t http=NULL;esp_ota_handle_t handle=0;bool active=false;
    psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
    // 优先按内容编号直读仓库，绕开不稳定的Release CDN；连接失败时只换源一次。
    // Prefer repository content by immutable ID over the Release CDN; switch once if opening it fails.
    esp_err_t err=http_open(s_candidate.blob_url,true,s_candidate.size,&http);
    if(err!=ESP_OK&&!atomic_load(&s_cancel)){
        phase(IF_UPDATE_DOWNLOADING,ui_message(UI_MSG("正在切换下载线路…")));
        err=http_open(s_candidate.asset_url,true,s_candidate.size,&http);
        if(err==ESP_OK)phase(IF_UPDATE_DOWNLOADING,ui_message(UI_MSG("正在下载并安装…")));
    }
    uint8_t *buffer=err==ESP_OK?malloc(8192):NULL;
    if(err==ESP_OK&&!buffer)err=ESP_ERR_NO_MEM;
    const size_t prefix=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t);
    size_t total=0;int64_t deadline=esp_timer_get_time()+600000000;
    // 在擦写前先收齐并验证芯片、项目和版本头。/ Collect and verify chip/project/version before erasing flash.
    while(err==ESP_OK&&total<prefix&&!atomic_load(&s_cancel)){
        if(esp_timer_get_time()>deadline){err=ESP_ERR_TIMEOUT;break;}
        int n=esp_http_client_read(http,(char*)buffer+total,(int)(prefix-total));
        if(n<=0){err=ESP_ERR_INVALID_SIZE;break;}total+=(size_t)n;
    }
    if(err==ESP_OK&&!atomic_load(&s_cancel)){
        if(!valid_prefix(buffer)){phase(IF_UPDATE_FAILED,ui_message(UI_MSG("升级包身份不匹配，已停止安装")));err=ESP_ERR_INVALID_RESPONSE;}
        else if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_setup(&hash,PSA_ALG_SHA_256)!=PSA_SUCCESS)err=ESP_FAIL;
        else{
            err=esp_ota_begin(target,s_candidate.size,&handle);active=err==ESP_OK;
            if(err==ESP_OK&&psa_hash_update(&hash,buffer,total)!=PSA_SUCCESS)err=ESP_FAIL;
            if(err==ESP_OK)err=esp_ota_write(handle,buffer,total);
        }
    }
    while(err==ESP_OK&&!atomic_load(&s_cancel)){
        if(esp_timer_get_time()>deadline){err=ESP_ERR_TIMEOUT;break;}
        int n=esp_http_client_read(http,(char*)buffer,8192);
        if(n<0){err=ESP_FAIL;break;}
        if(!n){if(!esp_http_client_is_complete_data_received(http))err=ESP_ERR_INVALID_SIZE;break;}
        if((size_t)n>s_candidate.size-total){err=ESP_ERR_INVALID_SIZE;break;}
        if(psa_hash_update(&hash,buffer,(size_t)n)!=PSA_SUCCESS){err=ESP_FAIL;break;}
        err=esp_ota_write(handle,buffer,(size_t)n);total+=(size_t)n;progress((uint32_t)total,s_candidate.size);
    }
    http_dispose(http);free(buffer);
    if(err==ESP_OK&&!atomic_load(&s_cancel)){
        phase(IF_UPDATE_VERIFYING,ui_message(UI_MSG("正在校验固件完整性…")));
        uint8_t digest[32];size_t n=0;
        if(total!=s_candidate.size||psa_hash_finish(&hash,digest,sizeof(digest),&n)!=PSA_SUCCESS||n!=32||memcmp(digest,s_candidate.sha256,32)){
            phase(IF_UPDATE_FAILED,ui_message(UI_MSG("下载校验失败，请重试")));err=ESP_ERR_INVALID_RESPONSE;
        }
        if(err==ESP_OK){err=esp_ota_end(handle);active=false;}
        if(err==ESP_OK&&!atomic_load(&s_cancel)){
            err=esp_ota_set_boot_partition(target);
            if(err==ESP_OK)phase(IF_UPDATE_READY,ui_message(UI_MSG("安装完成，即将重启")));
        }
    }
    if(active)esp_ota_abort(handle);
    psa_hash_abort(&hash);return err;
}
static void worker(void *argument){
    bool install=argument!=NULL;esp_err_t err=ESP_OK;read_pico_transfer_status_t network;
    for(int i=0;i<200;++i){
        read_pico_transfer_get_status(&network);
        if(network.network_ready||atomic_load(&s_cancel))break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if(!network.network_ready)err=ESP_ERR_TIMEOUT;
    if(err==ESP_OK&&!atomic_load(&s_cancel)&&time(NULL)<1704067200){uint32_t utc;err=read_pico_transfer_sync_time_online(&utc);}
    if(err==ESP_OK&&!atomic_load(&s_cancel))err=install?install_image():check_manifest();
    if_update_status_t status;if_update_get_status(&status);
    if(atomic_load(&s_cancel)&&status.state!=IF_UPDATE_READY)phase(IF_UPDATE_CANCELLED,ui_message(UI_MSG("操作已取消")));
    else if(err!=ESP_OK&&status.state!=IF_UPDATE_FAILED)
        phase(IF_UPDATE_FAILED,!network.network_ready?ui_message(UI_MSG("WiFi未连接，请检查网络设置")):err==ESP_ERR_NO_MEM?ui_message(UI_MSG("内存不足，请稍后重试")):install?ui_message(UI_MSG("下载或安装失败，当前版本已保留")):ui_message(UI_MSG("检查失败，请检查网络与系统时间")));
    xSemaphoreGive(s_finished);vTaskDelete(NULL);
}
void if_update_poll(void){
    if(s_task&&xSemaphoreTake(s_finished,0)==pdTRUE){
        s_task=NULL;
        if(s_owned_wifi){read_pico_transfer_stop();s_owned_wifi=false;}
        xSemaphoreTake(s_lock,portMAX_DELAY);s_status.busy=false;++s_status.revision;xSemaphoreGive(s_lock);
    }
    if(s_task)read_pico_transfer_service_poll();
}
static esp_err_t start(bool install){
    app_time_service_stop();
    if(!init())return ESP_ERR_NO_MEM;
    if_update_poll();if(s_task)return ESP_ERR_INVALID_STATE;
    cloud_status_t cloud;cloud_get_status(&cloud);
    if(cloud.busy||usb_storage_active()){phase(IF_UPDATE_FAILED,ui_message(UI_MSG("请先结束同步或USB连接")));return ESP_ERR_INVALID_STATE;}
    if(strcmp(esp_app_get_description()->project_name,IF_OTA_PROJECT)){phase(IF_UPDATE_FAILED,ui_message(UI_MSG("当前系统不支持此更新源")));return ESP_ERR_INVALID_STATE;}
    if(install&&(s_status.state!=IF_UPDATE_AVAILABLE||!s_candidate.available||!if_update_newer(s_candidate.version,esp_app_get_description()->version)))return ESP_ERR_INVALID_STATE;
    read_pico_transfer_status_t network;read_pico_transfer_get_status(&network);
    if(network.state==READ_PICO_TRANSFER_STOPPED||(network.state==READ_PICO_TRANSFER_ERROR&&!network.network_ready)){
        if(network.state==READ_PICO_TRANSFER_ERROR&&!read_pico_transfer_try_stop_if_idle()){phase(IF_UPDATE_FAILED,ui_message(UI_MSG("请先结束传输后重试")));return ESP_ERR_INVALID_STATE;}
        read_pico_transfer_cfg_t config={.mode=READ_PICO_TRANSFER_MODE_STA,.network_only=true};
        esp_err_t err=read_pico_transfer_start(&config);
        if(err!=ESP_OK){read_pico_transfer_get_status(&network);phase(IF_UPDATE_FAILED,err==ESP_ERR_NOT_FOUND?(network.wifi_configured?ui_message(UI_MSG("附近未找到已保存的WiFi")):ui_message(UI_MSG("请先在设置中配置WiFi"))):ui_message(UI_MSG("无法连接WiFi，请重试")));return err;}
        s_owned_wifi=true;
    }else if(network.mode!=READ_PICO_TRANSFER_MODE_STA||!network.network_ready||network.state!=READ_PICO_TRANSFER_READY){
        phase(IF_UPDATE_FAILED,ui_message(UI_MSG("请先结束传输并连接WiFi")));return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_cancel,false);
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.busy=true;s_status.received=0;
    if(!install){memset(&s_candidate,0,sizeof(s_candidate));s_status.version[0]=0;s_status.total=0;}
    xSemaphoreGive(s_lock);
    phase(install?IF_UPDATE_DOWNLOADING:IF_UPDATE_CHECKING,install?ui_message(UI_MSG("正在下载并安装…")):ui_message(UI_MSG("正在检查更新…")));
    if(xTaskCreate(worker,"if_ota",16384,(void*)(uintptr_t)install,4,&s_task)!=pdPASS){
        if(s_owned_wifi){read_pico_transfer_stop();s_owned_wifi=false;}
        xSemaphoreTake(s_lock,portMAX_DELAY);s_status.busy=false;xSemaphoreGive(s_lock);
        phase(IF_UPDATE_FAILED,ui_message(UI_MSG("无法启动升级任务，请重试")));return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t if_update_check(void){return start(false);}
esp_err_t if_update_install(void){return start(true);}
void if_update_close(void){
    atomic_store(&s_cancel,true);
    if(s_task){xSemaphoreTake(s_finished,portMAX_DELAY);s_task=NULL;}
    if(s_owned_wifi){read_pico_transfer_stop();s_owned_wifi=false;}
    if(s_lock){xSemaphoreTake(s_lock,portMAX_DELAY);s_status.busy=false;xSemaphoreGive(s_lock);}
}
