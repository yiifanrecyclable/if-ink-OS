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
const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t desc={.version="0.1.7",.project_name="Read_Pico_IF"};return &desc;}
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
void if_update_get_status(if_update_status_t *out){*out=preview_update;strcpy(out->current,"0.1.7");}
esp_err_t if_update_check(void){
    memset(&preview_update,0,sizeof(preview_update));
    const char *mode=getenv("PICO_SIM_OTA_STATE");
    preview_update.state=mode&&!strcmp(mode,"available")?IF_UPDATE_AVAILABLE:IF_UPDATE_CURRENT;
    if(preview_update.state==IF_UPDATE_AVAILABLE){strcpy(preview_update.version,"0.1.8");preview_update.total=3800000;}
    strcpy(preview_update.message,preview_update.state==IF_UPDATE_AVAILABLE?"发现新版本，请确认后安装":"模拟器仅预览，请在设备检查更新");
    ++preview_update.revision;return ESP_OK;
}
esp_err_t if_update_install(void){preview_update.state=IF_UPDATE_FAILED;strcpy(preview_update.message,"模拟器不执行固件升级");++preview_update.revision;return ESP_ERR_NOT_SUPPORTED;}
void if_update_poll(void){}
void if_update_close(void){}

esp_err_t read_pico_transfer_sync_time(uint32_t *utc){(void)utc;return ESP_ERR_NOT_SUPPORTED;}
