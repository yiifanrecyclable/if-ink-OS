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
/// 稳定版必须严格更新；兼容旧IF编号及本地test版本。/ Require a strictly newer stable release; accept legacy IF/test current versions.
bool if_update_newer(const char *candidate, const char *current);
/// 所选通道的新版本；测试版允许用户明确切回较早稳定版。/ Accept newer channel releases, or an explicitly requested return from beta to an older stable release.
bool if_update_candidate_allowed(const char *candidate, const char *current, if_update_channel_t channel);
/// 仅允许GitHub已知HTTPS资源重定向，不携带账号。/ Permit known GitHub HTTPS asset redirects without credentials.
bool if_update_redirect_allowed(const char *url);
