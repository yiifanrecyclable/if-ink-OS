/* SPDX-License-Identifier: Apache-2.0
 * 中文：原版图片浏览与手势；宿主解码替代嵌入式解码器。
 * English: Production image rendering and gestures, with host image decoding.
 */
#define app_image simulator_descriptor_image
#include "../../main/apps/app_image.c"
#include "replay.h"
extern int simulator_route,simulator_image_view;
const char *simulator_image_path(void){
 if(s_requested_path[0])return s_requested_path;
 if(simulator_image_view>=0&&s_items&&s_selected>=0&&s_selected<s_count)return s_items[s_selected].path;
 return "";
}
void simulator_image(uint8_t *fb,int viewing,const char *path,const char *events){
 bool direct=path&&(!strncmp(path,"/sdcard/",8)||!strncmp(path,"/flash/",7));
 if(direct)(void)app_image_request_open(path);
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);if(viewing&&!direct)load_image(0);
 simulator_replay(&ctx,on_gesture,on_key,events);render(&ctx,fb);
 if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
 if(ctx.request_return)simulator_route=2;
 simulator_image_view=s_viewing?1:0;image_on_exit(&ctx);
}
