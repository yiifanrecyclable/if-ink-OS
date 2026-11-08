/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF Markdown 入库日期、修订和独占附件管理。
 * English: TF Markdown import dates, revisions and owned attachments.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DOCUMENT_ROOT "/sdcard/markdown"
typedef struct {
    char name[256], path[288], date[11];
    uint32_t size;
    uint8_t pct;
    bool pinned, starred, selected, cleanup_pending, has_progress;
    uint32_t recent;
} document_entry_t;
esp_err_t document_store_validate(const char *staging_path);
/// WiFi 暂存 MD 通过同一事务入库，引用的灰度 PNG 必须已在 attachments。
/// Import a staged WiFi MD through the same transaction; grayscale PNGs must already exist in attachments.
esp_err_t document_store_import_local(const char *staging_path, const char *destination);
esp_err_t document_store_scan(document_entry_t **entries, size_t *count);
/// 只读文件属性和文档摘要。/ Read file attributes and document summary only.
esp_err_t document_store_read_entry(const char *path, document_entry_t *entry);
/// 文档入库变更代号，独立于图书清单。/ Document import revision, independent of the book catalog.
unsigned document_store_revision(void);
#define DOCUMENT_STORE_CHANGE_LIMIT 8
/// 读取连续的已确认变更；缺一条即要求重新对账。/ Read consecutive confirmed changes, or require a directory rescan.
esp_err_t document_store_read_changes(unsigned after, unsigned through,
                                     document_entry_t *entries, size_t capacity);
esp_err_t document_store_refresh(const char *path, bool *changed);
esp_err_t document_store_delete(const char *path);
/// 本机文档目录与移动，保留根目录同步契约。/ Local document paths and moves, preserving the flat sync contract.
bool document_store_local_directory(const char *path);
bool document_store_local_path(const char *path);
bool document_store_has_local(void);
esp_err_t document_store_move(const char *source, const char *target, bool *moved);
// 置顶同名更新保持、删除清理；starred参数仅保留旧数据兼容，不再提供星标界面。
// Pins survive replacement and clear on deletion; starred remains for legacy data compatibility only.
esp_err_t document_store_set_flags(const char *path, bool pinned, bool starred);

// 同步暂存与可恢复提交；只操作TF卡，不访问网络。
// Recoverable TF-only synchronization transactions, independent of networking.
#define DOCUMENT_SYNC_STAGE "/sdcard/.pico-sync/current"
#define DOCUMENT_SYNC_ASSETS_MAX 64
typedef struct {
    char name[241], sha256[65];
    uint32_t bytes;
} document_sync_asset_t;
esp_err_t document_store_hash_file(const char *path, char sha256[65], uint64_t *bytes);
bool document_store_safe_filename(const char *name);
esp_err_t document_store_sync_prepare(void);
esp_err_t document_store_sync_validate(const char *filename, const document_sync_asset_t *assets,
                                       size_t count);
/// 成功必须验证实际目标及附件；完成回滚不等于安装成功。/ Success verifies installed targets/assets; successful rollback is not successful installation.
esp_err_t document_store_sync_commit(const char *destination, const char *body_sha256, uint64_t body_bytes,
                                     const document_sync_asset_t *assets, size_t count, bool *unchanged);
esp_err_t document_store_sync_recover(void);
esp_err_t document_store_sync_abort(void);
#ifdef PICO_HOST_SIMULATOR
// Deterministic crash injection is available only to host verification.
extern int document_store_test_crash;
#endif
bool document_store_hash_bytes(const void *data, size_t length, char sha256[65]);
esp_err_t document_store_sync_collect_assets(document_sync_asset_t assets[64], size_t *count);
esp_err_t document_store_recent(document_entry_t entries[3], size_t *count);
