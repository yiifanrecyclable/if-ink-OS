/* SPDX-License-Identifier: Apache-2.0
 * 在已启动但尚未连接的STA中匹配网络历史；不另启射频，不修改已连接会话。
 * Match saved profiles on a started, unassociated STA without another radio lifecycle or changing an online session.
 */
#pragma once
#include "transfer_policy.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_err_t scan_saved_candidates(const transfer_wifi_profiles_t *profiles,transfer_wifi_candidates_t *out){
    memset(out,0,sizeof(*out));
    if(!profiles->count)return ESP_ERR_NOT_FOUND;
    // Hosted启动后稍候，最多重试一次扫描；没有候选时不盲连上次SSID。/ Let hosted startup settle; retry the scan once and never blindly connect to the last SSID.
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_err_t err=esp_wifi_scan_start(NULL,true);
    if(err!=ESP_OK){(void)esp_wifi_scan_stop();(void)esp_wifi_clear_ap_list();vTaskDelay(pdMS_TO_TICKS(180));err=esp_wifi_scan_start(NULL,true);}
    uint16_t found=0;
    wifi_ap_record_t *records=NULL;
    if(err==ESP_OK)err=esp_wifi_scan_get_ap_num(&found);
    if(err==ESP_OK&&found){
        // 自动匹配读取全部结果，不受界面仅显示16项的限制。/ Match every result rather than only the UI's sixteen strongest APs.
        records=heap_caps_calloc(found,sizeof(*records),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!records)records=heap_caps_calloc(found,sizeof(*records),MALLOC_CAP_8BIT);
        if(!records)err=ESP_ERR_NO_MEM;
        else{
            err=esp_wifi_scan_get_ap_records(&found,records);
            if(err==ESP_OK)for(unsigned i=0;i<found;++i){
                const wifi_ap_record_t *ap=&records[i];
                read_pico_transfer_network_t item={.rssi=ap->rssi,.requires_password=ap->authmode!=WIFI_AUTH_OPEN};
                memcpy(item.ssid,ap->ssid,32);
                item.supported=ap->authmode==WIFI_AUTH_OPEN||ap->authmode==WIFI_AUTH_WPA_PSK||ap->authmode==WIFI_AUTH_WPA2_PSK||ap->authmode==WIFI_AUTH_WPA_WPA2_PSK||ap->authmode==WIFI_AUTH_WPA3_PSK||ap->authmode==WIFI_AUTH_WPA2_WPA3_PSK;
                profiles_offer_candidate(profiles,out,&item);
            }
        }
    }
    heap_caps_free(records);(void)esp_wifi_clear_ap_list();
    if(err!=ESP_OK){memset(out,0,sizeof(*out));return err;}
    return out->count?ESP_OK:ESP_ERR_NOT_FOUND;
}
