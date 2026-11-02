/* SPDX-License-Identifier: Apache-2.0
 * 中文：连接配置及设置备份接口，不暴露网络或文章清单结构。
 * English: Connection settings and backup contract, independent of network and article structures.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#define CLOUD_DEFAULT_FOLDER "PicoBox"
typedef struct {
    char folder[241], account[129], password[257];
} cloud_config_t;
void cloud_config_load(cloud_config_t *config);
/// 编辑读取失败不输出默认凭据；缺失键沿用未配置值。/ Checked editing load; errors preserve output, missing keys use unconfigured defaults.
esp_err_t cloud_config_load_checked(cloud_config_t *config);
esp_err_t cloud_config_save(const cloud_config_t *config);
esp_err_t cloud_config_forget(void);
bool cloud_config_valid(const cloud_config_t *config, bool require_secret);
/// 为设置备份读取凭据；读取失败或同步中不生成不完整备份。/ Read credentials for settings backup; fail on storage errors or an active sync.
esp_err_t cloud_config_backup_export(cloud_config_t *config);
/// 从已校验的设置备份恢复连接配置。/ Restore credentials from a validated settings backup.
esp_err_t cloud_config_backup_restore(const cloud_config_t *config);
