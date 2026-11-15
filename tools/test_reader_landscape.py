#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise reader orientation, both touch mappings, columns and resume in isolated storage."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build/simulator/render"

with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="reader-landscape-") as tmp:
    work = Path(tmp)
    card = work / "card"
    sim = work / "build/simulator"
    nvs = sim / "nvs/read_pico"
    for folder in (card / "books", card / "fonts", card / "markdown", nvs):
        folder.mkdir(parents=True)
    (sim / "titles.txt").touch()
    text = "横屏阅读需要保留原文位置。中文标点、数字123和English一并检查。\n" * 300
    (card / "books/Book.txt").write_text(text)
    (card / "markdown/Doc.md").write_text("# 文档\n\n" + text)
    preference = nvs / "book_screen"

    def render(*events, panel=0, reverse=False, document=False, fail_save=False):
        env = {**os.environ, "PICO_SIM_CARD_ROOT": str(card)}
        if reverse:
            env["PICO_SIM_READER_REVERSE"] = "1"
        if fail_save:
            env["PICO_TEST_NVS_SET_FAIL"] = "book_screen"
        path = "/sdcard/markdown/Doc.md" if document else "/sdcard/books/Book.txt"
        args = [str(BINARY), "book-1", path, "-1", "48", "build/simulator/frame.bin",
                "build/simulator/titles.txt", str(panel), "0", "builtin", ";".join(events), ""]
        result = subprocess.run(args, cwd=work, env=env, capture_output=True,
                                text=True, check=True, timeout=30)
        return json.loads(result.stdout), (sim / "frame.bin").read_bytes()

    def tap(x, y):
        return f"t,{x},{y},{x},{y}"

    orientation, columns = tap(342, 1049), tap(546, 1049)
    assert not preference.exists()
    render(columns, panel=3)
    assert not preference.exists()
    render(orientation, panel=3, fail_save=True)
    assert not preference.exists()
    render(tap(530, 500))
    portrait, _ = render()
    anchor = portrait["reader_offset"]
    assert anchor > 0
    landscape, _ = render(orientation, panel=3)
    assert landscape["reader_panel"] == 3 and preference.read_bytes() == b"\x01"
    assert landscape["reader_offset"] == anchor
    def raw(x, y, reverse):
        return (y, 1215 - x) if reverse else (683 - y, x)

    def wide_tap(x, y, reverse=False):
        return tap(*raw(x, y, reverse))

    def scroll(reverse=False):
        x = 900 if reverse else 300
        sx, sy = raw(x, 580, reverse)
        ex, ey = raw(x, 200, reverse)
        return f"u,{sx},{sy},{ex},{ey}"

    def layout_button(x, reverse=False):
        return wide_tap((614 if reverse else 0) + x, 60, reverse)

    dual, _ = render(layout_button(501), panel=3)
    assert dual["reader_offset"] == anchor and preference.read_bytes() == b"\x02"
    assert render()[0]["reader_offset"] == anchor

    # Old installs may still have the handedness key. Direction alone drives the UI.
    hand = nvs / "rd_wide_left"
    for reverse in (False, True):
        hand.write_bytes(bytes([not reverse]))
        near_x, far_x = (125,1090) if reverse else (1090,125)
        upper_key, lower_key = (0,2) if reverse else (2,0)
        before, _ = render(reverse=reverse)
        ignored, _ = render(wide_tap(far_x, 310, reverse), reverse=reverse)
        assert ignored["reader_panel"] == 0 and ignored["reader_offset"] == before["reader_offset"]
        center_tap = wide_tap(near_x, 310, reverse)
        center, frame = render(center_tap, reverse=reverse)
        assert center["reader_panel"] == 1 and center["reader_offset"] == before["reader_offset"]
        tools_origin = 0 if reverse else 614
        layout_tap = wide_tap(tools_origin + 425, 642, reverse)
        layout, layout_frame = render(center_tap, layout_tap, reverse=reverse)
        assert layout["reader_panel"] == 3 and frame != layout_frame
        closed, _ = render(center_tap, layout_tap,
                           wide_tap((614 if reverse else 0)+150, 280, reverse),
                           wide_tap((614 if reverse else 0)+42, 34, reverse), reverse=reverse)
        assert closed["reader_panel"] == 3
        following, _ = render(wide_tap(near_x, 540, reverse), reverse=reverse)
        assert following["reader_offset"] > before["reader_offset"]
        previous, _ = render(wide_tap(near_x, 150, reverse), reverse=reverse)
        assert previous["reader_page"] == before["reader_page"]

        following, _ = render(f"k,{lower_key},0,0,0", reverse=reverse)
        assert following["reader_page"] == previous["reader_page"]+1
        back, _ = render(f"k,{upper_key},0,0,0", reverse=reverse)
        assert back["reader_page"] == previous["reader_page"]
        settings, _ = render(f"k,{lower_key},0,0,0", panel=3, reverse=reverse)
        assert settings["reader_detail_page"] == 1
        settings, _ = render(f"k,{lower_key},0,0,0", f"k,{upper_key},0,0,0", panel=3, reverse=reverse)
        assert settings["reader_detail_page"] == 0
    hand.unlink()

    def pixel(frame, x, y, reverse=False):
        px, py = raw(x, y, reverse)
        return (frame[py * 342 + px // 2] >> (4 * (px % 2))) & 15

    def ink(frame, x0, x1, y0, y1):
        return {(x,y) for y in range(y0,y1) for x in range(x0,x1)
                if pixel(frame,x,y) < 14}

    _, base_status = render()
    name = ink(base_status, 450, 766, 12, 49)
    assert name and abs((min(x for x,_ in name)+max(x for x,_ in name))/2-608) <= 6
    signature = nvs / "status_sig"
    signature.write_bytes(b"Signature\0")
    _, signed = render()
    assert all(pixel(signed,608,y)==5 for y in range(21,39))
    assert pixel(signed,608,20)==pixel(signed,608,39)==15
    left = ink(signed, 400, 608, 12, 49)
    right = ink(signed, 609, 816, 12, 49)
    assert 585 <= max(x for x,_ in left) <= 595
    assert 622 <= min(x for x,_ in right) <= 631
    signature.unlink()

    (nvs / "bk_para").write_bytes(b"\0")
    (nvs / "bk_line").write_bytes(bytes([110]))
    normal, regular_frame = render()
    toggle_y = 394
    fullscreen, _ = render(wide_tap(840, toggle_y), panel=1)
    assert fullscreen["reader_layout"] == 1 and fullscreen["reader_offset"] == normal["reader_offset"]
    full, full_frame = render()
    assert full["pages"] < normal["pages"]
    assert ink(full_frame,36,1180,620,662)
    assert all(pixel(full_frame,x,675) in (0,9) for x in range(205,1010))
    assert pixel(full_frame,161,675)==15

    immersive, _ = render(wide_tap(840, toggle_y), panel=1)
    assert immersive["reader_layout"] == 2 and immersive["reader_offset"] == normal["reader_offset"]
    deep, immersive_frame = render()
    assert not ink(immersive_frame, 0, 36, 0, 56)
    assert deep["pages"] < full["pages"]
    assert ink(immersive_frame,36,1180,620,673)
    assert all(pixel(immersive_frame,x,681)==15 for x in range(1216))

    detail, _ = render(wide_tap(538, 651), panel=3)
    assert detail["reader_detail_page"] == 1
    unchanged, _ = render(wide_tap(538, 651), scroll(), panel=3)
    assert unchanged["reader_detail_page"] == 1
    assert render(wide_tap(538, 651), wide_tap(63, 650), panel=3)[0]["reader_detail_page"] == 0

    # Native controls have a circular thumb in both orientations and inline pagers
    # reach nested lists without making those lists vertically scrollable.
    for reverse in (False, True):
        origin = 0 if reverse else 614
        y = 222
        state, frame = render(panel=1, reverse=reverse)
        center = origin + 36 + 530 * state["reader_progress"] // 100
        for dx, dy in ((12, 0), (0, -12), (0, 12)):
            assert pixel(frame, center + dx, y + dy, reverse) == 4
        for dx, dy in ((13, -4), (0, -13), (0, 13)):
            assert pixel(frame, center + dx, y + dy, reverse) != 4

        detail_x = 614 if reverse else 0
        def detail_tap(x, y):
            return wide_tap(detail_x + x, y, reverse)

        shortcuts = render(detail_tap(250, 120), detail_tap(543, 649), panel=9, reverse=reverse)[0]
        assert shortcuts["reader_panel"] == 12 and shortcuts["reader_detail_page"] == 1
        basics, frame = render(detail_tap(250, 52), panel=9, reverse=reverse)
        assert basics["reader_panel"] == 15
        # The guide and the miniature share the actual key-side strip bounds.
        edge = 405 if reverse else 811
        miniature_edge = detail_x + 62 + (158 if reverse else 478-159)
        assert pixel(frame, miniature_edge, 205, reverse) == 8
        guide_entry = detail_tap(250, 255)
        guide, frame = render(guide_entry, panel=9, reverse=reverse)
        assert guide["reader_panel"] == 14
        assert all(pixel(frame, edge, y, reverse) == 5 for y in range(56,613))
        assert pixel(frame, edge, 612, reverse) in (5,6)
        assert pixel(frame, 56, 638, reverse) == 0
        assert pixel(frame, 116, 638, reverse) == 0
        # The control row is blank on the far right, even over the active
        # right-hand strip in immersive mode.
        assert all(pixel(frame,x,y,reverse) == 15
                   for x in range(560,1216) for y in range(613,684))
        guide, frame = render(guide_entry, wide_tap(116,638,reverse), panel=9, reverse=reverse)
        assert guide["reader_panel"] == 14 and pixel(frame,116,638,reverse) == 15
        assert render(guide_entry, wide_tap(56,638,reverse),
                      panel=9, reverse=reverse)[0]["reader_panel"] == 9
        assert render(guide_entry, "k,1,0,0,0", panel=9, reverse=reverse)[0]["reader_panel"] == 9
        assert render(wide_tap(56,638,reverse), panel=14, reverse=reverse)[0]["reader_panel"] == 0

        start, end = raw(origin + 190, y, reverse), raw(origin + 390, y, reverse)
        events = [f"{kind},{pt[0]},{pt[1]},{pt[0]},{pt[1]}" for kind, pt in
                  (("b", start), ("v", end), ("e", end))]
        held, _ = render(*events[:2], panel=1, reverse=reverse)
        assert held["reader_panel"] == 1 and held["reader_area_updates"] >= 2
        released, _ = render(*events, panel=1, reverse=reverse)
        assert released["reader_panel"] == 0 and 60 <= released["reader_progress"] <= 67
        far_left = raw(origin, y, reverse)
        left_events = (events[0], *(f"{kind},{far_left[0]},{far_left[1]},{far_left[0]},{far_left[1]}"
                                    for kind in ("v", "e")))
        state, _ = render(*left_events, panel=1, reverse=reverse)
        assert state["reader_progress"] == 0

    (nvs / "rd_turn").write_bytes(b"\x01")
    for screen in (1, 2):
        preference.write_bytes(bytes([screen]))
        for reverse in (False, True):
            near_x = 125 if reverse else 1090
            for layout in (0, 1, 2):
                (nvs / "rd_fullscreen").write_bytes(bytes([int(layout > 0)]))
                (nvs / "rd_immersive").write_bytes(bytes([int(layout == 2)]))
                forward, _ = render(wide_tap(near_x, 540, reverse), reverse=reverse)
                assert forward["reader_water_turns"] == 1, forward
                back, _ = render(wide_tap(near_x, 150, reverse), reverse=reverse)
                assert back["reader_water_turns"] == 1, back
            (nvs / "rd_gc16").write_bytes(b"\x05")
            five_turns, _ = render(*[wide_tap(near_x, 540, reverse)] * 5, reverse=reverse)
            assert five_turns["reader_water_turns"] == 4, five_turns
            (nvs / "rd_gc16").unlink()
    (nvs / "rd_turn").unlink()
    assert render(wide_tap(1100, 540))[0]["reader_water_turns"] == 0
    # Regular reading uses this band for progress. In the guide it stays white
    # outside the footer controls and the outlined touch strip.
    (nvs / "rd_fullscreen").write_bytes(b"\0")
    (nvs / "rd_immersive").write_bytes(b"\0")
    assert pixel(render()[1], 600, 638) < 14
    assert pixel(render(panel=14)[1], 600, 638) == 15

    # TOC and bookmarks both use nine rows and fixed footers; a second page
    # selects its own entries rather than reusing the visible indices from page one.
    book = card / "books/Book.txt"
    book.write_text("".join(f"Chapter {chapter:02d}\n" + text[:1000] + "\n" for chapter in range(1, 21)))
    state, first_toc = render(wide_tap(670, 650), panel=1)
    assert state["toc_count"] == 20 and state["reader_page"] == 0
    for reverse in (False, True):
        toc_key_x = 56 if reverse else 670
        lower_key, upper_key = (2,0) if reverse else (0,2)
        toc = wide_tap(toc_key_x,650,reverse)
        assert render(toc,f"k,{lower_key},0,0,0",panel=1,reverse=reverse)[0]["reader_page"] == 1
        assert render(toc,f"k,{lower_key},0,0,0",f"k,{upper_key},0,0,0",
                      panel=1,reverse=reverse)[0]["reader_page"] == 0
    jumped, _ = render(wide_tap(670, 650), wide_tap(302, 650), wide_tap(555, 316), panel=1)
    assert jumped["toc_jump"] and jumped["toc_percent"] == 34
    _, second_toc = render(wide_tap(670, 650), wide_tap(422, 650), panel=1)
    _, last_toc = render(wide_tap(670, 650), wide_tap(545, 650), panel=1)
    assert len({first_toc, second_toc, last_toc}) == 3
    chosen, _ = render(wide_tap(670, 650), wide_tap(422, 650), wide_tap(300, 50), panel=1)
    assert chosen["reader_panel"] == 0 and chosen["reader_progress"] >= 40

    marks = []
    for _ in range(8):
        marks.extend((wide_tap(955, 395), "k,1,0,0,0", wide_tap(1100, 540), "k,1,0,0,0"))
    marked, _ = render(*marks, wide_tap(795, 650), wide_tap(540, 650), panel=1)
    assert marked["reader_panel"] == 7 and marked["reader_bookmark_count"] == 8 and marked["reader_detail_page"] == 1, marked

    for i in range(12):
        os.link(ROOT / "main/assets/builtin.ttf", card / "fonts" / f"Face-{i:02d}.ttf")
    fonts, _ = render(wide_tap(260, 187), wide_tap(540, 650), panel=2)
    assert fonts["reader_panel"] == 5 and fonts["reader_detail_page"] == 1
    chosen, _ = render(wide_tap(260, 187), wide_tap(540, 650), wide_tap(270, 105), panel=2)
    assert chosen["reader_panel"] == 2 and chosen["reading_font_path"].startswith("/sdcard/fonts/Face-")

    doc, _ = render(orientation, panel=3, document=True)
    assert doc["reader_controls_geometry"][2] == 6 and preference.read_bytes() == b"\x02"
    render(layout_button(301), panel=3)
    assert preference.read_bytes() == b"\x00"
    render(orientation, panel=3)
    assert preference.read_bytes() == b"\x01"

    # Run the real firmware gate against controlled SC7A20H output.
    source = (ROOT / "main/apps/app_book.c").read_text()
    sensor = (ROOT / "components/sc7a20h/include/sc7a20h.h").read_text()
    fresh = sensor.split("static inline bool sc7a20h_sample_fresh",1)[1].split("}\n",1)[0]+"}\n"
    gravity = source.split("static void reader_stop_landscape_sensor", 1)[1].split(
        '\n#include "reader_controls_actions.h"', 1)[0]
    harness = work / "gravity.c"
    harness.write_text(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_LOGI(...) ((void)0)
enum { ESP_OK, READING, TOC, SHELF };
typedef struct { bool touched; } touch_t;
typedef struct { void *acc; int64_t now_ms; bool consumed, released; touch_t *touch; } app_ctx_t;
typedef struct { int16_t x_mg, y_mg, z_mg; uint8_t status; } sc7a20h_sample_t;
static bool landscape = true, s_reader_landscape_reverse, powered, fail_wake, fail_read;
static bool s_reader_sensor_owned, s_progress_drag, s_toc_jump_drag;
static int s_view = READING, s_reader_slider = -1;
static const char *s_text = "book";
static int64_t s_reader_sensor_next_ms;
static unsigned s_reader_opposite_samples, wake_calls, read_calls, sleep_calls;
static sc7a20h_sample_t input = {20, 230, 974, 0x08};
static bool reader_landscape(void) { return landscape; }
static bool sc7a20h_powered(void *sensor) { assert(sensor); return powered; }
static void read_pico_sensor_wake(void *sensor) {
    assert(sensor && !powered);
    ++wake_calls;
    powered = !fail_wake;
}
static void read_pico_sensor_sleep(void *sensor) {
    assert(sensor && powered);
    ++sleep_calls;
    powered = false;
}
static int read_pico_accel_read(void *sensor, sc7a20h_sample_t *sample) {
    assert(sensor && powered);
    ++read_calls;
    *sample = input;
    return fail_read ? 1 : ESP_OK;
}
""" + "static inline bool sc7a20h_sample_fresh" + fresh +
        "static void reader_stop_landscape_sensor" + gravity + r"""
static bool poll(app_ctx_t *ctx, unsigned elapsed_ms) {
    ctx->now_ms += elapsed_ms;
    return reader_poll_landscape_direction(ctx);
}

int main(void) {
    touch_t touch = {0};
    app_ctx_t ctx = {.acc = &ctx, .touch = &touch};
    landscape = false;
    assert(!poll(&ctx, 0) && !wake_calls);
    landscape = true;
    fail_wake = true;
    assert(!poll(&ctx, 0) && wake_calls == 1 && !powered);
    assert(!poll(&ctx, 999) && wake_calls == 1);
    fail_wake = false;
    assert(!poll(&ctx, 1) && wake_calls == 2 && powered);
    assert(!poll(&ctx, 119) && !read_calls);
    assert(!poll(&ctx, 1) && read_calls == 1 && !s_reader_landscape_reverse);
    assert(!poll(&ctx, 159) && read_calls == 1);
    assert(poll(&ctx, 1) && read_calls == 2 && s_reader_landscape_reverse);
    assert(!poll(&ctx, 160) && s_reader_landscape_reverse);
    const sc7a20h_sample_t rejects[] = {
        {0,-100,995,0x08}, {660,-750,0,0x08}, {0,-1600,0,0x08}, {0,-300,0,0x08},
        {0,-990,0,0}, {980,0,200,0x08}, {-980,0,200,0x08}
    };
    for (unsigned i=0; i<sizeof(rejects)/sizeof(rejects[0]); ++i) {
        input = rejects[i];
        assert(!poll(&ctx, 160) && !poll(&ctx, 160) && s_reader_landscape_reverse);
    }
    input = (sc7a20h_sample_t){100, -980, 40, 0x08};
    assert(!poll(&ctx, 160));
    fail_read = true;
    assert(!poll(&ctx, 160));
    fail_read = false;
    assert(!poll(&ctx, 160));
    touch.touched = true;
    unsigned before = read_calls;
    assert(!poll(&ctx, 160) && read_calls == before);
    touch.touched = false;
    ctx.released = true;
    assert(!poll(&ctx, 160) && read_calls == before);
    ctx.released = false;
    assert(!poll(&ctx, 160) && s_reader_landscape_reverse);
    assert(poll(&ctx, 160) && !s_reader_landscape_reverse);
    s_view = TOC;
    input.y_mg = 980;
    assert(!poll(&ctx, 160));
    s_reader_slider = 1;
    assert(!poll(&ctx, 160) && !s_reader_landscape_reverse);
    s_reader_slider = -1;
    assert(!poll(&ctx, 160));
    assert(poll(&ctx, 160) && s_reader_landscape_reverse);
    s_view = SHELF;
    assert(!poll(&ctx, 1) && !powered && sleep_calls == 1);
    s_view = READING;
    assert(!poll(&ctx, 0) && powered && wake_calls == 3);
    reader_stop_landscape_sensor(&ctx);
    assert(!powered && sleep_calls == 2 && !s_reader_sensor_owned);
    powered = true;
    assert(!poll(&ctx, 1) && !s_reader_sensor_owned);
    landscape = false;
    assert(!poll(&ctx, 1) && powered && sleep_calls == 2);
}
""")
    executable = harness.with_suffix("")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

print("PASS: mirrored thumb zones, visual key order, guide footer and return, landscape pagination and gravity")
