# SPDX-License-Identifier: Apache-2.0
"""真实键校验、packed RMW与无卡壁纸故障测试。 / Real settings validators, packed RMW and no-card wallpaper faults."""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/settings.c').read_text()
def function(name):
    start=re.search(r'^(?:static )?(?:esp_err_t|bool|void) '+name+r'\([^;]*?\)\s*\{',source,re.M).start()
    at=source.index('{',start);depth=1;end=at+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]+'\n'
fake=(ROOT/'tools/settings_aux_storage_host_test.c').read_text().split('#include "../main/book/reader_input.c"')[0]
macros='\n'.join(line for line in source.splitlines() if line.startswith('#define NVS_'))
code=fake+'''
#include "settings.h"
#include "book/book_policy.h"
#include "settings_storage.h"
#include "wallpaper_preferences.h"
#include "ui/ui_wallpaper_rotation.h"
#include "esp_log.h"
#undef ESP_LOGW
#define ESP_LOGW(tag,...) ((void)snprintf(NULL,0,__VA_ARGS__))
static esp_err_t s_save_error,s_load_error,s_wallpaper_load_error;
static uint8_t s_status_date;
static char s_wallpaper[288],s_wallpaper_last[288];
static void settings_trace_record(const char *stage,esp_err_t error,bool arm){(void)stage;(void)error;(void)arm;}
static bool wallpaper_card_ready(void){return false;}
void app_settings_wallpaper_media_ready(void){}
esp_err_t wallpaper_preferences_save(const wallpaper_preferences_t *value){(void)value;assert(false);return ESP_FAIL;}
'''+macros+'\n'
for name in ('idle_sleep_valid','status_date_valid','settings_result','settings_u8_valid','settings_read_u8','settings_read_str','nvs_put_u8','status_date_read_current','app_settings_set_status_show_date','app_settings_set_status_date_format','wallpaper_store','app_settings_save_wallpaper_path','restore_verify_u8','restore_verify_str','app_settings_note_wallpaper'):
    code+=function(name)
code+='''
static void reset_faults(void){fail_open=fail_read=fail_set=fail_commit=fail_readback=corrupt_readback=wrote=false;}
int main(void){
    const struct {const char *key;uint8_t invalid;} cases[]={
        {NVS_KEY_LANGUAGE,2},{NVS_KEY_IDLE_SLEEP,2},{NVS_KEY_STATUS_DATE,7},{NVS_KEY_SHUTDOWN_MODE,2},
        {NVS_KEY_SYS_SIZE,99},{NVS_KEY_SLEEP,3},{NVS_KEY_HOME_FULL,2},{NVS_KEY_PICKUP,2},
        {NVS_KEY_BOOK_PX,35},{NVS_KEY_BOOK_PX,101},{NVS_KEY_READER_FULL,7},{NVS_KEY_READER_TURN,2},{NVS_KEY_BOOK_LINE,109},
        {NVS_KEY_BOOK_PARA,76},{NVS_KEY_BOOK_MARGIN,7},{NVS_KEY_BOOK_TRACK,5},{NVS_KEY_BOOK_INDENT,4},
        {NVS_KEY_BOOK_RULE,3},{NVS_KEY_BOOK_RULE_OFFSET,9},{NVS_KEY_FULLSCREEN,2}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        nvs_handle_t h;uint8_t got=0;s_load_error=ESP_OK;
        assert(nvs_open(NVS_NS,NVS_READWRITE,&h)==ESP_OK);assert(nvs_set_u8(h,cases[i].key,cases[i].invalid)==ESP_OK);
        assert(settings_read_u8(h,cases[i].key,&got)==ESP_ERR_INVALID_RESPONSE);assert(s_load_error==ESP_ERR_INVALID_RESPONSE);nvs_close(h);
    }
    assert(settings_u8_valid(NVS_KEY_BOOK_LINE,180)&&settings_u8_valid(NVS_KEY_SYS_SIZE,200));
    assert(settings_u8_valid(NVS_KEY_BOOK_PX,100));
    nvs_handle_t h;assert(nvs_open(NVS_NS,NVS_READWRITE,&h)==ESP_OK);assert(nvs_set_str(h,NVS_KEY_AVATAR,"/private/no.png")==ESP_OK);
    char text[288];size_t size=sizeof(text);s_load_error=ESP_OK;assert(settings_read_str(h,NVS_KEY_AVATAR,text,&size)==ESP_ERR_INVALID_RESPONSE&&s_load_error==ESP_ERR_INVALID_RESPONSE);nvs_close(h);
    assert(settings_storage_put_u8(NVS_NS,NVS_KEY_STATUS_DATE,0x82)==ESP_OK);s_status_date=0;
    assert(app_settings_set_status_date_format(APP_STATUS_DATE_MONTH_DAY_SLASH)==ESP_OK&&s_status_date==0x84);
    s_status_date=0;assert(app_settings_set_status_show_date(true)==ESP_OK&&s_status_date==4);
    int before=writes;fail_read=true;assert(app_settings_set_status_show_date(false)==ESP_FAIL&&writes==before);reset_faults();
    assert(settings_storage_put_u8(NVS_NS,NVS_KEY_STATUS_DATE,7)==ESP_OK);before=writes;assert(app_settings_set_status_date_format(APP_STATUS_DATE_ISO)==ESP_ERR_INVALID_RESPONSE&&writes==before);
    assert(app_settings_save_wallpaper_path("/sdcard/old.png")==ESP_OK);before=writes;fail_set=true;
    assert(app_settings_save_wallpaper_path("/sdcard/new.png")==ESP_FAIL&&writes==before&&!strcmp(s_wallpaper,"/sdcard/old.png"));reset_faults();
    fail_commit=true;assert(app_settings_save_wallpaper_path("/sdcard/new.png")==ESP_FAIL&&writes==before+1&&!strcmp(s_wallpaper,"/sdcard/old.png"));reset_faults();
    assert(nvs_open(NVS_NS,NVS_READONLY,&h)==ESP_OK);size=sizeof(text);assert(nvs_get_str(h,NVS_KEY_WALLPAPER,text,&size)==ESP_OK&&!strcmp(text,"/sdcard/new.png"));nvs_close(h);
    assert(app_settings_save_wallpaper_path("/sdcard/new.png")==ESP_OK&&!strcmp(s_wallpaper,"/sdcard/new.png"));
    assert(nvs_open(NVS_NS,NVS_READONLY,&h)==ESP_OK);
    assert(restore_verify_str(h,NVS_KEY_WALLPAPER,"/sdcard/new.png")==ESP_OK);
    assert(restore_verify_str(h,NVS_KEY_WALLPAPER,"/sdcard/old.png")==ESP_ERR_INVALID_RESPONSE);
    assert(restore_verify_u8(h,NVS_KEY_STATUS_DATE,7)==ESP_OK);
    assert(restore_verify_u8(h,NVS_KEY_STATUS_DATE,0)==ESP_ERR_INVALID_RESPONSE);nvs_close(h);
    assert(handles==0);puts("PASS invalid stored values, legacy migrations, fresh packed-date RMW, no-card wallpaper retry/no blind rollback and restore readback");
}
'''
with tempfile.TemporaryDirectory(prefix='pico-settings-validation-') as temp:
    work=Path(temp);(work/'test.c').write_text(code)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-g','-Itools/settings_backup_stubs','-Imain',str(work/'test.c'),'-o',str(work/'test')],cwd=ROOT,check=True)
    subprocess.run([str(work/'test')],cwd=ROOT,check=True)
