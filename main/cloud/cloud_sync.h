/* SPDX-License-Identifier: Apache-2.0
 * 中文：坚果云只读拉取会话；UI只读取快照，离开/锁屏/拔卡取消并等待任务退出。
 * English: Read-only Nutstore session; UI reads snapshots and cancellation joins before exit, sleep or media
 * loss.
 * 用户修订：云端原名管理阅读文件、PNG/JPEG图片和TTF字体，按类型落盘；旧清单不进入列表。
 * User revision: retain original reading/image/TTF names and route by type; hide legacy manifests.
 */
#pragma once
#include "cloud_protocol.h"
#include "cloud_config.h"
typedef struct {
    char href[241], name[241], filename[241], etag[129], revision_id[65], date[25], error[96];
    uint64_t date_ms, bytes;
    bool selected, selectable, manifest, date_fallback;
} cloud_entry_t;
typedef struct {
    unsigned revision, count, done, total, failed;
    uint64_t bytes_done, bytes_total;
    bool busy, configured, cancel_requested;
    /// 顶部短状态与操作详情分别保存。/ Keep the short header status separate from operation details.
    char folder[241], indicator[96], message[160];
} cloud_status_t;
void cloud_get_status(cloud_status_t *status);
bool cloud_get_entry(size_t index, cloud_entry_t *entry);
esp_err_t cloud_refresh(void);
esp_err_t cloud_toggle(size_t index);
esp_err_t cloud_download_selected(void);
void cloud_cancel(void);
void cloud_close(void);
