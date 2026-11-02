/* SPDX-License-Identifier: Apache-2.0
 * 中文：固定长度CRC双槽，仅写.readpico/settings/wallpaper.*，不挂卡、不格式化、不擦NVS。
 * English: Fixed CRC dual slots under .readpico/settings/wallpaper.*, never mount, format or erase NVS.
 */
#include "wallpaper_preferences.h"
#include "ui/ui_wallpaper_rotation.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef PICO_HOST_SIMULATOR
#include <stdlib.h>
#endif
#define WP_ROOT WALLPAPER_CARD_ROOT "/.readpico/settings"
#define WP_HEAD 16
#define WP_FILE_BYTES (sizeof(WP_ROOT)+sizeof("/wallpaper.a"))
#define WP_BYTES (WP_HEAD+2*WALLPAPER_PREF_PATH_BYTES)
typedef struct { bool exists,valid;uint32_t generation;wallpaper_preferences_t value; } slot_t;
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t crc(const uint8_t *data){uint32_t c=UINT32_MAX;for(unsigned i=0;i<WP_BYTES;++i){if(i>=12&&i<16)continue;c^=data[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(UINT32_C(0xedb88320)&(0-(c&1)));}return ~c;}
static bool valid_path(const char *p){return strnlen(p,WALLPAPER_PREF_PATH_BYTES)<WALLPAPER_PREF_PATH_BYTES&&(!*p||wallpaper_card_path_valid(p));}
static void path_for(unsigned slot,char out[WP_FILE_BYTES]){snprintf(out,WP_FILE_BYTES,WP_ROOT "/wallpaper.%c",slot?'b':'a');}
static slot_t read_slot(unsigned index){
    slot_t s={0};char path[WP_FILE_BYTES];path_for(index,path);FILE *f=fopen(path,"rb");if(!f){s.exists=errno!=ENOENT;return s;}s.exists=true;
    uint8_t bytes[WP_BYTES];bool ok=fread(bytes,1,sizeof(bytes),f)==sizeof(bytes)&&fgetc(f)==EOF&&!ferror(f);if(fclose(f))ok=false;
    if(!ok||memcmp(bytes,"IFWP",4)||bytes[4]!=1||bytes[5]||bytes[6]||bytes[7]||!get32(bytes+8)||get32(bytes+12)!=crc(bytes))return s;
    memcpy(s.value.path,bytes+WP_HEAD,WALLPAPER_PREF_PATH_BYTES);memcpy(s.value.last,bytes+WP_HEAD+WALLPAPER_PREF_PATH_BYTES,WALLPAPER_PREF_PATH_BYTES);
    s.valid=valid_path(s.value.path)&&valid_path(s.value.last);s.generation=get32(bytes+8);return s;
}
static int latest(const slot_t s[2]){if(!s[0].valid)return s[1].valid?1:-1;if(!s[1].valid)return 0;return s[1].generation>s[0].generation?1:0;}
esp_err_t wallpaper_preferences_load(wallpaper_preferences_t *out){
    if(!out)return ESP_ERR_INVALID_ARG;
    slot_t s[2]={read_slot(0),read_slot(1)};int at=latest(s);
    if(at<0)return s[0].exists||s[1].exists?ESP_ERR_INVALID_CRC:ESP_ERR_NOT_FOUND;
    *out=s[at].value;return ESP_OK;
}
static bool directory(const char *path){if(!mkdir(path,0755))return true;struct stat s;return errno==EEXIST&&!stat(path,&s)&&S_ISDIR(s.st_mode);}
esp_err_t wallpaper_preferences_save(const wallpaper_preferences_t *value){
    if(!value||!valid_path(value->path)||!valid_path(value->last))return ESP_ERR_INVALID_ARG;
    struct stat root;if(stat(WALLPAPER_CARD_ROOT,&root)||!S_ISDIR(root.st_mode))return ESP_ERR_INVALID_STATE;
    slot_t slots[2]={read_slot(0),read_slot(1)};int old=latest(slots);if(old>=0&&slots[old].generation==UINT32_MAX)return ESP_ERR_INVALID_STATE;
    if(!directory(WALLPAPER_CARD_ROOT "/.readpico")||!directory(WP_ROOT))return ESP_FAIL;
    unsigned target=old<0?0:1-old;uint32_t gen=old<0?1:slots[old].generation+1;
    uint8_t bytes[WP_BYTES]={'I','F','W','P',1};put32(bytes+8,gen);memcpy(bytes+WP_HEAD,value->path,strlen(value->path)+1);memcpy(bytes+WP_HEAD+WALLPAPER_PREF_PATH_BYTES,value->last,strlen(value->last)+1);put32(bytes+12,crc(bytes));
    char path[WP_FILE_BYTES];path_for(target,path);FILE *f=fopen(path,"wb");if(!f)return ESP_FAIL;
    size_t length=sizeof(bytes);
#ifdef PICO_HOST_SIMULATOR
    const char *fault=getenv("PICO_TEST_WALLPAPER_WRITE_FAIL");if(fault)length=sizeof(bytes)/2;
#endif
    bool ok=fwrite(bytes,1,length,f)==sizeof(bytes);if(fflush(f))ok=false;if(fsync(fileno(f)))ok=false;if(fclose(f))ok=false;
    slot_t read=read_slot(target);if(!ok||!read.valid||read.generation!=gen||strcmp(read.value.path,value->path)||strcmp(read.value.last,value->last))return ESP_FAIL;
    return ESP_OK;
}
