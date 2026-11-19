#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reopen saved vertical text in a fresh renderer with no warmed glyph coverage table."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from gen_book_fixtures import make_epub

ROOT = Path(__file__).resolve().parents[1]
RENDER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/simulator/render"

for ext in ("txt", "epub"):
    for layout in range(3):
        with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="vertical-resume-") as tmp:
            work = Path(tmp)
            card = work / "card"
            sim = work / "build/simulator"
            nvs = sim / "nvs/read_pico"
            (card / "books").mkdir(parents=True)
            nvs.mkdir(parents=True)
            (sim / "titles.txt").touch()
            for key, value in (("book_vertical", True), ("rd_fullscreen", layout > 0),
                               ("rd_immersive", layout == 2)):
                (nvs / key).write_bytes(bytes([value]))
            book = card / f"books/Test.{ext}"
            if ext == "epub":
                make_epub(book, "nav")
            else:
                line = "这是竖排续读测试：中文标点、English 和数字123。\n"
                book.write_text("前言\n\n" + line * 180, encoding="utf-8")

            def frame(events):
                result = subprocess.run(
                    [str(RENDER), "book-1", f"/sdcard/books/{book.name}", "-1", "48",
                     "build/simulator/frame.bin", "build/simulator/titles.txt",
                     "0", "0", "builtin", events],
                    cwd=work, env={**os.environ, "PICO_SIM_CARD_ROOT": str(card)},
                    capture_output=True, text=True, check=True, timeout=20,
                )
                # Exclude status, chapter header, and progress chrome.
                return json.loads(result.stdout), (sim / "frame.bin").read_bytes()[270 * 342:950 * 342]

            saved, _ = frame("k,2,0,0,0")
            resumed, body = frame("")
            for key in ("reader_chapter", "reader_page", "reader_offset"):
                assert resumed[key] == saved[key], (ext, layout, key)
            assert resumed["reader_chapter"] or resumed["reader_offset"], (ext, layout)
            assert resumed["book_view"] == 1 and resumed["reader_layout"] == layout, (ext, layout)
            assert any(byte != 255 for byte in body), (ext, layout, "cold reader drew a white body")
print("PASS: cold TXT/EPUB vertical resume restores saved text and draws ink in all three reader layouts")
