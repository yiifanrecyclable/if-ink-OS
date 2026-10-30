/* SPDX-License-Identifier: Apache-2.0
 * 中文：确定性的宿主硬件替身；不联网、不调用真实PMU、不写系统设置。
 * English: Deterministic host hardware model; no networking, PMU or system setting writes.
 */
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include "nvs.h"
#include "read_pico_sd.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "ui_wallpaper.h"
#include "read_pico_search.h"
#include "book_source.h"
#include "app_registry.h"
#include "ui_kit.h"
#include "ttf_font.h"

esp_err_t read_pico_pmu_cmd(uint16_t code,const uint8_t *p,uint8_t n){(void)code;(void)p;(void)n;return ESP_OK;}
static bool preview_usb;
static read_pico_transfer_status_t preview_network;
bool usb_storage_active(void){return preview_usb;}
esp_err_t usb_storage_stop(void){preview_usb=false;return ESP_OK;}
bool read_pico_transfer_try_stop_if_idle(void){return true;}
void read_pico_transfer_stop(void){preview_network.state=READ_PICO_TRANSFER_STOPPED;preview_network.network_ready=false;}
esp_err_t read_pico_transfer_forget_wifi(void){preview_network.wifi_configured=false;preview_network.wifi_ssid[0]=0;return ESP_OK;}
esp_err_t read_pico_transfer_select_saved_wifi(const char *ssid){return preview_network.wifi_configured&&!strcmp(ssid,preview_network.wifi_ssid)?ESP_OK:ESP_ERR_NOT_FOUND;}
esp_err_t read_pico_transfer_save_wifi(const char *s,const char *p){preview_network.wifi_configured=true;snprintf(preview_network.wifi_ssid,33,"%s",s);return ESP_OK;}
const app_desc_t app_dashboard={.title="首页",.detail="继续阅读与阅读记录"};
const app_desc_t app_book={.title="内容",.detail="书籍与阅读"};
const app_desc_t app_files={.title="应用",.detail="应用与存储管理"};
const app_desc_t app_device_settings={.title="设置",.detail="个性化设置"};
const app_desc_t app_image={.title="图片",.detail="图片浏览"};
const app_desc_t app_pindou={.title="数字拼豆",.detail="Cyber Beads"};
const app_desc_t app_cloud={.title="坚果云",.detail="只读下载"};
const app_desc_t app_transfer={.title="传输",.detail="模拟传输"};
static const app_desc_t *preview_apps[]={&app_dashboard,&app_book,&app_files,&app_device_settings,&app_image};
int app_count(void){return 5;}
const app_desc_t *app_at(int i){return i>=0&&i<5?preview_apps[i]:NULL;}
const app_desc_t *app_home_page(void){return &app_dashboard;}
int app_index_of(const app_desc_t *a){if(a==&app_pindou)return 7;if(a==&app_cloud)return 6;if(a==&app_transfer)return 5;for(int i=0;i<5;++i)if(a==preview_apps[i])return i;return -1;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){*out=(read_pico_sd_info_t){.present=true,.mounted=true,.space_valid=true,.filesystem_bytes=8ULL<<30,.capacity_bytes=8ULL<<30,.total_bytes=8ULL<<30,.free_bytes=7ULL<<30};return ESP_OK;}
esp_err_t read_pico_sd_mount(void){return ESP_OK;}
const pmu_snapshot_t *read_pico_pmu_get(void){static const pmu_snapshot_t p={.present=true,.status_ok=true,.flags=PMU_STATUS_BATTERY_VALID,.battery_mv=3950,.soc_permille=850};return &p;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *s){*s=preview_network;}
esp_err_t read_pico_transfer_get_saved_wifi(char *s,bool *configured){snprintf(s,33,"%s",preview_network.wifi_ssid);*configured=preview_network.wifi_configured;return ESP_OK;}
#include "book_store.h"
#include "book_progress.h"
#include "sc7a20h.h"
#include "sc7a20h_lab.h"

void pmu_selftest_prepare_powerdown(void){}
static unsigned revision;
esp_err_t book_store_roots(book_store_root_t out[3],int*n){out[0]=(book_store_root_t){.path="/sdcard/books"};out[1]=(book_store_root_t){.path="/flash/books",.is_flash=true};*n=2;return ESP_OK;}
bool book_store_roots_degraded(void){return false;}
esp_err_t book_store_upload_root(book_store_root_t*out){*out=(book_store_root_t){.path="/sdcard/books"};return ESP_OK;}
size_t book_store_file_limit(const book_store_root_t*r){return r->is_flash?BOOK_STORE_FLASH_FILE_MAX:SIZE_MAX;}
uint64_t book_store_free_bytes(const book_store_root_t*r){return r->is_flash?1024*1024:7ULL<<30;}
bool book_store_flash_ready(void){return true;}
void book_store_notify_changed(void){++revision;}
unsigned book_store_revision(void){return revision;}
esp_err_t book_store_delete(const char*p,bool*removed){
 *removed=false;const char *base=strrchr(p,'/');if(!base||(strncmp(p,"/sdcard/books/",14)&&strncmp(p,"/flash/books/",13)))return ESP_ERR_INVALID_ARG;
 const char *ext=strrchr(base,'.');if(!ext||(strcasecmp(ext,".txt")&&strcasecmp(ext,".epub")))return ESP_ERR_INVALID_ARG;
 if(unlink(p)&&errno!=ENOENT)return ESP_FAIL;*removed=true;return book_progress_forget(p);
}
esp_err_t sc7a20h_apply_config(sc7a20h_handle_t h,const sc7a20h_sensor_config_t*c){return ESP_OK;}
const sc7a20h_sensor_config_t *sc7a20h_get_config(sc7a20h_handle_t h){static sc7a20h_sensor_config_t c;return &c;}
esp_err_t sc7a20h_activity_config(sc7a20h_handle_t h,uint16_t t,uint8_t d){return ESP_OK;}
esp_err_t sc7a20h_aoi_config(sc7a20h_handle_t h,sc7a20h_aoi_t a,const sc7a20h_aoi_cfg_t*c){return ESP_OK;}

esp_err_t usb_storage_start(void){preview_usb=true;return ESP_OK;}
bool usb_storage_connected(void){return preview_usb;}
void read_pico_transfer_service_poll(void){}
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t*c){preview_network.mode=c->mode;preview_network.state=READ_PICO_TRANSFER_READY;preview_network.network_ready=true;snprintf(preview_network.ssid,33,"%s",c->mode==READ_PICO_TRANSFER_MODE_AP?"ReadPico-demo":preview_network.wifi_configured?preview_network.wifi_ssid:"Pico-demo-1");snprintf(preview_network.url,sizeof(preview_network.url),"http://192.0.2.1");return ESP_OK;}
esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX],size_t*count){*count=3;for(int i=0;i<3;++i){out[i]=(read_pico_transfer_network_t){.supported=true,.requires_password=true,.rssi=-35-i*12};snprintf(out[i].ssid,33,"Pico-demo-%d",i+1);}return ESP_OK;}

void read_pico_transfer_editor_poll(void) {}
