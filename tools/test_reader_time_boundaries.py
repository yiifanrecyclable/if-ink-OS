#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：直接编译生产计时函数，验证休眠边界和长时间无操作。
# English: Compile production accounting functions to verify sleep and inactivity boundaries.
from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[1];source=(R/'main/apps/app_book.c').read_text()
def function(name):
 start=source.index('static void '+name+'(');brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
harness='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef struct {int64_t now_ms;} app_ctx_t;
enum {READING=1,ESP_OK=0};
static int s_view=READING; static char *s_text="body"; static bool s_toolbar;
static int64_t s_stats_last_ms,s_stats_activity_ms;
static uint32_t s_stats_pending_ms,s_stats_pending_turns,s_session_read_ms,total;
static int book_ticket_record(uint32_t seconds,uint32_t turns){(void)turns;total+=seconds;return ESP_OK;}
'''+function('flush_ticket_stats')+'\n'+function('track_ticket_stats')+'''
int main(void){
 app_ctx_t ctx={1000};s_stats_last_ms=s_stats_activity_ms=1000;
 for(int i=0;i<120;++i){ctx.now_ms+=1000;track_ticket_stats(&ctx);}flush_ticket_stats();assert(total==120);
 /* Both manual/idle sleep call before_lock, whose final step zeros this baseline. */
 s_stats_last_ms=0;ctx.now_ms+=8*60*60*1000;track_ticket_stats(&ctx);assert(total==120);
 for(int i=0;i<120;++i){ctx.now_ms+=1000;track_ticket_stats(&ctx);}flush_ticket_stats();assert(total==240);
 /* With no further input, stop after five minutes from waking. */
 for(int i=0;i<900;++i){ctx.now_ms+=1000;track_ticket_stats(&ctx);}flush_ticket_stats();assert(total==420);
 /* A page turn resumes activity, while an open panel pauses it. */
 s_stats_activity_ms=ctx.now_ms;for(int i=0;i<60;++i){ctx.now_ms+=1000;track_ticket_stats(&ctx);}flush_ticket_stats();assert(total==480);
 s_toolbar=true;for(int i=0;i<60;++i){ctx.now_ms+=1000;track_ticket_stats(&ctx);}flush_ticket_stats();assert(total==480);
 return 0;
}
'''
with tempfile.TemporaryDirectory(dir=R/'build',prefix='reader-time-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: production timer accumulates before/after 8-hour sleep, excludes sleep, stops after five idle minutes, resumes on input and pauses in panels.')
