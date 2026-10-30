/* SPDX-License-Identifier: Apache-2.0
 * 中文：Flash擦写与屏幕输出共享的互斥边界。
 * English: Shared exclusion boundary for flash mutations and display output.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/// 必须配对；网络等待不占锁，Flash相关错误返回也须释放。/ Pair calls; do not lock network waits and release on flash errors too.
void display_flash_begin(void);
void display_flash_end(void);

#ifdef __cplusplus
}
#endif
