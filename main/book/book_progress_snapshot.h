/* SPDX-License-Identifier: Apache-2.0
 * 中文：首页与书架共用的有界内存进度快照，只由 UI 任务消费。
 * English: Bounded UI-task progress snapshot shared by Home and Books.
 */
#pragma once
#include "book_progress.h"

/// 校验介质与写入代号后复用；缓存不可用时沿用落盘遍历。/ Reuse only for matching media and revision; stream on cache failure.
esp_err_t book_progress_snapshot_list(book_progress_visit_fn visit, void* ctx);
/// 在存储锁内传入一次已确认的同路径写入及其实际序号。/ Under the storage lock, report a confirmed single-path write and its committed sequence.
void book_progress_snapshot_note(const char* path, const book_progress_t* progress, unsigned before, unsigned after);
/// 仅在完整变化区间属于同一路径时返回；路径指针在下一次 note 前有效。/ Return a delta only for a fully known single-path interval; path is borrowed until the next note.
const char* book_progress_snapshot_change(unsigned before, unsigned after, book_progress_t* progress);
