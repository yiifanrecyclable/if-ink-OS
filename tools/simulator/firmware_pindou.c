/* SPDX-License-Identifier: Apache-2.0
 * 中文：重放真机拼豆状态机与保存过程，存储仍受模拟器路径隔离保护。
 * English: Replay production bead controls and export within simulator storage isolation.
 */
#define app_pindou simulator_descriptor_pindou
#include "../../main/apps/app_pindou.c"
#include "replay.h"
#include "app_font_context.h"
extern int simulator_route;
void simulator_pindou(uint8_t *fb,const char *events) {
    app_font_activate_system();
    app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);render(&ctx,fb);
    simulator_replay(&ctx,on_gesture,on_key,events);render(&ctx,fb);
    if(ctx.request_app)simulator_route=2;
    beads_on_exit(&ctx);
}
