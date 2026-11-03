#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the production timed sleep loop: faults/timeouts never become fictitious key wakes."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/sleep.c').read_text()
start=source.index('app_wake_source_t app_light_sleep_wait_timed(')
end=source.index('\napp_wake_source_t app_light_sleep_wait(',start)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
typedef void *sc7a20h_handle_t;
typedef int gpio_num_t;
typedef int sc7a20h_events_t;
typedef struct {int ths_mg,duration;} sc7a20h_motion_cfg_t;
#define SC7A20H_MOTION_DEFAULT() ((sc7a20h_motion_cfg_t){0})
enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_STATE,ESP_SLEEP_WAKEUP_TIMER};
typedef enum {APP_WAKE_NONE,APP_WAKE_KEY,APP_WAKE_PICKUP,APP_WAKE_TIMEOUT} app_wake_source_t;
#define APP_LOCK_KEY_POLL_US 2000000LL
#define APP_LOCK_STUCK_IRQ_US 3000000LL
#define APP_LOCK_SLEEP_ERROR_LIMIT 5
#define READ_PICO_IOE_INT_GPIO 9
#define pdMS_TO_TICKS(x) (x)
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static int64_t now,key_at,timer_us;
static bool stuck,timer_bad,sleep_bad,masked;
static int traces,starts;
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;}
static void lock_arm_ioe_wakeup(void){}
static bool app_settings_pickup_wake(void){return false;}
static void read_pico_clear_ioe_int(void){}
static bool read_pico_pmu_take_key_wakeup(void){return key_at&&now>=key_at;}
static int gpio_get_level(int pin){(void)pin;return !stuck;}
static void gpio_wakeup_disable(int pin){if(pin==9)masked=true;}
static esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us){timer_us=us;return timer_bad?ESP_FAIL:ESP_OK;}
static esp_err_t esp_light_sleep_start(void){starts++;if(sleep_bad)return ESP_FAIL;now+=timer_us;return ESP_OK;}
static void esp_sleep_disable_wakeup_source(int source){(void)source;}
static const char *esp_err_to_name(int error){(void)error;return "test";}
static void app_settings_trace_storage_result(const char *phase,int error){assert(phase&&error);traces++;}
static void app_settings_trace_storage(const char *phase){assert(phase&&*phase);}
static void app_settings_set_last_wake(uint8_t wake){assert(wake<=APP_WAKE_PICKUP);}
static void sc7a20h_arm_pickup_wake(void *acc,void *cfg){(void)acc;(void)cfg;}
static void pickup_ack(void *acc){(void)acc;}
static bool pickup_wait_quiet(void *acc,int quiet,int timeout){(void)acc;(void)quiet;(void)timeout;return true;}
static void sc7a20h_config_light_sleep_wakeup(void *acc){(void)acc;}
static int sc7a20h_int1_gpio(void *acc){(void)acc;return 10;}
static int sc7a20h_int1_level(void *acc){(void)acc;return 0;}
static bool pickup_ia(void *acc){(void)acc;return false;}
static void sc7a20h_read_events(void *acc,void *event){(void)acc;(void)event;}
static void sc7a20h_power_down(void *acc){(void)acc;}
static void sc7a20h_int1_begin(void *acc){(void)acc;}
'''+source[start:end]+r'''
static void reset(void){now=1000000;key_at=timer_us=0;stuck=timer_bad=sleep_bad=masked=false;traces=starts=0;}
int main(void){
 reset();assert(app_light_sleep_wait_timed(NULL,600000)==APP_WAKE_TIMEOUT);assert(now>=601000000&&!traces);
 reset();stuck=true;assert(app_light_sleep_wait_timed(NULL,600000)==APP_WAKE_TIMEOUT);assert(masked&&traces==1&&starts>0);
 reset();stuck=true;key_at=now+6500000;assert(app_light_sleep_wait_timed(NULL,0)==APP_WAKE_KEY);assert(now>=key_at&&masked);
 reset();timer_bad=true;assert(app_light_sleep_wait_timed(NULL,6000)==APP_WAKE_TIMEOUT);assert(traces==1&&!starts);
 reset();timer_bad=true;key_at=now+2000000;assert(app_light_sleep_wait_timed(NULL,0)==APP_WAKE_KEY);assert(now>=key_at);
 reset();sleep_bad=true;assert(app_light_sleep_wait_timed(NULL,6000)==APP_WAKE_TIMEOUT);assert(traces==1&&starts==5);
 reset();sleep_bad=true;key_at=now+2000000;assert(app_light_sleep_wait_timed(NULL,0)==APP_WAKE_KEY);assert(now>=key_at);
 puts("PASS production sleep loop: ten-minute deadline, stuck IRQ and rejected timer/sleep stay locked; real keys still wake");
}
'''
# Log-only arguments are compiled out in the host harness.
code=code.replace('static const char *esp_err_to_name(int error){(void)error;return "test";}','')
with tempfile.TemporaryDirectory(prefix='sleep-wake-',dir=ROOT/'build') as tmp:
    work=Path(tmp);(work/'test.c').write_text(code)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
