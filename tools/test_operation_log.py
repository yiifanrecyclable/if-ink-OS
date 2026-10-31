#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：隔离存储验证诊断时限、脱敏、容量、睡眠与失败重试。/ Isolated trace deadline, privacy, bounds, sleep and retry checks.
from pathlib import Path
import subprocess,tempfile,os
R=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=R/'build',prefix='diag-test-') as tmp:
 w=Path(tmp);card=w/'card';card.mkdir();(w/'build/simulator').mkdir(parents=True)
 for name in ('operation_log.c','operation_log.h'):(w/name).write_bytes((R/'main'/name).read_bytes())
 (w/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
 (w/'test.c').write_text(r'''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "operation_log.c"
static int64_t clock_us=1000;
static bool mounted=true,usb;
int64_t esp_timer_get_time(void){return clock_us;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){memset(out,0,sizeof(*out));out->mounted=mounted;return ESP_OK;}
bool usb_storage_active(void){return usb;}
const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t a={.version="0.2.0-beta.2"};return &a;}
static char text[1200000];
static void read_log(const char *path){FILE *f=fopen(path,"rb");assert(f);size_t n=fread(text,1,sizeof(text)-1,f);text[n]=0;fclose(f);}
int main(void){
 mounted=false;assert(operation_log_start()!=ESP_OK);mounted=true;assert(operation_log_start()==ESP_OK);
 assert(operation_log_start()!=ESP_OK&&operation_log_remaining()==120);
 cst836u_touch_t t={.touched=true,.count=1,.x=543,.y=666};
 operation_log_sample(&t,0,true,false,false);
 clock_us+=50000;operation_log_sample(&t,0,false,false,true);
 assert(s_count>0);clock_us+=120000000;operation_log_poll(false);
 assert(!operation_log_active()&&s_pending);assert(access("/sdcard/diagnostics/operation-0001.csv",F_OK));
 mounted=false;operation_log_poll(true);assert(s_pending);mounted=true;usb=true;operation_log_poll(true);assert(s_pending);usb=false;
 clock_us+=5000000;operation_log_poll(true);assert(!s_pending&&!s_records);
 read_log(operation_log_status());assert(strstr(text,"schema=1")&&strstr(text,"duration_ms=120000"));
 assert(strstr(text,"sample,1,-1,-1,1,0")&&strstr(text,"sample,1,543,666,4,0"));
 assert(operation_log_start()==ESP_OK);
 for(unsigned i=0;i<CAPACITY+100;++i){++clock_us;operation_log_event(OPLOG_TURN,1,2,3,0,5);}
 assert(s_count==CAPACITY&&s_dropped==100);operation_log_finish();assert(!s_pending);
 assert(strstr(operation_log_status(),"0002.csv"));read_log(operation_log_status());assert(strstr(text,"dropped=100"));
 assert(operation_log_start()==ESP_OK);clock_us+=1000;
 assert(!rename("/sdcard/diagnostics","/sdcard/diagnostics-old"));FILE *f=fopen("/sdcard/diagnostics","wb");assert(f);fclose(f);
 operation_log_finish();assert(s_pending&&s_records);assert(operation_log_start()!=ESP_OK);
 assert(!unlink("/sdcard/diagnostics"));assert(!rename("/sdcard/diagnostics-old","/sdcard/diagnostics"));clock_us+=5000001;
 operation_log_poll(true);assert(!s_pending&&!s_records);assert(strstr(operation_log_status(),"0003.csv"));
 puts("PASS: 120s capture, numeric-only redacted records, bounded ring/drop count, unique files, sleep finish, USB/card gates and failed-write retry.");
}
''')
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-DPICO_SIM_CARD_PATH="unused"','-include',str(R/'tools/simulator/fs.h'),'-I'+str(w),'-I'+str(R/'tools/simulator/include'),'-I'+str(R/'main'),'-I'+str(R/'main/book'),'-I'+str(R/'main/apps')]
 subprocess.run(flags+[str(w/'test.c'),str(R/'tools/simulator/fs.c'),'-o',str(w/'test')],check=True)
 subprocess.run([str(w/'test')],cwd=w,env={**os.environ,'PICO_SIM_CARD_ROOT':str(card)},check=True)
