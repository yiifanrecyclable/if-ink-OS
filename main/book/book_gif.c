/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：解码 GIF 首帧，逐行采样到阅读器的目标灰阶缓冲。
 * English: Decode the first GIF frame and sample each row into the reader's grayscale buffer.
 */
#include "book_gif.h"
#include "book_image_header.h"
#include "esp_heap_caps.h"
#include <stdlib.h>

typedef struct {
    const uint8_t *at, *end;
    unsigned left, bits;
    uint32_t word;
} gif_stream_t;

typedef struct {
    unsigned canvas_width, canvas_height;
    unsigned left, top, width, height;
    unsigned palette_size;
    uint8_t gray[256];
    bool interlaced;
} gif_frame_t;

enum { GIF_CODES = 4096 };

typedef struct {
    uint16_t prefix[GIF_CODES];
    uint8_t suffix[GIF_CODES];
    uint8_t stack[GIF_CODES];
    uint8_t row[];
} gif_workspace_t;

static bool skip_subblocks(gif_stream_t *stream) {
    while (stream->at < stream->end) {
        unsigned count = *stream->at++;
        if (!count) return true;
        if (stream->end - stream->at < count) return false;
        stream->at += count;
    }
    return false;
}

static int read_code(gif_stream_t *stream, unsigned width) {
    while (stream->bits < width) {
        if (!stream->left) {
            if (stream->at == stream->end) return -1;
            stream->left = *stream->at++;
            if (!stream->left || stream->end - stream->at < stream->left) return -1;
        }
        stream->word |= (uint32_t)*stream->at++ << stream->bits;
        --stream->left;
        stream->bits += 8;
    }
    int code = stream->word & ((1u << width) - 1u);
    stream->word >>= width;
    stream->bits -= width;
    return code;
}

static bool read_palette(gif_stream_t *stream, gif_frame_t *frame, unsigned packed) {
    unsigned count = 2u << (packed & 7u);
    if (stream->end - stream->at < count * 3u) return false;
    frame->palette_size = count;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *rgb = stream->at + 3 * i;
        frame->gray[i] = (77u * rgb[0] + 150u * rgb[1] + 29u * rgb[2]) >> 8;
    }
    stream->at += count * 3u;
    return true;
}

static bool read_frame(gif_stream_t *stream, gif_frame_t *frame, unsigned packed,
                       unsigned background_index, uint8_t *background) {
    if ((packed & 0x80u) && !read_palette(stream, frame, packed)) return false;
    *background = background_index < frame->palette_size ? frame->gray[background_index] : 255;
    int transparent = -1;
    while (stream->at < stream->end && *stream->at == 0x21) {
        if (stream->end - stream->at < 3) return false;
        unsigned label = stream->at[1];
        stream->at += 2;
        if (label == 0xf9) {
            if (stream->end - stream->at < 6 || stream->at[0] != 4 || stream->at[5] != 0)
                return false;
            transparent = stream->at[1] & 1u ? stream->at[4] : -1;
        } else if (label == 0x01) {
            // 纯文本块消耗其前置图形控制扩展。/ A plain-text block consumes its graphic control extension.
            transparent = -1;
        }
        if (!skip_subblocks(stream)) return false;
    }
    if (stream->end - stream->at < 10 || *stream->at++ != 0x2c) return false;
    const uint8_t *p = stream->at;
    frame->left = p[0] | (unsigned)p[1] << 8;
    frame->top = p[2] | (unsigned)p[3] << 8;
    frame->width = p[4] | (unsigned)p[5] << 8;
    frame->height = p[6] | (unsigned)p[7] << 8;
    if (!frame->width || !frame->height ||
        frame->left + frame->width > frame->canvas_width ||
        frame->top + frame->height > frame->canvas_height) return false;
    packed = p[8];
    frame->interlaced = (packed & 0x40u) != 0;
    stream->at += 9;
    if ((packed & 0x80u) && !read_palette(stream, frame, packed)) return false;
    if (transparent >= 0 && (unsigned)transparent < frame->palette_size) {
        *background = 255;
        frame->gray[transparent] = 255;
    }
    return frame->palette_size != 0;
}

static void sample_row(const gif_frame_t *frame, const uint8_t *row, unsigned y,
                       unsigned out_width, unsigned out_height, uint8_t *out) {
    unsigned start = ((frame->top + y) * out_height + frame->canvas_height - 1) / frame->canvas_height;
    unsigned end = ((frame->top + y + 1) * out_height + frame->canvas_height - 1) / frame->canvas_height;
    if (start == end) return;
    uint8_t *dst = out + (size_t)start * out_width;
    for (unsigned x = 0; x < out_width; ++x) {
        unsigned sx = x * frame->canvas_width / out_width;
        if (sx >= frame->left && sx - frame->left < frame->width) dst[x] = row[sx - frame->left];
    }
    for (unsigned i = start + 1; i < end; ++i) memcpy(out + (size_t)i * out_width, dst, out_width);
}

static bool decode_rows(gif_stream_t *stream, const gif_frame_t *frame, gif_workspace_t *ws,
                        unsigned min_bits, unsigned out_width, unsigned out_height, uint8_t *out) {
    static const unsigned starts[] = {0, 4, 2, 1};
    static const unsigned steps[] = {8, 8, 4, 2};
    const unsigned clear = 1u << min_bits;
    const unsigned end = clear + 1;
    unsigned bits = min_bits + 1, next = end + 1;
    unsigned x = 0, y = 0, pass = 0, rows = 0;
    int previous = -1;
    uint8_t first = 0;
    for (int value; (value = read_code(stream, bits)) >= 0;) {
        unsigned code = (unsigned)value;
        if (code == clear) {
            bits = min_bits + 1;
            next = end + 1;
            previous = -1;
            continue;
        }
        if (code == end) {
            stream->at += stream->left;
            return rows == frame->height && skip_subblocks(stream);
        }
        if (code > next || (code == next && previous < 0)) return false;
        unsigned index = code, depth = 0;
        if (index == next) {
            // 编码器已经用了即将加入的条目。/ The encoder used the entry the decoder is about to add.
            ws->stack[depth++] = first;
            index = (unsigned)previous;
        }
        while (index > end) {
            if (depth == GIF_CODES || ws->prefix[index] >= index) return false;
            ws->stack[depth++] = ws->suffix[index];
            index = ws->prefix[index];
        }
        first = (uint8_t)index;
        if (previous >= 0 && next < GIF_CODES) {
            ws->prefix[next] = (uint16_t)previous;
            ws->suffix[next++] = first;
            if (next == (1u << bits) && bits < 12) ++bits;
        }
        previous = value;
        for (unsigned pixel = first;; pixel = ws->stack[--depth]) {
            if (rows == frame->height || pixel >= frame->palette_size) return false;
            ws->row[x++] = frame->gray[pixel];
            if (x == frame->width) {
                sample_row(frame, ws->row, y, out_width, out_height, out);
                x = 0;
                ++rows;
                if (!frame->interlaced) ++y;
                else {
                    y += steps[pass];
                    while (y >= frame->height && pass < 3) y = starts[++pass];
                }
            }
            if (!depth) break;
        }
    }
    return false;
}

bool book_gif_gray(const uint8_t *data, size_t size,
                   unsigned out_width, unsigned out_height, uint8_t *out) {
    gif_frame_t frame = {0};
    if (!out || !out_width || !out_height || out_width > 1216 || out_height > 1216 ||
        (size_t)out_width * out_height > 684u * 1216u ||
        !book_gif_frame(data, size, &frame.canvas_width, &frame.canvas_height)) return false;
    gif_stream_t stream = {.at = data + 13, .end = data + size};
    uint8_t background;
    if (!read_frame(&stream, &frame, data[10], data[11], &background) || stream.at == stream.end)
        return false;
    unsigned min_bits = *stream.at++;
    if (min_bits < 2 || min_bits > 8) return false;
    gif_workspace_t *ws = heap_caps_malloc(sizeof(*ws) + frame.width, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ws) return false;
    memset(out, background, (size_t)out_width * out_height);
    bool ok = decode_rows(&stream, &frame, ws, min_bits, out_width, out_height, out);
    free(ws);
    return ok;
}
