/* SPDX-License-Identifier: Apache-2.0
 * 中文：可选升级统计授权、独立于硬件的随机编号与撤回。
 * English: Optional update statistics consent, random installation identity, and withdrawal.
 */
#include "ota_stats.h"
#include "ota_online_policy.h"
#include "settings_storage.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"

#define STATS_NS "if_ota_stats"
#define STATS_KEY "consent_v1"
#define STATS_ID_BYTES 16

enum {STATS_OFF, STATS_ON, STATS_DELETE};
typedef struct {uint8_t state,serial[STATS_ID_BYTES];} stats_pref_t;

static bool has_serial(const stats_pref_t *pref){
    for(size_t i=0;i<sizeof(pref->serial);++i)if(pref->serial[i])return true;
    return false;
}
static bool read_pref(stats_pref_t *pref){
    *pref=(stats_pref_t){0};
    nvs_handle_t nvs;
    esp_err_t err=nvs_open(STATS_NS,NVS_READONLY,&nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return true;
    if(err!=ESP_OK)return false;
    size_t bytes=sizeof(*pref);
    err=nvs_get_blob(nvs,STATS_KEY,pref,&bytes);
    nvs_close(nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND){*pref=(stats_pref_t){0};return true;}
    if(err!=ESP_OK||bytes!=sizeof(*pref)||pref->state>STATS_DELETE)return false;
    return pref->state!=STATS_DELETE||has_serial(pref);
}
static esp_err_t save_pref(const stats_pref_t *pref){
    return settings_storage_put_blob(STATS_NS,STATS_KEY,pref,sizeof(*pref));
}
static void serial_hex(const stats_pref_t *pref,char out[STATS_ID_BYTES*2+1]){
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<sizeof(pref->serial);++i){
        out[2*i]=hex[pref->serial[i]>>4];out[2*i+1]=hex[pref->serial[i]&15u];
    }
    out[STATS_ID_BYTES*2]=0;
}
bool ota_stats_enabled(void){
    stats_pref_t pref;
    return read_pref(&pref)&&pref.state==STATS_ON;
}
esp_err_t ota_stats_set_enabled(bool enabled){
    stats_pref_t pref;
    if(!read_pref(&pref))return ESP_ERR_INVALID_RESPONSE;
    if(enabled){
        if(pref.state==STATS_ON)return ESP_OK;
        pref.state=STATS_ON;
    }else{
        if(pref.state!=STATS_ON)return ESP_OK;
        pref.state=has_serial(&pref)?STATS_DELETE:STATS_OFF;
    }
    return save_pref(&pref);
}
void ota_stats_add_headers(esp_http_client_handle_t http,const char *version){
    stats_pref_t pref;
    if(!read_pref(&pref)||pref.state!=STATS_ON)return;
    if(if_update_install_kind(version,"0.0.0",IF_UPDATE_STABLE)!=IF_UPDATE_INSTALL_UPGRADE&&
       if_update_install_kind(version,"0.0.0",IF_UPDATE_BETA)!=IF_UPDATE_INSTALL_UPGRADE)return;
    if(!has_serial(&pref)){
        // 仅在 Wi-Fi 已就绪的更新任务生成编号。/ Generate the identifier only in the network-ready update worker.
        esp_fill_random(pref.serial,sizeof(pref.serial));
        if(!has_serial(&pref)||save_pref(&pref)!=ESP_OK)return;
    }
    char serial[STATS_ID_BYTES*2+1];serial_hex(&pref,serial);
    if(esp_http_client_set_header(http,"X-Ifink-Serial",serial)==ESP_OK&&
       esp_http_client_set_header(http,"X-Ifink-Version",version)==ESP_OK)
        (void)esp_http_client_set_header(http,"X-Ifink-Consent","1");
}
void ota_stats_flush_withdrawal(void){
    stats_pref_t pref;
    if(!read_pref(&pref)||pref.state!=STATS_DELETE)return;
    esp_http_client_config_t config={.url=IF_OTA_MIRROR_ROOT "/forget",.method=HTTP_METHOD_POST,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.timeout_ms=2500};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if(!http)return;
    char serial[STATS_ID_BYTES*2+1];serial_hex(&pref,serial);
    bool deleted=esp_http_client_set_header(http,"X-Ifink-Serial",serial)==ESP_OK&&
                 esp_http_client_open(http,0)==ESP_OK&&esp_http_client_fetch_headers(http)>=0&&
                 esp_http_client_get_status_code(http)==204;
    esp_http_client_close(http);esp_http_client_cleanup(http);
    if(deleted){pref=(stats_pref_t){0};(void)save_pref(&pref);}
}
