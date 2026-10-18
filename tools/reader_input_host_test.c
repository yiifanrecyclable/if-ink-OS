/* SPDX-License-Identifier: Apache-2.0
 * 使用真实偏好及双指实现，替换NVS边界并注入提交失败。/ Exercise real preferences and multitouch with NVS fault injection.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reader_input.h"
#include "nvs.h"
static reader_input_preferences_t committed,staged;
static bool present,staged_present,fail_commit;
static char seen[16],staged_seen[16];
static unsigned handles;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"rp_input"));(void)mode;staged=committed;staged_present=present;strcpy(staged_seen,seen);++handles;*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1&&handles);--handles;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){assert(h==1&&!strcmp(key,"preferences"));if(!present)return ESP_ERR_NVS_NOT_FOUND;assert(*n>=sizeof(committed));memcpy(out,&committed,sizeof(committed));*n=sizeof(committed);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t n){assert(h==1&&!strcmp(key,"preferences")&&n==sizeof(staged));memcpy(&staged,in,n);staged_present=true;return ESP_OK;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){assert(h==1);if(strcmp(seen,key))return ESP_ERR_NVS_NOT_FOUND;*out=1;return ESP_OK;}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){assert(h==1&&strlen(key)==15&&value==1);strcpy(staged_seen,key);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);if(fail_commit)return ESP_FAIL;committed=staged;present=staged_present;strcpy(seen,staged_seen);return ESP_OK;}
#include "../main/book/reader_input.c"
int main(void){
    for(int i=0;i<READER_SHORTCUT_COUNT;++i)assert(reader_input_action(i)==READER_ACTION_NONE);
    assert(reader_input_assign(READER_SWIPE_RIGHT,READER_ACTION_TOC)==ESP_OK);
    s_loaded=false;s_prefs=(reader_input_preferences_t){.version=1};
    assert(reader_input_action(READER_SWIPE_RIGHT)==READER_ACTION_TOC);
    assert(reader_input_assign(READER_SWIPE_LEFT,READER_ACTION_TOC)==ESP_ERR_INVALID_STATE);
    assert(reader_input_action(READER_SWIPE_LEFT)==READER_ACTION_NONE);
    assert(reader_input_assign(READER_SWIPE_RIGHT,READER_ACTION_TOC)==ESP_OK);
    assert(reader_input_assign(READER_TWO_TAP,READER_ACTION_NONE)==ESP_OK);
    // 兼容备份中的旧重复项，不让它们重新激活。/ Normalize old duplicate backup entries.
    reader_input_preferences_t legacy;reader_input_export(&legacy);legacy.actions[READER_TWO_TAP]=READER_ACTION_TOC;
    assert(reader_input_import(&legacy)==ESP_OK&&reader_input_action(READER_TWO_TAP)==READER_ACTION_NONE);
    committed.actions[READER_TWO_TAP]=READER_ACTION_TOC;s_loaded=false;
    assert(reader_input_action(READER_TWO_TAP)==READER_ACTION_NONE&&reader_input_action(READER_SWIPE_RIGHT)==READER_ACTION_TOC);
    fail_commit=true;assert(reader_input_assign(READER_SWIPE_LEFT,READER_ACTION_BOOKMARKS)==ESP_FAIL);
    assert(reader_input_action(READER_SWIPE_LEFT)==READER_ACTION_NONE);
    assert(reader_input_ack_help("/sdcard/books/a.epub",true)==ESP_FAIL);
    assert(reader_input_needs_help("/sdcard/books/a.epub"));fail_commit=false;
    assert(reader_input_assign(READER_SHORTCUT_COUNT,0)==ESP_ERR_INVALID_ARG);
    assert(reader_input_assign(0,READER_ACTION_COUNT)==ESP_ERR_INVALID_ARG);
    assert(reader_input_ack_help("/sdcard/books/a.epub",false)==ESP_OK);
    assert(!reader_input_needs_help("/sdcard/books/a.epub"));assert(reader_input_needs_help("/sdcard/books/b.epub"));
    assert(reader_input_ack_help("/sdcard/books/b.epub",true)==ESP_OK);
    assert(!reader_input_needs_help("/sdcard/markdown/c.md"));
    reader_input_preferences_t p;reader_input_export(&p);assert(p.hide_help==1);p.hide_help=2;assert(reader_input_import(&p)==ESP_ERR_INVALID_ARG);
    assert(reader_input_ack_help("/sdcard/books/b.epub",false)==ESP_OK);
    assert(reader_input_needs_help("/sdcard/markdown/c.md"));
    reader_input_export(&legacy);legacy.actions[8]=READER_ACTION_NEXT;legacy.actions[11]=READER_ACTION_PANEL;
    assert(reader_input_import(&legacy)==ESP_OK);reader_input_export(&legacy);
    assert(legacy.actions[8]==0&&legacy.actions[11]==0&&sizeof(legacy)==14);
    // 新布局复用已退役槽，旧备份仍按默认列布局迁移。/ Reuse a retired slot; legacy backups retain the column default.
    assert(reader_input_tap_layout()==READER_TAP_COLUMNS);
    assert(reader_input_set_tap_layout(READER_TAP_ROWS)==ESP_OK);
    s_loaded=false;assert(reader_input_tap_layout()==READER_TAP_ROWS);
    reader_input_export(&p);assert(p.version==2&&p.actions[8]==1&&reader_input_preferences_valid(&p));
    p.actions[8]=2;assert(reader_input_import(&p)==ESP_ERR_INVALID_ARG);
    fail_commit=true;assert(reader_input_set_tap_layout(READER_TAP_COLUMNS)==ESP_FAIL);
    assert(reader_input_tap_layout()==READER_TAP_ROWS);fail_commit=false;
    for(int layout=0;layout<2;++layout){
        int coverage=0;
        for(int z=0;z<3;++z){reader_tap_region_t r=reader_input_tap_region(layout,z,684,1056);assert(r.width>0&&r.height>0);coverage+=r.width*r.height;}
        assert(coverage==684*1056);
    }
    reader_tap_region_t middle=reader_input_tap_region(READER_TAP_ROWS,1,684,1000);
    assert(middle.y==300&&middle.height==200);
    assert(reader_input_import(&legacy)==ESP_OK&&reader_input_tap_layout()==READER_TAP_COLUMNS);
    reader_multi_t m={0};
    assert(reader_multi_feed(&m,true,true,2,100,200,300,200,0)==-1);
    assert(reader_multi_feed(&m,true,true,1,100,200,0,0,100)==-1&&m.active);
    assert(reader_multi_feed(&m,true,false,0,0,0,0,0,200)==READER_TWO_TAP);
    assert(reader_multi_feed(&m,true,false,0,0,0,0,0,210)==-1);
    assert(reader_multi_feed(&m,true,true,2,100,200,300,200,300)==-1);
    assert(reader_multi_feed(&m,true,true,2,40,200,360,200,500)==-1);
    assert(reader_multi_feed(&m,true,true,1,40,200,0,0,600)==-1);
    assert(reader_multi_feed(&m,true,false,0,0,0,0,0,700)==READER_PINCH_OUT);
    assert(reader_multi_feed(&m,true,true,2,100,200,400,200,1000)==-1);
    assert(reader_multi_feed(&m,true,true,2,180,200,320,200,1100)==-1);
    assert(reader_multi_feed(&m,true,false,0,0,0,0,0,1200)==READER_PINCH_IN);
    assert(reader_multi_feed(&m,true,true,2,100,200,300,200,1300)==-1);
    assert(reader_multi_feed(&m,true,true,2,260,200,460,200,1400)==-1);
    assert(reader_multi_feed(&m,true,false,0,0,0,0,0,1500)==-1);
    assert(reader_multi_feed(&m,true,true,2,100,200,300,200,1600)==-1);
    assert(reader_multi_feed(&m,false,true,2,100,200,300,200,1700)==-1);
    assert(reader_multi_feed(&m,true,true,2,100,200,300,200,1800)==-1);
    assert(reader_multi_feed(&m,true,false,0,0,0,0,0,1900)==-1);
    assert(!m.active&&!m.blocked&&!handles);
    puts("reader input: unbound defaults, atomic preferences, help scope, pinch/swipe/tap and residual-finger cancellation passed");
}
