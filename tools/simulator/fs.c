/* SPDX-License-Identifier: Apache-2.0
 * 中文：模拟写入仅作用于测试 TF 卡和 build/simulator，包含字体使用的 POSIX open。
 * English: Simulator writes are confined to the test TF card and build/simulator, including font POSIX open.
 */
#define PICO_SIM_FS_IMPL
#include "fs.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#undef open
#undef fopen
#undef stat
#undef lstat
#undef opendir
#undef mkdir
#undef unlink
#undef remove
#undef rmdir
#undef rename
#undef access
static int mapped(const char *p,char out[PATH_MAX],int write){
 if(!p||!*p){errno=EINVAL;return 0;}
 char cwd[PATH_MAX];if(p[0]=='/'&&getcwd(cwd,sizeof(cwd))){size_t n=strlen(cwd);if(!strncmp(p,cwd,n)&&p[n]=='/')p+=n+1;}
 // 自动化测试使用独立卡目录，日常预览仍使用项目 TF 卡。/ Tests may use an isolated card; normal preview keeps the project card.
 const char *card=getenv("PICO_SIM_CARD_ROOT");if(!card||!*card)card=PICO_SIM_CARD_PATH;
 if(!strncmp(p,"/sdcard",7)&&(p[7]=='/'||!p[7]))snprintf(out,PATH_MAX,"%s%s",card,p+7);
 else if(!strncmp(p,"/flash",6)&&(p[6]=='/'||!p[6]))snprintf(out,PATH_MAX,"build/simulator/vfs/flash%s",p+6);
 else if(p[0]!='/'&&(!write||!strncmp(p,"build/simulator/",16)))snprintf(out,PATH_MAX,"%s",p);
 else {errno=EACCES;return 0;}
 if(strlen(p)>PATH_MAX-64){errno=ENAMETOOLONG;return 0;}
 char probe[PATH_MAX];snprintf(probe,sizeof(probe),"%s",out);
 for(char *c=probe;;++c){
   if(*c!='/'&&*c)continue;
   char saved=*c;*c=0;
   const char *part=strrchr(probe,'/');part=part?part+1:probe;
   if(!strcmp(part,"..")||!strcmp(part,".")){errno=EACCES;return 0;}
   struct stat st;if(!lstat(probe,&st)&&S_ISLNK(st.st_mode)){errno=EACCES;return 0;}
   *c=saved;if(!saved)break;
 }
 return 1;
}
FILE *sim_fopen(const char*p,const char*m){
 // 精确路径注入写失败，用于双槽落盘与重试测试。/ Exact-path write faults exercise durable slots and retries.
 const char *write_fault=getenv("PICO_TEST_FOPEN_WRITE_FAIL");
 if(write_fault&&!strcmp(p,write_fault)&&strpbrk(m,"wa+")){errno=EIO;return NULL;}

 const char *fault=getenv("PICO_TEST_JOURNAL_WRITE_FAIL_AT");
 if(fault&&!strcmp(p,"/sdcard/.pico-sync/transaction.part")&&strchr(m,'w')){
  static unsigned writes;
  if(++writes==strtoul(fault,NULL,10)){errno=EIO;return NULL;}
 }
 char v[PATH_MAX];return mapped(p,v,strpbrk(m,"wa+")!=NULL)?fopen(v,m):NULL;
}
int sim_stat(const char*p,struct stat*s,int link){const char *fault=getenv("PICO_TEST_LSTAT_EIO");if(link&&fault&&!strcmp(p,fault)){errno=EIO;return -1;}char v[PATH_MAX];return mapped(p,v,0)?(link?lstat(v,s):stat(v,s)):-1;}
DIR *sim_opendir(const char*p){char v[PATH_MAX];return mapped(p,v,0)?opendir(v):NULL;}
int sim_mkdir(const char*p,mode_t m){char v[PATH_MAX];return mapped(p,v,1)?mkdir(v,m):-1;}
int sim_unlink(const char*p){char v[PATH_MAX];return mapped(p,v,1)?unlink(v):-1;}
int sim_rmdir(const char*p){char v[PATH_MAX];return mapped(p,v,1)?rmdir(v):-1;}
int sim_rename(const char*a,const char*b){
 char x[PATH_MAX],y[PATH_MAX];if(!mapped(a,x,1)||!mapped(b,y,1))return -1;
 // 可选FatFs语义：目标存在时拒绝覆盖，不能用POSIX测试掩盖真机差异。
 // Optional FatFs semantics reject existing destinations rather than masking device differences with POSIX replacement.
 const char *fat=getenv("PICO_TEST_FATFS_RENAME");struct stat st;
 if(fat&&!strcmp(fat,"1")&&(!strncmp(a,"/sdcard/",8)||!strncmp(b,"/sdcard/",8))&&!stat(y,&st)){errno=EEXIST;return -1;}
 const char *fail=getenv("PICO_TEST_RENAME_FAIL_TO");
 if(fail&&!strcmp(b,fail)){errno=EIO;return -1;}
 return rename(x,y);
}
int sim_access(const char*p,int m){char v[PATH_MAX];return mapped(p,v,m&W_OK)?access(v,m):-1;}

// 字体读取使用文件描述符；和 stdio 采用相同路径与权限检查。
// Descriptor-based font reads share the same mapping and checks as stdio.
int sim_open(const char *path,int flags,...){
 char mapped_path[PATH_MAX];
 int writing=flags&(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND);
 if(!mapped(path,mapped_path,writing))return -1;
 if(flags&O_CREAT){va_list args;va_start(args,flags);int mode=va_arg(args,int);va_end(args);return open(mapped_path,flags,(mode_t)mode);}
 return open(mapped_path,flags);
}
