/* SPDX-License-Identifier: Apache-2.0
 * 中文：原版文件操作只作用于模拟器专属虚拟 SD。
 * English: Production file operations are confined to the simulator virtual SD.
 */
#define app_files simulator_descriptor_files
#include "../../main/apps/app_files.c"
#include "replay.h"
#include "app_registry.h"
extern int simulator_route,simulator_files_view,simulator_files_folder;
char simulator_files_dir[288];
int simulator_files_page(void){return s_page;}
void simulator_files(uint8_t *fb,int view,int folder,const char *dir,const char *events){
 app_font_activate_system();
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);on_tick(&ctx);
 s_folder=folder;if(folder>=0)scan_folder(folder);
 if(dir&&*dir){snprintf(s_dir,sizeof(s_dir),"%s",dir);scan_folder(folder>=0?folder:3);}
 s_view=(file_view_t)view;
 if(s_count){s_selected=s_files[0];snprintf(s_editor,sizeof(s_editor),"%s",s_selected.name);}
 if(view==FILE_VIEW_RENAME)editor_start();
 simulator_replay(&ctx,on_gesture,on_key,events);render(&ctx,fb);
 if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
 simulator_files_view=s_view;simulator_files_folder=s_folder;snprintf(simulator_files_dir,sizeof(simulator_files_dir),"%s",s_dir);
}
