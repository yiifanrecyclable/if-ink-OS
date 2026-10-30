/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实更新代码的传输/Flash故障注入，使用真实SHA256，绝不接设备。
 * English: Transport/flash fault injection over production updater and real SHA256, without hardware access.
 */
#include <assert.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/ota_online.c"
#include "cJSON.h"
static int fault,writes,begins,aborts,ends,boots,stops;
static unsigned display_flash_depth;
void display_flash_begin(void){assert(!display_flash_depth);++display_flash_depth;}
void display_flash_end(void){assert(display_flash_depth==1);--display_flash_depth;}
static unsigned disconnects,eagains,asset_opens,failed_opens;
static size_t flash_at;
static uint8_t flashed[32768];
void app_time_service_stop(void){}
static bool online=true,boot_switched;
static uint8_t image[32768];static char manifest[1024];static CC_SHA256_CTX digest;
static esp_app_desc_t current={.magic_word=ESP_APP_DESC_MAGIC_WORD,.version="0.1.0-test.2",.project_name=IF_OTA_PROJECT};
static if_update_channel_t requested_channel;
static esp_partition_t running={.address=0x10000,.size=IF_OTA_MAX_BYTES},target={.address=0x910000,.size=IF_OTA_MAX_BYTES},data={.address=0xd10000,.size=0x2000};
static void (*pending_worker)(void*);static void *pending_arg;
static struct {int token;} sems[8];static int sem_count;
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &sems[sem_count++];}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return &sems[sem_count++];}
int xSemaphoreTake(SemaphoreHandle_t h,uint32_t wait){(void)wait;if(h==s_finished){if(!((typeof(sems[0])*)h)->token)return 0;((typeof(sems[0])*)h)->token=0;}return 1;}
int xSemaphoreGive(SemaphoreHandle_t h){((typeof(sems[0])*)h)->token=1;return 1;}
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)name;(void)stack;(void)priority;if(fault==15)return 0;pending_worker=fn;pending_arg=arg;*(TaskHandle_t*)handle=(void*)1;return pdPASS;}
void vTaskDelete(void *p){(void)p;}
void vTaskDelay(unsigned n){(void)n;}
int64_t esp_timer_get_time(void){static int64_t t;t+=1000;return t;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *s){*s=(read_pico_transfer_status_t){.state=online?READ_PICO_TRANSFER_READY:READ_PICO_TRANSFER_STOPPED,.mode=READ_PICO_TRANSFER_MODE_STA,.network_ready=online};}
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg){assert(cfg->network_only);online=true;return ESP_OK;}
void read_pico_transfer_stop(void){++stops;online=false;}
bool read_pico_transfer_try_stop_if_idle(void){read_pico_transfer_stop();return true;}
void read_pico_transfer_service_poll(void){}
esp_err_t read_pico_transfer_sync_time_online(uint32_t *out){*out=1900000000;return ESP_OK;}
void cloud_get_status(cloud_status_t *out){memset(out,0,sizeof(*out));}
bool usb_storage_active(void){return false;}
const esp_app_desc_t *esp_app_get_description(void){return &current;}
const esp_partition_t *esp_ota_get_running_partition(void){return &running;}
const esp_partition_t *esp_ota_get_next_update_partition(void *p){(void)p;return &target;}
const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label){(void)type;(void)sub;(void)label;return &data;}
esp_err_t esp_ota_begin(const esp_partition_t *p,size_t size,esp_ota_handle_t *h){assert(display_flash_depth==1);assert(p->address!=running.address&&size==OTA_WITH_SEQUENTIAL_WRITES);++begins;flash_at=0;*h=1;return fault==27?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_write(esp_ota_handle_t h,const void *p,size_t n){assert(display_flash_depth==1);assert(h==1&&p&&n);++writes;if(fault==6)return ESP_FAIL;assert(flash_at+n<=sizeof(flashed));memcpy(flashed+flash_at,p,n);flash_at+=n;return ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t h){assert(display_flash_depth==1);assert(h);++aborts;return ESP_OK;}
esp_err_t esp_ota_end(esp_ota_handle_t h){assert(display_flash_depth==1);assert(h);assert(flash_at==sizeof(image));assert(fault==24||!memcmp(flashed,image,sizeof(image)));++ends;if(fault==12)atomic_store(&s_cancel,true);return fault==7?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p){assert(display_flash_depth==1);assert(p==&target);++boots;if(fault==13)atomic_store(&s_cancel,true);boot_switched=fault!=8;return fault==8?ESP_FAIL:ESP_OK;}
int psa_crypto_init(void){return 0;}
int psa_hash_setup(psa_hash_operation_t *op,int alg){(void)alg;op->bytes=0;CC_SHA256_Init(&digest);return 0;}
int psa_hash_update(psa_hash_operation_t *op,const void *p,size_t n){op->bytes+=n;CC_SHA256_Update(&digest,p,(CC_LONG)n);return 0;}
int psa_hash_finish(psa_hash_operation_t *op,uint8_t *out,size_t cap,size_t *n){(void)op;assert(cap>=32);CC_SHA256_Final(out,&digest);*n=32;return 0;}
int psa_hash_abort(psa_hash_operation_t *op){(void)op;return 0;}
struct FakeHttp {bool feed,redirect,blob,partial;size_t at,length,start;void *user;int (*event)(esp_http_client_event_t*);};
int esp_crt_bundle_attach(void *p){(void)p;return 0;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
 assert(cfg->disable_auto_redirect&&cfg->crt_bundle_attach&&cfg->timeout_ms==15000);
 struct FakeHttp *h=calloc(1,sizeof(*h));assert(h);h->feed=strstr(cfg->url,"/contents/")!=NULL;
 if(h->feed)requested_channel=strstr(cfg->url,"/beta.json")?IF_UPDATE_BETA:IF_UPDATE_STABLE;
 h->blob=strstr(cfg->url,"/git/blobs/")!=NULL;h->redirect=strstr(cfg->url,"/releases/assets/")!=NULL;
 h->length=h->feed?strlen(manifest):sizeof(image)-(fault==1?10:0)+(fault==2?1:0);
 if(!h->feed&&!h->redirect)++asset_opens;
 h->user=cfg->user_data;h->event=cfg->event_handler;return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *k,const char *v){(void)h;assert(strcmp(k,"Authorization"));if(!strcmp(k,"Accept-Encoding"))assert(!strcmp(v,"identity"));
 if(!strcmp(k,"Range")&&fault>=18&&fault!=21&&fault!=26){
  unsigned offset=0;assert(sscanf(v,"bytes=%u-",&offset)==1);h->partial=true;h->start=h->at=offset;
 }return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int size){assert(!size);if(!h->feed&&fault==25&&failed_opens++<2)return ESP_FAIL;return fault==9?ESP_FAIL:ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){
 if(h->redirect){esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.user_data=h->user,.header_key="Location",.header_value=fault==10?"http://evil.test/file":"https://release-assets.githubusercontent.com/path?sig=ok"};h->event(&e);return 0;}
 if(h->partial){char range[96];snprintf(range,sizeof(range),"bytes %u-%u/%u",(unsigned)h->start+(fault==19?1:0),(unsigned)h->length-1-(fault==22?1:0),(unsigned)h->length);
 esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_HEADER,.user_data=h->user,.header_key="Content-Range",.header_value=range};h->event(&e);}
 return fault==2?0:(int64_t)(h->length-h->start);
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return h->blob&&(fault==10||fault==16)?404:h->redirect?302:h->partial?206:200;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms){(void)h;assert(ms==15000);return ESP_OK;}
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t h,void *p){h->user=p;return ESP_OK;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int cap){
 assert(display_flash_depth==0);
 if(fault==11&&!h->feed&&h->at>=288)return -1;
 if(!h->feed){
  if(fault==20&&h->at>=288&&eagains++<3)return -ESP_ERR_HTTP_EAGAIN;
  if(fault>=17&&fault!=20&&h->at>=(fault==26?121u:2048u)&&!disconnects++){return fault==21?0:-1;}
  if(fault==23&&h->partial)atomic_store(&s_cancel,true);
 }
 size_t n=h->length-h->at;if(n>(size_t)cap)n=cap;if(n>121)n=121;
 for(size_t i=0;i<n;++i)out[i]=h->feed?manifest[h->at+i]:image[(h->at+i)%sizeof(image)];
 if(fault==24&&h->partial&&n)out[0]^=1;
 h->at+=n;if(fault==5&&!h->feed&&h->at>288)atomic_store(&s_cancel,true);return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h->at==h->length;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){(void)h;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){free(h);return ESP_OK;}
static void prepare_for(const char *channel,const char *version){
 assert(display_flash_depth==0);
 disconnects=eagains=asset_opens=failed_opens=0;
 memset(image,42,sizeof(image));esp_image_header_t header={.magic=ESP_IMAGE_HEADER_MAGIC,.chip_id=ESP_CHIP_ID_ESP32S3};
 esp_image_segment_header_t segment={.data_len=sizeof(image)-32};esp_app_desc_t desc=current;strcpy(desc.version,version);
 if(fault==3)strcpy(desc.project_name,"Read_Pico");if(fault==4)header.chip_id=0;
 memcpy(image,&header,sizeof(header));memcpy(image+sizeof(header),&segment,sizeof(segment));memcpy(image+sizeof(header)+sizeof(segment),&desc,sizeof(desc));
 unsigned char sha[32];CC_SHA256(image,sizeof(image),sha);char hex[65];for(int i=0;i<32;++i)sprintf(hex+2*i,"%02x",sha[i]);
 if(fault==14)hex[0]=hex[0]=='0'?'1':'0';
 snprintf(manifest,sizeof(manifest),"{\"schema\":1,\"product\":\"if-ink-os\",\"project\":\"Read_Pico_IF\",\"layout\":\"if-16m-ota-v1\",\"channel\":\"%s\",\"available\":true,\"version\":\"%s\",\"size\":%zu,\"sha256\":\"%s\",\"asset_id\":\"1234\",\"blob_sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}",channel,version,sizeof(image),hex);
}
static void prepare(void){prepare_for("stable","0.1.0");}
static void run_worker(void){assert(pending_worker);void(*fn)(void*)=pending_worker;pending_worker=NULL;fn(pending_arg);if_update_poll();assert(!s_status.busy&&!s_task);}
static void policy(void){
 assert(if_update_newer("0.1.6","0.1.6-beta3-r2"));
 assert(if_update_newer("0.1.7","0.1.7-beta-r2"));
 assert(!if_update_newer("0.1.7-beta-r2","0.1.6"));
 assert(!if_update_newer("0.1.7","0.1.7-beta-r0"));
 assert(!if_update_newer("0.1.7","0.1.7-beta-r2junk"));
 assert(!if_update_newer("0.1.6-beta3-r2","0.1.5"));
 assert(!if_update_newer("0.1.6","0.1.6-beta3-r0"));
 assert(!if_update_newer("0.1.6","0.1.6-beta3-r2junk"));
 assert(if_update_newer("0.1.6","0.1.6-beta2"));
 assert(if_update_newer("0.1.7","0.1.6-beta2"));
 assert(!if_update_newer("0.1.6-beta2","0.1.5"));
 assert(!if_update_newer("0.1.6","0.1.6-beta2-junk"));
 assert(!if_update_newer("0.1.6","0.1.6-beta0"));
 assert(if_update_newer("0.1.6","0.1.6-beta"));assert(if_update_newer("0.1.7","0.1.6-beta"));
 assert(!if_update_newer("0.1.5","0.1.6-beta"));assert(!if_update_newer("0.1.6-beta","0.1.5"));
 assert(!if_update_newer("0.1.6","0.1.6-beta-junk"));
 assert(if_update_newer("0.1.0","0.1.0-test.2"));assert(if_update_newer("0.1.0","0.3.3-rc83-if.8"));
 assert(!if_update_newer("0.1.0","0.1.0"));assert(!if_update_newer("0.1.0","0.1.1"));assert(!if_update_newer("0.2.0-test.1","0.1.0"));
 assert(!if_update_newer("01.2.0","0.1.0"));assert(!if_update_newer("0.1.0","unknown"));
 assert(if_update_redirect_allowed("https://release-assets.githubusercontent.com/path?sig=x"));
 const char *bad[]={"http://release-assets.githubusercontent.com/x","https://release-assets.githubusercontent.com.evil/x","https://release-assets.githubusercontent.com@evil/x","https://evil/x","https://release-assets.githubusercontent.com/\r\n"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!if_update_redirect_allowed(bad[i]));
 if_update_candidate_t candidate;assert(if_update_parse_manifest(manifest,strlen(manifest),&candidate));
 assert(!if_update_parse_manifest(manifest,strlen(manifest)-1,&candidate));
 char invalid[2048];snprintf(invalid,sizeof(invalid),"%.*s,\"schema\":1}",(int)strlen(manifest)-1,manifest);assert(!if_update_parse_manifest(invalid,strlen(invalid),&candidate));
 const char *keys[]={"schema","product","project","layout","channel","available","version","size","sha256","asset_id","blob_sha"};
 for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){
  cJSON *o=cJSON_Parse(manifest);assert(o);
  cJSON_DeleteItemFromObjectCaseSensitive(o,keys[i]);char *text=cJSON_PrintUnformatted(o);
  assert(!if_update_parse_manifest(text,strlen(text),&candidate));free(text);cJSON_Delete(o);
 }
 cJSON *o=cJSON_Parse(manifest);cJSON_ReplaceItemInObjectCaseSensitive(o,"available",cJSON_CreateFalse());
 char *text=cJSON_PrintUnformatted(o);assert(if_update_parse_manifest(text,strlen(text),&candidate)&&!candidate.available);free(text);cJSON_Delete(o);

}
static void channels(void){
 assert(if_update_candidate_allowed("0.1.8-beta.1","0.1.7",IF_UPDATE_BETA));
 assert(if_update_candidate_allowed("0.1.8-beta.10","0.1.8-beta.2",IF_UPDATE_BETA));
 assert(!if_update_candidate_allowed("0.1.8-beta.2","0.1.8-beta.10",IF_UPDATE_BETA));
 assert(!if_update_candidate_allowed("0.1.8-beta.2","0.1.8-beta.2",IF_UPDATE_BETA));
 assert(!if_update_candidate_allowed("0.1.8-beta.1","0.1.8",IF_UPDATE_BETA));
 assert(if_update_candidate_allowed("0.1.8-beta.1","0.1.8-beta-r2",IF_UPDATE_BETA));
 assert(if_update_candidate_allowed("0.1.8-beta3-r2","0.1.8-beta3",IF_UPDATE_BETA));
 assert(if_update_candidate_allowed("0.1.7","0.1.8-beta.1",IF_UPDATE_STABLE));
 assert(!if_update_newer("0.1.7","0.1.8-beta.1"));
 assert(!if_update_candidate_allowed("0.1.7","0.1.8",IF_UPDATE_STABLE));
 assert(!if_update_candidate_allowed("0.1.7","0.1.7",IF_UPDATE_STABLE));
 assert(!if_update_candidate_allowed("0.1.7","0.1.8-beta.1",IF_UPDATE_BETA));
 assert(!if_update_candidate_allowed("0.1.8-beta.1","0.1.7",IF_UPDATE_STABLE));
 const char *invalid[]={"0.1.8-beta.0","0.1.8-beta.01","0.1.8-beta.1junk","0.1.8-beta.100000","0.1.8-test.2","0.1.8-alpha.1"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i)assert(!if_update_candidate_allowed(invalid[i],"0.1.7",IF_UPDATE_BETA));
 if_update_candidate_t candidate;
 prepare_for("beta","0.1.8-beta.1");
 assert(if_update_parse_manifest_channel(manifest,strlen(manifest),IF_UPDATE_BETA,&candidate)&&candidate.channel==IF_UPDATE_BETA);
 assert(!if_update_parse_manifest(manifest,strlen(manifest),&candidate));
 prepare_for("stable","0.1.8-beta.1");assert(!if_update_parse_manifest_channel(manifest,strlen(manifest),IF_UPDATE_STABLE,&candidate));
 prepare_for("beta","0.1.8");assert(!if_update_parse_manifest_channel(manifest,strlen(manifest),IF_UPDATE_BETA,&candidate));
 const char *disabled="{\"schema\":1,\"product\":\"if-ink-os\",\"project\":\"Read_Pico_IF\",\"layout\":\"if-16m-ota-v1\",\"channel\":\"beta\",\"available\":false}";
 assert(if_update_parse_manifest_channel(disabled,strlen(disabled),IF_UPDATE_BETA,&candidate)&&!candidate.available);
 prepare_for("beta","0.1.8-beta.1");
 assert(if_update_check_channel(IF_UPDATE_BETA)==ESP_OK);
 assert(if_update_check_channel(IF_UPDATE_STABLE)==ESP_ERR_INVALID_STATE&&s_status.channel==IF_UPDATE_BETA);
 run_worker();assert(requested_channel==IF_UPDATE_BETA&&s_status.state==IF_UPDATE_AVAILABLE);
 assert(if_update_install()==ESP_OK);run_worker();assert(s_status.state==IF_UPDATE_READY);
 // 切换通道后禁止复用旧候选，即便服务器错误返回了另一通道清单。
 // Switching invalidates old candidates even if the server mistakenly returns the other channel's manifest.
 prepare_for("stable","0.1.8");assert(if_update_check_channel(IF_UPDATE_BETA)==ESP_OK);run_worker();
 assert(s_status.state==IF_UPDATE_FAILED&&!s_candidate.available&&if_update_install()==ESP_ERR_INVALID_STATE);
 strcpy(current.version,"0.1.8-beta.1");prepare_for("stable","0.1.7");
 assert(if_update_check()==ESP_OK);run_worker();
 assert(requested_channel==IF_UPDATE_STABLE&&s_status.state==IF_UPDATE_AVAILABLE&&s_status.return_to_stable);
 assert(if_update_install()==ESP_OK);run_worker();assert(s_status.state==IF_UPDATE_READY);
 assert(if_update_check_channel((if_update_channel_t)99)==ESP_ERR_INVALID_ARG);
 strcpy(current.version,"0.1.0-test.2");prepare();
}
int main(void){
 assert(init());prepare();policy();channels();
 for(fault=0;fault<=14;++fault){
  begins=writes=aborts=ends=boots=0;boot_switched=false;online=false;prepare();
  assert(if_update_check()==ESP_OK);run_worker();
  if(fault==9){assert(s_status.state==IF_UPDATE_FAILED&&!begins);continue;}
  assert(s_status.state==IF_UPDATE_AVAILABLE&&!begins&&!boots);
  assert(if_update_install()==ESP_OK);run_worker();
  bool success=fault==0||fault==13;
  assert(boot_switched==success);assert((s_status.state==IF_UPDATE_READY)==success);
  if(fault==3||fault==4||fault==10)assert(!begins&&!writes);
  if(fault==1||fault==2||fault==5||fault==6||fault==11||fault==14)assert(aborts==1&&!boots);
  if(fault==7)assert(ends==1&&!boots);
  if(fault==12)assert(ends==1&&!boots&&s_status.state==IF_UPDATE_CANCELLED);
  if(success)assert(boots==1&&ends==1&&!aborts);
 }
 fault=16;prepare();assert(if_update_check()==ESP_OK);run_worker();assert(if_update_install()==ESP_OK);run_worker();assert(s_status.state==IF_UPDATE_READY);
 for(fault=17;fault<=26;++fault){
  begins=writes=aborts=ends=boots=0;boot_switched=false;prepare();
  assert(if_update_check()==ESP_OK);run_worker();assert(if_update_install()==ESP_OK);run_worker();
  bool success=fault==17||fault==18||fault==20||fault==21||fault==25||fault==26;
  assert(boot_switched==success);assert((s_status.state==IF_UPDATE_READY)==success);
  assert(begins==1);assert(success?ends==1&&aborts==0:aborts==1&&boots==0);
  if(success)assert(!memcmp(flashed,image,sizeof(image)));
  if(fault==19||fault==22)assert(asset_opens<=OTA_CONNECTION_LIMIT);
 }
 fault=0;prepare();assert(if_update_check()==ESP_OK);run_worker();
 target.address=running.address;assert(if_update_install()==ESP_OK);run_worker();assert(s_status.state==IF_UPDATE_FAILED);target.address=0x910000;
 fault=27;begins=writes=aborts=ends=boots=0;boot_switched=false;prepare();
 assert(if_update_check()==ESP_OK);run_worker();assert(if_update_install()==ESP_OK);run_worker();
 assert(s_status.state==IF_UPDATE_FAILED&&begins==1&&!writes&&!aborts&&!boots&&display_flash_depth==0);
 fault=15;online=false;assert(if_update_check()==ESP_ERR_NO_MEM&&!s_status.busy&&!online);
 assert(stops>=20);assert(display_flash_depth==0);
 puts("PASS: sequential erase, bounded reconnect/200-skip/206-Range/prefix recovery, EAGAIN/EOF and resumed-byte SHA256, production updater, real SHA256, stable version/product policy, redirects, confirmation boundary, truncation/overflow/disconnect/hash/identity/chip/write/end/boot failures, cancellation/commit race, slot isolation and WiFi cleanup");
}
