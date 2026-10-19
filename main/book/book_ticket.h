/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：记录实际阅读时长和翻页，并绘制票根锁屏。
 * English: Record actual reading time and turns, and paint the ticket lock face.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t book_ticket_record(uint32_t seconds, uint32_t turns);
/// 近 30 天（最早到今天）的实测阅读秒数；时钟无效时返回 false 且清零。
/// Measured seconds for the last 30 days, oldest to today; false and zeroed without a valid clock.
/// end_day 可为空；否则接收与统计同一快照的本地日序号（UTC+8）。
/// Optional end_day receives the history snapshot's local day number (UTC+8).
bool book_ticket_recent_days(uint32_t seconds[30], uint32_t *end_day);
/// 把NVS中的近期记录幂等归档到TF卡，任一非空日保存失败返回false。/ Idempotently archive recent NVS days to TF; false if any nonempty day fails.
bool book_ticket_archive_days(void);
bool book_ticket_draw(uint8_t *fb, bool reader_background);
