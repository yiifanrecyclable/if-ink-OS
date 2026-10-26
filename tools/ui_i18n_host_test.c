/* SPDX-License-Identifier: Apache-2.0
 * 中文：双语目录、格式参数、截断和用户内容隔离回归。/ English: Catalog, formatting, truncation and user-content isolation regression.
 */
#include "ui_i18n.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const struct {const char *zh,*en;} catalog[]={
#include "ui_messages.inc"
};
int main(void){
 char key[512],out[512];
 for(unsigned i=0;i<sizeof(catalog)/sizeof(catalog[0]);++i){
  snprintf(key,sizeof(key),"\036%s\037",catalog[i].zh);
  ui_i18n_set_english(false);assert(!strcmp(ui_message(key),catalog[i].zh));
  ui_i18n_set_english(true);assert(!strcmp(ui_message(key),catalog[i].en));
 }
 assert(!strcmp(ui_message(UI_MSG("系统语言")),"System language"));
 assert(!strcmp(ui_message(UI_MSG("阅读打卡")),"Record"));
 assert(!strcmp(ui_message(UI_MSG("目标设定")),"Target"));
 const char *user="保存";assert(ui_message(user)==user);
 assert(ui_snprintf(out,sizeof(out),"%s",user)==6&&!strcmp(out,user));
 assert(ui_snprintf(out,sizeof(out),UI_MSG("阅读进度 %u%%"),37)==12&&!strcmp(out,"Progress 37%"));
 assert(ui_snprintf(out,sizeof(out),"%s · %s",UI_MSG("保存"),user)==14&&!strcmp(out,"Save · 保存"));
 int n=ui_snprintf(out,4,"%s",UI_MSG("取消"));assert(n==6&&!strcmp(out,"Can"));
 assert(ui_snprintf(NULL,0,"%s",UI_MSG("取消"))==6);
 assert(ui_snprintf(out,sizeof(out),UI_MSG("已复制：%.74s"),"保存.md")==17&&!strcmp(out,"Copied: 保存.md"));
 ui_i18n_set_english(false);
 assert(ui_snprintf(out,sizeof(out),UI_MSG("阅读进度 %u%%"),37)==16&&!strcmp(out,"阅读进度 37%"));
 puts("PASS: complete bilingual catalog, format parameters, nested labels, truncation lengths and unchanged user text");
}
