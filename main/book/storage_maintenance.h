/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct {unsigned examined,migrated,skipped,failed;esp_err_t error;size_t available_before,available_after;} storage_maintenance_result_t;
/// 调用方确保已挂载、无USB独占、无HTTP/阅读写入；本函数不挂载/清空分区。/ Caller owns a mounted, USB-free, idle storage boundary; never mount or erase partitions.
esp_err_t storage_maintenance_run(storage_maintenance_result_t *out);
