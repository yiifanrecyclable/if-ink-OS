/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "book_ticket.h"
#include "reading_history_store.h"
#include "nvs.h"
#include "read_pico_pmu.h"
static time_t now;
time_t history_test_time(time_t *out){if(out)*out=now;return now;}
const pmu_snapshot_t *read_pico_pmu_get(void){return NULL;}
static void at(uint32_t day){now=(time_t)((uint64_t)day*86400-8*3600+12*3600);}
int main(void){
    const uint32_t jan1=20454; /* 2026-01-01 */
    at(jan1);
    assert(!reading_history_merge(1,123));assert(!reading_history_merge(jan1,0));
    nvs_handle_t h;assert(nvs_open("rp_ticket",NVS_READWRITE,&h)==ESP_OK);
    uint32_t heat[61]={0x52504831};unsigned slot=(jan1-1)%30;
    heat[1+slot]=jan1-1;heat[31+slot]=1200;
    assert(nvs_set_blob(h,"heatmap",heat,sizeof(heat))==ESP_OK);nvs_close(h);
    book_ticket_archive_days();assert(reading_history_read(jan1-1)==1200);
    assert(reading_history_first(jan1)==jan1-1);
    assert(book_ticket_record(60,1)==ESP_OK);assert(book_ticket_record(70,1)==ESP_OK);
    assert(reading_history_read(jan1)==130);
    for(uint32_t day=jan1+1;day<=jan1+65;++day){at(day);assert(book_ticket_record(30,1)==ESP_OK);}
    assert(reading_history_read(jan1)==130); /* Older than 30 days remains archived. */
    uint32_t recent[30],end;assert(book_ticket_recent_days(recent,&end)&&end==jan1+65);
    assert(recent[0]==30&&recent[29]==30);
    char *card=strdup(getenv("PICO_SIM_CARD_ROOT"));assert(card);
    setenv("PICO_SIM_CARD_ROOT","missing-card",1);
    assert(book_ticket_record(90,0)==ESP_OK); /* NVS retains totals while TF is absent. */
    setenv("PICO_SIM_CARD_ROOT",card,1);free(card);
    book_ticket_archive_days();assert(reading_history_read(jan1+65)==120);
    assert(reading_history_merge(jan1+65,40));assert(reading_history_read(jan1+65)==120);
    /* Tear the newer 120-second copy; the earlier 30-second copy survives. */
    FILE *f=fopen("/sdcard/.readpico/reading/2026.bin","r+b");assert(f);
    long offset=65*32+16+12;assert(!fseek(f,offset,SEEK_SET));uint32_t bad=0;
    assert(fwrite(&bad,1,4,f)==4);assert(!fclose(f));
    assert(reading_history_read(jan1+65)==30);
    book_ticket_archive_days();assert(reading_history_read(jan1+65)==120);
    const uint32_t leapday=19782;
    assert(reading_history_merge(leapday,42));assert(reading_history_read(leapday)==42);
    assert(reading_history_first(jan1)==leapday);
    assert(!reading_history_first(19723));
    uint32_t range[3];reading_history_range(jan1-1,3,range);
    assert(range[0]==1200&&range[1]==130&&range[2]==30);
    /* A settings reset retains the card-backed Home heatmap. */
    assert(nvs_open("rp_ticket",NVS_READWRITE,&h)==ESP_OK);assert(nvs_erase_key(h,"heatmap")==ESP_OK);nvs_close(h);
    assert(book_ticket_recent_days(recent,&end)&&recent[29]==120&&recent[0]==30);
    assert(reading_history_merge(23011,99)); /* 2033-01-01; no fixed retention window. */
    assert(reading_history_read(23011)==99&&reading_history_first(23011)==leapday);
    puts("reading history storage: migration, accumulation, 30-day rollover, missing card recovery, torn-copy recovery, leap year and multi-year retention OK");
    return 0;
}
