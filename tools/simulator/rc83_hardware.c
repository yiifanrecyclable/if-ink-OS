/* SPDX-License-Identifier: Apache-2.0
 * 中文：rc83升级能力的显式演示替身，不安装固件。
 * English: Explicit rc83 update demo; no firmware installation.
 */
#include <stdio.h>
#include <string.h>
#include "ota_update.h"
#include "esp_app_desc.h"
#include "settings.h"
#include "read_pico_transfer.h"
const esp_app_desc_t *esp_app_get_description(void){
#ifdef SHELF_PERF_HOST_TEST
    static const esp_app_desc_t desc={.version="0.2.2-beta-r1t1",.project_name="Read_Pico_IF"};
#else
    static const esp_app_desc_t desc={.version=PICO_SIM_FIRMWARE_VERSION,.project_name="Read_Pico_IF"};
#endif
    return &desc;
}
esp_err_t pico_ota_inspect(const char *path,pico_ota_info_t *info){memset(info,0,sizeof(*info));snprintf(info->current_version,sizeof(info->current_version),"%s",esp_app_get_description()->version);snprintf(info->message,sizeof(info->message),"模拟器不安装固件，请在 Pico 上检查升级包");return ESP_ERR_NOT_SUPPORTED;}
esp_err_t pico_ota_install(const char *path,char *message,size_t size){snprintf(message,size,"请在 Pico 上安装 IF Edition 升级包");return ESP_ERR_NOT_SUPPORTED;}
void pico_ota_confirm_running(void){}
esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out){memset(out,0,sizeof(*out));return ESP_OK;}
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *in){return in&&in->configured<=1&&memchr(in->ssid,0,sizeof(in->ssid))&&memchr(in->password,0,sizeof(in->password));}
esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *in){return read_pico_transfer_wifi_backup_valid(in)?ESP_OK:ESP_ERR_INVALID_ARG;}

// 模拟器只演示页面与状态，不联网检查或写入固件。/ Preview UI states without network checks or firmware writes.
#include "ota_online.h"
#include <stdlib.h>
static if_update_status_t preview_update;
void if_update_get_status(if_update_status_t *out){*out=preview_update;strcpy(out->current,"0.2.2");}
static bool preview_ota_stats;
bool ota_stats_enabled(void){return preview_ota_stats;}
esp_err_t ota_stats_set_enabled(bool enabled){preview_ota_stats=enabled;return ESP_OK;}
static esp_err_t preview_check(unsigned mask,if_update_channel_t channel){
    static const char *const versions[]={"0.2.3","0.2.4-beta.1"};
    const char *mode=getenv("PICO_SIM_OTA_STATE");
    bool slow_beta=mode&&!strcmp(mode,"slow-beta");
    uint32_t revision=preview_update.revision;
    preview_update=(if_update_status_t){.channel=channel,.revision=revision+1};
    for(int i=0;i<IF_UPDATE_CHANNEL_COUNT;++i){
        if(!(mask&(1u<<i)))continue;
        if_update_offer_t *offer=&preview_update.offers[i];
        bool available=mode&&(!strcmp(mode,"available")||!strcmp(mode,"downgrade")||
                              (!strcmp(mode,"mixed")&&i==IF_UPDATE_BETA)||(slow_beta&&i==IF_UPDATE_STABLE));
        offer->state=available?IF_UPDATE_AVAILABLE:IF_UPDATE_CURRENT;
        if(mode&&!strcmp(mode,"mixed")&&i==IF_UPDATE_STABLE)offer->state=IF_UPDATE_FAILED;
        if((mode&&!strcmp(mode,"checking"))||(slow_beta&&i==IF_UPDATE_BETA))offer->state=IF_UPDATE_CHECKING;
        offer->downgrade=available&&i==IF_UPDATE_STABLE&&mode&&!strcmp(mode,"downgrade");
        if(available)strcpy(offer->version,offer->downgrade?"0.2.1":versions[i]);
        else if(offer->state==IF_UPDATE_CURRENT&&i==IF_UPDATE_STABLE)strcpy(offer->version,"0.2.2");
        const char *text=available?"发现新版本，请确认后安装":offer->state==IF_UPDATE_CHECKING?"正在检查更新…":
            offer->state==IF_UPDATE_FAILED?"检查失败，请检查网络与系统时间":
            i==IF_UPDATE_STABLE?"当前无需升级":"此通道暂未发布版本";
        strcpy(offer->message,text);
    }
    preview_update.state=preview_update.offers[channel].state;
    if(mode&&!strcmp(mode,"busy")){
        preview_update.state=IF_UPDATE_DOWNLOADING;preview_update.installing=true;
        preview_update.total=3800000;preview_update.received=1120000;
        strcpy(preview_update.message,"正在下载并安装…");
    }
    preview_update.busy=preview_update.state==IF_UPDATE_CHECKING||preview_update.installing||
                        preview_update.offers[IF_UPDATE_BETA].state==IF_UPDATE_CHECKING;
    return ESP_OK;
}
esp_err_t if_update_check_channel(if_update_channel_t channel){
    if(channel!=IF_UPDATE_STABLE&&channel!=IF_UPDATE_BETA)return ESP_ERR_INVALID_ARG;
    return preview_check(1u<<channel,channel);
}
esp_err_t if_update_check(void){return if_update_check_channel(IF_UPDATE_STABLE);}
esp_err_t if_update_check_all(void){return preview_check((1u<<IF_UPDATE_CHANNEL_COUNT)-1,IF_UPDATE_STABLE);}
esp_err_t if_update_install_channel(if_update_channel_t channel){
    if(channel!=IF_UPDATE_STABLE&&channel!=IF_UPDATE_BETA)return ESP_ERR_INVALID_ARG;
    if(preview_update.busy||preview_update.offers[channel].state!=IF_UPDATE_AVAILABLE)return ESP_ERR_INVALID_STATE;
    preview_update.channel=channel;preview_update.state=IF_UPDATE_FAILED;preview_update.installing=true;
    strcpy(preview_update.message,"模拟器不执行固件升级");++preview_update.revision;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t if_update_install(void){return if_update_install_channel(preview_update.channel);}
void if_update_poll(void){}
void if_update_close(void){
    if(!preview_update.busy)return;
    preview_update.state=IF_UPDATE_CANCELLED;preview_update.busy=false;
    strcpy(preview_update.message,"操作已取消");++preview_update.revision;
    for(int i=0;i<IF_UPDATE_CHANNEL_COUNT;++i)
        if(preview_update.offers[i].state==IF_UPDATE_CHECKING){
            preview_update.offers[i].state=IF_UPDATE_CANCELLED;
            strcpy(preview_update.offers[i].message,"操作已取消");
        }
}

esp_err_t read_pico_transfer_sync_time(uint32_t *utc){(void)utc;return ESP_ERR_NOT_SUPPORTED;}
