/* SPDX-License-Identifier: Apache-2.0
 * 两分钟本地诊断，不记录文本或凭据。/ Two-minute local diagnostics without text or credentials.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cst836u.h"
typedef enum { OPLOG_SAMPLE=1,OPLOG_GAP,OPLOG_GESTURE,OPLOG_TURN,OPLOG_PRESENT,OPLOG_MULTI,OPLOG_PAGE,OPLOG_SLEEP,OPLOG_CLOCK,OPLOG_READ,OPLOG_POINT,OPLOG_CANCEL } operation_log_event_t;
/// 启动/查询两分钟内存记录；只由UI任务调用。/ Start/query two-minute RAM capture from the UI task only.
esp_err_t operation_log_start(void);
bool operation_log_active(void);
/// 已结束但尚未成功写卡。/ Capture finished, awaiting successful card export.
bool operation_log_pending(void);
unsigned operation_log_remaining(void);
const char *operation_log_status(void);
/// 固定数值字段，不接受任意字符串。/ Fixed numeric fields; arbitrary strings are never accepted.
void operation_log_event(operation_log_event_t type,int a,int b,int c,int d,int e);
/// 正文外仅记录时序和触点数，隐藏坐标。/ Outside the reader body log timing/count only, with coordinates redacted.
void operation_log_sample(const cst836u_touch_t *touch,esp_err_t error,bool pressed,bool released,bool reader_body);
/// 结束后空闲导出；休眠/USB前提前结束，不边采样边写卡。/ Export when idle after capture; finish before sleep/USB without live card writes.
void operation_log_poll(bool safe);
void operation_log_finish(void);
