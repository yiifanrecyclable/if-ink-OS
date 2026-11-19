/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <unistd.h>

#define BOOK_RECENT_FILE "/sdcard/.readpico/recent-reading.ifr"
#define BOOK_RECENT_TEMP "/sdcard/.readpico/recent-reading.tmp"

/// 非顺序写入前丢弃可重建的摘要。/ Discard the rebuildable summary before out-of-sequence writes.
static inline void book_recent_disk_invalidate(void) {
    (void)unlink(BOOK_RECENT_FILE);
}
