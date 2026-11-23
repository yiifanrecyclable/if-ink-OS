/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：给 C 代码用的最小包装。jpeg.inl 里的 JPEG_* 函数族被
 * `__MACH__ / __LINUX__ / __MCUXPRESSO / _WIN64` 保护，ESP_PLATFORM 不在其中，
 * 所以 ESP-IDF 上只有 C++ 的 JPEGDEC 类可用；这里用 extern "C" 把它包出来。
 *
 * English: A minimal wrapper for C callers. The JPEG_* functions in jpeg.inl sit behind
 * __MACH__ / __LINUX__ / __MCUXPRESSO / _WIN64 and ESP_PLATFORM is not among them, so under
 * ESP-IDF only the C++ JPEGDEC class exists; this exposes it with C linkage.
 */
#include <new>

#include "JPEGDEC.h"

extern "C" {

// 解成 8 位灰度；渐进式沿用 1/8 模式，普通灰度 JPEG 可以按完整尺寸解码。
// Decode to 8-bit grayscale. Progressive frames use 1/8; baseline grayscale can decode at full size.
//
// 返回 0 表示打开或解码失败。/ Returns 0 when opening or decoding fails.
int jpegdec_gray_decode(const uint8_t *data, int size, JPEG_DRAW_CALLBACK *draw, void *user,
                        bool reduced) {
    if (!data || size <= 0 || !draw) return 0;
    JPEGDEC *jpeg = new (std::nothrow) JPEGDEC();
    if (!jpeg) return 0;
    int ok = 0;
    if (jpeg->openRAM(const_cast<uint8_t *>(data), size, draw)) {
        jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
        jpeg->setUserPointer(user);
        ok = jpeg->decode(0, 0, reduced ? JPEG_SCALE_EIGHTH : 0) != 0;
    }
    jpeg->close();
    delete jpeg;
    return ok;
}

}  // extern "C"
