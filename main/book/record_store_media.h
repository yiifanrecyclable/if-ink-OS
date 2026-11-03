/* SPDX-License-Identifier: Apache-2.0
 * 中文：可信挂载生命周期，不暴露NVS或记录格式。/ English: Trusted mount lifecycle, independent of NVS and record formats.
 */
#pragma once
#include <stdbool.h>
/// 切卡或USB独占使旧句柄失效。/ Card changes or exclusive USB access invalidate existing handles.
void record_store_media(bool ready);
bool record_store_ready(void);
