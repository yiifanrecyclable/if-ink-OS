/* SPDX-License-Identifier: Apache-2.0
 * 中文：更新通道拒绝上游固件、测试版及跨仓库资产地址。
 * English: Reject upstream firmware, prereleases and cross-repository asset URLs.
 */
#include "ota_online_policy.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool version_parts(const char *s, unsigned parts[3], bool *test) {
    if(!s || strlen(s)>32)return false;
    for(int i=0;i<3;++i){
        const char *start=s;unsigned n=0;
        while(*s>='0'&&*s<='9'){if(n>9999)return false;n=n*10+(*s++-'0');}
        if(s==start||(s-start>1&&*start=='0')||n>99999)return false;
        parts[i]=n;
        if(i<2&&*s++!='.')return false;
    }
    *test=false;
    if(!*s)return true;
    // beta仅作为当前版本参与比较，候选测试版仍由调用方拒绝。/ Recognize beta as the installed version; callers still reject prerelease candidates.
    if(!strcmp(s,"-beta")){*test=true;return true;}
    if(!strncmp(s,"-beta",5)){
        s+=5;
        if(*s>='1'&&*s<='9'){while(*s>='0'&&*s<='9')++s;}
        else if(strncmp(s,"-r",2))return false;
        if(!strncmp(s,"-r",2)){
            s+=2;if(*s<'1'||*s>'9')return false;
            while(*s>='0'&&*s<='9')++s;
        }
        if(*s)return false;
        *test=true;return true;
    }
    if(strncmp(s,"-test.",6))return false;
    s+=6;if(!*s)return false;
    while(*s>='0'&&*s<='9')++s;
    if(*s)return false;
    *test=true;return true;
}
bool if_update_newer(const char *candidate,const char *current){
    unsigned a[3],b[3];bool at,bt;
    if(!version_parts(candidate,a,&at)||at)return false;
    // 旧IF版本号来自原版编号体系，不能按0.3.3与独立系统的0.1.0直接比较。
    // Legacy IF versions used upstream numbering; do not compare their 0.3.3 against OS 0.1.0.
    const char *legacy="0.3.3-rc83-if.";
    if(current&&!strncmp(current,legacy,strlen(legacy))){
        const char *p=current+strlen(legacy);if(!*p)return false;
        while(*p>='0'&&*p<='9')++p;
        return !*p;
    }
    if(!version_parts(current,b,&bt))return false;
    for(int i=0;i<3;++i)if(a[i]!=b[i])return a[i]>b[i];
    return bt;
}
static bool is_text(const cJSON *o,const char *key,const char *value){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    return cJSON_IsString(v)&&!strcmp(v->valuestring,value);
}
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
bool if_update_parse_manifest(const char *json,size_t length,if_update_candidate_t *out){
    if(!json||!out||!length||length>IF_OTA_MANIFEST_LIMIT||memchr(json,0,length))return false;
    memset(out,0,sizeof(*out));
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
       !is_text(o,"layout",IF_OTA_LAYOUT)||!is_text(o,"channel","stable"))goto done;
    if(cJSON_IsFalse(available)){valid=true;goto done;}
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,"version"),*n=cJSON_GetObjectItemCaseSensitive(o,"size"),
                *sha=cJSON_GetObjectItemCaseSensitive(o,"sha256"),*id=cJSON_GetObjectItemCaseSensitive(o,"asset_id"),*blob=cJSON_GetObjectItemCaseSensitive(o,"blob_sha");
    unsigned parts[3];bool test;
    if(!cJSON_IsString(v)||!version_parts(v->valuestring,parts,&test)||test||
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
bool if_update_redirect_allowed(const char *url){
    if(!url||strlen(url)>=2048||strpbrk(url,"\r\n\\ "))return false;
    return !strncmp(url,"https://release-assets.githubusercontent.com/",sizeof("https://release-assets.githubusercontent.com/")-1)||
           !strncmp(url,"https://objects.githubusercontent.com/",sizeof("https://objects.githubusercontent.com/")-1);
}
