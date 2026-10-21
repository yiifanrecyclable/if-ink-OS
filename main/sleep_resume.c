/* SPDX-License-Identifier: Apache-2.0
 * 中文：紧凑NVS一次性标记和TF锁屏快照；保留原进度，兼容TXT/EPUB/Markdown。
 * English: Compact one-shot NVS token and SD lock snapshot; retain progress across TXT/EPUB/Markdown.
 * 冻结：不清空NVS分区；无卡、断电、损坏、固件变化一律安全回退。
 * Frozen: Never erase NVS partitions; missing media, interruption, corruption or a changed build falls back safely.
 */
#include "sleep_resume.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nvs.h"
#ifndef SLEEP_RESUME_DIR
#define SLEEP_RESUME_DIR "/sdcard/.readpico"
#endif
#define CACHE SLEEP_RESUME_DIR "/sleep-frame.bin"
#define FRAME_BYTES (684u*1216u/2u)
#define MAGIC UINT32_C(0x49465331)
typedef struct {
    uint32_t magic,rotation,bytes,frame_hash,record_hash;
    uint64_t source_size;
    int64_t source_time;
    uint8_t build[32],tab;
    char path[SLEEP_RESUME_PATH_MAX];
} resume_record_t;
static resume_record_t s_record;
static sleep_resume_target_t s_target;
static uint8_t s_build[32];
static bool s_candidate;
static uint32_t hash(const void *data,size_t size){
    const uint8_t *p=data;uint32_t h=2166136261u;
    while(size--)h=(h^*p++)*16777619u;
    return h;
}
static bool valid_path(const char *p){
    if(!p||strnlen(p,SLEEP_RESUME_PATH_MAX)==SLEEP_RESUME_PATH_MAX)return false;
    if(!*p)return true;
    if(strncmp(p,"/sdcard/",8)&&strncmp(p,"/flash/books/",13))return false;
    for(const char *part=p+1;*part;){
        const char *end=strchr(part,'/');size_t n=end?(size_t)(end-part):strlen(part);
        if(!n||(n==1&&*part=='.')||(n==2&&!strncmp(part,"..",2)))return false;
        for(size_t i=0;i<n;++i)if((unsigned char)part[i]<32||part[i]=='\\')return false;
        if(!end)break;
        part=end+1;if(!*part)return false;
    }
    const char *ext=strrchr(p,'.');
    return ext&&(!strcasecmp(ext,".txt")||!strcasecmp(ext,".epub")||!strcasecmp(ext,".md"));
}
void sleep_resume_set_target(unsigned tab,const char *path){
    memset(&s_target,0,sizeof(s_target));s_target.tab=tab<4?tab:0;
    if(path&&*path&&valid_path(path)){s_target.tab=1;snprintf(s_target.path,sizeof(s_target.path),"%s",path);}
}
bool sleep_resume_pending(void){return s_candidate;}
void sleep_resume_discard(void){s_candidate=false;memset(&s_record,0,sizeof(s_record));}
void sleep_resume_init(const uint8_t build_id[32],bool normal_poweron){
    sleep_resume_discard();memset(&s_target,0,sizeof(s_target));memcpy(s_build,build_id,32);
    nvs_handle_t h;if(nvs_open("if_sleep",NVS_READWRITE,&h)!=ESP_OK)return;
    uint8_t packed[4+sizeof(s_record.path)];size_t size=sizeof(packed);
    esp_err_t read=nvs_get_blob(h,"pending",packed,&size);
    // 先提交消费，之后即便开书崩溃也不能自动重试。/ Commit consumption before any parser can run.
    esp_err_t erased=nvs_erase_key(h,"pending");
    bool consumed=(erased==ESP_OK||erased==ESP_ERR_NVS_NOT_FOUND)&&nvs_commit(h)==ESP_OK;
    nvs_close(h);
    if(!normal_poweron||read!=ESP_OK||!consumed||size<5||size>sizeof(packed)||packed[size-1]||
       strnlen((char*)packed+4,size-4)!=size-5||!valid_path((char*)packed+4))return;
    memcpy(&s_record.record_hash,packed,4);snprintf(s_record.path,sizeof(s_record.path),"%s",(char*)packed+4);
    s_candidate=true;
}
bool sleep_resume_save(const uint8_t *frame,size_t bytes,unsigned rotation){
    if(!frame||bytes!=FRAME_BYTES)return false;
    resume_record_t r={.magic=MAGIC,.rotation=rotation,.bytes=(uint32_t)bytes,.frame_hash=hash(frame,bytes),.tab=s_target.tab};
    memcpy(r.build,s_build,32);snprintf(r.path,sizeof(r.path),"%s",s_target.path);
    struct stat st;
    if(*r.path){if(stat(r.path,&st)||!S_ISREG(st.st_mode)||st.st_size<0)return false;r.source_size=st.st_size;r.source_time=st.st_mtime;}
    r.record_hash=hash(&r,sizeof(r));
    (void)mkdir(SLEEP_RESUME_DIR,0775);
    FILE *f=fopen(CACHE ".tmp","wb");if(!f)return false;
    bool ok=fwrite(&r,1,sizeof(r),f)==sizeof(r)&&fwrite(frame,1,bytes,f)==bytes&&fflush(f)==0;
    if(ok&&fsync(fileno(f)))ok=false;
    if(fclose(f))ok=false;
    // 缓存是可丢弃的；消费标记尚未写入，替换期间断电只走普通开机。/ The cache is disposable; no token exists yet, so interrupted replacement uses normal boot.
    if(ok){(void)remove(CACHE);ok=rename(CACHE ".tmp",CACHE)==0;}
    if(!ok){(void)remove(CACHE ".tmp");return false;}
    uint8_t packed[4+SLEEP_RESUME_PATH_MAX];memcpy(packed,&r.record_hash,4);
    size_t n=strlen(r.path)+1;memcpy(packed+4,r.path,n);
    nvs_handle_t h;if(nvs_open("if_sleep",NVS_READWRITE,&h)!=ESP_OK)return false;
    ok=nvs_set_blob(h,"pending",packed,n+4)==ESP_OK&&nvs_commit(h)==ESP_OK;
    nvs_close(h);return ok;
}
bool sleep_resume_restore(uint8_t *front,uint8_t *back,size_t bytes,unsigned rotation,sleep_resume_target_t *target){
    if(!s_candidate||!front||!back||!target||bytes!=FRAME_BYTES)return false;
    s_candidate=false;
    uint32_t expected=s_record.record_hash;char path[SLEEP_RESUME_PATH_MAX];memcpy(path,s_record.path,sizeof(path));
    FILE *f=fopen(CACHE,"rb");if(!f)return false;
    resume_record_t r;
    bool ok=fread(&r,1,sizeof(r),f)==sizeof(r);
    uint32_t checksum=ok?r.record_hash:0;if(ok)r.record_hash=0;
    ok=ok&&checksum==expected&&hash(&r,sizeof(r))==checksum&&r.magic==MAGIC&&r.rotation==rotation&&r.bytes==bytes&&
        !memcmp(r.build,s_build,32)&&r.tab<4&&valid_path(r.path)&&!strcmp(path,r.path)&&(!*r.path||r.tab==1);
    struct stat st;
    if(ok&&*r.path)ok=!stat(r.path,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size==r.source_size&&st.st_mtime==r.source_time;
    if(ok)ok=fread(front,1,bytes,f)==bytes&&fgetc(f)==EOF&&hash(front,bytes)==r.frame_hash;
    fclose(f);
    if(!ok)return false;
    memcpy(back,front,bytes);target->tab=r.tab;memcpy(target->path,r.path,sizeof(target->path));return true;
}
