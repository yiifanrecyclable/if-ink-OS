/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_http_client.h"
/// 仅对国内清单请求添加经同意的字段。/ Attach consented fields to the domestic feed request only.
void ota_stats_add_headers(esp_http_client_handle_t http,const char *version);
/// 联网检查时处理已撤回但尚未确认的删除。/ Retry an explicitly requested deletion during an online check.
void ota_stats_flush_withdrawal(void);
