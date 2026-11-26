/* SPDX-License-Identifier: Apache-2.0
 * 中文：原版传输界面，SSID和地址均为测试数据。
 * English: Production transfer UI with fixture SSIDs and addresses.
 */
#define app_transfer simulator_descriptor_transfer
#include "../../main/apps/app_transfer.c"
#define SIM_REPLAY_TICK on_tick
#include "replay.h"
#include "app_registry.h"
extern int simulator_route,simulator_transfer_mode;
int simulator_transfer_entry(void){return s_usb_requested?3:s_hotspot_requested?2:s_wifi_upload_requested?1:s_wifi_setup_requested?4:0;}
bool simulator_transfer_forget_open(void){return s_forget_confirm;}
bool simulator_transfer_saved_wifi(void){return s_saved_configured;}
int simulator_transfer_forget_card_top(void){return s_forget_confirm?forget_card().top:0;}
int simulator_transfer(uint8_t *fb,int view,int mode,const char *events){
 bool settings_entry=mode==3;
 if(settings_entry){mode=READ_PICO_TRANSFER_MODE_STA;app_transfer_request_wifi_setup();}
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);
 s_view=(transfer_view_t)view;s_mode=(read_pico_transfer_mode_t)mode;
 s_usb_entry=mode==2;s_usb_capacity_bytes=8ULL*1024*1024*1024;
 s_status=(read_pico_transfer_status_t){.mode=mode,.state=READ_PICO_TRANSFER_READY,.network_ready=true};
 snprintf(s_status.ssid,sizeof(s_status.ssid),"ReadPico-demo");snprintf(s_status.url,sizeof(s_status.url),"http://192.0.2.1");
 s_network_count=3;
 for(int i=0;i<3;++i){s_networks[i]=(read_pico_transfer_network_t){.supported=true,.requires_password=true,.rssi=-35-i*12};snprintf(s_networks[i].ssid,33,"Pico-demo-%d",i+1);}
 s_selected=s_networks[0];s_scan_pending=false;
 if(view==TRANSFER_NETWORKS&&getenv("PICO_SIM_SAVED_WIFI")){
     read_pico_transfer_save_wifi("Pico-demo-saved","");
     read_pico_transfer_get_saved_wifi(s_saved_ssid,&s_saved_configured);
 }
 if(mode==2){s_usb_start_pending=true;on_tick(&ctx);}
 if(view==TRANSFER_HOME&&mode!=2){read_pico_transfer_cfg_t cfg={.mode=mode};read_pico_transfer_start(&cfg);}
 if(view==TRANSFER_PASSWORD)ui_text_input_open(&s_password_input,s_password,sizeof(s_password),false,true,UI_INPUT_ENGLISH);
 prepare_qr();simulator_replay(&ctx,on_gesture,on_key,events);render(&ctx,fb);
 if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
 if(ctx.request_return)simulator_route=2;
 simulator_transfer_mode=s_wifi_settings_only?3:s_usb_entry?2:s_mode;return s_view;
}
