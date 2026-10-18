#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""后台校时网络边界与调度回归；不联网。/ Offline fault tests for production time-service scheduling."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='time-service-') as directory:
    work = Path(directory)
    (work/'cloud').mkdir()
    (work/'time_service.c').write_bytes((ROOT/'main/time_service.c').read_bytes())
    (work/'time_service.h').write_bytes((ROOT/'main/time_service.h').read_bytes())
    (work/'common.h').write_text('''#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { int unused; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) ((esp_sntp_config_t){0})
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg);
esp_err_t esp_netif_sntp_sync_wait(unsigned ticks);
void esp_netif_sntp_deinit(void);
typedef struct { int mode,state; bool network_ready; } read_pico_transfer_status_t;
enum { READ_PICO_TRANSFER_MODE_STA=1, READ_PICO_TRANSFER_MODE_AP=2, READ_PICO_TRANSFER_UPLOADING=7 };
void read_pico_transfer_get_status(read_pico_transfer_status_t *out);
typedef struct { bool busy; } cloud_status_t;
void cloud_get_status(cloud_status_t *out);
typedef struct { bool busy; } if_update_status_t;
void if_update_get_status(if_update_status_t *out);
esp_err_t app_sync_time_to_pmu(void);
#define ESP_LOGI(tag,fmt,...) ((void)(tag),(void)(fmt),(void)(__VA_ARGS__))
''')
    for name in ('esp_log.h','esp_netif_sntp.h','read_pico_transfer.h','cloud/cloud_sync.h','ota_online.h','sleep.h'):
        (work/name).write_text('#include "'+('../' if '/' in name else '')+'common.h"\n')
    (work/'test.c').write_text('''#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "common.h"
static int inits,deinits,waits,pmu,cloud_reads;
static int init_result,wait_result=1;
static time_t wall=1760000000;
static read_pico_transfer_status_t network;
static cloud_status_t cloud;
static if_update_status_t update;
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg){assert(cfg);++inits;return init_result;}
void esp_netif_sntp_deinit(void){++deinits;}
esp_err_t esp_netif_sntp_sync_wait(unsigned ticks){assert(ticks==0);++waits;return wait_result;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *out){*out=network;}
void cloud_get_status(cloud_status_t *out){++cloud_reads;*out=cloud;}
void if_update_get_status(if_update_status_t *out){*out=update;}
esp_err_t app_sync_time_to_pmu(void){++pmu;return 0;}
time_t fake_time(time_t *out){if(out)*out=wall;return wall;}
#define time fake_time
#include "time_service.c"
static void reset(void){
    s_owned=false;s_next_attempt=s_deadline=0;s_last_poll=-1000;s_idle_since=-1;
    inits=deinits=waits=pmu=cloud_reads=init_result=0;wait_result=1;
    network=(read_pico_transfer_status_t){.mode=READ_PICO_TRANSFER_MODE_STA,.network_ready=true};
    cloud=(cloud_status_t){0};update=(if_update_status_t){0};
}
int main(void){
    reset();network.network_ready=false;app_time_service_poll(0,true);app_time_service_poll(10000,true);
    assert(!inits&&!cloud_reads);network.network_ready=true;network.mode=READ_PICO_TRANSFER_MODE_AP;
    app_time_service_poll(11000,true);assert(!inits&&!cloud_reads);
    network.mode=READ_PICO_TRANSFER_MODE_STA;network.state=READ_PICO_TRANSFER_UPLOADING;
    app_time_service_poll(12000,true);assert(!inits&&!cloud_reads);
    reset();app_time_service_poll(0,true);app_time_service_poll(9999,true);assert(!inits);
    app_time_service_poll(10000,true);assert(inits==1&&s_owned);
    app_time_service_poll(10500,true);assert(!waits);
    wait_result=0;app_time_service_poll(11000,true);assert(deinits==1&&!s_owned&&pmu==1&&waits==1);
    app_time_service_poll(3610000,true);assert(inits==1);
    app_time_service_poll(3611000,true);assert(inits==2);
    app_time_service_poll(3611001,false);assert(deinits==2&&!s_owned);
    reset();app_time_service_poll(0,true);app_time_service_poll(10000,true);
    app_time_service_poll(24999,true);assert(s_owned);
    app_time_service_poll(25999,true);assert(!s_owned&&deinits==1);
    app_time_service_poll(309999,true);assert(inits==1);
    app_time_service_poll(310999,true);assert(inits==2);
    cloud.busy=true;app_time_service_poll(311999,true);assert(!s_owned&&deinits==2);
    reset();cloud.busy=true;app_time_service_poll(0,true);app_time_service_poll(10000,true);assert(!inits);
    cloud.busy=false;update.busy=true;app_time_service_poll(11000,true);assert(!inits);
    update.busy=false;app_time_service_poll(12000,true);assert(s_owned);
    app_time_service_stop();app_time_service_stop();assert(deinits==1);
    reset();init_result=1;app_time_service_poll(0,true);app_time_service_poll(10000,true);
    assert(inits==1&&!s_owned);app_time_service_stop();assert(!deinits);
    app_time_service_poll(20000,true);assert(inits==1);
    puts("time service: existing STA only, idle gate, zero-wait polling, hourly success, bounded retry and foreground cancellation passed");
}
''')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(work),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
