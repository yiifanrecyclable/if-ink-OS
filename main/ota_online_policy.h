/* SPDX-License-Identifier: Apache-2.0
 * 中文：更新清单与版本校验，不访问网络/Flash。
 * English: Manifest/version validation without network or flash access.
 */
#pragma once
#include "ota_online.h"
#include <stddef.h>
typedef struct {
    bool available;
    if_update_channel_t channel; ///< 通过校验的清单通道 / Validated manifest channel
    char version[33], asset_url[160], blob_url[160];
    uint32_t size;
    uint8_t sha256[32];
} if_update_candidate_t;
/// 兼容旧调用：只接收稳定版清单。/ Compatibility entry: accept stable manifests only.
bool if_update_parse_manifest(const char *json, size_t length, if_update_candidate_t *out);
/// 只接收所选通道及本产品的固定分区、仓库清单。/ Accept only the selected channel, product, partition layout and repository.
bool if_update_parse_manifest_channel(const char *json, size_t length, if_update_channel_t channel, if_update_candidate_t *out);
typedef enum {
    IF_UPDATE_INSTALL_NONE, IF_UPDATE_INSTALL_UPGRADE, IF_UPDATE_INSTALL_DOWNGRADE,
} if_update_install_kind_t;
/// 所选通道可手动安装不同版本；本地测试包只能作为当前版本。/ Allow a different release on the selected channel; local test versions are valid only as the current version.
if_update_install_kind_t if_update_install_kind(const char *candidate, const char *current, if_update_channel_t channel);
/// 仅允许GitHub已知HTTPS资源重定向，不携带账号。/ Permit known GitHub HTTPS asset redirects without credentials.
bool if_update_redirect_allowed(const char *url);
