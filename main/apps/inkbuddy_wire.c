/* SPDX-License-Identifier: Apache-2.0
 * 中文：独占非阻塞 socket，响应上限与截止时间固定，取消无需 UI 等待。
 * English: Own nonblocking sockets with fixed response limits and deadlines; cancellation never joins from the UI.
 */
#include "inkbuddy_wire.h"
#include "cJSON.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_crc.h"
#include "esp_timer.h"
#else
#include <time.h>
#include <zlib.h>
#endif

static int64_t now_us(void) {
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
#endif
}

bool inkbuddy_endpoint(const char *text, uint32_t *ipv4, uint16_t *port) {
    if (!text || !ipv4 || !port) return false;
    const char *colon = strchr(text, ':');
    size_t length = colon ? (size_t)(colon - text) : strlen(text);
    if (!length || length >= INET_ADDRSTRLEN) return false;
    char address[INET_ADDRSTRLEN];
    memcpy(address, text, length);
    address[length] = 0;
    struct in_addr ip;
    if (inet_pton(AF_INET, address, &ip) != 1 || ip.s_addr == 0 || (ntohl(ip.s_addr) >> 24) >= 224) return false;
    unsigned number = 8791;
    if (colon) {
        number = 0;
        if (!colon[1]) return false;
        for (const char *p = colon + 1; *p; ++p) {
            if (*p < '0' || *p > '9') return false;
            number = number * 10 + (unsigned)(*p - '0');
            if (number > 65535) return false;
        }
        if (!number) return false;
    }
    *ipv4 = ip.s_addr;
    *port = (uint16_t)number;
    return true;
}

uint32_t inkbuddy_crc(const uint8_t *pixels, size_t bytes) {
#ifdef ESP_PLATFORM
    return esp_crc32_le(0, pixels, bytes);
#else
    return (uint32_t)crc32(0, pixels, bytes);
#endif
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

inkbuddy_result_t inkbuddy_decode(const uint8_t *h, const uint8_t *pixels, inkbuddy_frame_t *frame) {
    if (memcmp(h, "INK1", 4) || h[4] != 1 || h[5] != 1 || (h[6] & ~1u) ||
        h[7] || h[18] || h[19] || !le32(h + 8)) return INKBUDDY_BAD_FRAME;
    uint16_t minute = h[16] | (uint16_t)h[17] << 8;
    if ((minute >= 1440 && minute != UINT16_MAX) ||
        le32(h + 12) != inkbuddy_crc(pixels, INKBUDDY_FRAME_BYTES)) return INKBUDDY_BAD_FRAME;
    *frame = (inkbuddy_frame_t){.revision = le32(h + 8), .minute = minute, .flags = h[6]};
    return INKBUDDY_FRAME;
}

static bool await_socket(int fd, bool writing, int64_t deadline, const atomic_bool *cancel) {
    while (!atomic_load(cancel)) {
        int64_t left = deadline - now_us();
        if (left <= 0) break;
        struct timeval delay = {.tv_usec = left < 100000 ? left : 100000};
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(fd, &ready);
        int count = select(fd + 1, writing ? NULL : &ready, writing ? &ready : NULL, NULL, &delay);
        if (count > 0) return !atomic_load(cancel);
        if (count < 0 && errno != EINTR) break;
    }
    return false;
}

static int open_socket(int type) {
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) return -1;
    if (fd >= FD_SETSIZE || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool connect_socket(int fd, const struct sockaddr_in *peer, int64_t deadline, const atomic_bool *cancel) {
    if (!connect(fd, (const struct sockaddr *)peer, sizeof(*peer))) return true;
    if (errno != EINPROGRESS || !await_socket(fd, true, deadline, cancel)) return false;
    int error = 0;
    socklen_t length = sizeof(error);
    return !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) && !error;
}

static bool send_request(int fd, const char *data, int64_t deadline, const atomic_bool *cancel) {
    size_t left = strlen(data);
    while (left && await_socket(fd, true, deadline, cancel)) {
        ssize_t n = send(fd, data, left, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if (n <= 0) break;
        left -= (size_t)n;
        data += n;
    }
    return !left;
}

static ssize_t receive(int fd, void *buf, size_t bytes, int64_t deadline, const atomic_bool *cancel) {
    int64_t limit = now_us() + 10000000;
    if (deadline < limit) limit = deadline;
    while (await_socket(fd, false, limit, cancel)) {
        ssize_t n = recv(fd, buf, bytes, 0);
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) return n;
    }
    return -1;
}

static int parse_header(char *header, size_t *length) {
    if (strncmp(header, "HTTP/1.0 ", 9) && strncmp(header, "HTTP/1.1 ", 9)) return 0;
    if (strlen(header) < 13 || header[12] != ' ' || !isdigit((unsigned char)header[9]) ||
        !isdigit((unsigned char)header[10]) || !isdigit((unsigned char)header[11])) return 0;
    int status = atoi(header + 9);
    *length = SIZE_MAX;
    char *line = strstr(header, "\r\n");
    while (line && line[2]) {
        line += 2;
        char *end = strstr(line, "\r\n");
        if (end) *end = 0;
        if (isspace((unsigned char)*line) || !strchr(line, ':')) return 0;
        if (!strncasecmp(line, "Transfer-Encoding:", 18) || !strncasecmp(line, "Content-Encoding:", 17)) return 0;
        if (!strncasecmp(line, "Content-Length:", 15)) {
            if (*length != SIZE_MAX) return 0;
            char *value = line + 15;
            while (*value == ' ' || *value == '\t') ++value;
            if (!isdigit((unsigned char)*value)) return 0;
            char *after;
            unsigned long number = strtoul(value, &after, 10);
            if (*after || number > INKBUDDY_FRAME_BYTES + INKBUDDY_HEADER_BYTES) return 0;
            *length = number;
        }
        line = end;
    }
    return status;
}

static inkbuddy_result_t http_get(const char *endpoint, const char *path, const atomic_bool *cancel,
                                  uint8_t *head, size_t head_capacity, uint8_t *payload,
                                  size_t payload_bytes, size_t *body_bytes, int64_t *received_us) {
    struct sockaddr_in peer = {.sin_family = AF_INET};
    uint16_t port;
    if (!inkbuddy_endpoint(endpoint, &peer.sin_addr.s_addr, &port)) return INKBUDDY_SERVER;
    peer.sin_port = htons(port);
    int fd = open_socket(SOCK_STREAM);
    if (fd < 0) return INKBUDDY_OFFLINE;
    inkbuddy_result_t result = INKBUDDY_OFFLINE;
    int64_t begin = now_us(), deadline = begin + 20000000;
    char request[256];
    snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, endpoint);
    if (!connect_socket(fd, &peer, begin + 5000000, cancel) || !send_request(fd, request, deadline, cancel)) goto done;

    char header[2048];
    size_t used = 0, size;
    char *end = NULL;
    while (used < sizeof(header) - 1 && !end) {
        ssize_t n = receive(fd, header + used, sizeof(header) - 1 - used, deadline, cancel);
        if (n <= 0) goto done;
        used += (size_t)n;
        header[used] = 0;
        end = strstr(header, "\r\n\r\n");
    }
    result = payload ? INKBUDDY_BAD_FRAME : INKBUDDY_SERVER;
    if (!end) goto done;
    char *body = end + 4;
    size_t buffered = used - (size_t)(body - header);
    *end = 0;
    int status = parse_header(header, &size);
    if (status == 304) {
        if (payload && !buffered && (size == SIZE_MAX || !size)) result = INKBUDDY_UNCHANGED;
        goto done;
    }
    if (status != 200) {
        if (status) result = INKBUDDY_SERVER;
        goto done;
    }
    if (buffered > size || (payload ? size != head_capacity + payload_bytes : size >= head_capacity)) goto done;
    size_t head_bytes = payload ? head_capacity : size;
    size_t copied = buffered < head_bytes ? buffered : head_bytes;
    memcpy(head, body, copied);
    if (payload && buffered > copied) memcpy(payload, body + copied, buffered - copied);
    while (buffered < size) {
        uint8_t *dst = buffered < head_bytes ? head + buffered : payload + buffered - head_bytes;
        size_t left = (buffered < head_bytes ? head_bytes : size) - buffered;
        ssize_t n = receive(fd, dst, left, deadline, cancel);
        if (n <= 0) goto done;
        buffered += (size_t)n;
    }
    if (received_us) *received_us = now_us();
    if (!payload) head[size] = 0;
    *body_bytes = size;
    result = INKBUDDY_FRAME;
done:
    close(fd);
    return atomic_load(cancel) ? INKBUDDY_CANCELLED : result;
}

static bool json_number(const cJSON *object, const char *key, uint32_t *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0 && item->valuedouble <= UINT32_MAX)) return false;
    *out = (uint32_t)item->valuedouble;
    return (double)*out == item->valuedouble;
}

static bool json_text(const cJSON *object, const char *key, const char *expected) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && !strcmp(item->valuestring, expected);
}

static cJSON *parse_json(const char *text, size_t bytes) {
    // 局域网响应也不可信；限制 cJSON 递归深度。/ Bound cJSON recursion for untrusted LAN replies.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < bytes; ++i) {
        char c = text[i];
        if (!c) return NULL;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 8) return NULL;
        } else if (c == '}' || c == ']') {
            if (!depth) return NULL;
            --depth;
        }
    }
    if (depth || quoted) return NULL;
    return cJSON_ParseWithLengthOpts(text, bytes + 1, NULL, true);
}

static inkbuddy_result_t handshake(const char *text, size_t bytes, uint32_t *epoch) {
    cJSON *hello = parse_json(text, bytes);
    const cJSON *protocol = cJSON_GetObjectItemCaseSensitive(hello, "proto");
    const cJSON *panel = cJSON_GetObjectItemCaseSensitive(hello, "panel");
    uint32_t min, max, w, h, size;
    inkbuddy_result_t result = INKBUDDY_SERVER;
    if (!json_text(hello, "svc", "inkbuddy") || !json_number(protocol, "min", &min) ||
        !json_number(protocol, "max", &max) || min > max ||
        !json_number(hello, "epoch", epoch) || !*epoch) goto done;
    if (min > 1) result = INKBUDDY_FIRMWARE_OLD;
    else if (max < 1) result = INKBUDDY_DESKTOP_OLD;
    else if (!json_number(panel, "w", &w) || !json_number(panel, "h", &h) ||
             !json_number(panel, "fb_bytes", &size) || w != INKBUDDY_WIDTH || h != INKBUDDY_HEIGHT ||
             size != INKBUDDY_FRAME_BYTES || !json_text(panel, "nibble", "lo-first")) result = INKBUDDY_PANEL;
    else result = INKBUDDY_FRAME;
done:
    cJSON_Delete(hello);
    return result;
}

inkbuddy_result_t inkbuddy_fetch(const inkbuddy_request_t *r, const atomic_bool *cancel,
                               uint8_t *pixels, uint32_t *epoch, inkbuddy_frame_t *frame, int64_t *received_us) {
    uint8_t hello[1024];
    size_t bytes = 0;
    inkbuddy_result_t result = http_get(r->endpoint, "/hello", cancel, hello, sizeof(hello), NULL, 0, &bytes, NULL);
    if (result != INKBUDDY_FRAME) return result;
    result = handshake((const char *)hello, bytes, epoch);
    if (result != INKBUDDY_FRAME) return result;
    uint32_t revision = *epoch == r->epoch ? r->revision : 0;
    char path[128];
    snprintf(path, sizeof(path), "/frame?v=1&dev=%s&rev=%lu&epoch=%lu&bat=%u&st=%u", r->device,
             (unsigned long)revision, (unsigned long)*epoch, r->battery, r->status);
    uint8_t header[INKBUDDY_HEADER_BYTES];
    result = http_get(r->endpoint, path, cancel, header, sizeof(header), pixels, INKBUDDY_FRAME_BYTES, &bytes, received_us);
    if (result == INKBUDDY_UNCHANGED && !revision) return INKBUDDY_BAD_FRAME;
    if (result != INKBUDDY_FRAME) return result;
    return inkbuddy_decode(header, pixels, frame);
}

unsigned inkbuddy_discover(const atomic_bool *cancel, char peers[INKBUDDY_PEERS][INKBUDDY_ENDPOINT_BYTES]) {
    int fd = open_socket(SOCK_DGRAM);
    if (fd < 0) return 0;
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(8792)};
    unsigned count = 0;
    if (bind(fd, (const struct sockaddr *)&local, sizeof(local))) {
        close(fd);
        return 0;
    }
    int64_t deadline = now_us() + 3000000;
    while (count < INKBUDDY_PEERS && await_socket(fd, false, deadline, cancel)) {
        struct sockaddr_in peer;
        socklen_t length = sizeof(peer);
        char buf[257], address[INET_ADDRSTRLEN];
        ssize_t bytes = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &length);
        if (bytes <= 0 || bytes > 256 || !inet_ntop(AF_INET, &peer.sin_addr, address, sizeof(address))) continue;
        buf[bytes] = 0;
        cJSON *ad = parse_json(buf, (size_t)bytes);
        uint32_t port, version;
        bool ok = json_text(ad, "svc", "inkbuddy") && json_number(ad, "ver", &version) && version == 1 &&
                  json_number(ad, "port", &port) && port > 0 && port <= 65535;
        cJSON_Delete(ad);
        if (!ok) continue;
        char endpoint[INKBUDDY_ENDPOINT_BYTES];
        snprintf(endpoint, sizeof(endpoint), "%s:%u", address, (unsigned)port);
        bool known = false;
        for (unsigned i = 0; i < count; ++i) known |= !strcmp(endpoint, peers[i]);
        if (!known) strcpy(peers[count++], endpoint);
    }
    close(fd);
    return count;
}
