/* SPDX-License-Identifier: Apache-2.0 */
#include "standard_font_install.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "font_path_policy.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "record_store_media.h"

#ifdef PICO_HOST_SIMULATOR
#include <CommonCrypto/CommonDigest.h>
#else
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#define lstat stat
#define FONT_URL "https://ifink.revcloud.top/resources/fonts/09f17deea660/" APP_FONT_BUNDLED_NAME
#endif

#define FONT_PATH APP_FONT_DIRECTORY "/" APP_FONT_BUNDLED_NAME
#define FONT_PART APP_FONT_DIRECTORY "/.ifink-standard-font.part"
enum { FONT_BYTES = 10533840, READ_BYTES = 4096 };
// Identity of the original, unmodified OFL asset in assets/fonts.
static const uint8_t s_digest[32] = {
    0x09,0xf1,0x7d,0xee,0xa6,0x60,0xcd,0xd9,0xcc,0x2b,0xa1,0xe8,0xb9,0x0a,0xda,0xd1,
    0x8e,0xab,0xec,0xa7,0x3b,0x62,0xb3,0xc5,0xf7,0x6b,0x7e,0xc3,0xb6,0x1b,0x33,0xe0,
};
static atomic_bool s_running, s_cancelled;
static _Atomic unsigned s_bytes;
static _Atomic standard_font_state_t s_state;

static bool card_ready(void) {
    read_pico_sd_info_t info = {0};
    return read_pico_sd_get_info(&info) == ESP_OK && info.present && info.mounted;
}

standard_font_file_t standard_font_file(void) {
    if (!card_ready()) return STANDARD_FONT_NO_CARD;
    struct stat st;
    if (lstat(FONT_PATH, &st))
        return errno == ENOENT ? STANDARD_FONT_MISSING : STANDARD_FONT_CONFLICT;
    return S_ISREG(st.st_mode) && st.st_size == FONT_BYTES ? STANDARD_FONT_PRESENT : STANDARD_FONT_CONFLICT;
}

bool standard_font_wifi_ready(void) {
    read_pico_transfer_status_t wifi = {0};
    read_pico_transfer_get_status(&wifi);
    return wifi.network_ready && wifi.mode == READ_PICO_TRANSFER_MODE_STA;
}

static standard_font_state_t check_card(uint32_t epoch) {
    if (atomic_load(&s_cancelled)) return STANDARD_FONT_CANCELLED;
    return card_ready() && epoch == record_store_media_epoch() ?
        STANDARD_FONT_DOWNLOADING : STANDARD_FONT_MEDIA_ERROR;
}

static standard_font_state_t download(void) {
    if (standard_font_file() != STANDARD_FONT_MISSING) return STANDARD_FONT_MEDIA_ERROR;
    uint32_t epoch = record_store_media_epoch();
    uint8_t *buffer = malloc(READ_BYTES), digest[32];
    if (!buffer) return STANDARD_FONT_WRITE_ERROR;
    standard_font_state_t result = STANDARD_FONT_NETWORK_ERROR;
    FILE *out = NULL;
    bool part_created = false;
#ifdef PICO_HOST_SIMULATOR
    const char *fixture = getenv("PICO_SIM_STANDARD_FONT_SOURCE");
    FILE *input = fixture ? fopen(fixture,"rb") : NULL;
    CC_SHA256_CTX hash;
    if (!input) goto done;
    if (!CC_SHA256_Init(&hash)) goto done;
#else
    esp_http_client_config_t config = {
        .url = FONT_URL, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 2500, .disable_auto_redirect = true,
    };
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http || esp_http_client_set_header(http,"Accept-Encoding","identity") != ESP_OK ||
        esp_http_client_open(http,0) != ESP_OK) goto done;
    int64_t length = esp_http_client_fetch_headers(http);
    if (esp_http_client_get_status_code(http) != 200) goto done;
    if (length != FONT_BYTES || esp_http_client_is_chunked_response(http)) {
        result = STANDARD_FONT_INVALID;
        goto done;
    }
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&hash,PSA_ALG_SHA_256) != PSA_SUCCESS) goto done;
    const int64_t deadline = esp_timer_get_time() + INT64_C(180000000);
#endif
    result = check_card(epoch);
    if (result != STANDARD_FONT_DOWNLOADING) goto done;
    // A prior interrupted attempt may leave this reserved temporary path.
    if (unlink(FONT_PART) && errno != ENOENT) { result = STANDARD_FONT_WRITE_ERROR; goto done; }
    out = fopen(FONT_PART,"wb");
    if (!out) { result = STANDARD_FONT_WRITE_ERROR; goto done; }
    part_created = true;

    unsigned total = 0;
    while (total < FONT_BYTES) {
        result = check_card(epoch);
        if (result != STANDARD_FONT_DOWNLOADING) goto done;
        size_t want = FONT_BYTES - total;
        if (want > READ_BYTES) want = READ_BYTES;
#ifdef PICO_HOST_SIMULATOR
        int count = (int)fread(buffer,1,want,input);
        if (ferror(input) || count <= 0) { result = STANDARD_FONT_NETWORK_ERROR; goto done; }
        bool hashed = CC_SHA256_Update(&hash,buffer,(CC_LONG)count);
#else
        if (esp_timer_get_time() >= deadline) { result = STANDARD_FONT_NETWORK_ERROR; goto done; }
        int count = esp_http_client_read(http,(char *)buffer,(int)want);
        if (count <= 0) { result = STANDARD_FONT_NETWORK_ERROR; goto done; }
        bool hashed = psa_hash_update(&hash,buffer,(size_t)count) == PSA_SUCCESS;
#endif
        result = check_card(epoch);
        if (result != STANDARD_FONT_DOWNLOADING) goto done;
        if (!hashed || fwrite(buffer,1,(size_t)count,out) != (size_t)count) {
            result = STANDARD_FONT_WRITE_ERROR;
            goto done;
        }
        total += (unsigned)count;
        atomic_store(&s_bytes,total);
#ifdef PICO_HOST_SIMULATOR
        if (getenv("PICO_SIM_CANCEL_FONT_DOWNLOAD")) standard_font_cancel();
#endif
    }
#ifdef PICO_HOST_SIMULATOR
    bool valid = fgetc(input) == EOF && !ferror(input) && CC_SHA256_Final(digest,&hash);
#else
    size_t digest_bytes = 0;
    bool valid = esp_http_client_is_complete_data_received(http) &&
        psa_hash_finish(&hash,digest,sizeof(digest),&digest_bytes) == PSA_SUCCESS &&
        digest_bytes == sizeof(digest);
#endif
    if (!valid || memcmp(digest,s_digest,sizeof(digest))) { result = STANDARD_FONT_INVALID; goto done; }
    bool synced = !fflush(out) && !fsync(fileno(out));
    if (fclose(out)) synced = false;
    out = NULL;
    result = check_card(epoch);
    if (result != STANDARD_FONT_DOWNLOADING) goto done;
    if (!synced || standard_font_file() != STANDARD_FONT_MISSING || rename(FONT_PART,FONT_PATH)) {
        result = STANDARD_FONT_WRITE_ERROR;
        goto done;
    }
    part_created = false;
    result = STANDARD_FONT_INSTALLED;

done:
#ifdef PICO_HOST_SIMULATOR
    if (input) fclose(input);
#else
    if (http) esp_http_client_cleanup(http);
    psa_hash_abort(&hash);
#endif
    if (out) fclose(out);
    if (part_created && card_ready() && epoch == record_store_media_epoch()) (void)unlink(FONT_PART);
    free(buffer);
    return result;
}

static void run_download(void) {
    standard_font_state_t result = download();
    if (result != STANDARD_FONT_INSTALLED && atomic_load(&s_cancelled)) result = STANDARD_FONT_CANCELLED;
    atomic_store(&s_state,result);
    atomic_store(&s_running,false);
}

#ifndef PICO_HOST_SIMULATOR
static void worker(void *unused) {
    (void)unused;
    run_download();
    vTaskDelete(NULL);
}
#endif

void standard_font_start(void) {
    if (atomic_load(&s_running)) return;
    atomic_store(&s_bytes,0);
    atomic_store(&s_cancelled,false);
    atomic_store(&s_state,STANDARD_FONT_DOWNLOADING);
    atomic_store(&s_running,true);
#ifdef PICO_HOST_SIMULATOR
    run_download();
#else
    if (xTaskCreate(worker,"std_font",8192,NULL,4,NULL) != pdPASS) {
        atomic_store(&s_state,STANDARD_FONT_WRITE_ERROR);
        atomic_store(&s_running,false);
    }
#endif
}

void standard_font_cancel(void) { atomic_store(&s_cancelled,true); }

void standard_font_stop(void) {
    standard_font_cancel();
#ifndef PICO_HOST_SIMULATOR
    while (atomic_load(&s_running)) vTaskDelay(pdMS_TO_TICKS(20));
#endif
}

standard_font_status_t standard_font_status(void) {
    bool busy = atomic_load(&s_running);
    return (standard_font_status_t){atomic_load(&s_state),
        atomic_load(&s_bytes) * 100u / FONT_BYTES,busy};
}
