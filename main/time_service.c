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

static bool s_owned;
static int64_t s_next_attempt,s_deadline,s_last_poll=-1000,s_idle_since=-1;
void app_time_service_stop(void){if(s_owned){esp_netif_sntp_deinit();s_owned=false;}}
void app_time_service_poll(int64_t now,bool idle){
    if(!idle){s_idle_since=-1;app_time_service_stop();return;}
    if(s_idle_since<0)s_idle_since=now;
    if(now-s_idle_since<10000)return;
    if(now-s_last_poll<1000)return;
    s_last_poll=now;
    read_pico_transfer_status_t network={0};read_pico_transfer_get_status(&network);
    if(network.mode!=READ_PICO_TRANSFER_MODE_STA||!network.network_ready||network.state==READ_PICO_TRANSFER_UPLOADING){app_time_service_stop();return;}
    cloud_status_t cloud={0};cloud_get_status(&cloud);
    if_update_status_t update={0};if_update_get_status(&update);
    if(cloud.busy||update.busy){app_time_service_stop();return;}
    if(s_owned){
        // 零等待轮询，UI/触摸不等待网络。/ Zero-wait polling never stalls input.
        if(esp_netif_sntp_sync_wait(0)==ESP_OK){
            app_time_service_stop();s_next_attempt=now+INT64_C(3600000);
            if(time(NULL)>=1704067200){esp_err_t e=app_sync_time_to_pmu();ESP_LOGI("if_time","network time received; PMU sync %d",(int)e);}
        }else if(now>=s_deadline)app_time_service_stop();
        return;
    }
    if(now<s_next_attempt)return;
    s_next_attempt=now+INT64_C(300000);
    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if(esp_netif_sntp_init(&cfg)==ESP_OK){s_owned=true;s_deadline=now+15000;}
}
