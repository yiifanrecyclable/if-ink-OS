/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "book_progress.h"
#include "book_store.h"

#define BOOK_RECENT_BOOKS 9
#define BOOK_RECENT_DOCUMENTS 6

typedef struct {
    char path[BOOK_STORE_PATH_MAX]; ///< 完整路径。/ Full path.
    char title[256]; ///< 显示标题。/ Display title.
    book_progress_t progress; ///< 已提交的进度副本。/ Copy of committed progress.
} book_recent_entry_t;

typedef struct {
    uint32_t book_count, document_count;
    uint32_t all_books;
    book_recent_entry_t books[BOOK_RECENT_BOOKS];
    book_recent_entry_t documents[BOOK_RECENT_DOCUMENTS];
} book_recent_view_t;

/// UI任务读取摘要；允许重建时才遍历历史。结果只在下一次索引操作前有效。
/// UI-task summary lookup. Walk the history only when allowed to rebuild. Valid until the next index operation.
const book_recent_view_t *book_recent_index_get(bool rebuild);
/// 只接纳已提交的单条写入，由持久进度写入方在同一锁内调用。
/// Accept one committed write from the progress writer under the same lock.
void book_recent_index_note(const char *path, const book_progress_t *progress, unsigned before, unsigned after);
/// 在阅读结束或空闲期间提交已变动的摘要。失败可回退到原始进度。
/// Persist a changed summary on reader exit or while idle. Progress remains available on failure.
void book_recent_index_flush(void);
