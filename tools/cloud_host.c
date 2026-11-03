/* SPDX-License-Identifier: Apache-2.0
 * 中文：宿主桥调用正式云清单校验与TF事务；命令行永不接收账号密码。
 * English: Host bridge invokes production manifest checks and TF transactions; credentials are never command-line arguments.
 */
#include "cloud_protocol.h"
#include "cloud_dav.h"
#include "cJSON.h"
#include "book_progress.h"
#include "record_store.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static char *read_json(const char *path,size_t *length){FILE *f=fopen(path,"rb");if(!f)return NULL;struct stat st;if(stat(path,&st)||st.st_size<0||st.st_size>CLOUD_MANIFEST_MAX){fclose(f);return NULL;}char *s=malloc(st.st_size+1);if(!s){fclose(f);return NULL;}*length=fread(s,1,st.st_size,f);bool ok=*length==(size_t)st.st_size&&!ferror(f);fclose(f);if(!ok){free(s);return NULL;}s[*length]=0;return s;}
int main(int argc,char **argv){
    record_store_media(true);
    esp_err_t error=ESP_ERR_INVALID_ARG;bool unchanged=false;
    if(argc==3&&!strcmp(argv[1],"etag")){char tag[129];bool valid=cloud_etag_version(argv[2],tag);cJSON *object=cJSON_CreateObject();cJSON_AddNumberToObject(object,"error",0);cJSON_AddBoolToObject(object,"valid",valid);if(valid)cJSON_AddStringToObject(object,"version",tag);char *out=cJSON_PrintUnformatted(object);if(out){puts(out);free(out);}cJSON_Delete(object);return 0;}
    else if(argc==2&&!strcmp(argv[1],"bookdir")){
        char folder[96]="/sdcard/books";nvs_handle_t h;
        if(nvs_open("read_pico",NVS_READONLY,&h)==ESP_OK){size_t size=sizeof(folder);if(nvs_get_str(h,"books_dir",folder,&size)!=ESP_OK)strcpy(folder,"/sdcard/books");nvs_close(h);}
        cJSON *result=cJSON_CreateObject();cJSON_AddNumberToObject(result,"error",0);cJSON_AddStringToObject(result,"path",folder);char *out=cJSON_PrintUnformatted(result);if(out){puts(out);free(out);}cJSON_Delete(result);return 0;
    }
    else if(argc==3&&!strcmp(argv[1],"refresh")){bool changed;error=document_store_refresh(argv[2],&changed);}
    else if(argc==3&&!strcmp(argv[1],"progress-save")){struct stat st;if(!stat(argv[2],&st)){book_progress_t p={.file_size=st.st_size,.px=48,.pct=33,.byte_off=1};error=book_progress_save(argv[2],&p);}}
    else if(argc==3&&!strcmp(argv[1],"progress-load")){struct stat st;book_progress_t p={0};bool present=!stat(argv[2],&st)&&book_progress_load(argv[2],st.st_size,&p);printf("{\"error\":0,\"present\":%s,\"pct\":%u}\n",present?"true":"false",p.pct);return 0;}
    else if(argc==4&&!strcmp(argv[1],"listing")){
        FILE *f=fopen(argv[2],"rb");cloud_dav_entry_t *entries=calloc(CLOUD_ENTRY_MAX,sizeof(*entries));size_t count=0;bool truncated=false;
        if(f&&entries)error=cloud_dav_parse(f,argv[3],entries,CLOUD_ENTRY_MAX,&count,&truncated);if(f)fclose(f);
        cJSON *result=cJSON_CreateObject(),*array=cJSON_CreateArray();if(result&&array){cJSON_AddNumberToObject(result,"error",error);cJSON_AddBoolToObject(result,"truncated",truncated);cJSON_AddItemToObject(result,"entries",array);
            for(size_t i=0;i<count;++i){cJSON *entry=cJSON_CreateObject();cJSON_AddStringToObject(entry,"href",entries[i].href);cJSON_AddStringToObject(entry,"etag",entries[i].etag);cJSON_AddStringToObject(entry,"created_at",entries[i].created_at);cJSON_AddStringToObject(entry,"modified",entries[i].modified);cJSON_AddNumberToObject(entry,"bytes",entries[i].bytes);cJSON_AddBoolToObject(entry,"manifest",entries[i].manifest);cJSON_AddItemToArray(array,entry);}char *output=cJSON_PrintUnformatted(result);if(output){puts(output);free(output);}cJSON_Delete(result);
        }else{cJSON_Delete(result);cJSON_Delete(array);}free(entries);return 0;
    }
    else if(argc==2&&!strcmp(argv[1],"links")){
        document_sync_asset_t *assets=calloc(64,sizeof(*assets));size_t count=0;error=assets?document_store_sync_collect_assets(assets,&count):ESP_ERR_NO_MEM;
        cJSON *object=cJSON_CreateObject(),*array=cJSON_CreateArray();if(object&&array){cJSON_AddNumberToObject(object,"error",error);cJSON_AddItemToObject(object,"names",array);for(size_t i=0;i<count;++i)cJSON_AddItemToArray(array,cJSON_CreateString(assets[i].name));char *text=cJSON_PrintUnformatted(object);if(text){puts(text);free(text);}cJSON_Delete(object);}else{cJSON_Delete(object);cJSON_Delete(array);}free(assets);return 0;
    }
    else if(argc==2&&!strcmp(argv[1],"prepare"))error=document_store_sync_prepare();
    else if(argc==2&&!strcmp(argv[1],"recover"))error=document_store_sync_recover();
    else if(argc==2&&!strcmp(argv[1],"abort"))error=document_store_sync_abort();
    else if(argc==4&&!strcmp(argv[1],"manifest")){
        size_t length;char *text=read_json(argv[2],&length);cloud_manifest_t *manifest=calloc(1,sizeof(*manifest));
        if(text&&manifest)error=cloud_manifest_parse(text,length,argv[3],manifest);else error=ESP_ERR_NO_MEM;
        if(error==ESP_OK||error==ESP_ERR_NOT_SUPPORTED){cJSON *object=cJSON_Parse(text);char *out=object?cJSON_PrintUnformatted(object):NULL;if(out){printf("{\"error\":%d,\"manifest\":%s}\n",error,out);free(out);cJSON_Delete(object);free(manifest);free(text);return 0;}cJSON_Delete(object);error=ESP_ERR_NO_MEM;}
        free(manifest);free(text);
    } else if((argc==4||argc==5)&&!strcmp(argv[1],"commit")){
        size_t length;char *text=read_json(argv[3],&length);cJSON *plan=text?cJSON_ParseWithLengthOpts(text,length+1,NULL,1):NULL;
        const cJSON *list=cJSON_GetObjectItemCaseSensitive(plan,"assets"),*body=cJSON_GetObjectItemCaseSensitive(plan,"body");
        const cJSON *body_hash=cJSON_GetObjectItemCaseSensitive(body,"sha256"),*body_bytes=cJSON_GetObjectItemCaseSensitive(body,"bytes");
        document_sync_asset_t *assets=calloc(64,sizeof(*assets));size_t count=0;bool ok=plan&&cJSON_IsArray(list)&&assets&&cJSON_GetArraySize(list)<=64&&cJSON_IsString(body_hash)&&cJSON_IsNumber(body_bytes)&&body_bytes->valuedouble>=0&&body_bytes->valuedouble<=64*1024*1024&&body_bytes->valuedouble==(uint64_t)body_bytes->valuedouble;
        if(ok)for(const cJSON *a=list->child;a;a=a->next){const cJSON *name=cJSON_GetObjectItemCaseSensitive(a,"name"),*hash=cJSON_GetObjectItemCaseSensitive(a,"sha256"),*bytes=cJSON_GetObjectItemCaseSensitive(a,"bytes");
            if(!cJSON_IsString(name)||!cJSON_IsString(hash)||!cJSON_IsNumber(bytes)||strlen(name->valuestring)>240||strlen(hash->valuestring)!=64||bytes->valuedouble<67||bytes->valuedouble>17*1024*1024||bytes->valuedouble!=(uint32_t)bytes->valuedouble){ok=false;break;}
            snprintf(assets[count].name,sizeof(assets[count].name),"%s",name->valuestring);snprintf(assets[count].sha256,65,"%s",hash->valuestring);assets[count++].bytes=(uint32_t)bytes->valuedouble;
        }
        if(argc==5)document_store_test_crash=atoi(argv[4]);if(ok)error=document_store_sync_commit(argv[2],body_hash->valuestring,(uint64_t)body_bytes->valuedouble,assets,count,&unchanged);free(assets);cJSON_Delete(plan);free(text);
    }
    printf("{\"error\":%d,\"unchanged\":%s}\n",error,unchanged?"true":"false");return 0;
}
