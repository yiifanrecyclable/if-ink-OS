/* SPDX-License-Identifier: Apache-2.0
 * 两套真实字体的缓存、切换和释放回归。/ Regression for real font-context reuse, switching and release.
 */
#include <assert.h>
#include "../main/font/ttf_font.c"
#include "../main/font/font_path_policy.h"
static const char *fixture_path;
static bool is_font_path_for_test(const char *path){
    return app_font_path_valid(path)||(fixture_path && !strcmp(path,fixture_path));
}
#define app_font_path_valid is_font_path_for_test
#include "../main/font/app_font_context.c"
#undef app_font_path_valid
static const char *reading_font = TTF_FONT_BUILTIN;
static int saved_slot=-1;
static app_language_t language;
static unsigned store_revision, media_epoch;
const char *app_settings_fonts_dir(void){return "/missing";}
const char *app_settings_font_path(void){return reading_font;}
const char *app_settings_system_font_path(void){return "";}
app_language_t app_settings_language(void){return language;}
esp_err_t app_settings_set_font_path(const char *path){(void)path;saved_slot=0;return ESP_OK;}
esp_err_t app_settings_set_system_font_path(const char *path){(void)path;saved_slot=2;return ESP_OK;}
esp_err_t content_english_font_set(bool document,const char *path){assert(!document);(void)path;saved_slot=1;return ESP_OK;}
esp_err_t content_english_system_font_set(const char *path){(void)path;saved_slot=3;return ESP_OK;}
esp_err_t content_english_system_font_checked(char *path,size_t capacity){assert(capacity);path[0]=0;return ESP_OK;}
const char *content_english_font(bool document){(void)document;return "";}
unsigned book_store_revision(void){return store_revision;}
uint32_t record_store_media_epoch(void){return media_epoch;}
void ui_text_set_system_font(bool enabled){(void)enabled;}
void ui_text_set_system_scale(bool enabled){(void)enabled;}
static uint8_t frame[684*1216/2],reference[684*1216/2];
static void sample(void){
    memset(frame,255,sizeof(frame));
    ttf_draw_text_px(frame,36,120,48,"阅读 Aa 0123",EPD_DRAW_ALIGN_LEFT,0,15);
}

static void check_english_justification(void) {
    const char words[] = "red sun moon";
    int natural = ttf_text_width_px(48, words);
    int target = natural + 13;
    int first, last;
    assert(ttf_text_range_px(48, words, 0, target, true, 0, strlen(words), &first, &last, NULL));
    assert(first == 0 && last == target);
    assert(ttf_text_range_px(48, words, 0, target, true, 4, 7, &first, &last, NULL));
    int sun = ttf_text_width_px(48, "red ") + 7;
    assert(first == sun && last - first == ttf_text_width_px(48, "sun"));
    int moon = ttf_text_width_px(48, "red sun ") + 13;
    memset(frame, 255, sizeof(frame));
    memset(reference, 255, sizeof(reference));
    ttf_draw_text_px_fitted(frame, 36, 120, 48, words, 0, target, true, 0, 15);
    ttf_draw_text_px(reference, 36, 120, 48, "red", EPD_DRAW_ALIGN_LEFT, 0, 15);
    ttf_draw_text_px(reference, 36 + sun, 120, 48, "sun", EPD_DRAW_ALIGN_LEFT, 0, 15);
    ttf_draw_text_px(reference, 36 + moon, 120, 48, "moon", EPD_DRAW_ALIGN_LEFT, 0, 15);
    assert(!memcmp(frame, reference, sizeof(frame)));
    assert(ttf_text_range_px(48, words, 0, target, false, 0, strlen(words), &first, &last, NULL));
    assert(last == natural);
    assert(ttf_text_range_px(48, words, 0, natural + 100, true, 0, strlen(words), &first, &last, NULL));
    assert(last == natural);
}

int main(int argc,char **argv){
    assert(argc==2);
    assert(ttf_font_select_context(false,TTF_FONT_BUILTIN)==ESP_OK);
    check_english_justification();
    sample();memcpy(reference,frame,sizeof(frame));
    int width=ttf_text_width_px(48,"阅读 Aa 0123");
    assert(ttf_font_select_context(true,argv[1])==ESP_OK);
    sample();unsigned loads=ttf_sim_font_loads;
    assert(s_font_contexts[1].state_glyf_ram==NULL&&s_font_contexts[1].state_gvar_ram==NULL);
    for(int i=0;i<100;++i){
        assert(ttf_font_select_context(false,TTF_FONT_BUILTIN)==ESP_OK);
        assert(ttf_text_width_px(48,"阅读 Aa 0123")==width);
        sample();assert(!memcmp(reference,frame,sizeof(frame)));
        assert(ttf_font_select_context(true,argv[1])==ESP_OK);sample();
    }
    assert(ttf_sim_font_loads==loads);
    assert(s_font_contexts[0].state_cache_bytes<=1280u*1024&&s_font_contexts[1].state_cache_bytes<=256u*1024);
    unsigned before_clear=ttf_sim_font_loads;
    assert(ttf_font_cache_clear_all()>0);
    assert(s_font_contexts[0].state_font_ready&&s_font_contexts[1].state_font_ready);
    assert(!s_font_contexts[0].state_cache_bytes&&!s_font_contexts[1].state_cache_bytes);
    assert(ttf_font_select_context(false,TTF_FONT_BUILTIN)==ESP_OK);sample();
    assert(!memcmp(reference,frame,sizeof(frame))&&ttf_sim_font_loads==before_clear);
    fixture_path=argv[1];
    for(int role=0;role<2;++role)for(int en=0;en<2;++en){
        assert(app_font_save_choice(role,en,argv[1])==ESP_OK);
        assert(saved_slot==2*role+en);
    }
    assert(app_font_save_choice(APP_FONT_SYSTEM,false,"/sdcard/elsewhere/Test.ttf")==ESP_ERR_INVALID_ARG);
    assert(app_font_save_choice(APP_FONT_READING,false,"/sdcard/fonts/Test.otf")==ESP_ERR_INVALID_ARG);
    assert(app_font_path_protected("/SDCARD/FONTS/picosans-medium.TTF"));
    assert(app_font_path_protected("/sdcard/fonts/PICOSA~1.TTF"));
    assert(!app_font_path_protected("/sdcard/fonts/MyFace.ttf"));
    // 缺失的阅读字体保留已加载字体且轮询不重绘。/ A missing reading face preserves the active face without redraw.
    fixture_path=reading_font=argv[1];
    assert(app_font_activate_reading());
    unsigned before_missing=ttf_sim_font_loads;
    char candidate[]="build/book-tests/font-retry-XXXXXX";
    int fd=mkstemp(candidate);
    assert(fd>=0&&close(fd)==0&&unlink(candidate)==0);
    fixture_path=reading_font=candidate;
    for(int i=0;i<8;++i)assert(!app_font_retry_active());
    assert(!strcmp(ttf_font_path(),argv[1])&&ttf_sim_font_loads==before_missing);
    fd=open(candidate,O_WRONLY|O_CREAT|O_EXCL,0600);
    size_t size=(size_t)(builtin_ttf_end-builtin_ttf_start);
    assert(fd>=0&&write(fd,builtin_ttf_start,size)==(ssize_t)size&&close(fd)==0);
    assert(!app_font_retry_active()&&ttf_sim_font_loads==before_missing);
    ++store_revision;
    assert(app_font_retry_active()&&!strcmp(ttf_font_path(),candidate));
    assert(!app_font_retry_active()&&ttf_sim_font_loads==before_missing+1);
    assert(unlink(candidate)==0);
    assert(ttf_font_select_context(false,TTF_FONT_BUILTIN)==ESP_OK);
    // 替换或打开失败只影响选中的槽；另一个字体及字形保持有效。/ Replacing or failing a face preserves the other slot.
    assert(ttf_font_select_context(true,"/missing-font.ttf")==ESP_ERR_NOT_FOUND);
    assert(ttf_font_select_context(false,TTF_FONT_BUILTIN)==ESP_OK);
    sample();assert(!memcmp(reference,frame,sizeof(frame)));
    ttf_font_open_builtin();
    assert(s_font_contexts[1].state_font_ready);
    ttf_font_unload();ttf_font_unload();
    for(unsigned i=0;i<2;++i){
        ttf_context_t *ctx=&s_font_contexts[i];
        assert(!ctx->state_font_ready&&ctx->state_font_fd==-1&&!ctx->state_font_mem);
        assert(!ctx->state_font_data&&!ctx->state_file_loca&&!ctx->state_cache_bytes);
        assert(!ctx->state_io_data&&!ctx->state_io_run_buf&&!ctx->state_glyf_ram&&!ctx->state_gvar_ram);
    }
    assert(ttf_font_select_context(false,TTF_FONT_BUILTIN)==ESP_OK);sample();
    assert(!memcmp(reference,frame,sizeof(frame)));
    ttf_font_unload();
    puts("font contexts: cached switches, stable glyphs, missing-font retries and recovery after a card file update passed");
    return 0;
}
