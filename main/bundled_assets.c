/* SPDX-License-Identifier: Apache-2.0
 * 中文：内置背景与壁纸PNG流式解压到TF；保留已有同名文件，校验后原子安装。
 * English: Stream bundled banner and wallpaper PNGs to TF, preserve existing files and install only after verification.
 */
#include "bundled_assets.h"
#include "assets/bundled_banner.h"
#include "assets/bundled_wallpaper.h"
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

static esp_err_t install_asset(const char *directory,const char *target,const char *part,const unsigned char *data,size_t length,size_t expected,uint32_t expected_crc) {
    struct stat st;
    if (!lstat(target, &st)) return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (errno != ENOENT) return ESP_FAIL;
    // 不创建挂载根目录；缺卡或拔卡后不能误写入其他文件系统。
    // Never create the mount root or spill files into another filesystem when media is absent.
    if (stat("/sdcard", &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_NOT_FOUND;
    if (mkdir(directory, 0700) && errno != EEXIST) return ESP_FAIL;
    if (lstat(directory, &st) || !S_ISDIR(st.st_mode)) return ESP_ERR_INVALID_STATE;
    FILE *out = fopen(part, "wb");
    if (!out) return ESP_FAIL;
    z_stream stream = {0};
    stream.next_in = (Bytef *)data;
    stream.avail_in = length;
    int result = inflateInit(&stream);
    bool initialized = result == Z_OK, ok = initialized;
    uint8_t buffer[2048];
    uint32_t crc = crc32(0, Z_NULL, 0);
    while (ok) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        size_t count = sizeof(buffer) - stream.avail_out;
        if ((result != Z_OK && result != Z_STREAM_END) || stream.total_out > expected ||
            fwrite(buffer, 1, count, out) != count) { ok = false; break; }
        crc = crc32(crc, buffer, count);
        if (result == Z_STREAM_END) break;
    }
    ok = ok && result == Z_STREAM_END && stream.avail_in == 0 &&
         stream.total_out == expected && crc == expected_crc;
    if (initialized) inflateEnd(&stream);
    if (ok && (fflush(out) || fsync(fileno(out)))) ok = false;
    if (fclose(out)) ok = false;
    if (!ok) { (void)unlink(part); return ESP_FAIL; }
    if (!lstat(target, &st)) { (void)unlink(part); return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE; }
    if (errno != ENOENT || rename(part, target)) { (void)unlink(part); return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t bundled_assets_install(void) {
    esp_err_t banner=install_asset(BANNER_DIRECTORY,BANNER_TARGET,BANNER_PART,bundled_banner_z,sizeof(bundled_banner_z),BUNDLED_BANNER_SIZE,BUNDLED_BANNER_CRC);
    esp_err_t wallpaper=install_asset("/sdcard/pictures","/sdcard/pictures/默认壁纸.png","/sdcard/pictures/.默认壁纸.png.part",bundled_wallpaper_z,sizeof(bundled_wallpaper_z),BUNDLED_WALLPAPER_SIZE,BUNDLED_WALLPAPER_CRC);
    return banner!=ESP_OK?banner:wallpaper;
}
