/* SPDX-License-Identifier: Apache-2.0
 * 中文：原版文件操作只作用于模拟器专属虚拟 SD。
 * English: Production file operations are confined to the simulator virtual SD.
 */
#define app_files simulator_descriptor_files
#include "../../main/apps/app_files.c"
// 模拟时间推进只作用于文件页；使用真机同一tick处理通知消退。
// Advance only file-page time, using the production tick for notice expiry.
#define SIM_REPLAY_RAW(ctx,type,x,y) do { if((type)=='n')(ctx)->now_ms+=(x); } while(0)
#define SIM_REPLAY_TICK(ctx) do { on_tick(ctx);prepare_thumbnails(); } while(0)
#include "replay.h"
#include "app_registry.h"
#include <assert.h>
extern int simulator_route,simulator_files_view,simulator_files_folder;
char simulator_files_dir[288];
int simulator_files_page(void){return s_page;}
int simulator_files_grid(void){return grid_active();}
int simulator_files_grid_available(void){return grid_available();}
int simulator_files_grid_modes(void){return (s_grid_modes[0]?1:0)|(s_grid_modes[1]?2:0);}
int simulator_files_grid_geometry(int part){if(!grid_available())return 0;EpdRect box=grid_cell(0);const int values[]={grid_columns(),grid_rows(),box.x,box.y,box.width,box.height,visible_rows()};return part<7?values[part]:0;}
int simulator_files_count(void){return s_count;}
int simulator_files_row_height(void){return file_row_height();}
const char *simulator_files_message(void){return s_message;}
const char *simulator_files_notice(void){return ui_nav_notice_text();}
unsigned simulator_files_notice_serial(void){return s_notice_serial;}
int simulator_files_card_geometry(int part){
 if(s_view!=FILE_VIEW_ACTIONS)return 0;
 EpdRect w=wallpaper_action_rect(),a=file_regular_action_rect(0),c=file_card_close_rect();
 const int values[]={file_card_top(),UI_NAV_TOP,selected_wallpaper_image()&&!s_delete_confirm,w.x,w.y,w.width,w.height,a.x,a.y,a.width,a.height,c.x,c.y,c.width,c.height,s_delete_confirm};
 return part<16?values[part]:0;
}
const char *simulator_files_item(int row){return row>=0&&row<s_count?s_files[row].path:"";}
const char *simulator_files_note(int row){const char *v=row>=0&&row<s_count?root_note(s_files[row].path):NULL;return v?v:"";}
int simulator_files_item_image_folder(int row){
 if(row<0||row>=s_count||!s_files[row].is_dir)return 0;
 if(grid_active()){int slot=row-s_page*visible_rows();return slot>=0&&slot<9&&s_thumbs[slot].image_folder;}
 return image_tree(s_files[row].path)>=0;
}
int simulator_files_thumb_ready(int row){return row>=0&&row<9&&s_thumbs[row].pixels!=NULL;}

void simulator_files(uint8_t *fb,int view,int folder,const char *dir,const char *events){
 app_font_activate_system();
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);on_tick(&ctx);
 s_folder=folder;if(folder>=0)scan_folder(folder);
 if(dir&&*dir){snprintf(s_dir,sizeof(s_dir),"%s",dir);scan_folder(folder>=0?folder:3);}
 s_view=(file_view_t)view;
 const char *modes=getenv("PICO_SIM_FILES_GRID");if(modes){unsigned mask=(unsigned)atoi(modes);s_grid_modes[0]=mask&1;s_grid_modes[1]=mask&2;}
 const char *restore_page=getenv("PICO_SIM_FILES_PAGE");if(restore_page){int page=atoi(restore_page),last=s_count?(s_count-1)/visible_rows():0;s_page=page<0?0:page>last?last:page;}
 if(s_count){s_selected=s_files[0];probe_wallpaper_action();snprintf(s_editor,sizeof(s_editor),"%s",s_selected.name);}
 if(view==FILE_VIEW_RENAME)editor_start();
 simulator_replay(&ctx,on_gesture,on_key,events);
 if(getenv("PICO_TEST_IMAGE_FILE_RETURN")){
  extern const app_desc_t simulator_descriptor_image;
  int folder_before=s_folder,page_before=s_page;char directory_before[288];strlcpy(directory_before,s_dir,sizeof(directory_before));
  assert(ctx.request_app&&s_image_origin.valid);files_on_exit(&ctx);ctx.request_app=NULL;
  simulator_descriptor_image.on_enter(&ctx);
  unsigned x=0,y=0;sscanf(getenv("PICO_TEST_IMAGE_FILE_RETURN"),"%u,%u",&x,&y);
  ui_gesture_event_t tap={.type=UI_GESTURE_TAP,.x0=x,.y0=y,.x=x,.y=y};
  simulator_descriptor_image.on_gesture(&ctx,&tap);assert(ctx.request_return&&s_image_restore);
  simulator_descriptor_image.on_exit(&ctx);ctx.request_return=false;on_enter(&ctx);on_tick(&ctx);
  assert(s_folder==folder_before&&s_page==page_before&&!strcmp(s_dir,directory_before));
 }
 if(getenv("PICO_TEST_FILES_NOTICE_FULL")){
  // 与主循环AREA提升为FULL的顺序一致，随后再验证键盘局部呈现。
  // Match the loop's AREA-to-FULL ordering, then verify the next keyboard-area presentation.
  assert(ui_nav_notice_text()[0]);ctx.now_ms+=5000;
  assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_status_area.height==UI_STATUS_HEIGHT);
  assert(!files_present(&ctx,APP_REDRAW_FULL)&&s_status_area.height==0);
  editor_start();render_rename(fb);
  assert(files_present(&ctx,APP_REDRAW_AREA)&&s_status_area.height==0);
  s_status_area=(EpdRect){0,0,684,56};files_on_exit(&ctx);assert(!s_status_area.height);
  s_status_area=(EpdRect){0,0,684,56};on_enter(&ctx);assert(!s_status_area.height);
 }
 prepare_thumbnails();render(&ctx,fb);
 if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
 simulator_files_view=s_view;simulator_files_folder=s_folder;snprintf(simulator_files_dir,sizeof(simulator_files_dir),"%s",s_dir);
}

int simulator_wallpaper_picker_state(int part){const int values[]={s_picker,s_picker_rotation,s_picker_grid,s_view==FILE_VIEW_WALLPAPER_PREVIEW,s_picker_eligible,s_page,s_count,visible_rows(),file_pager_top()};return part<9?values[part]:0;}
int simulator_wallpaper_picker_hit(int part){EpdRect r=part==0?picker_exit_rect():part==1?picker_up_rect():part==2?grid_toggle_rect():picker_button_rect();return r.x;}
const char *simulator_wallpaper_picker_dir(void){return s_dir;}
