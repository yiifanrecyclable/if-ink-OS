/* SPDX-License-Identifier: Apache-2.0
 * 中文：更新通道隔离稳定版和测试版，拒绝上游固件及跨仓库资产地址。
 * English: Isolate stable and beta channels; reject upstream firmware and cross-repository asset URLs.
 */
#include "ota_online_policy.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    unsigned parts[3], number, revision;
    bool test, beta;
} version_t;
static bool version_number(const char **cursor,unsigned *value){
    const char *s=*cursor,*start=s;unsigned n=0;
    while(*s>='0'&&*s<='9'){if(n>9999)return false;n=n*10+(*s++-'0');}
    if(s==start||(s-start>1&&*start=='0')||n>99999)return false;
    *value=n;*cursor=s;return true;
}
static bool version_parts(const char *s, version_t *v) {
    if(!s || strlen(s)>32)return false;
    memset(v,0,sizeof(*v));
    for(int i=0;i<3;++i){
        if(!version_number(&s,&v->parts[i]))return false;
        if(i<2&&*s++!='.')return false;
    }
    if(!*s)return true;
    // 新测试版使用beta.N；兼容已安装的beta、betaN及-rN后缀。
    // New betas use beta.N; retain installed beta, betaN and -rN suffix compatibility.
    if(!strncmp(s,"-beta",5)){
        s+=5;v->test=v->beta=true;
        if(!*s)return true;
        if(*s=='.'){
            ++s;if(!version_number(&s,&v->number)||!v->number||*s)return false;
            return true;
        }
        if(*s>='1'&&*s<='9'){if(!version_number(&s,&v->number))return false;}
        else if(strncmp(s,"-r",2))return false;
        if(!strncmp(s,"-r",2)){
            s+=2;if(!version_number(&s,&v->revision)||!v->revision)return false;
        }
        return !*s;
    }
    if(strncmp(s,"-test.",6))return false;
    s+=6;v->test=true;
    return version_number(&s,&v->number)&&!*s;
}
static bool legacy_current(const char *current){
    // 旧IF版本号来自原版编号体系，不能按0.3.3与独立系统的0.1.0直接比较。
    // Legacy IF versions used upstream numbering; do not compare their 0.3.3 against OS 0.1.0.
    const char *legacy="0.3.3-rc83-if.";
    if(current&&!strncmp(current,legacy,strlen(legacy))){
        const char *p=current+strlen(legacy);if(!*p)return false;
        while(*p>='0'&&*p<='9')++p;
        return !*p;
    }
    return false;
}
static int compare_versions(const version_t *a,const version_t *b){
    for(int i=0;i<3;++i)if(a->parts[i]!=b->parts[i])return a->parts[i]>b->parts[i]?1:-1;
    if(a->test!=b->test)return a->test?-1:1;
    if(!a->test)return 0;
    if(a->beta!=b->beta)return a->beta?1:-1;
    if(a->number!=b->number)return a->number>b->number?1:-1;
    if(a->revision!=b->revision)return a->revision>b->revision?1:-1;
    return 0;
}
bool if_update_newer(const char *candidate,const char *current){
    version_t a,b;
    if(!version_parts(candidate,&a)||a.test)return false;
    if(legacy_current(current))return true;
    return version_parts(current,&b)&&compare_versions(&a,&b)>0;
}
bool if_update_candidate_allowed(const char *candidate,const char *current,if_update_channel_t channel){
    if(channel!=IF_UPDATE_STABLE&&channel!=IF_UPDATE_BETA)return false;
    version_t a,b;
    if(!version_parts(candidate,&a)||(channel==IF_UPDATE_BETA?!a.beta:a.test))return false;
    if(legacy_current(current))return true;
    if(!version_parts(current,&b))return false;
    return compare_versions(&a,&b)>0||(channel==IF_UPDATE_STABLE&&b.beta);
}
static bool is_text(const cJSON *o,const char *key,const char *value){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    return cJSON_IsString(v)&&!strcmp(v->valuestring,value);
}
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
bool if_update_parse_manifest_channel(const char *json,size_t length,if_update_channel_t channel,if_update_candidate_t *out){
    if(!json||!out||!length||length>IF_OTA_MANIFEST_LIMIT||memchr(json,0,length)||
       (channel!=IF_UPDATE_STABLE&&channel!=IF_UPDATE_BETA))return false;
    memset(out,0,sizeof(*out));
    out->channel=channel;
    const char *end=NULL;
    cJSON *o=cJSON_ParseWithLengthOpts(json,length,&end,false);bool valid=false;
    if(!cJSON_IsObject(o))goto done;
    while(end<json+length&&(*end==' '||*end=='\t'||*end=='\r'||*end=='\n'))++end;
    if(end!=json+length)goto done;
    for(const cJSON *p=o->child;p;p=p->next)for(const cJSON *q=p->next;q;q=q->next)
        if(!strcmp(p->string,q->string))goto done;
    const cJSON *schema=cJSON_GetObjectItemCaseSensitive(o,"schema"),*available=cJSON_GetObjectItemCaseSensitive(o,"available");
    if(!cJSON_IsNumber(schema)||schema->valuedouble!=1||!cJSON_IsBool(available)||
       !is_text(o,"product","if-ink-os")||!is_text(o,"project",IF_OTA_PROJECT)||
       !is_text(o,"layout",IF_OTA_LAYOUT)||!is_text(o,"channel",channel==IF_UPDATE_BETA?"beta":"stable"))goto done;
    if(cJSON_IsFalse(available)){valid=true;goto done;}
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,"version"),*n=cJSON_GetObjectItemCaseSensitive(o,"size"),
                *sha=cJSON_GetObjectItemCaseSensitive(o,"sha256"),*id=cJSON_GetObjectItemCaseSensitive(o,"asset_id"),*blob=cJSON_GetObjectItemCaseSensitive(o,"blob_sha");
    version_t version;
    if(!cJSON_IsString(v)||!version_parts(v->valuestring,&version)||(channel==IF_UPDATE_BETA?!version.beta:version.test)||
       !cJSON_IsNumber(n)||n->valuedouble<288||n->valuedouble>IF_OTA_MAX_BYTES||n->valuedouble!=(uint32_t)n->valuedouble||
       !cJSON_IsString(blob)||strlen(blob->valuestring)!=40||!cJSON_IsString(sha)||strlen(sha->valuestring)!=64||!cJSON_IsString(id)||!id->valuestring[0]||strlen(id->valuestring)>20)goto done;
    for(const char *p=id->valuestring;*p;++p)if(*p<'0'||*p>'9')goto done;
    if(id->valuestring[0]=='0')goto done;
    for(int i=0;i<32;++i){int a=nibble(sha->valuestring[2*i]),b=nibble(sha->valuestring[2*i+1]);if(a<0||b<0)goto done;out->sha256[i]=(a<<4)|b;}
    char blob_hex[41]={0};
    for(int i=0;i<40;++i){int n=nibble(blob->valuestring[i]);if(n<0)goto done;blob_hex[i]="0123456789abcdef"[n];}
    snprintf(out->blob_url,sizeof(out->blob_url),"https://api.github.com/repositories/" IF_OTA_REPOSITORY_ID "/git/blobs/%s",blob_hex);
    strcpy(out->version,v->valuestring);out->size=(uint32_t)n->valuedouble;
    snprintf(out->asset_url,sizeof(out->asset_url),"https://api.github.com/repositories/" IF_OTA_REPOSITORY_ID "/releases/assets/%s",id->valuestring);
    out->available=true;valid=true;
done:
    cJSON_Delete(o);if(!valid)memset(out,0,sizeof(*out));return valid;
}
bool if_update_parse_manifest(const char *json,size_t length,if_update_candidate_t *out){
    return if_update_parse_manifest_channel(json,length,IF_UPDATE_STABLE,out);
}
bool if_update_redirect_allowed(const char *url){
    if(!url||strlen(url)>=2048||strpbrk(url,"\r\n\\ "))return false;
    return !strncmp(url,"https://release-assets.githubusercontent.com/",sizeof("https://release-assets.githubusercontent.com/")-1)||
           !strncmp(url,"https://objects.githubusercontent.com/",sizeof("https://objects.githubusercontent.com/")-1);
}
