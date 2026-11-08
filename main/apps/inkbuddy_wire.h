/* SPDX-License-Identifier: Apache-2.0
 * 中文：本地测试版的 inkbuddy v1 帧与网络契约。
 * English: inkbuddy v1 wire contract for local test builds.
 */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { INKBUDDY_WIDTH = 1216, INKBUDDY_HEIGHT = 684, INKBUDDY_FRAME_BYTES = 415872,
       INKBUDDY_HEADER_BYTES = 20, INKBUDDY_ENDPOINT_BYTES = 24, INKBUDDY_PEERS = 3 };

typedef enum {
    INKBUDDY_FRAME, INKBUDDY_UNCHANGED, INKBUDDY_OFFLINE, INKBUDDY_SERVER,
    INKBUDDY_BAD_FRAME, INKBUDDY_FIRMWARE_OLD, INKBUDDY_DESKTOP_OLD, INKBUDDY_PANEL,
    INKBUDDY_CANCELLED
} inkbuddy_result_t;

typedef struct {
    uint32_t revision;
    uint16_t minute;
    uint8_t flags;
} inkbuddy_frame_t;

typedef struct {
    char endpoint[INKBUDDY_ENDPOINT_BYTES];
    char device[7];
    uint32_t epoch, revision;
    unsigned battery, status;
} inkbuddy_request_t;

bool inkbuddy_endpoint(const char *text, uint32_t *ipv4, uint16_t *port);
uint32_t inkbuddy_crc(const uint8_t *pixels, size_t bytes);
inkbuddy_result_t inkbuddy_decode(const uint8_t *header, const uint8_t *pixels, inkbuddy_frame_t *frame);
inkbuddy_result_t inkbuddy_fetch(const inkbuddy_request_t *request, const atomic_bool *cancel,
                               uint8_t *pixels, uint32_t *epoch, inkbuddy_frame_t *frame, int64_t *received_us);
unsigned inkbuddy_discover(const atomic_bool *cancel, char peers[INKBUDDY_PEERS][INKBUDDY_ENDPOINT_BYTES]);
