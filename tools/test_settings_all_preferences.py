#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""真实全局设置：失败不改RAM，同值可重试，新进程保留。 / Real global preferences: failed writes retain RAM, retries work and fresh processes reload."""
from pathlib import Path
import subprocess,tempfile,os,sys
R=Path(__file__).resolve().parents[1]
CASES=[
 ('sleep_mode','sleep_mode','sleep',0,2),('staged_shutdown','staged_shutdown','shutdown',1,0),('home_full_refresh','home_full_refresh','home_full',1,0),
 ('idle_sleep_minutes','idle_sleep_minutes','idle_sleep',30,10),('system_font_size','system_font_size','sys_size',160,140),('pickup_wake','pickup_wake','pickup',1,0),
 ('book_px','book_px','bk_px',52,100),('reader_full_pages','reader_full_pages','rd_gc16',30,5),('reader_turn_effect','reader_turn_effect','rd_turn',1,0),
 ('reader_power_turn','reader_power_turn','rd_power',1,0),('reader_fullscreen','reader_fullscreen','rd_fullscreen',1,0),('reader_immersive','reader_immersive','rd_immersive',1,0),
 ('reader_hide_images','reader_hide_images','rd_no_image',1,0),('reader_vertical_turn','reader_vertical_turn','rd_vertical',1,0),('reader_hold_refresh','reader_hold_refresh','rd_hold_full',1,0),
 ('shelf_recent_sort','shelf_recent_sort','shelf_recent',1,0),('book_tracking','book_tracking','bk_track',4,1),('book_indent','book_indent','bk_indent',3,1),
 ('book_reading_line','book_reading_line','bk_rule',2,1),('book_reading_line_offset','book_reading_line_offset','bk_rule_y',8,-6),
 ('book_line_spacing','book_line_spacing','bk_line',150,110),('book_margin','book_margin','bk_margin',8,20),('book_paragraph_spacing','book_paragraph_spacing','bk_para',75,25),
 ('language','language','language',1,0),('status_show_date','status_show_date','status_date',0,1),('status_date_format','status_date_format','status_date',2,1)]
STRINGS=[('device_name','device_name','dev_name','First','Second'),('avatar_path','avatar_path','dev_avatar','/sdcard/pictures/a.png','/sdcard/pictures/b.png'),
 ('status_signature','status_signature','status_sig','First','Second'),('font_path','font_path','font','/sdcard/fonts/a.ttf','/sdcard/fonts/b.ttf'),
 ('system_font_path','system_font_path','sys_font','/sdcard/fonts/a.ttf','/sdcard/fonts/b.ttf')]
head=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "read_pico_sd.h"
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){memset(out,0,sizeof(*out));return ESP_OK;}
int main(int argc,char **argv){(void)argc;app_settings_init();int read_only=!strcmp(argv[1],"read");
'''
body=''
for setter,getter,key,first,last in CASES:
 body+=f'''if(!read_only){{assert(app_settings_set_{setter}({first})==ESP_OK);setenv("PICO_TEST_NVS_SET_FAIL","{key}",1);assert(app_settings_set_{setter}({last})!=ESP_OK);assert(app_settings_{getter}()=={first});assert(app_settings_take_save_error()!=ESP_OK);unsetenv("PICO_TEST_NVS_SET_FAIL");assert(app_settings_set_{setter}({last})==ESP_OK);}}assert(app_settings_{getter}()=={last});\n'''
for setter,getter,key,first,last in STRINGS:
 body+=f'''if(!read_only){{assert(app_settings_set_{setter}("{first}")==ESP_OK);setenv("PICO_TEST_NVS_SET_FAIL","{key}",1);assert(app_settings_set_{setter}("{last}")!=ESP_OK);assert(!strcmp(app_settings_{getter}(),"{first}"));assert(app_settings_take_save_error()!=ESP_OK);unsetenv("PICO_TEST_NVS_SET_FAIL");assert(app_settings_set_{setter}("{last}")==ESP_OK);}}assert(!strcmp(app_settings_{getter}(),"{last}"));\n'''
body+=r'''
if(!read_only){
 const char *faults[]={"PICO_TEST_NVS_OPEN_FAIL","PICO_TEST_NVS_GET_FAIL","PICO_TEST_NVS_READBACK_FAIL","PICO_TEST_NVS_COMMIT_FAIL"};
 const char *values[]={"rw","sys_size","sys_size","1"};
 for(unsigned i=0;i<4;i++){
  setenv(faults[i],values[i],1);assert(app_settings_set_system_font_size(160)!=ESP_OK);assert(app_settings_system_font_size()==140);unsetenv(faults[i]);assert(app_settings_set_system_font_size(140)==ESP_OK);
 }
 setenv("PICO_TEST_NVS_OPEN_FAIL","ro",1);assert(app_settings_set_system_font_size(160)!=ESP_OK);assert(app_settings_system_font_size()==140);unsetenv("PICO_TEST_NVS_OPEN_FAIL");assert(app_settings_set_system_font_size(140)==ESP_OK);
}
assert(app_settings_load_status()==ESP_OK);puts("PASS: global preferences preserve confirmed RAM on failure and reload across processes");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='settings-all-',dir=R/'build') as tmp:
 p=Path(tmp);(p/'build/simulator').mkdir(parents=True);(p/'test.c').write_text(head+body)
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-DPICO_HOST_SIMULATOR','-DPICO_SIM_CARD_PATH="unused"','-include',str(R/'tools/simulator/fs.h')]
 for inc in ['tools/simulator/include','main','main/book','main/ui','main/cloud','components/read_pico_transfer/include']:flags+=['-I'+str(R/inc)]
 sources=[p/'test.c',R/'main/settings.c',R/'main/wallpaper_preferences.c',R/'main/ui/ui_i18n.c',R/'tools/simulator/fs.c',R/'tools/simulator/nvs.c']
 subprocess.run(flags+list(map(str,sources))+(['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections'])+['-o',str(p/'test')],check=True)
 for mode in ['write','read']:subprocess.run([str(p/'test'),mode],cwd=p,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print(f'PASS: {len(CASES)+len(STRINGS)} global preferences, five failure stages, same-value retries and fresh-process durability.')
