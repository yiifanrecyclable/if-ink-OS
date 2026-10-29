#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：真实PMU按键消费与唤醒松开保护的确定性回归。/ English: Production PMU event consumption and wake-release guard regressions."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'components/read_pico_pmu/read_pico_pmu.c').read_text()
def extract(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M)
    assert m,name
    end=m.end();depth=1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[m.start():end]
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'driver').mkdir()
    (p/'driver/i2c_master.h').write_text('#pragma once\ntypedef void *i2c_master_bus_handle_t;\n')
    (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n')
    code='''#include "read_pico_pmu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static pmu_snapshot_t s_snap;
static bool s_key_release_pending,held,fail_poll,fail_ack;
static int64_t s_key_idle_since_us,now=1000;
static uint8_t queue[32];static unsigned count,acked;
#define TAG "test"
#define ESP_LOGI(tag,fmt,...) ((void)(tag),(void)sizeof(printf(fmt,__VA_ARGS__)))
const char *pmu_event_name(uint8_t n){(void)n;return "key";}
int64_t esp_timer_get_time(void){return now;}
esp_err_t read_pico_pmu_poll(void){
 if(fail_poll)return ESP_FAIL;
 s_snap.status_ok=true;s_snap.key_state=held;s_snap.pending_events=count;
 s_snap.event_ok=count>0;s_snap.event.type=count?queue[0]:0;s_snap.event.event_id=count?1:0;return ESP_OK;
}
esp_err_t read_pico_pmu_event_ack(uint16_t id){
 assert(id==1);if(fail_ack)return ESP_FAIL;
 assert(count);memmove(queue,queue+1,--count);++acked;return read_pico_pmu_poll();
}
static void event(uint8_t e){queue[count++]=e;}
'''
    code+='\n'.join(extract(f) for f in ['read_pico_pmu_drain_events','read_pico_pmu_suppress_key_until_release','key_release_guard','read_pico_pmu_take_key_action','read_pico_pmu_take_key_short','read_pico_pmu_take_key_wakeup'])
    code+='''
int main(void){
 // Release can arrive well after the previous fixed boot-ignore timeout.
 held=true;event(PMU_EVT_KEY_DOWN);assert(read_pico_pmu_take_key_wakeup());
 assert(s_key_release_pending);now+=5000000;assert(!read_pico_pmu_take_key_action());
 held=false;event(PMU_EVT_KEY_SHORT);assert(!read_pico_pmu_take_key_action());
 now+=150000;assert(!read_pico_pmu_take_key_action());assert(!s_key_release_pending);
 event(PMU_EVT_KEY_SHORT);assert(read_pico_pmu_take_key_short());assert(!read_pico_pmu_take_key_short());
 // Read failure and key bounce reset the quiet interval; no release inferred from stale status.
 read_pico_pmu_suppress_key_until_release();assert(!read_pico_pmu_take_key_action());
 now+=150000;fail_poll=true;assert(!read_pico_pmu_take_key_action());assert(s_key_release_pending);
 fail_poll=false;now+=150000;assert(!read_pico_pmu_take_key_action());held=true;now+=150000;
 assert(!read_pico_pmu_take_key_action());held=false;now+=150000;assert(!read_pico_pmu_take_key_action());
 now+=150000;assert(!read_pico_pmu_take_key_action());assert(!s_key_release_pending);
 // Failed ACK is retried without dispatching the same lock operation twice.
 event(PMU_EVT_KEY_SHORT);fail_ack=true;assert(!read_pico_pmu_take_key_action());assert(count==1);
 fail_ack=false;assert(read_pico_pmu_take_key_short());assert(!read_pico_pmu_take_key_action());
 event(PMU_EVT_KEY_SHORT);event(PMU_EVT_KEY_LONG);assert(read_pico_pmu_take_key_action()==READ_PICO_PMU_KEY_LONG);
 event(PMU_EVT_KEY_SHORT);held=false;assert(read_pico_pmu_take_key_wakeup());assert(!read_pico_pmu_take_key_action());
 now+=150000;assert(!read_pico_pmu_take_key_action());event(PMU_EVT_KEY_LONG);
 assert(read_pico_pmu_take_key_action()==READ_PICO_PMU_KEY_LONG);
 puts("PASS: delayed wake release, held/bouncing key, I2C/ACK failure, one-shot normal short/long and priority");
}
'''
    (p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+tmp,'-I'+str(root/'components/read_pico_pmu/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
