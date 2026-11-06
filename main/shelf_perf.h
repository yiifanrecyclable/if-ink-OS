/* SPDX-License-Identifier: Apache-2.0
 * 本地r9t诊断：UI任务内只记有界RAM数据，结束后才写卡，不接受文章内容。
 * Local r9t diagnostics: bounded UI-task RAM capture, exported only after capture; no content strings.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    SP_BOOT=1,SP_ENTER,SP_REUSE,SP_SCAN,SP_ROOTS,SP_CAPACITY,SP_ENUMERATE,
    SP_DOCUMENTS,SP_RENDER,SP_COVER_LOAD,SP_CACHE_PROBE,SP_CACHE_WRITE,
    SP_INDEX_PROBE,SP_INDEX_WRITE,SP_PRESENT,SP_EPD,SP_FRAME,SP_LOOP_GAP,
    SP_INPUT,SP_MEDIA,SP_SCAN_WAIT,
    SP_NAV_SWITCH,SP_NAV_EXIT,SP_NAV_FONT,SP_NAV_ENTER,SP_NAV_PRESENT,
    SP_NAV_RENDER,SP_NAV_OUTPUT,SP_HOME_BANNER,
    SP_COVER_SOURCE,SP_COVER_DECODE,SP_COVER_PERSIST_READ,SP_COVER_PERSIST_WRITE,
    SP_WAKE_BOOT,SP_WAKE_SETTINGS,SP_WAKE_BOARD,SP_WAKE_PMU,SP_WAKE_SD_WAIT,
    SP_WAKE_RESTORE,SP_WAKE_RESUME_RESULT,SP_WAKE_SPLASH,SP_WAKE_FONT,
    SP_WAKE_UI_PREPARE,SP_WAKE_FIRST_FRAME,SP_WAKE_UI_READY,
    SP_WAKE_CLEAR,SP_WAKE_OUTPUT,SP_WAKE_LIGHT,
    SP_WAKE_TONE,SP_WAKE_MAINTENANCE,SP_WAKE_ASSETS,SP_WAKE_FONT_RETRY,
    SP_WAKE_PAGE_ENTER,SP_WAKE_MEDIA_POLL,SP_WAKE_HOME_RECENTS,SP_WAKE_HOME_HISTORY,
    SP_WAKE_HOME_SCAN,SP_WAKE_HOME_COVER_CACHE,
    SP_BANNER_LAYER_KEY,SP_BANNER_LAYER_READ,SP_BANNER_LAYER_RENDER,
    SP_BANNER_LAYER_WRITE,SP_BANNER_LAYER_COMPOSE,SP_WAKE_NOTICE
} shelf_perf_stage_t;
typedef struct {int64_t start;uint32_t request;int stage,a,b;} shelf_perf_span_t;
#if defined(ESP_PLATFORM) || defined(SHELF_PERF_HOST_TEST)
void shelf_perf_init(void);
void shelf_perf_request(int view,int epoch);
void shelf_perf_event(shelf_perf_stage_t stage,int a,int b,int c,int d);
shelf_perf_span_t shelf_perf_begin(shelf_perf_stage_t stage,int a,int b);
void shelf_perf_end(shelf_perf_span_t *span);
void shelf_perf_loop(bool pressed,bool released,int points);
void shelf_perf_finish(void);
void shelf_perf_poll(bool safe);
#else
// 普通宿主回归不启用采集；独立诊断测试编译真实模块。/ Only dedicated host diagnostics enable the real capture module.
static inline void shelf_perf_init(void){}
#define shelf_perf_request(...) ((void)0)
#define shelf_perf_event(...) ((void)0)
static inline shelf_perf_span_t shelf_perf_begin(shelf_perf_stage_t s,int a,int b){(void)s;(void)a;(void)b;return(shelf_perf_span_t){0};}
static inline void shelf_perf_end(shelf_perf_span_t *s){(void)s;}
static inline void shelf_perf_loop(bool p,bool r,int n){(void)p;(void)r;(void)n;}
static inline void shelf_perf_finish(void){}
static inline void shelf_perf_poll(bool safe){(void)safe;}
#endif
#define SHELF_PERF_SCOPE(name,stage,a,b) shelf_perf_span_t name __attribute__((cleanup(shelf_perf_end)))=shelf_perf_begin(stage,a,b)
#define SHELF_PERF_SCOPE_IF(name,condition,stage,a,b) shelf_perf_span_t name __attribute__((cleanup(shelf_perf_end)))=(condition)?shelf_perf_begin(stage,a,b):(shelf_perf_span_t){0}

