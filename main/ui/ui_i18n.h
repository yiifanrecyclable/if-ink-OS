/* SPDX-License-Identifier: Apache-2.0
 * 中文：显式标记系统文案，保留用户内容；字典与格式化不依赖网络或字体。
 * English: Mark system messages explicitly, preserving user content; lookup/formatting is offline and font independent.
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#define UI_MSG(text) "\036" text "\037"
/// 由设置层在加载或保存语言后更新；后台提示读取原子快照。/ Settings updates this after loading/saving; background notices read an atomic snapshot.
void ui_i18n_set_english(bool english);
bool ui_i18n_english(void);
/// 仅解析标记过的完整文案；用户字符串原样返回。/ Resolve whole marked messages only; return user strings unchanged.
const char *ui_message(const char *text);
/// 格式化前翻译模板、格式化后解析嵌套标签，保持printf返回值语义。/ Translate templates and nested labels while retaining snprintf length semantics.
int ui_snprintf(char *out, size_t size, const char *format, ...) __attribute__((format(printf,3,4)));
