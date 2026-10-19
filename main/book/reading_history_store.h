/* SPDX-License-Identifier: Apache-2.0
 * 中文：按年保存每日累计秒数，固定内存，不按天数淘汰。
 * English: Year-indexed daily totals with bounded memory and no age-based eviction.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

uint32_t reading_history_read(uint32_t day);
/// 连续日期批读，同年共用文件句柄；调用方提供count个元素。/ Read consecutive days with one file handle per year into count caller-owned elements.
void reading_history_range(uint32_t first,unsigned count,uint32_t *seconds);
/// 幂等合并累计值；TF不可用或写入失败时返回false。/ Idempotently merge a cumulative total; false when storage is unavailable or fails.
bool reading_history_merge(uint32_t day,uint32_t seconds);
/// 返回不晚于today的最早有效记录，0表示尚无归档。/ Earliest valid archived day through today, or zero if none.
uint32_t reading_history_first(uint32_t today);
