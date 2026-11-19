#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the native bookshelf index through power-on and both real page controls."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("PICO_SIM_RENDER", ROOT / "build/simulator/render")).resolve()

with tempfile.TemporaryDirectory(prefix="shelf-index-ui-", dir=ROOT / "build") as directory:
    work = Path(directory)
    card = work / "card"
    (work / "build/simulator/vfs/flash/books").mkdir(parents=True)
    (work / "build/simulator/titles.txt").touch()
    (card / "books/sub").mkdir(parents=True)
    for i in range(25):
        (card / f"books/sub/B{i:02}.txt").write_text("Sample book text. Many words.\n" * 48)

    def render(events=""):
        args = [str(BINARY), "book-0", "", "0", "48", "build/simulator/frame.bin",
                "build/simulator/titles.txt", "0", "0", "builtin", events]
        env = {**os.environ, "PICO_SIM_CARD_ROOT": str(card), "PICO_SIM_SHELF_INDEX": "1"}
        checked = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True, check=True)
        return json.loads(checked.stdout)

    catalog = card / ".readpico/shelf.ifx"
    seeded = render()
    assert seeded["pages"] == 3 and not seeded["shelf_indexed"] and catalog.is_file()
    assert seeded["book_sort_mode"] == 3
    first = render()
    assert first["pages"] == 3 and first["shelf_indexed"] and first["book_items"][0].endswith("B00.txt")
    for event in ("k,2,0,0,0", "t,628,1087,628,1087"):
        second = render(event)
        assert second["shelf_indexed"] and second["pages"] == 3 and second["reader_page"] == 1, second
    third = render("k,2,0,0,0;k,2,0,0,0")
    assert third["shelf_indexed"] and third["reader_page"] == 2
    opened = render("k,2,0,0,0;t,100,300,100,300")
    assert opened["book_view"] == 1 and opened["book_path"].endswith("B09.txt"), opened
    updated = render()
    assert updated["book_items"][0].endswith("B09.txt") and updated["book_progresses"][0] == 0
    root_stamp = (card / "books").stat().st_mtime_ns
    (card / "books/sub/A00.txt").write_text("Added beneath an existing folder.\n" * 48)
    assert (card / "books").stat().st_mtime_ns == root_stamp
    assert render()["shelf_indexed"]
    sort = render("t,610,90,610,90;t,70,990,70,990;k,1,0,0,0")
    assert sort["book_sort_mode"] == 0 and not sort["content_sort_open"], sort
    assert sort["book_items"][0].endswith("A00.txt"), sort
    sorted_again = render()
    assert sorted_again["shelf_indexed"] and sorted_again["book_items"][0].endswith("A00.txt"), sorted_again
    catalog.write_bytes(catalog.read_bytes() + b"x")
    rebuilt = render()
    assert not rebuilt["shelf_indexed"] and rebuilt["pages"] == 3 and render()["shelf_indexed"]
    print("PASS native shelf: seed, cold reopen, second page by hardware/touch, arbitrary page, direct reading, recent-order rebuild, sort persistence and corrupt-index fallback")
