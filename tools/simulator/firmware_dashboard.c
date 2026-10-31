/* SPDX-License-Identifier: Apache-2.0
 * 中文：原版首页和手势，图书读取虚拟 SD，硬件状态为样例。
 * English: Production home and gestures over virtual SD and modeled hardware.
 */
#define app_dashboard simulator_descriptor_dashboard
#include "../../main/apps/app_dashboard.c"
#include "replay.h"
#include "app_registry.h"
extern int simulator_route;
void simulator_dashboard(uint8_t *fb,const uint32_t *days,const char *events){
 app_ctx_t ctx={.fb=fb,.now_ms=1000};on_enter(&ctx);on_tick(&ctx);on_tick(&ctx);
 s_reading_days_valid=true;memcpy(s_reading_days,days,30*sizeof(*days));
 const char *day=getenv("PICO_SIM_HOME_DAY");if(day){unsigned long value=strtoul(day,NULL,10);if(value>=19723&&value<50000)s_reading_end_day=(uint32_t)value;}
 prepare_quote_bitmap();
 simulator_replay(&ctx,on_gesture,on_key,events);render(&ctx,fb);
 if(ctx.request_app)simulator_route=app_index_of(ctx.request_app);
}

int simulator_home_geometry(int item) {
 home_geometry_t g=home_geometry();
 const int values[]={g.list_y,g.row_height,g.rows,g.grid_y,g.col_x,g.col_step,g.row_step,g.month_x,g.week_y,g.heat_title,g.recent_rule,g.recent_title};
 return item>=0 && item<(int)(sizeof(values)/sizeof(values[0]))?values[item]:0;
}

int simulator_home_cover_geometry(unsigned part) {
 home_book_t *item=s_current.path[0]?&s_current:&s_recent[0];
 if(!item->path[0])return 0;
 home_geometry_t g=home_geometry();unsigned pct=item->has_progress?item->progress.pct:0;if(pct>100)pct=100;
 recent_title_layout_t title=recent_title_layout(item->title,172);
 char detail[48];snprintf(detail,sizeof(detail),"阅读进度%u%%",pct);
 recent_title_layout_t caption=recent_title_layout_role(detail,172,UI_TEXT_CAPTION,2);
 EpdRect image=home_cover_rect(item,home_cover_slot(g,title.count*title.line_height,caption.count*caption.line_height));
 const int values[]={image.x,image.y,image.width,image.height,(int)item->cover_width,(int)item->cover_height};
 return part<6?values[part]:0;
}

int simulator_history_state(unsigned part) {
 const int values[]={s_history_open,s_history_target_open,s_history_page,history_pages(),s_history_count,(int)s_history_first,(int)s_history_today,history_rows(),history_row_height(),history_target_row_height(),history_list_top()};
 return part<11?values[part]:0;
}
unsigned long long simulator_history_seconds(unsigned row){return row<HOME_HISTORY_MAX_ROWS?s_history_seconds[row]:0;}

bool simulator_home_has_documents(void){return s_has_documents;}
const char *simulator_home_list_path(unsigned row){const char *path=recent_list_path(row);return path?path:"";}
const char *simulator_home_featured_path(void){return s_current.path[0]?s_current.path:s_recent[0].path;}

unsigned simulator_reading_target(void){return content_reading_target_minutes();}
unsigned simulator_history_bar_width(unsigned row){return row<HOME_HISTORY_MAX_ROWS?history_bar_width(s_history_seconds[row],content_reading_target_minutes()):0;}

// 直接观察实际首页数据，避免测试另算百分比。/ Observe actual Home state rather than recalculating progress in tests.
int simulator_home_progress(int row){if(row<0){home_book_t *b=s_current.path[0]?&s_current:&s_recent[0];return b->has_progress?b->progress.pct:-1;}if(s_has_documents)return (unsigned)row<s_recent_document_count?s_recent_documents[row].pct:-1;home_book_t *b=recent_book_row((size_t)row);return b&&b->has_progress?b->progress.pct:-1;}
