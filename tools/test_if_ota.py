# SPDX-License-Identifier: Apache-2.0
"""IF本地升级项目隔离与边界检查。 / Project isolation and bounds for local IF upgrades."""
from pathlib import Path
import subprocess,tempfile,re
root=Path(__file__).resolve().parents[1]
source=(root/'main/ota_update.c').read_text()
def function(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M)
    assert m,name
    n=m.end();depth=1
    while depth:
        if source[n]=='{':depth+=1
        elif source[n]=='}':depth-=1
        n+=1
    return source[m.start():n]
unit=r'''
#include "ui/ui_i18n.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "ota_update.h"
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_FAIL -1
typedef struct {unsigned char magic,padding[23];} esp_image_header_t;
typedef struct {unsigned address,data_len;} esp_image_segment_header_t;
typedef struct {unsigned magic_word,secure_version,reserv1[2];char version[32],project_name[32];unsigned char padding[176];} esp_app_desc_t;
typedef struct {size_t size;} esp_partition_t;
static esp_app_desc_t current={.magic_word=ESP_APP_DESC_MAGIC_WORD,.version="0.3.3-rc83-if.1",.project_name="Read_Pico_IF"};
static esp_partition_t target={.size=4*1024*1024};
static int no_slot;
static const esp_app_desc_t *esp_app_get_description(void){return &current;}
static const esp_partition_t *esp_ota_get_next_update_partition(void *unused){(void)unused;return no_slot?NULL:&target;}
'''
unit+='\n'.join(function(n) for n in ('set_message','copy_field','read_candidate','pico_ota_inspect'))
unit+=r'''
static void candidate(const char *path,const char *project,const char *version){
 esp_image_header_t h={.magic=ESP_IMAGE_HEADER_MAGIC};
 esp_image_segment_header_t s={.data_len=sizeof(esp_app_desc_t)};
 esp_app_desc_t d={.magic_word=ESP_APP_DESC_MAGIC_WORD};
 snprintf(d.project_name,sizeof(d.project_name),"%s",project);snprintf(d.version,sizeof(d.version),"%s",version);
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(&h,1,sizeof(h),f)==sizeof(h));assert(fwrite(&s,1,sizeof(s),f)==sizeof(s));assert(fwrite(&d,1,sizeof(d),f)==sizeof(d));assert(!fclose(f));
}
int main(int argc,char **argv){
 assert(argc==2);const char *path=argv[1];pico_ota_info_t info;
 assert(!strcmp(PICO_OTA_UPDATE_PATH,"/sdcard/Pico-IF-update.bin"));
 assert(pico_ota_inspect(path,&info)==ESP_ERR_NOT_FOUND&&!info.ready);
 candidate(path,"Read_Pico_IF","0.3.3-rc83-if.2");assert(pico_ota_inspect(path,&info)==ESP_OK&&info.ready);
 no_slot=1;assert(pico_ota_inspect(path,&info)==ESP_ERR_NOT_SUPPORTED&&!info.ready);no_slot=0;
 target.size=10;assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_SIZE&&!info.ready);target.size=4*1024*1024;
 candidate(path,"Read_Pico","0.3.3-rc84");assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_RESPONSE&&!info.ready);
 candidate(path,"Read_Pico_IF","0.3.3-rc83-if.1");assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_STATE&&!info.ready);
 candidate(path,"Read_Pico_IF","0.3.3-rc83-if.2");FILE *f=fopen(path,"r+b");assert(f);fputc(0,f);fclose(f);assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_RESPONSE&&!info.ready);
 f=fopen(path,"wb");assert(f);fputc(0,f);fclose(f);assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_SIZE&&!info.ready);
 snprintf(current.version,sizeof(current.version),"%s","0.1.6-beta3");
 candidate(path,"Read_Pico_IF","0.1.6-beta3-r2");assert(pico_ota_inspect(path,&info)==ESP_OK&&info.ready);
 snprintf(current.version,sizeof(current.version),"%s","0.1.6-beta3-r2");
 assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_STATE&&!info.ready);
 snprintf(current.version,sizeof(current.version),"%s","0.1.6");
 candidate(path,"Read_Pico_IF","0.1.7-beta");assert(pico_ota_inspect(path,&info)==ESP_OK&&info.ready);
 snprintf(current.version,sizeof(current.version),"%s","0.1.7-beta");assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_STATE&&!info.ready);
 candidate(path,"Read_Pico_IF","0.1.7-beta-r2");assert(pico_ota_inspect(path,&info)==ESP_OK&&info.ready);
 snprintf(current.version,sizeof(current.version),"%s","0.1.7-beta-r2");assert(pico_ota_inspect(path,&info)==ESP_ERR_INVALID_STATE&&!info.ready);
 puts("PASS IF local upgrade identity, beta3-to-r2, same-version rejection, missing slot, capacity, malformed header and truncation");
}
'''
with tempfile.TemporaryDirectory(prefix='if-ota-') as temp:
    work=Path(temp);file=work/'test.c';file.write_text(unit);binary=work/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'tools/html_text_stubs'),'-I'+str(root/'main'),str(root/'main/ui/ui_i18n.c'),str(file),'-o',str(binary)],check=True)
    subprocess.run([str(binary),str(work/'candidate.bin')],check=True)
