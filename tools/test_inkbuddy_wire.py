#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real wire client against a bounded loopback HTTP fixture."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import parse_qs, urlsplit
import zlib

ROOT = Path(__file__).resolve().parents[1]
FRAME_BYTES = 415872
WHITE = b"\xff" * FRAME_BYTES
CRC = zlib.crc32(WHITE)
CLIENT = r'''
#include "inkbuddy_wire.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_bool cancel;

static void *cancel_later(void *unused) {
    (void)unused;
    usleep(220000);
    atomic_store(&cancel, true);
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 5);
    uint8_t *pixels = malloc(INKBUDDY_FRAME_BYTES);
    assert(pixels);
    memset(pixels, 255, INKBUDDY_FRAME_BYTES);
    assert(inkbuddy_crc(pixels, 0) == 0 && inkbuddy_crc((const uint8_t*)"123456789", 9) == 0xcbf43926u);
    assert(inkbuddy_crc(pixels, INKBUDDY_FRAME_BYTES) == 0xf6dc332fu);
    uint32_t ip;
    uint16_t port;
    assert(inkbuddy_endpoint("192.168.31.101", &ip, &port) && port == 8791);
    assert(!inkbuddy_endpoint("1.2.3.4:65536", &ip, &port));
    assert(!inkbuddy_endpoint("1.2.3.4:80/path", &ip, &port));
    inkbuddy_request_t r = {.device="123456", .battery=88, .status=0};
    snprintf(r.endpoint, sizeof(r.endpoint), "%s", argv[1]);
    r.epoch = (uint32_t)strtoul(argv[2], NULL, 10);
    r.revision = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t epoch = 0;
    inkbuddy_frame_t frame = {0};
    int64_t received_us = 0;
    bool cancel_mode = !strcmp(argv[4], "cancel");
    pthread_t t;
    if (cancel_mode) assert(!pthread_create(&t, NULL, cancel_later, NULL));
    inkbuddy_result_t result = inkbuddy_fetch(&r, &cancel, pixels, &epoch, &frame, &received_us);
    if (cancel_mode) pthread_join(t, NULL);
    printf("%d %u %u %u %u %08x\n", result, epoch, frame.revision, frame.minute, frame.flags,
           inkbuddy_crc(pixels, INKBUDDY_FRAME_BYTES));
    free(pixels);
}
'''


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        route = urlsplit(self.path)
        if route.path == "/hello":
            if self.server.mode == "deep-json":
                self.reply(200, b"[" * 500 + b"]" * 500)
                return
            limits = {"min": 1, "max": 1}
            if self.server.mode == "firmware-old":
                limits = {"min": 2, "max": 2}
            elif self.server.mode == "desktop-old":
                limits = {"min": 0, "max": 0}
            hello = {"svc": "inkbuddy", "proto": limits, "epoch": self.server.epoch,
                     "panel": {"w": 1216, "h": 684, "fb_bytes": FRAME_BYTES, "nibble": "lo-first"}}
            if self.server.mode == "bad-panel":
                hello["panel"]["nibble"] = "hi-first"
            self.reply(200, json.dumps(hello).encode())
            return
        self.server.query = parse_qs(route.query)
        if self.server.mode == "stall":
            time.sleep(1.6)
            return
        if self.server.mode == "unchanged":
            self.reply(304, b"")
            return
        if self.server.mode == "redirect":
            self.reply(302, b"")
            return
        crc = CRC ^ (self.server.mode == "bad-crc")
        minute = 0xFFFF if self.server.mode == "unknown-time" else 0
        flags = int(self.server.mode == "force")
        header = struct.pack("<4sBBBBIIHH", b"INK1", 1, 1, flags, 0, 1, crc, minute, 0)
        body = header + WHITE
        if self.server.mode == "short":
            self.reply(200, body[:-1], size=len(body))
        elif self.server.mode == "long":
            self.reply(200, body + b"x")
        else:
            self.reply(200, body)

    def reply(self, status, body, size=None):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body) if size is None else size))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def build_client(output):
    source = output.with_suffix(".c")
    source.write_text(CLIENT)
    vendor = ROOT / "tools/simulator/vendor/cjson"
    app = ROOT / "main/apps"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
                    "-I", str(app), "-I", str(vendor), str(source), str(app / "inkbuddy_wire.c"),
                    str(vendor / "cJSON.c"), "-lz", "-lpthread", "-o", str(output)], check=True)


def client(exe, endpoint, epoch=0, revision=0, cancel=False):
    return subprocess.check_output([str(exe), endpoint, str(epoch), str(revision),
                                    "cancel" if cancel else "wait"], text=True, timeout=8).split()


with tempfile.TemporaryDirectory(prefix="inkbuddy-wire-") as temp:
    exe = Path(temp) / "client"
    build_client(exe)
    if len(sys.argv) == 2:
        endpoint = sys.argv[1]
        first = client(exe, endpoint)
        assert first[0] == "0", first
        second = client(exe, endpoint, first[1], first[2])
        assert second[0] in ("0", "1"), second
        print("Live frame: result epoch rev minute flags CRC", *first)
        print("Live repeat: result epoch rev minute flags CRC", *second)
    else:
        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            endpoint = "127.0.0.1:" + str(server.server_address[1])
            server.epoch = 41
            for mode, expected in (("ok", "0"), ("bad-crc", "4"), ("short", "4"), ("long", "4"),
                                   ("redirect", "3"), ("firmware-old", "5"), ("desktop-old", "6"),
                                   ("bad-panel", "7"), ("unknown-time", "0"), ("force", "0"), ("deep-json", "3")):
                server.mode = mode
                result = client(exe, endpoint)
                assert result[0] == expected, (mode, result)
                if mode == "ok":
                    assert result[1:] == ["41", "1", "0", "0", "f6dc332f"]
                    assert server.query == {"v":["1"],"dev":["123456"],"rev":["0"],"epoch":["41"],"bat":["88"],"st":["0"]}
                if mode == "unknown-time":
                    assert result[3] == "65535"
                if mode == "force":
                    assert result[4] == "1"
            server.mode = "unchanged"
            assert client(exe, endpoint, 41, 1)[0] == "1"
            assert client(exe, endpoint, 41, 0)[0] == "4"
            server.epoch = 42
            server.mode = "ok"
            assert client(exe, endpoint, 41, 1)[:3] == ["0", "42", "1"]
            assert server.query["rev"] == ["0"]
            server.mode = "stall"
            start = time.monotonic()
            assert client(exe, endpoint, cancel=True)[0] == "8"
            assert time.monotonic() - start < 1
            server.shutdown()
        print("PASS: CRC vectors, live TCP framing, 304 rules, epoch reset, versions, bad frames and cancellation.")
