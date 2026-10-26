/* SPDX-License-Identifier: Apache-2.0
 * 中文：系统文案集中翻译，不对书名、正文、文件名和用户输入进行词语替换。
 * English: Localize marked system messages, never word-replace titles, body text, file names or user input.
 */
#include "ui_i18n.h"
#include <stdatomic.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const char *zh, *en; } message_t;
static atomic_bool s_english;
void ui_i18n_set_english(bool english){atomic_store(&s_english,english);}
bool ui_i18n_english(void){return atomic_load(&s_english);}
static const message_t messages[] = {
#include "ui_messages.inc"
};
static const char *lookup(const char *key, size_t length) {
    size_t lo=0,hi=sizeof(messages)/sizeof(messages[0]);
    while(lo<hi){size_t mid=lo+(hi-lo)/2;int cmp=strncmp(key,messages[mid].zh,length);
        if(!cmp)cmp=messages[mid].zh[length]? -1:0;
        if(cmp<0)hi=mid;else if(cmp>0)lo=mid+1;else return ui_i18n_english()?messages[mid].en:messages[mid].zh;
    }
    return NULL;
}
const char *ui_message(const char *text){
    if(!text || *text!='\036')return text;
    const char *end=strchr(text+1,'\037');
    if(!end || end[1])return text;
    const char *found=lookup(text+1,(size_t)(end-text-1));
    return found?found:text;
}
static size_t resolve(char *out,size_t capacity,const char *input){
    size_t used=0;
    while(*input){
        const char *chunk=input,*end=NULL;size_t n=1;
        if(*input=='\036'&&(end=strchr(input+1,'\037'))){
            chunk=lookup(input+1,(size_t)(end-input-1));
            if(chunk)n=strlen(chunk);else {chunk=input+1;n=(size_t)(end-chunk);}
            input=end+1;
        }else ++input;
        if(out&&capacity&&used<capacity-1){size_t copy=n<capacity-1-used?n:capacity-1-used;memcpy(out+used,chunk,copy);}
        used+=n;
    }
    if(out&&capacity)out[used<capacity?used:capacity-1]=0;
    return used;
}
int ui_snprintf(char *out,size_t size,const char *format,...){
    if(!format)return -1;
    format=ui_message(format);
    char *localized_format=NULL;
    if(strchr(format,'\036')){
        size_t n=resolve(NULL,0,format);
        localized_format=malloc(n+1);
        if(!localized_format)return -1;
        resolve(localized_format,n+1,format);format=localized_format;
    }
    va_list args,again;va_start(args,format);va_copy(again,args);
    int length=vsnprintf(out,size,format,args);va_end(args);
    // 普通数字与路径不分配；只在标签标记出现或输出被截断时复核完整结果。
    // Ordinary numbers/paths allocate nothing; recheck full output only for markers or truncation.
    if(length>=0&&((out&&size&&strchr(out,'\036'))||!size||(size_t)length>=size)){
        char *raw=malloc((size_t)length+1);
        if(raw){vsnprintf(raw,(size_t)length+1,format,again);size_t translated=resolve(out,size,raw);free(raw);length=translated<=INT_MAX?(int)translated:-1;}
        else if(out&&size){size_t to=0;for(size_t i=0;out[i];++i)if(out[i]!='\036'&&out[i]!='\037')out[to++]=out[i];out[to]=0;}
    }
    va_end(again);free(localized_format);return length;
}
