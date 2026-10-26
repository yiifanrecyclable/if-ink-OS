/* SPDX-License-Identifier: Apache-2.0
 * 中文：固件坚果云HTTPS只读客户端，后台串行处理网络/存储，UI线程管理WiFi生命周期。
 * 用户修订：直属阅读文件、图片和字体按类型下载到标准目录，不读取旧清单或编号目录。
 * User revision: route direct reading files, images and fonts by type; ignore legacy manifests/directories.
 * English: Firmware read-only HTTPS client;
 one worker owns requests/storage and the UI owner controls WiFi lifecycle.
 */
#include "ui/ui_i18n.h"

#include "cloud_sync.h"
#include "book_store.h"
#include "cloud_dav.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "time_service.h"
#include "settings.h"
#include "sync_file_policy.h"
#include "content_library.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CLOUD_CACHE "/sdcard/.pico-cloud"
#define CLOUD_CONNECT_TIMEOUT_MS 15000
#define CLOUD_READ_TIMEOUT_MS 5000
static SemaphoreHandle_t s_lock, s_finished;
static TaskHandle_t s_task;
static cloud_entry_t *s_entries;
static cloud_status_t s_status;
static atomic_bool s_cancel;
static bool s_owned_network, s_session_fatal, s_notice_error;
static cloud_config_t s_config;
static char s_base_url[1024], s_folder_path[512], s_scope[65];
static unsigned s_request_count;
static time_t s_budget_start;
static const char *propfind = "<?xml version=\"1.0\"?><d:propfind "
                              "xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/><d:getetag/"
                              "><d:creationdate/><d:getlastmodified/></d:prop></d:propfind>";
static void clear_secret(void *memory, size_t n) {
    volatile unsigned char *p = memory;
    while (n--)
        *p++ = 0;
}
static bool init(void) {
    if (!s_lock)
        s_lock = xSemaphoreCreateMutex();
    if (!s_finished)
        s_finished = xSemaphoreCreateBinary();
    if (!s_entries)
        s_entries =
            heap_caps_calloc(CLOUD_ENTRY_MAX, sizeof(*s_entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_lock && s_finished && s_entries;
}
bool cloud_config_valid(const cloud_config_t *c, bool secret) {
    if (!c || !memchr(c->folder, 0, sizeof(c->folder)) || !memchr(c->account, 0, sizeof(c->account)) ||
        !memchr(c->password, 0, sizeof(c->password)) || !cloud_relative_path(c->folder))
        return false;
    if ((secret && !c->account[0]) || strpbrk(c->account, ":\r\n") ||
        !cloud_utf8_valid(c->account, strlen(c->account)))
        return false;
    return (!secret || c->password[0]) && !strpbrk(c->password, "\r\n");
}
void cloud_config_load(cloud_config_t *c) {
    memset(c, 0, sizeof(*c));
    strcpy(c->folder, CLOUD_DEFAULT_FOLDER);
    nvs_handle_t h;
    if (nvs_open("rp_cloud", NVS_READONLY, &h) != ESP_OK)
        return;
    size_t n = sizeof(c->folder);
    if (nvs_get_str(h, "folder", c->folder, &n) != ESP_OK)
        strcpy(c->folder, CLOUD_DEFAULT_FOLDER);
    n = sizeof(c->account);
    if (nvs_get_str(h, "account", c->account, &n) != ESP_OK)
        c->account[0] = 0;
    n = sizeof(c->password);
    if (nvs_get_str(h, "password", c->password, &n) != ESP_OK)
        c->password[0] = 0;
    nvs_close(h);
}
esp_err_t cloud_config_save(const cloud_config_t *c) {
    if (s_task || !cloud_config_valid(c, false))
        return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t e = nvs_open("rp_cloud", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_str(h, "folder", c->folder);
    if (e == ESP_OK)
        e = nvs_set_str(h, "account", c->account);
    if (e == ESP_OK)
        e = nvs_set_str(h, "password", c->password);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
esp_err_t cloud_config_forget(void) {
    if (s_task)
        return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t e = nvs_open("rp_cloud", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_erase_all(h);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
esp_err_t cloud_config_backup_export(cloud_config_t *c) {
    if (!c) return ESP_ERR_INVALID_ARG;
    memset(c, 0, sizeof(*c));
    strcpy(c->folder, CLOUD_DEFAULT_FOLDER);
    if (s_task) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t e = nvs_open("rp_cloud", NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (e != ESP_OK) return e;
    char *values[] = {c->folder, c->account, c->password};
    size_t caps[] = {sizeof(c->folder), sizeof(c->account), sizeof(c->password)};
    const char *keys[] = {"folder", "account", "password"};
    for (size_t i = 0; i < 3 && e == ESP_OK; ++i) {
        size_t n = caps[i];
        e = nvs_get_str(h, keys[i], values[i], &n);
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    }
    nvs_close(h);
    if (e == ESP_OK && !cloud_config_valid(c, false)) e = ESP_ERR_INVALID_RESPONSE;
    if (e != ESP_OK) clear_secret(c, sizeof(*c));
    return e;
}
esp_err_t cloud_config_backup_restore(const cloud_config_t *c) {
    return cloud_config_save(c);
}
static void notice(const char *value, const char *indicator) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ui_snprintf(s_status.message, sizeof(s_status.message), "%s", value);
    if (indicator)
        ui_snprintf(s_status.indicator, sizeof(s_status.indicator), "%s", indicator);
    ++s_status.revision;
    xSemaphoreGive(s_lock);
}
static void message(const char *value) {
    notice(value, NULL);
}
static void fail_notice(const char *value, const char *indicator) {
    s_notice_error = true;
    notice(value, indicator);
}
void cloud_get_status(cloud_status_t *out) {
    if (!init()) {
        memset(out, 0, sizeof(*out));
        strcpy(out->message, ui_message(UI_MSG("同步内存不足")));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    out->cancel_requested = atomic_load(&s_cancel);
    xSemaphoreGive(s_lock);
}
bool cloud_get_entry(size_t i, cloud_entry_t *out) {
    if (!init())
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool found = i < s_status.count;
    if (found)
        *out = s_entries[i];
    xSemaphoreGive(s_lock);
    return found;
}
esp_err_t cloud_toggle(size_t i) {
    if (!init())
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t e = ESP_ERR_INVALID_STATE;
    if (!s_status.busy && i < s_status.count && s_entries[i].selectable) {
        s_entries[i].selected = !s_entries[i].selected;
        ++s_status.revision;
        e = ESP_OK;
    }
    xSemaphoreGive(s_lock);
    return e;
}
static bool cancelled(void) {
    return atomic_load(&s_cancel);
}
static bool strong_etag(const char *value) {
    size_t n = strlen(value);
    return n > 2 && value[0] == '"' && value[n - 1] == '"' && !strpbrk(value, "\r\n");
}
static esp_err_t budget(void) {
    time_t now = time(NULL);
    if (!s_budget_start || now - s_budget_start >= 1800) {
        s_budget_start = now;
        s_request_count = 0;
    }
    if (s_request_count >= 450) {
        s_session_fatal = true;
        fail_notice(ui_message(UI_MSG("本轮访问预算已用完，请稍后再试")), ui_message(UI_MSG("访问受限，稍后重试")));
        return ESP_ERR_INVALID_STATE;
    }
    ++s_request_count;
    FILE *f = fopen(CLOUD_CACHE "/budget", "wb");
    if (!f)
        return ESP_FAIL;
    bool ok = fprintf(f, "%llu %u\n", (unsigned long long)s_budget_start, s_request_count) > 0 &&
              !fflush(f) && !fsync(fileno(f));
    if (fclose(f))
        ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
typedef struct {
    char etag[129], retry[32], encoding[32];
} response_headers_t;
// 诊断仅包含状态码，不写账号、密码、网址或文章内容。/ Diagnostics contain codes only, never credentials, URLs or article content.
static void network_diagnostic(bool listing, esp_err_t error, int status, int tls, int mbedtls,
                               int flags, uint64_t received, uint64_t expected) {
    FILE *f=fopen(CLOUD_CACHE "/last-network-error.txt","w");
    if (!f) return;
    fprintf(f,"time=%llu\nrequest=%s\nerror=%d\nhttp=%d\ntls=%d\nmbedtls=%d\ncert_flags=%d\nreceived=%llu\nexpected=%llu\n",
            (unsigned long long)time(NULL),listing?"listing":"content",error,status,tls,mbedtls,flags,
            (unsigned long long)received,(unsigned long long)expected);
    fclose(f);
}
static esp_err_t http_event(esp_http_client_event_t *event) {
    response_headers_t *h = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && h) {
        if (!strcasecmp(event->header_key, "Content-Encoding"))
            ui_snprintf(h->encoding, sizeof(h->encoding), "%s", event->header_value);
        if (!strcasecmp(event->header_key, "ETag"))
            ui_snprintf(h->etag, sizeof(h->etag), "%s", event->header_value);
        if (!strcasecmp(event->header_key, "Retry-After"))
            ui_snprintf(h->retry, sizeof(h->retry), "%s", event->header_value);
    }
    return ESP_OK;
}
static esp_err_t fetch(const char *path, bool listing, const char *target, uint64_t limit, uint64_t expected,
                       const char *expected_sha, const char *etag, char actual_sha[65],
                       uint64_t *actual_bytes) {
    char encoded[724], url[1748];
    if (*path && !cloud_encode_path(path, encoded, sizeof(encoded)))
        return ESP_ERR_INVALID_ARG;
    if (!*path)
        encoded[0] = 0;
    if (ui_snprintf(url, sizeof(url), "%s%s", s_base_url, encoded) >= (int)sizeof(url))
        return ESP_ERR_INVALID_SIZE;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (cancelled())
            return ESP_ERR_INVALID_STATE;
        esp_err_t e = budget();
        if (e != ESP_OK)
            return e;
        response_headers_t headers = {0};
        esp_http_client_config_t cfg = {.url = url,
                                        .username = s_config.account,
                                        .password = s_config.password,
                                        .auth_type = HTTP_AUTH_TYPE_BASIC,
                                        .crt_bundle_attach = esp_crt_bundle_attach,
                                        .disable_auto_redirect = true,
                                        .timeout_ms = CLOUD_CONNECT_TIMEOUT_MS,
                                        .buffer_size = 4096,
                                        .event_handler = http_event,
                                        .user_data = &headers};
        esp_http_client_handle_t http = esp_http_client_init(&cfg);
        if (!http)
            return ESP_ERR_NO_MEM;
        esp_http_client_set_method(http, listing ? HTTP_METHOD_PROPFIND : HTTP_METHOD_GET);
        esp_http_client_set_header(http, "Accept-Encoding", "identity");
        if (listing) {
            esp_http_client_set_header(http, "Depth", "1");
            esp_http_client_set_header(http, "Content-Type", "application/xml; charset=utf-8");
        }
        if (etag && strong_etag(etag))
            esp_http_client_set_header(http, "If-Match", etag);
        int length = listing ? (int)strlen(propfind) : 0;
        e = esp_http_client_open(http, length);
        if (e == ESP_OK && length && esp_http_client_write(http, propfind, length) != length)
            e = ESP_FAIL;
        int64_t declared = e == ESP_OK ? esp_http_client_fetch_headers(http) : -1;
        int status = esp_http_client_get_status_code(http);
        bool retry_transport = false;
        if (e != ESP_OK || declared < 0) {
            int tls_error = 0, tls_flags = 0;
            esp_err_t tls = esp_http_client_get_and_clear_last_tls_error(http, &tls_error, &tls_flags);
            ESP_LOGW("pico_cloud", "%s connection failed: err=%d status=%d tls=%d mbedtls=%d flags=%d attempt=%u",
                     listing ? "listing" : "content", e, status, tls, tls_error, tls_flags, attempt + 1);
            network_diagnostic(listing,e==ESP_OK?ESP_FAIL:e,status,tls,tls_error,tls_flags,0,expected);
            read_pico_transfer_status_t network;
            read_pico_transfer_get_status(&network);
            // 只重试只读请求的短暂连接故障；证书失败不绕过验证。/ Retry transient read-only transport failures without bypassing certificate verification.
            if (network.network_ready && !tls_flags && attempt < 2 && !cancelled()) {
                esp_http_client_close(http);
                esp_http_client_cleanup(http);
                notice(ui_message(UI_MSG("云端连接较慢，正在重试…")), ui_message(UI_MSG("正在重试连接…")));
                for (unsigned tick = 0; tick < 5 && !cancelled(); ++tick)
                    vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            fail_notice(network.network_ready ? ui_message(UI_MSG("云端连接失败，请检查网络和系统时间"))
                                              : ui_message(UI_MSG("WiFi未连接，请检查网络配置")),
                        network.network_ready ? ui_message(UI_MSG("云端连接失败")) : ui_message(UI_MSG("未连接WiFi")));
            e = ESP_FAIL;
        }
        if (e == ESP_OK && (status == 429 || status == 503) && attempt < 2) {
            char *end = NULL;
            unsigned long delay = headers.retry[0] ? strtoul(headers.retry, &end, 10) : attempt + 1;
            bool valid = !headers.retry[0] || (end != headers.retry && !*end);
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            if (!valid || delay > 10) {
                s_session_fatal = true;
                fail_notice(ui_message(UI_MSG("坚果云限制访问，请稍后重试")), ui_message(UI_MSG("访问受限，稍后重试")));
                return ESP_ERR_TIMEOUT;
            }
            for (unsigned tick = 0; tick < delay * 10 && !cancelled(); ++tick)
                vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (e == ESP_OK && status != (listing ? 207 : 200)) {
            if (status == 401 || status == 403) {
                s_session_fatal = true;
                fail_notice(ui_message(UI_MSG("坚果云账号或应用密码不可用")), ui_message(UI_MSG("账号或应用密码错误")));
            } else if (status == 429 || status == 503) {
                s_session_fatal = true;
                fail_notice(ui_message(UI_MSG("坚果云限制访问，请稍后重试")), ui_message(UI_MSG("访问受限，稍后重试")));
            } else if (status == 404) {
                fail_notice(listing ? ui_message(UI_MSG("同步文件夹不存在，请检查设置")) : ui_message(UI_MSG("云端内容已删除，请刷新目录")),
                            listing ? ui_message(UI_MSG("同步文件夹不存在")) : ui_message(UI_MSG("云端内容已删除")));
            } else if (status == 412)
                fail_notice(ui_message(UI_MSG("云端内容已更新，请刷新目录")), ui_message(UI_MSG("文章已更新，请刷新")));
            else
                fail_notice(ui_message(UI_MSG("云端读取失败，请检查同步目录")), ui_message(UI_MSG("云端读取失败")));
            ESP_LOGW("pico_cloud", "%s HTTP status=%d", listing ? "listing" : "content", status);
            e = ESP_FAIL;
        }
        if (e == ESP_OK && headers.encoding[0] && strcasecmp(headers.encoding, "identity"))
            e = ESP_ERR_NOT_SUPPORTED;
        if (e == ESP_OK && !esp_http_client_is_chunked_response(http) && declared >= 0 &&
            ((uint64_t)declared > limit || (expected != UINT64_MAX && (uint64_t)declared != expected)))
            e = ESP_ERR_INVALID_SIZE;
        // 只有两端均提供可比较的版本串才检查变化；无标记文件由调用方二次哈希复核。
        // Compare usable version tokens when both exist; the caller hash-verifies files without strong validators twice.
        if (e == ESP_OK && cloud_etag_version(etag, NULL) &&
            cloud_etag_version(headers.etag, NULL) && !cloud_etag_same(etag, headers.etag)) {
            fail_notice(ui_message(UI_MSG("文章版本已变化，请刷新目录后重试")), ui_message(UI_MSG("文章版本校验失败")));
            e = ESP_ERR_INVALID_RESPONSE;
        }
        FILE *file = e == ESP_OK ? fopen(target, "wb") : NULL;
        if (e == ESP_OK && !file)
            e = ESP_FAIL;
        uint64_t received = 0;
        uint8_t buffer[4096];
        if (e == ESP_OK)
            esp_http_client_set_timeout_ms(http, CLOUD_READ_TIMEOUT_MS);
        while (e == ESP_OK && !cancelled()) {
            int n = esp_http_client_read(http, (char *)buffer, sizeof(buffer));
            if (n < 0) {
                e = ESP_FAIL;
                retry_transport = true;
                break;
            }
            if (!n)
                break;
            received += (unsigned)n;
            if (received > limit || (expected != UINT64_MAX && received > expected)) {
                e = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (fwrite(buffer, 1, n, file) != (size_t)n) {
                e = ESP_FAIL;
                break;
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.bytes_done = received;
            s_status.bytes_total = expected == UINT64_MAX ? (declared > 0 ? declared : 0) : expected;
            ++s_status.revision;
            xSemaphoreGive(s_lock);
        }
        if (cancelled())
            e = ESP_ERR_INVALID_STATE;
        if (e == ESP_OK && !esp_http_client_is_complete_data_received(http)) {
            retry_transport = true;
            e = ESP_ERR_INVALID_RESPONSE;
        }
        if (e == ESP_OK && expected != UINT64_MAX && received != expected)
            e = ESP_ERR_INVALID_RESPONSE;
        if (file) {
            if (e == ESP_OK && (fflush(file) || fsync(fileno(file))))
                e = ESP_FAIL;
            if (fclose(file) && e == ESP_OK)
                e = ESP_FAIL;
        }
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        char hash[65];
        uint64_t bytes = 0;
        if (e == ESP_OK)
            e = document_store_hash_file(target, hash, &bytes);
        if (e == ESP_OK && expected_sha && strcmp(hash, expected_sha)) {
            fail_notice(ui_message(UI_MSG("云端正文或图片校验不一致，请重新下载")), ui_message(UI_MSG("内容校验失败")));
            e = ESP_ERR_INVALID_CRC;
        }
        if (e == ESP_OK) {
            if (actual_sha)
                strcpy(actual_sha, hash);
            if (actual_bytes)
                *actual_bytes = bytes;
            return ESP_OK;
        }
        ESP_LOGW("pico_cloud", "%s receive failed: err=%d received=%llu expected=%llu attempt=%u",
                 listing ? "listing" : "content", e, (unsigned long long)received,
                 (unsigned long long)expected, attempt + 1);
        // 连接错误已保存更详细的TLS诊断，不用普通接收错误覆盖。/ Keep the more detailed TLS record for connection failures.
        if (declared >= 0)
            network_diagnostic(listing,e,status,0,0,0,received,expected);
        if (retry_transport && !cancelled() && attempt < 2) {
            notice(ui_message(UI_MSG("云端传输中断，正在重新下载…")), ui_message(UI_MSG("正在重试下载…")));
            for (unsigned tick = 0; tick < 5 && !cancelled(); ++tick)
                vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (retry_transport && !cancelled())
            fail_notice(ui_message(UI_MSG("云端传输未完成，请检查WiFi后重试")), ui_message(UI_MSG("云端传输中断")));
        return e;
    }
    return ESP_FAIL;
}
static bool paths(void) {
    char encoded[724];
    if (!cloud_encode_path(s_config.folder, encoded, sizeof(encoded)))
        return false;
    if (ui_snprintf(s_base_url, sizeof(s_base_url), "https://dav.jianguoyun.com/dav/%s/", encoded) >=
        (int)sizeof(s_base_url))
        return false;
    if (ui_snprintf(s_folder_path, sizeof(s_folder_path), "/dav/%s/", s_config.folder) >=
        (int)sizeof(s_folder_path))
        return false;
    char key[1024];
    int n = ui_snprintf(key, sizeof(key), "%s\n%s", s_base_url, s_config.account);
    return n > 0 && (size_t)n < sizeof(key) && document_store_hash_bytes(key, n, s_scope);
}
static uint64_t entry_date(const cloud_dav_entry_t *item) {
    if (item->created_ms)
        return item->created_ms;
    char day[4], month[4], iso[25];
    unsigned d, y, h, m, s;
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (sscanf(item->modified, "%3s, %u %3s %u %u:%u:%u GMT", day, &d, month, &y, &h, &m, &s) != 7)
        return 0;
    const char *p = strstr(months, month);
    if (!p || (p - months) % 3)
        return 0;
    ui_snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02uZ", y, (unsigned)((p - months) / 3 + 1), d, h, m,
             s);
    uint64_t value = 0;
    return cloud_timestamp(iso, &value) ? value : 0;
}
static int order(const void *a, const void *b) {
    const cloud_entry_t *x = a, *y = b;
    if (x->date_ms != y->date_ms)
        return x->date_ms > y->date_ms ? -1 : 1;
    return strcmp(x->name, y->name);
}
static esp_err_t list_cloud(void) {
    char file[256];
    ui_snprintf(file, sizeof(file), CLOUD_CACHE "/%s-listing.xml", s_scope);
    notice(ui_message(UI_MSG("正在读取坚果云目录…")), ui_message(UI_MSG("正在读取目录…")));
    esp_err_t e = fetch("", true, file, CLOUD_XML_MAX, UINT64_MAX, NULL, NULL, NULL, NULL);
    if (e != ESP_OK)
        return e;
    cloud_dav_entry_t *rows =
        heap_caps_calloc(CLOUD_ENTRY_MAX, sizeof(*rows), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    FILE *f = fopen(file, "rb");
    size_t count = 0;
    bool truncated = false;
    if (!rows || !f) {
        free(rows);
        if (f)
            fclose(f);
        return ESP_ERR_NO_MEM;
    }
    e = cloud_dav_parse(f, s_folder_path, rows, CLOUD_ENTRY_MAX, &count, &truncated);
    fclose(f);
    if (e != ESP_OK || truncated) {
        fail_notice(ui_message(UI_MSG("目录过大或不完整，请拆分同步文件夹")), ui_message(UI_MSG("目录过大或不完整")));
        free(rows);
        return ESP_ERR_INVALID_SIZE;
    }
    // 完成整次读取后才交换目录；失败/取消保留上次列表。
    // Publish only a complete listing; failure/cancellation keeps the previous catalog.
    cloud_entry_t *catalog =
        heap_caps_calloc(CLOUD_ENTRY_MAX, sizeof(*catalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!catalog) {
        free(rows);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < count && !cancelled(); ++i) {
        cloud_entry_t entry = {0};
        ui_snprintf(entry.href, sizeof(entry.href), "%s", rows[i].href);
        ui_snprintf(entry.etag, sizeof(entry.etag), "%s", rows[i].etag);
        entry.bytes = rows[i].bytes;
        entry.selectable = true;
        ui_snprintf(entry.name, sizeof(entry.name), "%s", entry.href);
        ui_snprintf(entry.filename, sizeof(entry.filename), "%s", entry.href);
        entry.date_ms = entry_date(&rows[i]);
        entry.date_fallback = rows[i].date_fallback;
        time_t t = entry.date_ms / 1000;
        struct tm tm;
        if (entry.date_ms && gmtime_r(&t, &tm))
            strftime(entry.date, sizeof(entry.date), "%Y-%m-%dT%H:%M:%SZ", &tm);
        if (entry.bytes > sync_file_limit(sync_file_kind(entry.filename))) {
            entry.selectable = false;
            ui_snprintf(entry.error, sizeof(entry.error), "%s", ui_message(UI_MSG("文件超过大小上限")));
        }
        catalog[i] = entry;
    }
    if (!cancelled()) {
        qsort(catalog, count, sizeof(*catalog), order);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(s_entries, catalog, count * sizeof(*catalog));
        s_status.count = count;
        ++s_status.revision;
        xSemaphoreGive(s_lock);
    }
    free(catalog);
    free(rows);
    return cancelled() ? ESP_ERR_INVALID_STATE : ESP_OK;
}

// 使用中的字体可能仍有流式读取句柄；仅允许内容相同的原位重复下载。
// Active fonts can retain streaming handles; allow only identical-content re-downloads in place.
static bool active_font_replace(const char *path, const char *incoming_hash) {
    const char *configured[] = {app_settings_system_font_path(), app_settings_font_path(),
                                content_markdown_font(), content_home_quote_font()};
    for (size_t i = 0; i < sizeof(configured) / sizeof(configured[0]); ++i) {
        if (!configured[i] || strcasecmp(configured[i], path)) continue;
        struct stat st;
        if (stat(path, &st)) return errno != ENOENT;
        char hash[65];
        return document_store_hash_file(path, hash, NULL) != ESP_OK || strcmp(hash, incoming_hash);
    }
    return false;
}
static esp_err_t download_one(const cloud_entry_t *entry) {
    sync_file_kind_t kind = sync_file_kind(entry->filename);
    if (entry->manifest || strcmp(entry->href, entry->filename) ||
        !document_store_safe_filename(entry->filename) || kind == SYNC_FILE_UNSUPPORTED)
        return ESP_ERR_NOT_SUPPORTED;
    if (entry->bytes > sync_file_limit(kind)) return ESP_ERR_INVALID_SIZE;
    bool md = kind == SYNC_FILE_MARKDOWN;
    esp_err_t e = document_store_sync_prepare();
    if (e != ESP_OK) {
        char detail[160];
        ui_snprintf(detail,sizeof(detail),ui_message(UI_MSG("准备TF卡失败（%s）")),esp_err_to_name(e));
        fail_notice(detail,ui_message(UI_MSG("TF卡准备失败")));
        return e;
    }
    const char *phase = ui_message(UI_MSG("读取文件"));
    document_sync_asset_t *assets =
        md ? heap_caps_calloc(64, sizeof(*assets), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (md && !assets) {
        free(assets);
        (void)document_store_sync_abort();
        return ESP_ERR_NO_MEM;
    }
    size_t count = 0;
    char body_sha[65];
    uint64_t body_bytes = 0;
    phase = ui_message(UI_MSG("下载文件"));
    e = fetch(entry->href, false, DOCUMENT_SYNC_STAGE "/body", sync_file_limit(kind),
              entry->bytes, NULL, entry->etag, body_sha, &body_bytes);
    if (e == ESP_OK && md) {
        phase = ui_message(UI_MSG("解析图片链接"));
        e = document_store_sync_collect_assets(assets, &count);
    }
    uint64_t total = 0;
    for (size_t i = 0; e == ESP_OK && i < count; ++i) {
        phase = ui_message(UI_MSG("下载图片"));
        char relative[256], path[512], sha[65];
        uint64_t bytes;
        ui_snprintf(relative, sizeof(relative), "attachments/%s", assets[i].name);
        ui_snprintf(path, sizeof(path), DOCUMENT_SYNC_STAGE "/attachments/%s", assets[i].name);
        e = fetch(relative, false, path, 17 * 1024 * 1024, UINT64_MAX, NULL, NULL, sha, &bytes);
        if (e == ESP_OK) {
            total += bytes;
            if (total > 32 * 1024 * 1024)
                e = ESP_ERR_INVALID_SIZE;
            else {
                strcpy(assets[i].sha256, sha);
                assets[i].bytes = bytes;
            }
        }
    }
    if (e == ESP_OK && !strong_etag(entry->etag)) {
        phase = ui_message(UI_MSG("复核文件版本"));
        message(ui_message(UI_MSG("正在复核文件版本…")));
        const char *verification = CLOUD_CACHE "/verify-body.part";
        e = fetch(entry->href, false, verification, sync_file_limit(kind), body_bytes, body_sha, entry->etag, NULL,
                  NULL);
        (void)unlink(verification);
    }
    if (e == ESP_OK && cancelled())
        e = ESP_ERR_INVALID_STATE;
    if (e == ESP_OK) {
        phase = ui_message(UI_MSG("保存文件"));
        char path[288];
        const char *directory = kind == SYNC_FILE_BOOK ? app_settings_books_dir() : sync_file_directory(kind);
        int n = ui_snprintf(path, sizeof(path), "%s/%s", directory,
                         entry->filename);
        bool unchanged;
        if (n >= (int)sizeof(path))
            e = ESP_ERR_INVALID_SIZE;
        else if (kind == SYNC_FILE_FONT && active_font_replace(path, body_sha)) {
            fail_notice(ui_message(UI_MSG("此字体正在使用，请切换其他字体后再更新")), ui_message(UI_MSG("字体正在使用")));
            e = ESP_ERR_INVALID_STATE;
        }
        else
            e = document_store_sync_commit(path, body_sha, body_bytes, assets, count, &unchanged);
    }
    if (e != ESP_OK) {
        ESP_LOGW("pico_cloud","download failed at %s: %s",phase,esp_err_to_name(e));
        if (!s_notice_error && !cancelled()) {
            char detail[160];
            ui_snprintf(detail,sizeof(detail),ui_message(UI_MSG("%s失败（%s）")),phase,esp_err_to_name(e));
            fail_notice(detail,ui_message(UI_MSG("同步失败")));
        }
        (void)document_store_sync_abort();
    }
    free(assets);
    return e;
}
static void worker(void *argument) {
    bool download = argument != NULL;
    esp_err_t e = ESP_OK;
    int waits = 0;
    read_pico_transfer_status_t network;
    do {
        read_pico_transfer_get_status(&network);
        if (network.network_ready)
            break;
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (!cancelled() && ++waits < 160);
    if (cancelled() || !network.network_ready) {
        e = ESP_ERR_TIMEOUT;
        if (!cancelled())
            fail_notice(ui_message(UI_MSG("WiFi未连接，请检查网络配置后重试")), ui_message(UI_MSG("未连接WiFi")));
    }
    if (e == ESP_OK)
        notice(download ? ui_message(UI_MSG("准备下载所选内容")) : ui_message(UI_MSG("准备读取坚果云目录")),
               download ? ui_message(UI_MSG("正在同步…")) : ui_message(UI_MSG("正在读取目录…")));
    if (e == ESP_OK && time(NULL) < 1704067200) {
        uint32_t utc;
        e = read_pico_transfer_sync_time_online(&utc);
        if (e != ESP_OK)
            fail_notice(ui_message(UI_MSG("无法校准系统时间，请检查网络")), ui_message(UI_MSG("网络校时失败")));
    }
    if (e == ESP_OK) {
        e = document_store_sync_recover();
        if (e != ESP_OK)
            fail_notice(ui_message(UI_MSG("TF卡同步恢复未完成，原文件已保留")), ui_message(UI_MSG("TF卡恢复未完成")));
    }
    if (e == ESP_OK && !download)
        e = list_cloud();
    if (e == ESP_OK && download) {
        cloud_status_t initial;
        cloud_get_status(&initial);
        for (unsigned i = 0; i < initial.count && !cancelled(); ++i) {
            cloud_entry_t entry;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            entry = s_entries[i];
            xSemaphoreGive(s_lock);
            if (!entry.selected || !entry.selectable)
                continue;
            read_pico_sd_info_t sd = {0};
            if (read_pico_sd_get_info(&sd) != ESP_OK || !sd.mounted ||
                sd.free_bytes < entry.bytes * 2 + 17 * 1024 * 1024) {
                e = ESP_ERR_INVALID_STATE;
                fail_notice(ui_message(UI_MSG("TF卡不可用或剩余空间不足")), ui_message(UI_MSG("TF卡不可用或空间不足")));
                break;
            }
            s_notice_error = false;
            message(entry.filename);
            e = download_one(&entry);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (e == ESP_OK) {
                ++s_status.done;
                s_entries[i].error[0] = 0;
            } else {
                ++s_status.failed;
                ui_snprintf(s_entries[i].error, sizeof(s_entries[i].error), "%.95s",
                         s_notice_error ? s_status.message : ui_message(UI_MSG("下载未完成，原内容已保留")));
                size_t length = strlen(s_entries[i].error);
                while (length && !cloud_utf8_valid(s_entries[i].error,length))
                    s_entries[i].error[--length] = 0;
            }
            ++s_status.revision;
            xSemaphoreGive(s_lock);
            if (e != ESP_OK && (s_session_fatal || !access("/sdcard/.pico-sync/transaction", F_OK)))
                break;
        }
        book_store_notify_changed();
    }
    cloud_status_t final;
    cloud_get_status(&final);
    if (cancelled())
        notice(ui_message(UI_MSG("同步已取消，已入库文件保留")), ui_message(UI_MSG("已取消")));
    else if (!download && e == ESP_OK) {
        char label[96];
        ui_snprintf(label, sizeof(label), ui_message(UI_MSG("目录已读取，共%u项内容")), final.count);
        notice(final.count ? ui_message(UI_MSG("目录已读取，请勾选需要下载的内容")) : ui_message(UI_MSG("同步文件夹中还没有可读内容")), label);
    } else if (download && !s_session_fatal && (final.failed || (!s_notice_error && e == ESP_OK))) {
        char result[96];
        ui_snprintf(result, sizeof(result), ui_message(UI_MSG("同步完成：成功%u项，失败%u项")), final.done, final.failed);
        notice(result, final.failed ? ui_message(UI_MSG("部分内容同步失败")) : ui_message(UI_MSG("同步完成")));
    } else if (!s_notice_error)
        fail_notice(ui_message(UI_MSG("同步未完成，请检查网络、配置和TF卡")), download ? ui_message(UI_MSG("同步失败，请重试")) : ui_message(UI_MSG("读取失败，请重试")));
    clear_secret(&s_config, sizeof(s_config));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = false;
    ++s_status.revision;
    xSemaphoreGive(s_lock);
    xSemaphoreGive(s_finished);
    vTaskDelete(NULL);
}
static esp_err_t begin(bool download) {
    app_time_service_stop();
    if (!init())
        return ESP_ERR_NO_MEM;
    if (s_task) {
        if (xSemaphoreTake(s_finished, 0) == pdTRUE)
            s_task = NULL;
        else
            return ESP_ERR_INVALID_STATE;
    }
    cloud_config_load(&s_config);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.configured = cloud_config_valid(&s_config, true);
    ui_snprintf(s_status.folder, sizeof(s_status.folder), "%s", s_config.folder);
    s_status.total = 0;
    xSemaphoreGive(s_lock);
    s_session_fatal = s_notice_error = false;
    if (!s_status.configured) {
        fail_notice(ui_message(UI_MSG("请在设置中配置坚果云账号和应用密码")), ui_message(UI_MSG("未配置账号")));
        clear_secret(&s_config, sizeof(s_config));
        return ESP_ERR_NOT_FOUND;
    }
    if (download) {
        unsigned selected = 0;
        for (unsigned i = 0; i < s_status.count; ++i)
            if (s_entries[i].selected && s_entries[i].selectable) {
                ++selected;
                for (unsigned j = i + 1; j < s_status.count; ++j)
                    if (s_entries[j].selected && s_entries[j].selectable &&
                        !strcasecmp(s_entries[i].filename, s_entries[j].filename)) {
                        fail_notice(ui_message(UI_MSG("所选内容同名，请一次选择其中一篇")), ui_message(UI_MSG("所选内容有同名文件")));
                        clear_secret(&s_config, sizeof(s_config));
                        return ESP_ERR_INVALID_ARG;
                    }
            }
        if (!selected) {
            clear_secret(&s_config, sizeof(s_config));
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.total = selected;
        xSemaphoreGive(s_lock);
    }
    if (!paths()) {
        clear_secret(&s_config, sizeof(s_config));
        return ESP_ERR_INVALID_ARG;
    }
    read_pico_sd_info_t sd = {0};
    if (read_pico_sd_get_info(&sd) != ESP_OK || !sd.mounted) {
        fail_notice(ui_message(UI_MSG("请插入并挂载TF卡后同步")), ui_message(UI_MSG("TF卡未挂载")));
        clear_secret(&s_config, sizeof(s_config));
        return ESP_ERR_INVALID_STATE;
    }
    (void)mkdir(CLOUD_CACHE, 0700);
    FILE *budget_file = fopen(CLOUD_CACHE "/budget", "rb");
    if (budget_file) {
        unsigned long long start;
        unsigned count;
        if (fscanf(budget_file, "%llu %u", &start, &count) == 2 && time(NULL) - (time_t)start < 1800) {
            s_budget_start = start;
            s_request_count = count;
        }
        fclose(budget_file);
    }
    read_pico_transfer_status_t network;
    read_pico_transfer_get_status(&network);
    if (network.state == READ_PICO_TRANSFER_STOPPED || (network.state==READ_PICO_TRANSFER_ERROR&&!network.network_ready)) {
        if(network.state==READ_PICO_TRANSFER_ERROR&&!read_pico_transfer_try_stop_if_idle()){fail_notice(ui_message(UI_MSG("请先结束传输后重试")),ui_message(UI_MSG("网络忙")));clear_secret(&s_config,sizeof(s_config));return ESP_ERR_INVALID_STATE;}
        read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
        esp_err_t e = read_pico_transfer_start(&cfg);
        if (e != ESP_OK) {
            read_pico_transfer_get_status(&network);
            bool nearby=e==ESP_ERR_NOT_FOUND&&network.wifi_configured;
            fail_notice(nearby?ui_message(UI_MSG("附近未找到已保存的WiFi，请前往设置")):e==ESP_ERR_NOT_FOUND?ui_message(UI_MSG("请先在设置中配置WiFi网络")):ui_message(UI_MSG("无法启动WiFi，请稍后重试")),
                        nearby?ui_message(UI_MSG("未找到已保存的WiFi")):e==ESP_ERR_NOT_FOUND?ui_message(UI_MSG("请先配置WiFi")):ui_message(UI_MSG("WiFi启动失败")));
            clear_secret(&s_config, sizeof(s_config));
            return e;
        }
        s_owned_network = true;
    } else if (network.mode != READ_PICO_TRANSFER_MODE_STA) {
        fail_notice(ui_message(UI_MSG("请先连接已有WiFi，关闭设备热点或其他传输")), ui_message(UI_MSG("请先连接WiFi")));
        clear_secret(&s_config, sizeof(s_config));
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_cancel, false);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = true;
    s_status.done = s_status.failed = 0;
    s_status.bytes_done = s_status.bytes_total = 0;
    xSemaphoreGive(s_lock);
    notice(ui_message(UI_MSG("准备连接坚果云…")),
           network.network_ready ? (download ? ui_message(UI_MSG("正在同步…")) : ui_message(UI_MSG("正在读取目录…"))) : ui_message(UI_MSG("正在连接WiFi…")));
    if (xTaskCreate(worker, "pico_cloud", 16384, (void *)(uintptr_t)download, 4, &s_task) != pdPASS) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.busy = false;
        xSemaphoreGive(s_lock);
        fail_notice(ui_message(UI_MSG("无法启动同步任务，请稍后重试")), ui_message(UI_MSG("同步任务启动失败")));
        clear_secret(&s_config, sizeof(s_config));
        if (s_owned_network) {
            read_pico_transfer_stop();
            s_owned_network = false;
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t cloud_refresh(void) {
    return begin(false);
}
esp_err_t cloud_download_selected(void) {
    return begin(true);
}
void cloud_cancel(void) {
    atomic_store(&s_cancel, true);
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_status.busy) {
            ui_snprintf(s_status.indicator, sizeof(s_status.indicator), "%s", ui_message(UI_MSG("正在停止…")));
            ++s_status.revision;
        }
        xSemaphoreGive(s_lock);
    }
}
void cloud_close(void) {
    cloud_cancel();
    if (s_task) {
        xSemaphoreTake(s_finished, portMAX_DELAY);
        s_task = NULL;
    }
    if (s_owned_network) {
        read_pico_transfer_stop();
        s_owned_network = false;
    }
    clear_secret(&s_config, sizeof(s_config));
}
