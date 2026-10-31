/* SPDX-License-Identifier: Apache-2.0
 * 不启动WiFi、不取凭据、不阻塞等NTP；成功后同步PMU。/ Never start WiFi, read credentials or block for NTP; update the PMU after success.
 */
#include "time_service.h"
#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "read_pico_transfer.h"
#include "cloud/cloud_sync.h"
#include "ota_online.h"
#include "sleep.h"
#include "nvs.h"
#include "operation_log.h"

static bool s_owned,s_loaded,s_pmu_pending;
static uint32_t s_last_sync;
static int64_t s_next_attempt,s_deadline,s_last_poll=-1000,s_idle_since=-1;
void app_time_service_stop(void){if(s_owned){esp_netif_sntp_deinit();s_owned=false;}}
void app_time_service_poll(int64_t now,bool idle){
    if(!idle){s_idle_since=-1;app_time_service_stop();return;}
    if(s_idle_since<0)s_idle_since=now;
    if(now-s_idle_since<10000)return;
    if(now-s_last_poll<1000)return;
    s_last_poll=now;
    if(s_pmu_pending){
        esp_err_t e=app_sync_time_to_pmu();operation_log_event(OPLOG_CLOCK,2,e,0,0,0);
        if(e==ESP_OK)s_pmu_pending=false;
    }
    read_pico_transfer_status_t network={0};read_pico_transfer_get_status(&network);
    if(network.mode!=READ_PICO_TRANSFER_MODE_STA||!network.network_ready||network.state==READ_PICO_TRANSFER_UPLOADING){app_time_service_stop();return;}
    cloud_status_t cloud={0};cloud_get_status(&cloud);
    if_update_status_t update={0};if_update_get_status(&update);
    if(cloud.busy||update.busy){app_time_service_stop();return;}
    if(s_owned){
        // 零等待轮询，UI/触摸不等待网络。/ Zero-wait polling never stalls input.
        if(esp_netif_sntp_sync_wait(0)==ESP_OK){
            app_time_service_stop();s_next_attempt=now+INT64_C(3600000);
            time_t wall=time(NULL);
            if(wall>=1704067200&&wall<=UINT32_MAX){
                s_last_sync=(uint32_t)wall;s_pmu_pending=true;
                nvs_handle_t h;if(nvs_open("if_clock",NVS_READWRITE,&h)==ESP_OK){
                    if(nvs_set_u32(h,"last_ntp",s_last_sync)==ESP_OK)(void)nvs_commit(h);
                    nvs_close(h);
                }
                esp_err_t e=app_sync_time_to_pmu();s_pmu_pending=e!=ESP_OK;
                operation_log_event(OPLOG_CLOCK,1,e,0,0,0);
                ESP_LOGI("if_time","network time received; PMU sync %d",(int)e);
            }else s_next_attempt=now+INT64_C(300000);
        }else if(now>=s_deadline)app_time_service_stop();
        return;
    }
    if(!s_loaded){
        nvs_handle_t h;if(nvs_open("if_clock",NVS_READONLY,&h)==ESP_OK){(void)nvs_get_u32(h,"last_ntp",&s_last_sync);nvs_close(h);}
        s_loaded=true;
    }
    time_t wall=time(NULL);
    if(s_last_sync>=1704067200&&wall>=(time_t)s_last_sync&&(uint64_t)wall-s_last_sync<3600)return;
    if(now<s_next_attempt)return;
    s_next_attempt=now+INT64_C(300000);
    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t e=esp_netif_sntp_init(&cfg);operation_log_event(OPLOG_CLOCK,0,e,0,0,0);
    if(e==ESP_OK){s_owned=true;s_deadline=now+15000;}
}
