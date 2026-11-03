/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"

// 阅读资料按记录流附在设置备份后，避免把大量书籍进度装入内存。
// Append reading data as a stream so a large library does not require a large allocation.
esp_err_t book_history_backup_write(FILE *file);
bool book_history_backup_validate(FILE *file);
// 底层替换操作，由设置恢复的整组回退快照保护，不能独立用于产品恢复。
// Low-level replacement; product restores must use the settings rollback snapshot.
esp_err_t book_history_backup_restore(FILE *file);

#include <stddef.h>
#include <stdint.h>
bool book_history_record_valid(const char *space,uint8_t type,const char *key,const void *value,size_t bytes);
