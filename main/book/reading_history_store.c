/* SPDX-License-Identifier: Apache-2.0
 * 中文：每年最多366个日槽，每槽两份带校验累计值；只更新较旧副本。
 * English: Up to 366 daily slots per year, each with two checked cumulative copies; update the older copy.
 */
#include "reading_history_store.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define HISTORY_DIR "/sdcard/.readpico/reading"
#define HISTORY_MAGIC UINT32_C(0x49485231)
typedef struct { uint32_t magic,day,seconds,check; } history_record_t;
_Static_assert(sizeof(history_record_t)==16,"Stable history record size");

static bool year_of(uint32_t day,int *year,uint32_t *first) {
    if(day<19723||day>2932896)return false; /* 2024..9999 / Supported calendar range. */
    time_t stamp=(time_t)((uint64_t)day*86400);struct tm tm;
    if(!gmtime_r(&stamp,&tm))return false;
    *year=tm.tm_year+1900;*first=day-(uint32_t)tm.tm_yday;return true;
}
static uint32_t checksum(const history_record_t *r) {
    uint32_t h=2166136261u,fields[]={r->magic,r->day,r->seconds};
    for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)h=(h^((fields[i]>>(8*b))&255u))*16777619u;
    return h;
}
static bool valid(const history_record_t *r,uint32_t day) {
    return r->magic==HISTORY_MAGIC&&r->day==day&&r->seconds&&r->check==checksum(r);
}
static void read_pair(FILE *f,long offset,history_record_t pair[2]) {
    memset(pair,0,2*sizeof(*pair));
    if(fseek(f,offset,SEEK_SET))return;
    size_t bytes=fread(pair,1,2*sizeof(*pair),f);
    if(bytes<sizeof(*pair))memset(pair,0,2*sizeof(*pair));
    else if(bytes<2*sizeof(*pair))memset(&pair[1],0,sizeof(*pair));
}
void reading_history_range(uint32_t start,unsigned count,uint32_t *seconds) {
    if(!seconds)return;
    FILE *f=NULL;int opened_year=0;
    for(unsigned i=0;i<count;++i){
        seconds[i]=0;int year;uint32_t first,day=start+i;
        if(day<start||!year_of(day,&year,&first))continue;
        if(year!=opened_year){
            if(f)fclose(f);
            char path[80];snprintf(path,sizeof(path),HISTORY_DIR"/%04d.bin",year);
            f=fopen(path,"rb");opened_year=year;
        }
        if(!f)continue;
        history_record_t pair[2];read_pair(f,(long)(day-first)*2*sizeof(*pair),pair);
        uint32_t a=valid(&pair[0],day)?pair[0].seconds:0,b=valid(&pair[1],day)?pair[1].seconds:0;
        seconds[i]=a>b?a:b;
    }
    if(f)fclose(f);
}
uint32_t reading_history_read(uint32_t day) {
    uint32_t seconds=0;reading_history_range(day,1,&seconds);return seconds;
}
bool reading_history_merge(uint32_t day,uint32_t seconds) {
    int year;uint32_t first;if(!seconds||!year_of(day,&year,&first))return false;
    struct stat st;if(stat("/sdcard",&st)||!S_ISDIR(st.st_mode))return false;
    if(mkdir("/sdcard/.readpico",0777)&&errno!=EEXIST)return false;
    if(mkdir(HISTORY_DIR,0777)&&errno!=EEXIST)return false;
    char path[80];snprintf(path,sizeof(path),HISTORY_DIR"/%04d.bin",year);
    FILE *f=fopen(path,"r+b");if(!f&&errno==ENOENT)f=fopen(path,"w+b");if(!f)return false;
    history_record_t pair[2];long offset=(long)(day-first)*2*sizeof(*pair);read_pair(f,offset,pair);
    uint32_t a=valid(&pair[0],day)?pair[0].seconds:0,b=valid(&pair[1],day)?pair[1].seconds:0;
    if(a>=seconds||b>=seconds){fclose(f);return true;}
    history_record_t next={HISTORY_MAGIC,day,seconds,0};next.check=checksum(&next);
    unsigned slot=a<=b?0:1;
    clearerr(f);
    bool ok=fseek(f,offset+(long)slot*sizeof(next),SEEK_SET)==0&&fwrite(&next,1,sizeof(next),f)==sizeof(next);
    if(ok)ok=fflush(f)==0&&fsync(fileno(f))==0;
    if(fclose(f))ok=false;
    return ok;
}
uint32_t reading_history_first(uint32_t today) {
    DIR *dir=opendir(HISTORY_DIR);if(!dir)return 0;
    uint32_t earliest=0;struct dirent *entry;
    while((entry=readdir(dir))){
        int year=0;char extra;
        if(strlen(entry->d_name)!=8||sscanf(entry->d_name,"%4d.bin%c",&year,&extra)!=1||year<2024||year>9999)continue;
        char expected[16];snprintf(expected,sizeof(expected),"%04d.bin",year);if(strcmp(expected,entry->d_name))continue;
        uint32_t first=(uint32_t)(365*(year-1970)+(year-1)/4-1969/4-(year-1)/100+1969/100+(year-1)/400-1969/400);
        if(first>today||(earliest&&first>earliest))continue;
        char path[80];snprintf(path,sizeof(path),HISTORY_DIR"/%04d.bin",year);
        FILE *f=fopen(path,"rb");if(!f)continue;
        for(uint32_t i=0;i<366&&first+i<=today;++i){
            int row_year;uint32_t row_first;if(!year_of(first+i,&row_year,&row_first)||row_year!=year)break;
            history_record_t pair[2];read_pair(f,(long)i*2*sizeof(*pair),pair);
            if(valid(&pair[0],first+i)||valid(&pair[1],first+i)){
                if(!earliest||first+i<earliest)earliest=first+i;
                break;
            }
        }
        fclose(f);
    }
    closedir(dir);return earliest;
}
