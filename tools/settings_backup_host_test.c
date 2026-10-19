/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the firmware backup format and validation against a temporary card. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __APPLE__
/* Some host libc versions predate strlcpy, which ESP-IDF provides. */
size_t strlcpy(char *dst, const char *src, size_t cap) {
    size_t length = strlen(src);
    if (cap) {
        size_t copied = length < cap - 1 ? length : cap - 1;
        memcpy(dst, src, copied);
        dst[copied] = 0;
    }
    return length;
}
#endif

#define APP_SETTINGS_BACKUP_ROOT "build/book-tests/settings-card"
#include "../main/settings.c"

static read_pico_transfer_wifi_backup_t saved_wifi = {
    .configured = 1, .ssid = "Home_2.4G", .password = "password123"
};
static unsigned wifi_imports;
static cloud_config_t saved_cloud = {.folder="PicoBox",.account="backup-test@example.invalid",.password="test-only-secret"};
static esp_err_t cloud_export_error,cloud_restore_error;
static unsigned cloud_restores;
static bool cloud_fail_once;
bool cloud_config_valid(const cloud_config_t *c,bool secret) {
    return c&&memchr(c->folder,0,sizeof(c->folder))&&c->folder[0]&&c->folder[0]!='/'&&
        memchr(c->account,0,sizeof(c->account))&&memchr(c->password,0,sizeof(c->password))&&
        !strstr(c->folder,"..")&&!strpbrk(c->account,":\r\n")&&!strpbrk(c->password,"\r\n")&&
        (!secret||(c->account[0]&&c->password[0]));
}
esp_err_t cloud_config_backup_export(cloud_config_t *out) { *out=saved_cloud;return cloud_export_error; }
esp_err_t cloud_config_backup_restore(const cloud_config_t *c) {
    if(cloud_restore_error)return cloud_restore_error;
    if(cloud_fail_once){cloud_fail_once=false;return ESP_FAIL;}
    assert(cloud_config_valid(c,false));saved_cloud=*c;++cloud_restores;return ESP_OK;
}
esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out) {
    *out = saved_wifi;
    return ESP_OK;
}
bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *backup) {
    if (!backup || backup->configured > 1) return false;
    if (!backup->configured) return !backup->ssid[0] && !backup->password[0];
    return backup->ssid[0] &&
           strnlen(backup->ssid, sizeof(backup->ssid)) < sizeof(backup->ssid) &&
           strnlen(backup->password, sizeof(backup->password)) < sizeof(backup->password);
}
esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *backup) {
    if (!read_pico_transfer_wifi_backup_valid(backup)) return ESP_ERR_INVALID_ARG;
    saved_wifi = *backup;
    ++wifi_imports;
    return ESP_OK;
}

static unsigned history_saves, history_restores;
static bool history_fail_once;
static unsigned history_failures;
static unsigned history_value = 17;
esp_err_t book_history_backup_write(FILE *file) {
    ++history_saves;
    return fwrite("RPHIST1", 1, 8, file) == 8 && fwrite(&history_value,1,sizeof(history_value),file)==sizeof(history_value) ? ESP_OK : ESP_FAIL;
}
bool book_history_backup_validate(FILE *file) {
    char marker[8];
    unsigned value;
    return fread(marker, 1, 8, file) == 8 && !memcmp(marker, "RPHIST1", 8) && fread(&value,1,sizeof(value),file)==sizeof(value) && fgetc(file) == EOF;
}
esp_err_t book_history_backup_restore(FILE *file) {
    ++history_restores;
    if (!book_history_backup_validate(file)) return ESP_ERR_INVALID_RESPONSE;
    assert(fseek(file,-(long)sizeof(history_value),SEEK_END)==0);
    if(history_fail_once||history_failures){history_fail_once=false;if(history_failures)--history_failures;history_value=0;return ESP_FAIL;}
    assert(fread(&history_value,1,sizeof(history_value),file)==sizeof(history_value));
    return ESP_OK;
}

static bool card_mounted = true;
static bool commit_fails;
static int commit_count;
static uint8_t test_loaded_system_size, persisted_book_px;
static reader_input_preferences_t input_saved,input_staged;
static bool input_present,input_pending;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) { info->mounted = card_mounted; return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { (void)ns; (void)mode; *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; input_pending=false; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) { (void)h; if (!strcmp(key, NVS_KEY_SYS_SIZE) && test_loaded_system_size) { *value = test_loaded_system_size; return ESP_OK; } return ESP_FAIL; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) { (void)h; if(!strcmp(key,NVS_KEY_BOOK_PX))persisted_book_px=value; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size) { (void)h; (void)key; (void)value; (void)size; return ESP_FAIL; }
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) { (void)h; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { (void)h; (void)key; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; ++commit_count;if(commit_fails)return ESP_FAIL;if(input_pending){input_saved=input_staged;input_present=true;}return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){(void)h;assert(!strcmp(key,"preferences"));if(!input_present)return ESP_ERR_NVS_NOT_FOUND;assert(*n>=sizeof(input_saved));memcpy(out,&input_saved,sizeof(input_saved));*n=sizeof(input_saved);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t n){(void)h;assert(!strcmp(key,"preferences")&&n==sizeof(input_staged));memcpy(&input_staged,in,n);input_pending=true;return ESP_OK;}

int main(void) {
    // 日期格式、UTC+8跨日、隐藏日期保留格式。/ Numeric formats, UTC+8 rollover and retaining format while hidden.
    char clock[64];const char *expected[]={"2024-01-02 04:00","2024/01/02 04:00","2024.01.02 04:00","01-02 04:00","01/02 04:00"};
    for(int i=0;i<APP_STATUS_DATE_FORMAT_COUNT;++i){
        app_settings_set_status_date_format((app_status_date_format_t)i);
        app_settings_format_status_clock(1704139200,clock,sizeof(clock));assert(!strcmp(clock,expected[i]));
    }
    app_settings_set_status_show_date(false);app_settings_format_status_clock(1704139200,clock,sizeof(clock));assert(!strcmp(clock,"04:00"));
    assert(app_settings_status_date_format()==APP_STATUS_DATE_MONTH_DAY_SLASH);
    app_settings_format_status_clock(0,clock,sizeof(clock));assert(!strcmp(clock,"--:--"));
    s_status_date=0;commit_count=0;

    assert(app_settings_book_line_spacing() == 130); /* New installations start at the middle slider stop. */
    (void)mkdir(APP_SETTINGS_BACKUP_ROOT, 0700);
    (void)remove(BACKUP_FILE);
    (void)remove(BACKUP_PREVIOUS);
    (void)remove(BACKUP_TEMP);
    (void)remove(RESTORE_BEFORE);
    (void)remove(RESTORE_TEMP);
    (void)remove(RESTORE_PREVIOUS);
    (void)remove(RESTORE_DONE);
    card_mounted = false;
    s_status_date=0x80|APP_STATUS_DATE_DOT;s_idle_sleep_minutes=30;
    assert(app_settings_backup_save() == ESP_ERR_INVALID_STATE);
    card_mounted = true;
    s_system_size = 200;
    s_book_px = 62;
    s_book_tracking = 4;
    s_book_indent = 3;
    s_book_rule_offset = 7; /* +6 px */
    s_reader_full_pages = 5;
    s_reader_turn_effect = 1;
    s_reader_power_turn = true;
    s_reader_immersive = true;
    s_reader_fullscreen=true;s_book_para=37;
    s_staged_shutdown = true;
    s_home_full_refresh = true;
    s_reader_hold_refresh = true;
    app_settings_set_reader_vertical_turn(true);
    assert(app_settings_reader_vertical_turn());
    strlcpy(s_device_name, "Kiiko Pico", sizeof(s_device_name));
    strlcpy(s_status_signature, "今天也要读书", sizeof(s_status_signature));
    strlcpy(s_avatar, "/sdcard/pictures/missing-avatar.jpg", sizeof(s_avatar));
    strlcpy(s_font, "/sdcard/fonts/missing.ttf", sizeof(s_font));
    s_lock_style = 1;
    strlcpy(s_wallpaper, "/sdcard/pictures/missing.jpg", sizeof(s_wallpaper));
    assert(app_settings_backup_save() == ESP_OK);
    assert(history_saves == 1);
    // 备份保留旧外观槽以兼容格式，但旧样式不再控制陈列。/ Retain the legacy slot without restoring style behavior.
    FILE *legacy_check=fopen(BACKUP_FILE,"rb");settings_backup_v1_t legacy_style;
    assert(legacy_check&&fread(&legacy_style,1,sizeof(legacy_style),legacy_check)==sizeof(legacy_style)&&fclose(legacy_check)==0);
    assert(legacy_style.flags[BK_SHELF]==2);
    for(int style=1;style<=4;++style){legacy_style.flags[BK_SHELF]=style;backup_seal(&legacy_style);assert(backup_valid(&legacy_style));}

    FILE *saved = fopen(BACKUP_FILE, "rb");
    settings_backup_v1_t retired_header;uint8_t retired_extension[7];
    assert(saved && fread(&retired_header,1,sizeof(retired_header),saved)==sizeof(retired_header));
    assert(fread(retired_extension,1,sizeof(retired_extension),saved)==sizeof(retired_extension));
    settings_backup_profile_t saved_profile;
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    settings_backup_wifi_t saved_network;
    assert(fread(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    settings_backup_cloud_t saved_connection;
    assert(fread(&saved_connection, 1, sizeof(saved_connection), saved) == sizeof(saved_connection));
    assert(backup_cloud_valid(&saved_connection));
    assert(!strcmp(saved_connection.credentials.account,"backup-test@example.invalid") &&
           !strcmp(saved_connection.credentials.password,"test-only-secret") &&
           !strcmp(saved_connection.credentials.folder,"PicoBox"));
    assert(fclose(saved) == 0);
    assert(!(saved_profile.home_full_refresh&8));
    // 模拟旧固件启用蓝牙的备份；退休位须能校验并恢复其余字段。/ Accept a legacy enabled-Bluetooth backup while restoring other fields and ignoring its retired bit.
    saved_profile.home_full_refresh|=8;
    uint32_t retired_hash=backup_profile_checksum(&retired_header,retired_extension[0],retired_extension[1],retired_extension[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(retired_hash>>(i*8));
    FILE *old_file=fopen(BACKUP_FILE,"r+b");assert(old_file&&fseek(old_file,sizeof(retired_header)+sizeof(retired_extension),SEEK_SET)==0);
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),old_file)==sizeof(saved_profile)&&fclose(old_file)==0);
    assert(!strcmp(saved_profile.device_name, "Kiiko Pico") &&
           !strcmp(saved_profile.status_signature, "今天也要读书") &&
           !strcmp(saved_profile.avatar, "/sdcard/pictures/missing-avatar.jpg"));
    assert(backup_wifi_valid(&saved_network) && saved_network.credentials.configured &&
           !strcmp(saved_network.credentials.ssid, "Home_2.4G") &&
           !strcmp(saved_network.credentials.password, "password123"));
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t) +
                            offsetof(settings_backup_wifi_t, credentials.password), SEEK_SET) == 0);
    assert(fputc('X', saved) != EOF && fclose(saved) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(!saved_wifi.configured && wifi_imports == 0);
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t), SEEK_SET) == 0);
    assert(fwrite(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    saved = fopen(BACKUP_FILE, "r+b");
    long cloud_offset=sizeof(settings_backup_v1_t)+7+sizeof(settings_backup_profile_t)+sizeof(settings_backup_wifi_t);
    assert(saved && fseek(saved,cloud_offset+offsetof(settings_backup_cloud_t,credentials.password),SEEK_SET)==0);
    assert(fputc('X',saved)!=EOF&&fclose(saved)==0);
    assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE && !cloud_restores && !wifi_imports);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,cloud_offset,SEEK_SET)==0);
    assert(fwrite(&saved_connection,1,sizeof(saved_connection),saved)==sizeof(saved_connection)&&fclose(saved)==0);
    cloud_export_error=ESP_FAIL;
    assert(app_settings_backup_save()==ESP_FAIL);
    cloud_export_error=ESP_ERR_INVALID_STATE;
    assert(app_settings_backup_restore()==ESP_ERR_INVALID_STATE&&!cloud_restores&&!wifi_imports);
    cloud_export_error=ESP_OK;
    strcpy(saved_cloud.folder,"Changed");strcpy(saved_cloud.password,"changed-test-only");
    s_system_size = 120;
    s_book_px = 48;
    s_book_tracking = 2;
    s_book_indent = 0;
    s_book_rule_offset = 4;
    s_reader_full_pages = 15;
    s_reader_turn_effect = 0;
    s_reader_power_turn = false;
    s_reader_immersive = false;
    s_staged_shutdown = false;
    s_home_full_refresh = false;
    s_reader_fullscreen=false;s_book_para=0;
    s_reader_hold_refresh = false;
    app_settings_set_reader_vertical_turn(false);
    strlcpy(s_device_name, "Pico", sizeof(s_device_name));
    s_status_signature[0] = 0;
    s_avatar[0] = 0;
    s_lock_style = 0;
    s_wallpaper[0] = 0;
    s_status_date=APP_STATUS_DATE_ISO;s_idle_sleep_minutes=0;
    assert(app_settings_backup_restore() == ESP_OK);
    assert(history_restores == 1);
    assert(!app_settings_status_show_date()&&app_settings_status_date_format()==APP_STATUS_DATE_DOT);
    assert(app_settings_idle_sleep_minutes()==30);
    assert(cloud_restores==1&&!strcmp(saved_cloud.folder,"PicoBox")&&!strcmp(saved_cloud.password,"test-only-secret"));
    assert(app_settings_system_font_size() == 160);
    assert(wifi_imports == 1 && saved_wifi.configured &&
           !strcmp(saved_wifi.ssid, "Home_2.4G") &&
           !strcmp(saved_wifi.password, "password123"));
    assert(s_book_px == 62 && s_book_tracking == 4 && s_book_indent == 3 &&
           s_book_rule_offset == 7 && s_reader_full_pages == 5);
    assert(s_reader_turn_effect == 1 && s_reader_power_turn && s_reader_immersive);
    assert(s_staged_shutdown);
    assert(s_reader_fullscreen&&s_book_para==37);
    assert(s_reader_hold_refresh);
    assert(app_settings_reader_vertical_turn());
    assert(s_home_full_refresh && !strcmp(s_device_name, "Kiiko Pico") &&
           !strcmp(s_status_signature, "今天也要读书"));
    assert(!s_avatar[0]); /* Missing avatar falls back to the default mark. */
    assert(!s_font[0]); /* Missing external font falls back to built-in. */
    assert(s_lock_style == 1 && !s_wallpaper[0]); /* Missing wallpaper uses bundled wallpaper. */
    assert(commit_count == 4); /* Two mode changes, settings restore and reader preferences. */
    app_settings_set_reader_full_pages(30);
    assert(s_reader_full_pages == 30);
    app_settings_set_reader_full_pages(0);
    assert(s_reader_full_pages == 0);
    app_settings_set_book_reading_line_offset(-8);
    assert(app_settings_book_reading_line_offset() == -8);
    app_settings_set_book_reading_line_offset(7); /* Reject odd-pixel shifts. */
    assert(app_settings_book_reading_line_offset() == -8);

    s_book_px = 70;
    assert(app_settings_backup_save() == ESP_OK); /* Overwrite an existing backup. */
    s_book_px = 48;
    commit_fails = true;
    strlcpy(saved_wifi.ssid, "Other_2.4G", sizeof(saved_wifi.ssid));
    assert(app_settings_backup_restore() == ESP_FAIL);
    assert(s_book_px == 48); /* RAM state is unchanged on commit failure. */
    assert(!strcmp(saved_wifi.ssid, "Other_2.4G"));
    assert(access(RESTORE_BEFORE,F_OK)==0);
    assert(app_settings_backup_save()==ESP_ERR_INVALID_STATE);
    commit_fails = false;
    assert(app_settings_backup_restore() == ESP_OK && s_book_px == 70 &&
           s_reader_full_pages == 0 && app_settings_book_reading_line_offset() == -8 &&
           s_staged_shutdown);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G"));
    assert(access(RESTORE_BEFORE,F_OK)!=0);

    // 新格式删除坚果云扩展后构造上游v7，旧备份不会覆盖连接配置。/ Construct upstream v7 without the cloud block; preserve current credentials.
    settings_backup_v1_t v7;
    FILE *old=fopen(BACKUP_FILE,"rb");assert(old);
    assert(fread(&v7,1,sizeof(v7),old)==sizeof(v7)&&fclose(old)==0);
    memcpy(v7.magic,"PICOSET7",8);backup_seal(&v7);
    uint8_t ext7[7]={s_book_indent,s_book_rule_offset,s_staged_shutdown?1:0};
    uint32_t eh=backup_shutdown_checksum(&v7,ext7[0],ext7[1],ext7[2]);
    for(int i=0;i<4;++i)ext7[3+i]=(uint8_t)(eh>>(i*8));
    uint32_t ph=backup_profile_checksum(&v7,ext7[0],ext7[1],ext7[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(ph>>(i*8));
    old=fopen(BACKUP_FILE,"wb");assert(old);
    assert(fwrite(&v7,1,sizeof(v7),old)==sizeof(v7));assert(fwrite(ext7,1,sizeof(ext7),old)==sizeof(ext7));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),old)==sizeof(saved_profile));
    assert(fwrite(&saved_network,1,sizeof(saved_network),old)==sizeof(saved_network));
    assert(book_history_backup_write(old)==ESP_OK&&fclose(old)==0);
    unsigned prior_cloud_restores=cloud_restores;strcpy(saved_cloud.folder,"KeepThis");
    assert(app_settings_backup_restore()==ESP_OK&&cloud_restores==prior_cloud_restores&&!strcmp(saved_cloud.folder,"KeepThis"));

    /* PICOSET3 backups predate the shutdown choice and restore the safe off default. */
    FILE *file = fopen(BACKUP_FILE, "rb");
    assert(file);
    settings_backup_v1_t legacy;
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET3", sizeof(legacy.magic));
    backup_seal(&legacy);
    uint8_t v3_extension[6] = {3, 4};
    uint32_t v3_hash = backup_rule_offset_checksum(&legacy, v3_extension[0], v3_extension[1]);
    for (int i = 0; i < 4; ++i) v3_extension[i + 2] = (uint8_t)(v3_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(v3_extension, 1, sizeof(v3_extension), file) == sizeof(v3_extension));
    assert(fclose(file) == 0);
    assert(app_settings_backup_restore() == ESP_OK && !s_staged_shutdown);
    assert(!s_home_full_refresh && !strcmp(s_device_name, "Pico") && !s_status_signature[0]);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G")); /* Old backups leave network alone. */

    /* Existing PICOSET2 backups remain readable, with centered guide lines. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET2", sizeof(legacy.magic));
    backup_seal(&legacy);
    uint8_t old_extension[5] = {3};
    uint32_t old_hash = backup_indent_checksum(&legacy, old_extension[0]);
    for (int i = 0; i < 4; ++i) old_extension[i + 1] = (uint8_t)(old_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(old_extension, 1, sizeof(old_extension), file) == sizeof(old_extension));
    assert(fclose(file) == 0);
    s_book_rule_offset = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_rule_offset == 4 && !s_reader_hold_refresh);
    assert(!app_settings_reader_vertical_turn());

    /* Existing PICOSET1 backups remain readable and use the two-em default. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET1", sizeof(legacy.magic));
    backup_seal(&legacy);
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    s_book_indent = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_indent == 2);
    /* A backup without a configured network clears an existing saved network. */
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    memset(&saved_cloud,0,sizeof(saved_cloud));strcpy(saved_cloud.folder,"PicoBox");
    assert(app_settings_backup_save() == ESP_OK);
    saved_wifi.configured = 1;
    strlcpy(saved_wifi.ssid, "Temporary", sizeof(saved_wifi.ssid));
    strlcpy(saved_wifi.password, "temporary123", sizeof(saved_wifi.password));
    strcpy(saved_cloud.account,"temporary@example.invalid");strcpy(saved_cloud.password,"temporary-test-secret");
    assert(app_settings_backup_restore() == ESP_OK && !saved_wifi.configured);
    assert(!saved_cloud.account[0]&&!saved_cloud.password[0]&&!strcmp(saved_cloud.folder,"PicoBox"));

    file = fopen(BACKUP_FILE, "r+b");
    assert(file);
    assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
    s_book_px = 48;
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(s_book_px == 48);
    assert(remove(BACKUP_FILE) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_NOT_FOUND);
    for (int percent = 100; percent <= 200; percent += 10) {
        app_settings_set_system_font_size((uint8_t)percent);
        assert(app_settings_system_font_size() == system_size_preset((uint8_t)percent));
    }
    app_settings_set_system_font_size(210);
    app_settings_set_system_font_size(195);
    assert(app_settings_system_font_size() == 160);
    test_loaded_system_size = 180; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 160);
    test_loaded_system_size = 200; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 160);
    // 普通写入失败应恢复整组旧资料，不覆盖用户备份。/ Ordinary write failures restore all old state without replacing the user backup.
    s_book_px=70;history_value=70;strcpy(saved_wifi.ssid,"BackupWifi");saved_wifi.configured=1;strcpy(saved_wifi.password,"test-pass");
    strcpy(saved_cloud.folder,"BackupCloud");assert(app_settings_backup_save()==ESP_OK);
    s_book_px=48;history_value=48;strcpy(saved_wifi.ssid,"CurrentWifi");strcpy(saved_cloud.folder,"CurrentCloud");
    history_fail_once=true;assert(app_settings_backup_restore()==ESP_FAIL);
    assert(persisted_book_px==48&&s_book_px==48&&history_value==48&&!strcmp(saved_wifi.ssid,"CurrentWifi")&&!strcmp(saved_cloud.folder,"CurrentCloud"));
    assert(access(RESTORE_BEFORE,F_OK)!=0);
    cloud_fail_once=true;assert(app_settings_backup_restore()==ESP_FAIL);
    assert(persisted_book_px==48&&s_book_px==48&&history_value==48&&!strcmp(saved_wifi.ssid,"CurrentWifi")&&!strcmp(saved_cloud.folder,"CurrentCloud"));
    assert(access(RESTORE_BEFORE,F_OK)!=0);
    assert(app_settings_backup_restore()==ESP_OK&&persisted_book_px==70&&s_book_px==70&&history_value==70&&!strcmp(saved_wifi.ssid,"BackupWifi")&&!strcmp(saved_cloud.folder,"BackupCloud"));
    assert(access(RESTORE_BEFORE,F_OK)!=0);
    // 连续失败保留旧状态文件；故障消失后由恢复入口先回退再重试。
    // Repeated failures retain old state; the restore entry recovers it before retrying when writes work again.
    s_book_px=48;history_value=48;strcpy(saved_wifi.ssid,"RetryWifi");strcpy(saved_cloud.folder,"RetryCloud");
    history_failures=2;assert(app_settings_backup_restore()==ESP_FAIL&&access(RESTORE_BEFORE,F_OK)==0);
    assert(app_settings_backup_save()==ESP_ERR_INVALID_STATE);
    assert(app_settings_backup_restore()==ESP_OK&&persisted_book_px==70&&history_value==70&&
           !strcmp(saved_wifi.ssid,"BackupWifi")&&!strcmp(saved_cloud.folder,"BackupCloud"));
    assert(access(RESTORE_BEFORE,F_OK)!=0);
    assert(reader_input_assign(READER_SWIPE_RIGHT,READER_ACTION_TOC)==ESP_OK);
    assert(reader_input_set_tap_layout(READER_TAP_ROWS)==ESP_OK);
    app_settings_set_book_margin(8);
    assert(app_settings_backup_save()==ESP_OK);
    assert(reader_input_assign(READER_SWIPE_RIGHT,READER_ACTION_BOOKMARKS)==ESP_OK);
    assert(reader_input_set_tap_layout(READER_TAP_COLUMNS)==ESP_OK);
    app_settings_set_book_margin(60);
    assert(app_settings_backup_restore()==ESP_OK);
    assert(reader_input_action(READER_SWIPE_RIGHT)==READER_ACTION_TOC&&app_settings_book_margin()==8);
    assert(reader_input_tap_layout()==READER_TAP_ROWS);
    assert(app_settings_set_wallpaper_directory("/sdcard/wallpapers")==ESP_OK);
    assert(app_settings_wallpaper_rotating()&&!strcmp(app_settings_wallpaper_path(),"/sdcard/wallpapers/"));
    assert(app_settings_set_wallpaper_directory("/sdcard/../outside")==ESP_ERR_INVALID_ARG);
    assert(app_settings_backup_save()==ESP_OK);
    FILE *wallpaper_backup=fopen(BACKUP_FILE,"rb");settings_backup_v1_t wallpaper_record;
    assert(wallpaper_backup&&fread(&wallpaper_record,1,sizeof(wallpaper_record),wallpaper_backup)==sizeof(wallpaper_record));fclose(wallpaper_backup);
    assert(backup_valid(&wallpaper_record)&&!strcmp(wallpaper_record.wallpaper,"/sdcard/wallpapers/"));
    assert(app_settings_backup_restore()==ESP_OK&&!app_settings_wallpaper_rotating()); // 缺失目录回退默认。/ Missing directories use the default.
    puts("settings backup host test passed (including numeric status dates, display preferences, four size presets and legacy restore)");
    return 0;
}
