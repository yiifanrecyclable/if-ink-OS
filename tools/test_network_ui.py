# SPDX-License-Identifier: Apache-2.0
"""执行生产网络页面动作及云端推屏策略，只替换硬件边界。
Run production network actions and cloud presentation with hardware boundaries stubbed.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / "build/book-tests"
out.mkdir(parents=True, exist_ok=True)


def function(file, declaration):
    source = (root / file).read_text()
    start = source.index(declaration)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int esp_err_t;
enum {ESP_OK, ESP_ERR_NOT_FOUND, ESP_FAIL};
typedef enum {APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL} app_redraw_t;
enum {TRANSFER_NETWORKS, TRANSFER_PASSWORD, TRANSFER_HOME, READ_PICO_TRANSFER_MODE_STA, UI_INPUT_ENGLISH};
#define NETWORK_ROWS 4
#define UI_MSG(x) (x)
#define ui_snprintf snprintf
typedef struct {char ssid[33]; bool supported, requires_password;} network_t;
static network_t s_selected, s_networks[8];
static bool s_forget_confirm, s_session_started, s_saved_configured, s_network_connect_pending, s_scan_pending;
static int s_view, s_network_page, s_network_count=8, s_scan_error, s_mode, s_password_input;
static char s_saved_ssid[33], s_network_message[160], s_password[65];
static struct {bool network_ready;} s_status;
static int selected_calls, queued, prompts, saves, select_result;
static bool busy;
static void stop_session(void){s_session_started=false;}
static bool stop_upload_if_idle(void){if(busy)return false;stop_session();return true;}
static int read_pico_transfer_forget_wifi(void){return ESP_OK;}
static int read_pico_transfer_get_saved_wifi(char *ssid, bool *configured){ssid[0]=0;*configured=false;return ESP_OK;}
static int read_pico_transfer_select_saved_wifi(const char *ssid){assert(!s_session_started);assert(*ssid);++selected_calls;return select_result;}
static int read_pico_transfer_save_wifi(const char *ssid,const char *password){assert(*ssid&&!password[0]);++saves;return ESP_OK;}
static void queue_network_start(int mode,bool use_selected_wifi){assert(use_selected_wifi);(void)mode;++queued;s_view=TRANSFER_HOME;}
static void clear_password(void){memset(s_password,0,sizeof(s_password));}
static void ui_text_input_open(int *input,char *text,size_t cap,bool cn,bool secret,int mode){(void)input;assert(!*text&&cap==65&&!cn&&secret&&mode==UI_INPUT_ENGLISH);++prompts;}
'''
code += function("main/apps/app_transfer.c", "static app_redraw_t provisioning_action(int id)")
code += r'''
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl; uint8_t *fb;} app_ctx_t;
enum {MODE_GL16=5};
enum {UI_LOCK_WIDTH=684, UI_LOCK_HEIGHT=1216};
static int E0470_WAVEFORM, presented, rendered;
static void render(app_ctx_t *ctx,uint8_t *fb){assert(ctx->fb==fb);++rendered;}
static int update_display_area_diff_with(void *hl,const int *waveform,int mode,EpdRect area){assert(hl&&waveform==&E0470_WAVEFORM&&mode==MODE_GL16);assert(area.x==0&&area.y==0&&area.width==684&&area.height==1216);++presented;return 0;}
static void guard_draw_result(void *hl,int result){assert(hl&&result==0);}
'''
code += function("main/apps/app_cloud.c", "static bool present(app_ctx_t *ctx,app_redraw_t redraw)")
code += r'''
int main(void){
    s_networks[0]=(network_t){.ssid="known",.supported=true,.requires_password=true};
    s_networks[1]=(network_t){.ssid="new",.supported=true,.requires_password=true};
    s_view=TRANSFER_NETWORKS;select_result=ESP_OK;
    assert(provisioning_action(0)==APP_REDRAW_PAGE);
    assert(queued==1&&prompts==0&&saves==0&&s_view==TRANSFER_HOME);
    s_view=TRANSFER_NETWORKS;select_result=ESP_ERR_NOT_FOUND;
    assert(provisioning_action(1)==APP_REDRAW_PAGE);
    assert(s_view==TRANSFER_PASSWORD&&queued==1&&prompts==1);
    s_view=TRANSFER_NETWORKS;select_result=ESP_FAIL;
    provisioning_action(0);
    assert(s_view==TRANSFER_NETWORKS&&queued==1&&prompts==1&&s_network_message[0]);
    busy=s_session_started=true;int attempts=selected_calls;
    provisioning_action(0);
    assert(s_session_started&&selected_calls==attempts&&queued==1);
    busy=false;select_result=ESP_OK;
    provisioning_action(0);
    assert(!s_session_started&&queued==2&&prompts==1);
    uint8_t fb=0;app_ctx_t ctx={.hl=&fb,.fb=&fb};
    for(int i=0;i<200;++i)assert(present(&ctx,APP_REDRAW_PAGE));
    assert(presented==200&&rendered==200);
    assert(!present(&ctx,APP_REDRAW_FULL)&&presented==200);
    puts("network UI: known SSID skips password, new SSID prompts, busy/error blocks; cloud uses gray differences and preserves explicit full refresh");
}
'''
source = out / "network-ui.c"
source.write_text(code)
binary = out / "network-ui"
subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
