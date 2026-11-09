#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare the picker first frame and settled name previews using the firmware renderer."""
from pathlib import Path
import json
import os
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("PICO_SIM_RENDER", ROOT / "build/simulator/render")).resolve()


def widen_advances(data):
    font = bytearray(data)
    tables = {
        font[at:at+4].decode(): struct.unpack_from(">I", font, at+8)[0]
        for at in range(12, 12 + 16 * struct.unpack_from(">H", font, 4)[0], 16)
    }
    metrics = struct.unpack_from(">H", font, tables["hhea"]+34)[0]
    for i in range(metrics):
        offset = tables["hmtx"] + 4*i
        advance = struct.unpack_from(">H", font, offset)[0]
        struct.pack_into(">H", font, offset, advance*3//2)
    return font


def check_changed_area(before, after, left, right, top, bottom):
    changed = [(i % 342 * 2, i // 342) for i, (a, b) in enumerate(zip(before, after)) if a != b]
    assert len(changed) > 50, changed[:10]
    assert all(left <= x < right and top <= y < bottom for x, y in changed), changed[:10]


with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="name-previews-") as tmp:
    work = Path(tmp)
    card = work / "card"
    sim = work / "build/simulator"
    for folder in (card/"fonts", card/"books", card/"markdown", sim/"nvs/read_pico"):
        folder.mkdir(parents=True)
    (sim/"titles.txt").touch()
    reference = (ROOT/"main/assets/builtin.ttf").read_bytes()
    (card/"fonts/Alpha Wide.ttf").write_bytes(widen_advances(reference))
    (card/"fonts/Broken.ttf").write_bytes(b"\x00" * 768)
    (card/"fonts/No \U0001f6f8 Glyph.ttf").write_bytes(reference)
    (card/"books/Book.txt").write_text("Book text and progress.\n\n" * 100)
    (card/"markdown/Doc.md").write_text("# Document\n\nText and progress.\n\n" * 100)

    def render(view, path, panel, deferred):
        env = {**os.environ, "PICO_SIM_CARD_ROOT": str(card)}
        if deferred:
            env["PICO_SIM_DEFER_FONT_NAMES"] = "1"
        else:
            env.pop("PICO_SIM_DEFER_FONT_NAMES", None)
        args = [str(BINARY), view, path, "1", "48", "build/simulator/frame.bin",
                "build/simulator/titles.txt", str(panel), "0", "builtin", "", ""]
        result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True,
                                check=True, timeout=30)
        return json.loads(result.stdout), (sim/"frame.bin").read_bytes()

    for view, path, panel in (
        ("settings-5", "", 0),
        ("book-1", "/sdcard/books/Book.txt", 5),
        ("book-1", "/sdcard/markdown/Doc.md", 5),
    ):
        initial, before = render(view, path, panel, True)
        settled, after = render(view, path, panel, False)
        if view.startswith("settings"):
            top, step, _ = settled["settings_geometry"][6:9]
            assert initial["settings_geometry"] == settled["settings_geometry"]
            check_changed_area(before, after, 60, 570, top+step, top+2*step-12)
        else:
            _, step, _ = settled["reader_controls_geometry"]
            top = 196  # UI_PAGE_Y(220) is the first picker row.
            assert initial["reader_controls_geometry"] == settled["reader_controls_geometry"]
            assert initial["font_path"] == settled["font_path"]
            assert initial["reader_progress"] == settled["reader_progress"]
            check_changed_area(before, after, 48, 618, top, top+step-1)

    (card/"fonts/Alpha Wide.ttf").rename(card/"fonts/Hei.ttf")
    (sim/"nvs/read_pico/language").write_bytes(bytes([1]))
    _, before = render("settings-5", "", 0, True)
    settled, after = render("settings-5", "", 0, False)
    top, step, _ = settled["settings_geometry"][6:9]
    check_changed_area(before, after, 60, 570, top+2*step, top+3*step-12)

print("PASS system (Chinese and English), book and Markdown name previews; unchanged geometry and safe fallback")
