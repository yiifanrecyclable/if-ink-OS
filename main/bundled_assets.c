/* SPDX-License-Identifier: Apache-2.0
 * 中文：内置猩猩PNG流式解压到TF；保留已有同名文件，校验后原子安装。
 * English: Stream the bundled ape PNG to TF, preserve existing files and install only after verification.
 */
#include "bundled_assets.h"
#include "assets/bundled_banner.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "zlib.h"
#ifndef PICO_HOST_SIMULATOR
#define lstat stat
#endif

#define BANNER_DIRECTORY "/sdcard/banner pic"
#define BANNER_TARGET BANNER_DIRECTORY "/思考大猩猩.png"
#define BANNER_PART BANNER_DIRECTORY "/.思考大猩猩.png.part"

esp_err_t bundled_assets_install(void) {
    struct stat st;
    if (!lstat(BANNER_TARGET, &st)) return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (errno != ENOENT) return ESP_FAIL;
    // 不创建挂载根目录；缺卡或拔卡后不能误写入其他文件系统。
    // Never create the mount root or spill files into another filesystem when media is absent.
    if (stat("/sdcard", &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_NOT_FOUND;
    if (mkdir(BANNER_DIRECTORY, 0700) && errno != EEXIST) return ESP_FAIL;
    if (lstat(BANNER_DIRECTORY, &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_INVALID_STATE;
    FILE *out = fopen(BANNER_PART, "wb");
    if (!out) return ESP_FAIL;
    z_stream stream = {0};
    stream.next_in = (Bytef *)bundled_banner_z;
    stream.avail_in = sizeof(bundled_banner_z);
    int result = inflateInit(&stream);
    bool initialized = result == Z_OK, ok = initialized;
    uint8_t buffer[2048];
    uint32_t crc = crc32(0, Z_NULL, 0);
    while (ok) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        size_t count = sizeof(buffer) - stream.avail_out;
        if ((result != Z_OK && result != Z_STREAM_END) || stream.total_out > BUNDLED_BANNER_SIZE ||
            fwrite(buffer, 1, count, out) != count) { ok = false; break; }
        crc = crc32(crc, buffer, count);
        if (result == Z_STREAM_END) break;
    }
    ok = ok && result == Z_STREAM_END && stream.avail_in == 0 &&
         stream.total_out == BUNDLED_BANNER_SIZE && crc == BUNDLED_BANNER_CRC;
    if (initialized) inflateEnd(&stream);
    if (ok && (fflush(out) || fsync(fileno(out)))) ok = false;
    if (fclose(out)) ok = false;
    if (!ok) { (void)unlink(BANNER_PART); return ESP_FAIL; }
    if (!lstat(BANNER_TARGET, &st)) { (void)unlink(BANNER_PART); return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE; }
    if (errno != ENOENT || rename(BANNER_PART, BANNER_TARGET)) { (void)unlink(BANNER_PART); return ESP_FAIL; }
    return ESP_OK;
}
