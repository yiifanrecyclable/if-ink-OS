/* SPDX-License-Identifier: Apache-2.0
 * 中文：一次性深睡续读及经过校验的物理锁屏参考；错误回到普通开机。
 * English: One-shot deep-sleep resume with a checked retained-panel reference; faults use normal boot.
 * 冻结：恢复请求必须先消费；不擦其他NVS、不在render写存储、不恢复异常复位。
 * Frozen: Consume before restoring; never erase other NVS, persist from render, or resume abnormal resets.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SLEEP_RESUME_PATH_MAX 288
typedef struct { uint8_t tab; char path[SLEEP_RESUME_PATH_MAX]; } sleep_resume_target_t;
/// 设置本次锁屏的返回目标，空路径表示主页面。/ Set the lock return target; an empty path means a main root.
void sleep_resume_set_target(unsigned tab,const char *path);
/// 读取并消费小型恢复标记，在分配大缓冲前调用。/ Read and consume the small token before allocating large buffers.
void sleep_resume_init(const uint8_t build_id[32],bool normal_poweron);
/// 完整锁屏成功推屏后，在深睡前保存；失败不影响休眠。/ Save only a successfully presented lock before deep sleep; failure cannot block sleep.
bool sleep_resume_save(const uint8_t *frame,size_t bytes,unsigned rotation);
/// 只有校验完成才覆盖back并返回目标；失败调用方重建普通开机画面。/ Commit back and return the target only after validation; the caller rebuilds normal boot on failure.
bool sleep_resume_restore(uint8_t *front,uint8_t *back,size_t bytes,unsigned rotation,sleep_resume_target_t *target);
/// 已验证后不再保留内存中的启动候选。/ Discard the in-memory startup candidate.
void sleep_resume_discard(void);

/// 是否存在本次开机尚未验证的快照候选。/ Whether this boot has a snapshot candidate to validate.
bool sleep_resume_pending(void);
