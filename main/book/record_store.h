/* SPDX-License-Identifier: Apache-2.0
 * 中文：非秘密内容记录的TF双槽逻辑视图；有界偏好继续NVS，内容键无卡只读。
 * English: SD dual-slot view for non-secret content records; bounded preferences remain in NVS, content writes require media.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nvs.h"
#include "record_store_media.h"
typedef uint64_t records_handle_t;
typedef struct records_iterator *records_iterator_t;
/// 仅可信挂载边界调用；切卡使旧句柄失效。/ Trusted mount boundaries only; media changes invalidate open handles.
void record_store_lock(void);
void record_store_unlock(void);
bool record_store_content_key(const char *space,const char *key);
esp_err_t records_open(const char *space,int mode,records_handle_t *out);
void records_close(records_handle_t h);
esp_err_t records_get_blob(records_handle_t h,const char *key,void *value,size_t *bytes);
esp_err_t records_get_str(records_handle_t h,const char *key,char *value,size_t *bytes);
esp_err_t records_get_u8(records_handle_t h,const char *key,uint8_t *value);
esp_err_t records_get_u32(records_handle_t h,const char *key,uint32_t *value);
esp_err_t records_set_blob(records_handle_t h,const char *key,const void *value,size_t bytes);
esp_err_t records_set_str(records_handle_t h,const char *key,const char *value);
esp_err_t records_set_u8(records_handle_t h,const char *key,uint8_t value);
esp_err_t records_set_u32(records_handle_t h,const char *key,uint32_t value);
esp_err_t records_erase_key(records_handle_t h,const char *key);
esp_err_t records_erase_all(records_handle_t h);
esp_err_t records_commit(records_handle_t h);
esp_err_t records_entry_find_in_handle(records_handle_t h,nvs_type_t filter,records_iterator_t *out);
esp_err_t records_entry_next(records_iterator_t *it);
esp_err_t records_entry_info(records_iterator_t it,nvs_entry_info_t *info);
void records_release_iterator(records_iterator_t it);
/// 原始TF状态，绝不通过旧NVS fallback证明已迁移。/ Raw SD state; never validate a migration through NVS fallback.
esp_err_t records_sd_state(const char *space,const char *key,bool *exists,bool strict);
esp_err_t records_migrate_key(const char *space,const char *key,nvs_type_t type,const void *value,size_t bytes);

esp_err_t records_archive_legacy(const char *space,const char *key,nvs_type_t type,const void *value,size_t bytes);

/// 严格验证TF当前值的领域格式；删除标记合法，不读NVS。/ Validate the current SD domain payload; tombstones are valid and NVS is never consulted.
typedef bool (*records_validate_fn)(const char *space,uint8_t type,const char *key,const void *value,size_t bytes);
esp_err_t records_sd_validate(const char *space,const char *key,records_validate_fn validate);
