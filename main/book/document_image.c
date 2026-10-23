/* SPDX-License-Identifier: Apache-2.0
 * 中文：自定义固件 PNG 灰阶解码器。只接受插件输出的非交错 4-bit 灰阶 PNG。
 * English: Derivative grayscale decoder for the plugin's non-interlaced 4-bit gray PNG.
 * 中文：逐个校验 PNG 块 CRC，逐行解压；高度不设屏幕上限，RAM 只保留三行和 zlib 状态。
 * English: Verify chunk CRCs and inflate rows;
 height has no screen cap and RAM holds three rows plus zlib state.
 */
#include "document_image.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#ifdef PICO_HOST_SIMULATOR
#include <CommonCrypto/CommonDigest.h>
#else
#include "psa/crypto.h"
#endif
#define IMAGE_MAX_BYTES (17u * 1024u * 1024u)
#define IMAGE_MAX_PIXELS (32u * 1024u * 1024u)
#define CACHE_HEADER 56u
#define CACHE_ROOT "/sdcard/markdown/.cache"
#define CACHE_BUDGET (64u * 1024u * 1024u)
#ifdef PICO_HOST_SIMULATOR
static unsigned s_decode_count;
unsigned document_image_decode_count(void) { return s_decode_count; }
#endif
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static bool read_exact(FILE *f, void *out, size_t n) {
    return fread(out, 1, n, f) == n;
}
static void put32(uint8_t *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
// 仅图片路径计算摘要；纯文本解析和修订计算不变。/ Hash only image paths; text parsing and revisions stay unchanged.
static bool source_digest(FILE *f, uint8_t sum[32]) {
    if (fseek(f, 0, SEEK_SET)) return false;
    uint8_t data[1024]; size_t n; bool ok = true;
#ifdef PICO_HOST_SIMULATOR
    CC_SHA256_CTX digest; CC_SHA256_Init(&digest);
    while ((n = fread(data, 1, sizeof(data), f))) CC_SHA256_Update(&digest, data, (CC_LONG)n);
    ok = !ferror(f) && CC_SHA256_Final(sum, &digest);
#else
    psa_hash_operation_t digest = psa_hash_operation_init(); size_t used = 0;
    ok = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&digest, PSA_ALG_SHA_256) == PSA_SUCCESS;
    while (ok && (n = fread(data, 1, sizeof(data), f))) ok = psa_hash_update(&digest, data, n) == PSA_SUCCESS;
    ok = ok && !ferror(f) && psa_hash_finish(&digest, sum, 32, &used) == PSA_SUCCESS && used == 32;
    psa_hash_abort(&digest);
#endif
    rewind(f);
    return ok;
}
static bool cached_image(const char *path, const uint8_t source[32], document_image_t *image) {
    FILE *f = fopen(path, "rb"); if (!f) return false;
    uint8_t header[CACHE_HEADER], buffer[1024]; struct stat st;
    bool ok = !fstat(fileno(f), &st) && read_exact(f, header, sizeof(header)) &&
        !memcmp(header, "MDGRAY2", 8) && !memcmp(header + 8, source, 32) &&
        be32(header + 52) == (uint32_t)crc32(0, header, 52);
    uint32_t width = ok ? be32(header + 40) : 0, height = ok ? be32(header + 44) : 0;
    ok = ok && width && width <= 684 && height && (uint64_t)width * height <= IMAGE_MAX_PIXELS &&
        (uint64_t)st.st_size == CACHE_HEADER + (uint64_t)((width + 1) / 2) * height;
    uint32_t crc = crc32(0, Z_NULL, 0); size_t n;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), f))) crc = crc32(crc, buffer, (uInt)n);
    ok = ok && !ferror(f) && crc == be32(header + 48);
    fclose(f);
    if (ok) { image->width = width; image->height = height; snprintf(image->cache, sizeof(image->cache), "%s", path); }
    return ok;
}
// 缓存是可丢弃数据，限制总容量及文件数；不删除正文或原图。
// Cache is disposable: bound total size/count without touching text or source images.
static bool cache_room(uint64_t need, const char *keep) {
    for (;;) {
        DIR *d = opendir(CACHE_ROOT); if (!d) return false;
        struct dirent *de; uint64_t bytes = 0; unsigned count = 0;
        char oldest[512] = {0}; time_t oldest_time = 0;
        while ((de = readdir(d))) {
            size_t length = strlen(de->d_name);
            bool legacy = length == 13 && strspn(de->d_name, "0123456789abcdef") == 8 &&
                (!strcmp(de->d_name + 8, ".gray") || !strcmp(de->d_name + 8, ".part"));
            bool current = length == 81 && !strncmp(de->d_name, "v2-", 3) &&
                strspn(de->d_name + 3, "0123456789abcdef") == 8 && de->d_name[11] == '-' &&
                strspn(de->d_name + 12, "0123456789abcdef") == 64 && !strcmp(de->d_name + 76, ".gray");
            if (!legacy && !current) continue;
            char path[512]; struct stat st;
            if (snprintf(path, sizeof(path), CACHE_ROOT "/%s", de->d_name) >= (int)sizeof(path) || stat(path, &st) || !S_ISREG(st.st_mode)) continue;
            if (legacy && !unlink(path)) continue;
            bytes += st.st_size; count++;
            if (strcmp(path, keep) && (!oldest[0] || st.st_mtime < oldest_time)) {
                snprintf(oldest, sizeof(oldest), "%s", path); oldest_time = st.st_mtime;
            }
        }
        closedir(d);
        if (bytes + need <= CACHE_BUDGET && count < 32) return true;
        if (!oldest[0] || unlink(oldest)) return false;
    }
}
static unsigned paeth(unsigned a, unsigned b, unsigned c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a), pb = abs(p - (int)b), pc = abs(p - (int)c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
bool document_image_open(const char *path, document_image_t *image) {
    if (!path || !image)
        return false;
    memset(image, 0, sizeof(*image));
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    struct stat source_stat; uint8_t source[32];
    if (fstat(fileno(f), &source_stat) || !S_ISREG(source_stat.st_mode) || source_stat.st_size < 67 ||
        source_stat.st_size > IMAGE_MAX_BYTES || !source_digest(f, source)) { fclose(f); return false; }
    uint32_t key = crc32(0, (const Bytef *)path, (uInt)strlen(path));
    char hex[65], destination[512];
    for (unsigned i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", source[i]);
    snprintf(destination, sizeof(destination), CACHE_ROOT "/v2-%08lx-%s.gray", (unsigned long)key, hex);
    if (cached_image(destination, source, image)) { fclose(f); return true; }
    // 不完整缓存不能阻止从原图恢复，也不能占住空间预算。
    // An invalid cache must neither block source reconstruction nor occupy its budget.
    if (unlink(destination) && errno != ENOENT) { fclose(f); return false; }
    uint8_t signature[8];
    bool ok = read_exact(f, signature, 8) && !memcmp(signature, "\x89PNG\r\n\x1a\n", 8);
    uint8_t input[2048], row[344], previous[342] = {0};
    size_t stride = 0, fill = 0;
    uint32_t rows = 0;
    bool header = false, started = false, ended = false, done = false, idat_closed = false, saw_idat = false;
    z_stream z = {0};
    FILE *cache = NULL;
    uint32_t raw_crc = crc32(0, Z_NULL, 0);
    char temporary[512] = {0};
    (void)mkdir("/sdcard/markdown/.cache", 0700);
    // 临时文件由 source 路径命名；只在应用串行的打开/加载回调调用。
    // Source-derived temporary names are used only by serialized app open/load callbacks.
    snprintf(temporary, sizeof(temporary), "/sdcard/markdown/.cache/%08lx.part", (unsigned long)key);
    while (ok && !done) {
        uint8_t h[8];
        if (!read_exact(f, h, 8)) {
            ok = false;
            break;
        }
        uint32_t count = be32(h);
        if (count > INT32_MAX) {
            ok = false;
            break;
        }
        bool ihdr = !memcmp(h + 4, "IHDR", 4), idat = !memcmp(h + 4, "IDAT", 4),
             iend = !memcmp(h + 4, "IEND", 4);
        if (!header && !ihdr) {
            ok = false;
            break;
        }
        if (ihdr && (header || count != 13)) {
            ok = false;
            break;
        }
        if (iend && count) {
            ok = false;
            break;
        }
        if (idat && idat_closed) {
            ok = false;
            break;
        }
        if (!idat && saw_idat)
            idat_closed = true;
        if (idat)
            saw_idat = true;
        if (!ihdr && !idat && !iend && !(h[4] & 32)) {
            ok = false;
            break;
        }
        uint32_t crc = crc32(0, h + 4, 4);
        uint32_t remaining = count;
        while (ok && remaining) {
            size_t n = remaining > sizeof(input) ? sizeof(input) : remaining;
            if (!read_exact(f, input, n)) {
                ok = false;
                break;
            }
            remaining -= (uint32_t)n;
            crc = crc32(crc, input, (uInt)n);
            if (ihdr) {
                image->width = be32(input);
                image->height = be32(input + 4);
                if (!image->width || image->width > 684 || !image->height ||
                    (uint64_t)image->width * image->height > IMAGE_MAX_PIXELS ||
                    input[8] != 4 || input[9] != 0 || input[10] || input[11] || input[12]) {
                    ok = false;
                    break;
                }
                stride = (image->width + 1) / 2;
                header = true;
                if (!cache_room(CACHE_HEADER + (uint64_t)stride * image->height, destination)) { ok = false; break; }
                cache = fopen(temporary, "wb");
                uint8_t blank[CACHE_HEADER] = {0};
                if (!cache || fwrite(blank, 1, sizeof(blank), cache) != sizeof(blank) || inflateInit(&z) != Z_OK) {
                    ok = false;
                    break;
                }
                started = true;
#ifdef PICO_HOST_SIMULATOR
                s_decode_count++;
#endif
            } else if (idat) {
                if (ended) {
                    ok = false;
                    break;
                }
                z.next_in = input;
                z.avail_in = (uInt)n;
                while (ok && z.avail_in) {
                    z.next_out = row + fill;
                    z.avail_out = (uInt)(stride + 1 - fill);
                    uInt before = z.avail_in;
                    uLong old_output = z.total_out;
                    int status = inflate(&z, Z_NO_FLUSH);
                    fill = stride + 1 - z.avail_out;
                    if (status != Z_OK && status != Z_STREAM_END) {
                        ok = false;
                        break;
                    }
                    if (fill == stride + 1) {
                        if (rows >= image->height || row[0] > 4) {
                            ok = false;
                            break;
                        }
                        for (size_t x = 0; x < stride; x++) {
                            unsigned a = x ? row[x] : 0, b = previous[x], c = x ? previous[x - 1] : 0;
                            unsigned predictor = row[0] == 1   ? a
                                                 : row[0] == 2 ? b
                                                 : row[0] == 3 ? (a + b) / 2
                                                 : row[0] == 4 ? paeth(a, b, c)
                                                               : 0;
                            row[x + 1] = (uint8_t)(row[x + 1] + predictor);
                        }
                        if (fwrite(row + 1, 1, stride, cache) != stride) {
                            ok = false;
                            break;
                        }
                        raw_crc = crc32(raw_crc, row + 1, (uInt)stride);
                        memcpy(previous, row + 1, stride);
                        rows++;
                        fill = 0;
                    }
                    if (status == Z_STREAM_END) {
                        ended = true;
                        if (z.avail_in)
                            ok = false;
                        break;
                    }
                    if (before == z.avail_in && old_output == z.total_out) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        uint8_t sum[4];
        if (!ok || !read_exact(f, sum, 4) || crc != be32(sum)) {
            ok = false;
            break;
        }
        if (iend) {
            done = true;
            ok = header && ended && rows == image->height && !fill;
        }
    }
    if (started)
        inflateEnd(&z);
    if (ok && fgetc(f) != EOF)
        ok = false;
    uint8_t current[32];
    if (ok && (!source_digest(f, current) || memcmp(source, current, 32))) ok = false;
    fclose(f);
    if (cache) {
        uint8_t record[CACHE_HEADER] = {0};
        memcpy(record, "MDGRAY2", 8); memcpy(record + 8, source, 32);
        put32(record + 40, image->width); put32(record + 44, image->height); put32(record + 48, raw_crc);
        put32(record + 52, crc32(0, record, 52));
        if (ok && (fseek(cache, 0, SEEK_SET) || fwrite(record, 1, sizeof(record), cache) != sizeof(record) ||
                   fflush(cache) || fsync(fileno(cache)))) ok = false;
        if (fclose(cache)) ok = false;
    }
    if (ok) {
        snprintf(image->cache, sizeof(image->cache), "%s", destination);
        // FatFS不允许rename覆盖；缓存可丢弃，源图始终保留用于重建。
        // FatFS cannot rename over a file; discard only cache, retaining its source for reconstruction.
        if (unlink(destination) && errno != ENOENT) ok = false;
        if (ok) ok = rename(temporary, image->cache) == 0;
    }
    if (!ok) {
        unlink(temporary);
        memset(image, 0, sizeof(*image));
    }
    return ok;
}
bool document_image_sample(const document_image_t *image, uint32_t top, uint32_t span, unsigned width,
                           unsigned height, uint8_t *gray) {
    if (!image || !image->width || !image->height || !gray || !width || !height || width > 684 ||
        height > 1216 || top >= image->height || !span)
        return false;
    if (span > image->height - top)
        span = image->height - top;
    FILE *f = fopen(image->cache, "rb");
    if (!f)
        return false;
    uint8_t row[342];
    size_t stride = (image->width + 1) / 2;
    uint32_t last = UINT32_MAX;
    bool ok = true;
    for (unsigned y = 0; y < height; y++) {
        uint32_t source = top + (uint32_t)((uint64_t)y * span / height);
        if (source != last) {
            uint64_t offset = CACHE_HEADER + (uint64_t)source * stride;
            if (offset > LONG_MAX || fseek(f, (long)offset, SEEK_SET) || !read_exact(f, row, stride)) {
                ok = false;
                break;
            }
            last = source;
        }
        for (unsigned x = 0; x < width; x++) {
            uint32_t sx = (uint64_t)x * image->width / width;
            gray[(size_t)y * width + x] = (uint8_t)(((sx & 1) ? row[sx / 2] & 15 : row[sx / 2] >> 4) * 17);
        }
    }
    fclose(f);
    return ok;
}

void document_image_remove_cache(const char *path) {
    if (!path)
        return;
    uint32_t key = crc32(0, (const Bytef *)path, (uInt)strlen(path));
    char cache[512];
    snprintf(cache, sizeof(cache), "/sdcard/markdown/.cache/%08lx.gray", (unsigned long)key);
    (void)unlink(cache);
    snprintf(cache, sizeof(cache), "/sdcard/markdown/.cache/%08lx.part", (unsigned long)key);
    (void)unlink(cache);
    DIR *d = opendir(CACHE_ROOT);
    if (!d) return;
    char prefix[20]; snprintf(prefix, sizeof(prefix), "v2-%08lx-", (unsigned long)key);
    struct dirent *de;
    while ((de = readdir(d))) if (!strncmp(de->d_name, prefix, strlen(prefix))) {
        if (snprintf(cache, sizeof(cache), CACHE_ROOT "/%s", de->d_name) < (int)sizeof(cache)) (void)unlink(cache);
    }
    closedir(d);
}
