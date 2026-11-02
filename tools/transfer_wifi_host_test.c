/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 配网解析与连接时限的真实策略回归。/ Regression of actual provisioning parsing and connection deadlines.
 */
#include <assert.h>
#include <stdio.h>
#include "../components/read_pico_transfer/transfer_policy.h"

static int64_t body_time;
static int body_reads, body_cancel_at, body_step = 4000, body_failure;
static bool body_stopped(void *ctx) { (void)ctx; return body_cancel_at && body_reads >= body_cancel_at; }
static int64_t body_now(void *ctx) { (void)ctx; return body_time; }
static int body_receive(void *ctx, char *out, size_t cap) {
    (void)ctx; assert(cap); *out = 'a'; ++body_reads; body_time += body_step;
    return body_failure ? body_failure : 1;
}
static void test_body_deadline(void) {
    char body[257] = {0};
    body_time = body_reads = 0; body_cancel_at = 1;
    assert(!receive_credentials_body(body, 1, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 1);
    body_time = body_reads = body_cancel_at = 0;
    assert(!receive_credentials_body(body, 256, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 4);
    body_time = body_reads = 0;
    assert(receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 2);
    body_time = body_reads = 0; body_cancel_at = -1;
    assert(!receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    assert(body_reads == 0);
    body_cancel_at = 0; body_step = 15000;
    assert(!receive_credentials_body(body, 1, body_receive, body_stopped, body_now, NULL));
    body_time = body_reads = 0; body_step = 1; body_failure = -1;
    assert(!receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    body_time = body_reads = 0; body_failure = 3;
    assert(!receive_credentials_body(body, 2, body_receive, body_stopped, body_now, NULL));
    body_failure = 0;
}

// 故障注入只替换NVS边界；生产读写函数原样执行。/ Inject faults only at NVS boundaries; run production storage helpers unchanged.
#define READ_PICO_TRANSFER_NVS_TEST
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
enum { ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_FAIL };
enum { NVS_READONLY, NVS_READWRITE };
static transfer_wifi_profiles_t committed, staged;
static transfer_credentials_t legacy;
static bool legacy_present;
static bool present, pending_present;
static int fail_open, fail_write, fail_commit, handles,fail_read;
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "rp_wifi")); (void)mode;
    if (fail_open) return ESP_FAIL;
    ++handles; *handle = 1; staged = committed; pending_present = present; return ESP_OK;
}
static esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    assert(handle == 1);
    if(fail_read)return ESP_FAIL;
    const void *record; size_t bytes;
    if (!strcmp(key, "profiles")) {
        if (!present) return ESP_ERR_NVS_NOT_FOUND;
        record = &committed; bytes = sizeof(committed);
    } else {
        assert(!strcmp(key, "credentials"));
        if (!legacy_present) return ESP_ERR_NVS_NOT_FOUND;
        record = &legacy; bytes = sizeof(legacy);
    }
    assert(*size >= bytes); *size = bytes; memcpy(out, record, bytes); return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *in, size_t size) {
    assert(handle == 1 && !strcmp(key, "profiles") && size == sizeof(staged));
    if (fail_write) return ESP_FAIL;
    memcpy(&staged, in, size); pending_present = true; return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1);
    if (fail_commit) return ESP_FAIL;
    committed = staged; present = pending_present; return ESP_OK;
}
static void nvs_close(nvs_handle_t handle) { assert(handle == 1); --handles; clear_secret(&staged, sizeof(staged)); }
#include "../components/read_pico_transfer/transfer_credentials_store.h"

static void test_saved_networks(void) {
    present = false; legacy_present = true;
    assert(credentials_from_text("legacy-network", "12345678", &legacy));
    transfer_wifi_profiles_t profiles;
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 1);
    assert(!strcmp(profiles.entries[0].ssid, "legacy-network"));
    transfer_credentials_t next;
    for (int i = 0; i < 10; ++i) {
        char name[33]; snprintf(name, sizeof(name), "saved-%d", i);
        assert(credentials_from_text(name, "12345678", &next));
        assert(store_credentials(&next) == ESP_OK && !handles);
    }
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == TRANSFER_WIFI_PROFILE_MAX);
    assert(profiles_find(&profiles, "saved-0") < 0);
    assert(profiles_find(&profiles, "saved-2") == 7);
    // 选择旧网络保留密码并移到首位，不影响其他网络。/ Selecting an old network retains its password and promotes it without losing peers.
    next = profiles.entries[profiles_find(&profiles, "saved-3")];
    assert(store_credentials(&next) == ESP_OK);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 8);
    assert(!strcmp(profiles.entries[0].ssid, "saved-3") && !strcmp(profiles.entries[0].password, "12345678"));
    assert(credentials_from_text("saved-3", "changed-pass", &next));
    assert(store_credentials(&next) == ESP_OK);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 8);
    assert(!strcmp(profiles.entries[0].password, "changed-pass"));
    profiles_remove(&profiles, "saved-3");
    assert(store_profiles(&profiles) == ESP_OK);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 7 && profiles_find(&profiles, "saved-3") < 0);
    assert(profiles_find(&profiles, "saved-9") >= 0);
    // 重复记录和脏尾部不能成为有效凭据；空新记录阻止旧凭据复活。/ Reject duplicates and dirty tails; a new empty record prevents legacy resurrection.
    profiles.entries[1] = profiles.entries[0];
    assert(store_profiles(&profiles) == ESP_ERR_INVALID_ARG);
    assert(load_profiles(&profiles) == ESP_OK);
    profiles.entries[7].version = 1;
    assert(store_profiles(&profiles) == ESP_ERR_INVALID_ARG);
    assert(store_credentials(NULL) == ESP_OK);
    assert(load_profiles(&profiles) == ESP_OK && profiles.count == 0 && legacy_present);
    committed.version = 2;
    assert(load_profiles(&profiles) == ESP_ERR_INVALID_STATE);
    assert(store_credentials(&next) == ESP_ERR_INVALID_STATE);
    assert(store_credentials(NULL) == ESP_OK);
    // 写后读失败只报告未确认；同值恢复读回后可重试。/ A failed post-write read is unconfirmed; retry the same value once reads recover.
    assert(load_profiles(&profiles)==ESP_OK);fail_read=1;assert(store_profiles(&profiles)==ESP_FAIL&&!handles);fail_read=0;assert(store_profiles(&profiles)==ESP_OK&&!handles);
    legacy_present = false;
    clear_secret(&legacy, sizeof(legacy));
    clear_secret(&next, sizeof(next));
    clear_secret(&profiles, sizeof(profiles));
}

static bool parse(const char *json, transfer_credentials_t *c) { return parse_credentials(json, strlen(json), c); }
int main(void) {
    test_body_deadline();
    test_saved_networks();
    bool stopping = false, uploading = false;
    assert(admission_begin(stopping, &uploading));
    assert(!admission_stop(&stopping, uploading) && !stopping);
    uploading = false;
    assert(admission_stop(&stopping, uploading) && stopping);
    assert(!admission_begin(stopping, &uploading) && !uploading);
    stopping = false;
    assert(admission_stop(&stopping, uploading));
    assert(!admission_begin(stopping, &uploading));
    transfer_credentials_t c;
    assert(credentials_from_text("device", "12345678", &c));
    assert(!credentials_from_text("device", "short", &c) && c.version == 0);
    assert(!credentials_from_text("", "12345678", &c));
    assert(!credentials_from_text("123456789012345678901234567890123", "12345678", &c));
    read_pico_transfer_network_t networks[READ_PICO_TRANSFER_SCAN_MAX] = {0};
    size_t count = 0;
    read_pico_transfer_network_t ap = {.ssid = "same", .rssi = -70, .requires_password = true, .supported = true};
    scan_offer(networks, &count, &ap); ap.rssi = -40; scan_offer(networks, &count, &ap);
    assert(count == 1 && networks[0].rssi == -40);
    ap.ssid[0] = 0; scan_offer(networks, &count, &ap); assert(count == 1);
    for (int i = 0; i < 25; ++i) {
        snprintf(ap.ssid, sizeof(ap.ssid), "network-%02d", i); ap.rssi = (int8_t)(-100 + i);
        scan_offer(networks, &count, &ap);
    }
    assert(count == READ_PICO_TRANSFER_SCAN_MAX && networks[0].rssi == -40);
    for (size_t i = 1; i < count; ++i) assert(networks[i - 1].rssi >= networks[i].rssi);
    strcpy(ap.ssid, "network-24"); ap.rssi = -20; ap.requires_password = false;
    scan_offer(networks, &count, &ap);
    assert(count == READ_PICO_TRANSFER_SCAN_MAX && networks[0].rssi == -20 && !networks[0].requires_password);
    assert(parse("{\"ssid\":\"家庭 WiFi\",\"password\":\"12345678\"}", &c));
    assert(!strcmp(c.ssid, "家庭 WiFi"));
    assert(parse("{\"ssid\":\"open\",\"password\":\"\"}", &c));
    assert(store_credentials(&c) == ESP_OK && present && !handles);
    transfer_credentials_t old;
    assert(load_credentials(&old) == ESP_OK && !strcmp(old.ssid, "open"));
    strcpy(c.ssid, "replacement"); fail_write = 1;
    assert(store_credentials(&c) == ESP_FAIL && !handles);
    assert(load_credentials(&old) == ESP_OK && !strcmp(old.ssid, "open"));
    fail_write = 0; fail_commit = 1;
    assert(store_credentials(&c) == ESP_FAIL && !handles);
    assert(load_credentials(&old) == ESP_OK && !strcmp(old.ssid, "open"));
    assert(store_credentials(NULL) == ESP_FAIL && present && !handles);
    fail_commit = 0; fail_open = 1;
    assert(store_credentials(&c) == ESP_FAIL && !handles);
    assert(load_credentials(&old) == ESP_FAIL && old.version == 0);
    fail_open = 0;
    assert(store_credentials(NULL) == ESP_OK && present && committed.count == 0 && !handles);
    assert(load_credentials(&old) == ESP_OK && old.version == 0);
    assert(store_credentials(NULL) == ESP_OK && !handles);
    const char *bad[] = {"{}", "[]", "null", "{\"ssid\":\"\",\"password\":\"12345678\"}",
        "{\"ssid\":\"home\",\"password\":12345678}", "{\"ssid\":\"home\",\"password\":\"short\"}",
        "{\"ssid\":\"home\",\"password\":\"12345678\"}x", "{\"ssid\":\"home\\u0000x\",\"password\":\"12345678\"}",
        "{\"ssid\":\"home\",\"ssid\":\"evil\",\"password\":\"12345678\"}",
        "{\"ssid\":\"home\",\"password\":\"12345678\",\"unknown\":1}"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) assert(!parse(bad[i], &c));
    assert(!parse("{\"ssid\":[],\"password\":\"12345678\"}", &c));
    assert(parse("{\"ssid\":\"home[{}]\",\"password\":\"12345678\"}", &c));
    const char embedded[] = "{\"ssid\":\"home\",\"password\":\"12345678\"}\0junk";
    assert(!parse_credentials(embedded, sizeof(embedded) - 1, &c));
    memset(&c, 0, sizeof(c)); c.version = 1; memset(c.ssid, 'a', 32); memset(c.password, 'a', 64);
    assert(credentials_valid(&c)); c.password[0] = 'g'; assert(!credentials_valid(&c));
    c.password[0] = 'a'; c.ssid[32] = 'b'; assert(!credentials_valid(&c));
    clear_secret(&c, sizeof(c)); for (size_t i = 0; i < sizeof(c); ++i) assert(((unsigned char *)&c)[i] == 0);
    transfer_connection_t net;
    connection_begin(&net, 100);
    assert(connection_poll(&net, 100) == 1 && net.attempts == 1);
    assert(connection_poll(&net, 29999) == 0);
    assert(connection_poll(&net, 30100) == -1 && net.failed);
    assert(connection_poll(&net, 40000) == 0);
    connection_begin(&net, 0);
    for (int i = 0; i < 5; ++i) {
        assert(connection_poll(&net, i * 1000) == 1);
        connection_lost(&net, i * 1000);
    }
    assert(connection_poll(&net, 5000) == -1);
    connection_begin(&net, 0); net.online = true;
    assert(connection_poll(&net, 100000) == 0);
    connection_lost(&net, 100000);
    assert(connection_poll(&net, 100999) == 0);
    assert(connection_poll(&net, 101000) == 1 && net.attempts == 1);
    assert(time_route(true, true, true, false) == TRANSFER_TIME_ONLINE);
    assert(time_route(true, true, false, false) == TRANSFER_TIME_UNAVAILABLE);
    assert(time_route(true, false, true, false) == TRANSFER_TIME_UNAVAILABLE);
    assert(time_route(false, true, false, true) == TRANSFER_TIME_TEMPORARY);
    assert(time_route(false, true, false, false) == TRANSFER_TIME_UNAVAILABLE);
    puts("transfer WiFi policy tests passed");
}
