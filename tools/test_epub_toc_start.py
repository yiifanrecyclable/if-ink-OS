#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise EPUB directory jumps with the actual reader in the host simulator."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "build/simulator/render"


def make_book(path):
    # Two front pages precede the original NCX. The first is empty.
    names = [
        ("blank", "", ""),
        ("cover", "扉页", ""),
        ("preface", "译者序", "译者序"),
    ] + [(f"c{i}", f"第{i}章 正文", f"第{i}章") for i in range(1, 12)]
    manifest = "".join(
        f'<item id="{name}" href="{name}.xhtml" media-type="application/xhtml+xml"/>'
        for name, _, _ in names
    )
    spine = "".join(f'<itemref idref="{name}"/>' for name, _, _ in names)
    nav = "".join(
        f'<navPoint><navLabel><text>{title}</text></navLabel>'
        f'<content src="{name}.xhtml#start"/></navPoint>'
        for name, _, title in names if title
    )
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as book:
        book.writestr("mimetype", "application/epub+zip")
        book.writestr("META-INF/container.xml",
                      '<container><rootfiles><rootfile full-path="book.opf"/>'
                      '</rootfiles></container>')
        book.writestr("book.opf",
                      f'<package><manifest>{manifest}'
                      '<item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                      f'</manifest><spine toc="toc">{spine}</spine></package>')
        book.writestr("toc.ncx", f"<ncx><navMap>{nav}</navMap></ncx>")
        for name, heading, _ in names:
            body = f'<h1 id="start">{heading}</h1><p>测试正文。</p>' if heading else ""
            book.writestr(f"{name}.xhtml", f"<html><body>{body}</body></html>")


def tap(x, y):
    return f"t,{x},{y},{x},{y}"


def wide_tap(x, y, reverse):
    return tap(y, 1215 - x) if reverse else tap(683 - y, x)


with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="epub-toc-start-") as temp:
    work = Path(temp)
    card = work / "card"
    nvs = work / "build/simulator/nvs/read_pico"
    (card / "books").mkdir(parents=True)
    nvs.mkdir(parents=True)
    make_book(card / "books/TOC.epub")
    (work / "build/simulator/titles.txt").touch()

    def render(*events, view=2, reverse=False):
        env = {**os.environ, "PICO_SIM_CARD_ROOT": str(card)}
        env.pop("PICO_SIM_READER_REVERSE", None)
        if reverse:
            env["PICO_SIM_READER_REVERSE"] = "1"
        args = [str(RENDER), f"book-{view}", "/sdcard/books/TOC.epub", "-1", "48",
                "build/simulator/frame.bin", "build/simulator/titles.txt",
                "0", "0", "builtin", ";".join(events)]
        result = subprocess.run(args, cwd=work, env=env, capture_output=True,
                                text=True, check=True, timeout=25)
        return json.loads(result.stdout)

    assert render()["toc_count"] == 12
    portrait_preface = render(tap(300, 286))
    assert portrait_preface["book_view"] == 1 and portrait_preface["reader_chapter"] == 2, portrait_preface
    assert portrait_preface["reader_section_caption"] == "译者序", portrait_preface
    assert portrait_preface["reader_chapter_lead_height"] == 214, portrait_preface
    portrait_start = render(tap(300, 214))
    assert portrait_start["book_view"] == 1 and portrait_start["reader_chapter"] == 1, portrait_start
    assert portrait_start["reader_page"] == 0 and not portrait_start["reader_note_open"], portrait_start
    assert portrait_start["reader_section_caption"] == "" and portrait_start["reader_chapter_lead_height"] == 0, portrait_start
    assert render(view=1)["reader_chapter"] == 1
    second_portrait = render(tap(480, 1060), tap(300, 214))
    assert second_portrait["book_view"] == 1 and second_portrait["reader_chapter"] == 12, second_portrait
    (nvs / "sys_size").write_bytes(bytes([160]))
    second_xl = render(tap(480, 1060), tap(300, 214))
    assert second_xl["book_view"] == 1 and second_xl["reader_chapter"] == 11, second_xl
    (nvs / "sys_size").unlink()

    for reverse in (False, True):
        (nvs / "book_screen").write_bytes(b"\x01")
        origin = 614 if reverse else 0
        next_page = wide_tap(origin + 422, 650, reverse)
        second = render(next_page, reverse=reverse)
        assert second["book_view"] == 2 and second["reader_page"] == 1, second
        chosen = render(next_page, wide_tap(origin + 300, 50, reverse), reverse=reverse)
        assert chosen["book_view"] == 1 and chosen["reader_chapter"] == 10, chosen
        start = render(wide_tap(origin + 300, 50, reverse), reverse=reverse)
        assert start["book_view"] == 1 and start["reader_chapter"] == 1, start
        assert start["reader_page"] == 0 and not start["reader_note_open"], start
        assert render(view=1, reverse=reverse)["reader_chapter"] == 1

print("EPUB TOC: authored preface, first readable page, portrait and both landscape directions passed")
