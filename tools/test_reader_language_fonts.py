#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise automatic profiles and font coverage through the real reader and settings."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]


def latin_fixture(face):
    """Retain the bundled face's ASCII glyph IDs and drop its Chinese mappings."""
    font = bytearray(face)
    count = struct.unpack_from(">H", font, 4)[0]
    record = next(i for i in range(12, 12 + count * 16, 16) if font[i:i + 4] == b"cmap")
    offset, capacity = struct.unpack_from(">II", font, record + 8)
    subtable = struct.pack(">HHHHH", 6, 200, 0, 32, 95) + struct.pack(">95H", *range(1, 96))
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + subtable
    assert len(cmap) <= capacity
    font[offset:offset + len(cmap)] = cmap
    struct.pack_into(">I", font, record + 12, len(cmap))
    return font


def make_epub(path, first, second, local_cover=False):
    with ZipFile(path, "w") as archive:
        archive.writestr("META-INF/container.xml", '<container><rootfiles><rootfile full-path="book.opf"/></rootfiles></container>')
        language = "zh-CN" if local_cover else "en-US"
        first_file = "coverpage.xhtml" if local_cover else "a.xhtml"
        archive.writestr("book.opf", f"""<package xmlns:dc="http://purl.org/dc/elements/1.1/">
          <metadata><dc:title>An English book</dc:title><dc:language>{language}</dc:language></metadata>
          <manifest><item id="a" href="{first_file}" media-type="application/xhtml+xml"/>
          <item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest>
          <spine><itemref idref="a"/><itemref idref="b"/></spine></package>""")
        cover = '<img src="cover.png"/><h1>An English book</h1><b>简介</b>' if local_cover else ""
        archive.writestr(first_file, f"<html><body>{cover}<p>{first}</p></body></html>")
        archive.writestr("b.xhtml", f"<html><body><p>{second}</p></body></html>")


with tempfile.TemporaryDirectory(prefix="reader-language-", dir=ROOT / "build") as directory:
    work = Path(directory)
    card = work / "card"
    sim = work / "build/simulator"
    nvs = sim / "nvs/read_pico"
    for folder in ("books", "markdown", "fonts"):
        (card / folder).mkdir(parents=True)
    nvs.mkdir(parents=True)
    old_doc = nvs.parent / "if_content"
    old_doc.mkdir()
    (sim / "titles.txt").touch()
    (nvs / "book_vertical").write_bytes(b"\1")

    full = (ROOT / "main/assets/builtin.ttf").read_bytes()
    latin = latin_fixture(full)
    for name, data in (("A-Latin", latin), ("B-Latin", latin), ("M-Chinese", full), ("N-Chinese", full)):
        (card / "fonts" / f"{name}.ttf").write_bytes(data)
    (card / "fonts/00-Broken.ttf").write_bytes(bytes(768))

    english = 'This is the story of a traveler, and “Renée” was with him. — All the way! '
    chinese = "这是中英夹杂的正文。"
    for relative, text in (
        ("books/English.txt", english * 18), ("books/Mixed.txt", english * 12 + chinese),
        ("markdown/English.md", "# An English document\n\n" + english * 18),
        ("markdown/Mixed.md", "# 标题\n\n" + english + chinese),
        ("markdown/Short.md", "Hi!"), ("books/French.txt", "Bonjour, je suis une lectrice. " * 20),
    ):
        (card / relative).write_text(text, encoding="utf-8")
    make_epub(card / "books/English.epub", english, english)
    make_epub(card / "books/Late mixed.epub", english, chinese)
    make_epub(card / "books/Localized cover.epub", english, english, local_cover=True)
    make_epub(card / "books/Mixed cover.epub", english + chinese, english, local_cover=True)

    def render(path, events="", panel=0, screen="book-1"):
        files = screen == "files-0"
        command = [str(ROOT / "build/simulator/render"), screen, f"/sdcard/{path}", "3" if files else "0", "48",
                   "build/simulator/frame.bin", "build/simulator/titles.txt", str(panel), "0",
                   "builtin", events, "/sdcard/fonts" if files else ""]
        result = subprocess.run(command, cwd=work, env={**os.environ, "PICO_SIM_CARD_ROOT": str(card)},
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)

    def pick(path, row):
        state = render(path, panel=5)
        step = state["reader_controls_geometry"][1]
        y = 196 + (row * step) + step // 2
        return render(path, f"t,300,{y},300,{y}", panel=5)

    (nvs / "font").write_bytes(b"/sdcard/fonts/M-Chinese.ttf\0")
    legacy = b"/sdcard/fonts/N-Chinese.ttf\0"
    (old_doc / "md_font").write_bytes(legacy)
    (old_doc / "md_font_en").write_bytes(b"/sdcard/fonts/A-Latin.ttf\0")
    (nvs / "bk_margin").write_bytes(bytes([36]))
    for path, english_mode, vertical in (
        ("books/English.txt", True, False), ("markdown/English.md", True, False),
        ("books/Mixed.txt", False, True), ("markdown/Mixed.md", False, False),
    ):
        baseline = render(path, panel=5)
        assert baseline["reader_font_count"] == (4 if english_mode else 2), baseline
        assert baseline["reader_english"] == english_mode and baseline["reader_vertical"] == vertical
        assert baseline["font_path"].endswith("/M-Chinese.ttf"), baseline
    assert render("markdown/English.md", panel=2)["markdown_heading_names"] == ["H1", "H2", "H3"]
    assert render("markdown/Mixed.md", panel=2)["markdown_heading_names"] == ["一级标题", "二级标题", "三级标题"]

    assert pick("books/English.txt", 0)["font_path"].endswith("/A-Latin.ttf")
    assert render("markdown/English.md")["font_path"].endswith("/A-Latin.ttf")
    assert pick("markdown/English.md", 1)["font_path"].endswith("/B-Latin.ttf")
    assert render("books/English.txt")["font_path"].endswith("/B-Latin.ttf")
    assert pick("markdown/Mixed.md", 1)["font_path"].endswith("/N-Chinese.ttf")
    assert render("books/Mixed.txt")["font_path"].endswith("/N-Chinese.ttf")
    assert (old_doc / "md_font").read_bytes() == legacy
    assert (old_doc / "md_font_en").read_bytes() == b"/sdcard/fonts/A-Latin.ttf\0"

    # Recover an invalid old Markdown layout while preserving the pre-edit book defaults.
    (old_doc / "md_layout_v1").write_bytes(bytes(9))
    book = "books/Mixed.txt"
    doc = "markdown/Mixed.md"
    line = render(book, panel=3)["reader_controls_geometry"][1]
    y = 196 + line - 22
    def margin(path, x):
        return render(path, f"t,{x},{y},{x},{y}", panel=3)["reader_margin"]
    expected_book = margin(book, 526)
    assert expected_book != 36 and render(doc)["reader_margin"] == 36, (expected_book, y)
    expected_doc = margin(doc, 168)
    assert expected_doc != 36 and render(book)["reader_margin"] == expected_book
    assert render(doc)["reader_margin"] == expected_doc

    attempted_vertical = render("books/English.txt", "t,125,1057,125,1057", panel=3)
    assert not attempted_vertical["reader_vertical"] and (nvs / "book_vertical").read_bytes() == b"\1"
    assert not render("markdown/Short.md")["reader_english"]
    assert not render("books/French.txt")["reader_english"]
    epub = render("books/English.epub")
    assert epub["reader_english"] and epub["font_path"].endswith("/B-Latin.ttf"), epub
    cover = render("books/Localized cover.epub", panel=5)
    assert cover["reader_english"] and not cover["reader_vertical"], cover
    assert cover["reader_font_count"] == 2 and cover["font_path"].endswith("/N-Chinese.ttf"), cover
    body = render("books/Localized cover.epub", ";".join(["k,2,0,0,0"] * 2), panel=0)
    assert body["reader_chapter"] == 1 and body["reader_english"] and not body["reader_vertical"], body
    assert body["font_path"].endswith("/B-Latin.ttf"), body
    assert not render("books/Mixed cover.epub")["reader_english"]
    changed = render("books/Late mixed.epub", "k,2,0,0,0")
    assert not changed["reader_english"] and changed["reader_vertical"], changed
    assert changed["font_path"].endswith("/N-Chinese.ttf"), changed
    back = render("books/Late mixed.epub", "k,2,0,0,0;k,0,0,0,0")
    assert not back["reader_english"] and back["font_path"].endswith("/N-Chinese.ttf"), back

    # Replacing a document with the same filename, size and timestamp cannot carry over its language.
    document = card / "markdown/English.md"
    timestamp = document.stat()
    old_body = document.read_bytes()
    replacement = chinese.encode("utf-8")
    document.write_bytes(old_body[:-len(replacement)] + replacement)
    os.utime(document, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
    edited = render("markdown/English.md")
    assert not edited["reader_english"] and edited["font_path"].endswith("/N-Chinese.ttf"), edited

    assert render("books/English.txt", screen="settings-5")["system_font_count"] == 3
    (nvs / "language").write_bytes(b"\1")
    en_default = render("books/English.txt", screen="settings-5")
    assert en_default["system_font_count"] == 3
    (nvs / "sys_font").write_bytes(b"/sdcard/fonts/M-Chinese.ttf\0")
    (old_doc / "sys_font_en").write_bytes(b"/sdcard/fonts/A-Latin.ttf\0")
    assert render("books/English.txt", screen="settings-5")["font_path"].endswith("/M-Chinese.ttf")
    assert render("books/English.txt", screen="settings-23")["font_path"].endswith("/M-Chinese.ttf")
    font_top, font_step = render("books/English.txt", screen="settings-5")["settings_geometry"][6:8]
    y = font_top + 2 * font_step + font_step // 2
    after = render("books/English.txt", f"t,300,{y},300,{y}", screen="settings-5")
    assert after["font_path"].endswith("/N-Chinese.ttf"), after
    assert render("books/English.txt", screen="settings-5")["font_path"].endswith("/N-Chinese.ttf")
    assert (nvs / "sys_font").read_bytes() == b"/sdcard/fonts/M-Chinese.ttf\0"
    default_y = font_top + font_step // 2
    assert render("books/English.txt", f"t,300,{default_y},300,{default_y}", screen="settings-5")["font_path"] == en_default["font_path"]
    assert render("books/English.txt", screen="settings-5")["font_path"] == en_default["font_path"]

    assert render("markdown/Mixed.md", panel=2)["markdown_heading_names"] == ["Heading 1", "Heading 2", "Heading 3"]
    (nvs / "language").write_bytes(b"\0")
    fallback = render("books/English.txt", screen="settings-5")
    assert fallback["system_font_count"] == 3 and fallback["font_path"].endswith("/M-Chinese.ttf")

    listing = render("", screen="files-0")
    row_height = listing["files_row_height"]
    y = 190 + row_height + row_height // 2  # A-Latin follows 00-Broken.
    open_latin = f"t,180,{y},180,{y}"
    options = render("", open_latin, screen="files-0")
    assert options["files_view"] == 5, options
    system = render("", open_latin + ";t,186,1125,186,1125", screen="files-0")
    assert not system["files_notice"], system
    assert system["system_font_path"].endswith("/M-Chinese.ttf")
    (nvs / "language").write_bytes(b"\1")
    assert render("", screen="settings-5")["font_path"] == en_default["font_path"]
    reading = render("", open_latin + ";t,342,1125,342,1125", screen="files-0")
    assert reading["files_notice"] in ("已设为英文阅读字体", "English reading font set"), reading
    assert reading["reading_font_path"].endswith("/N-Chinese.ttf")
    assert render("books/English.txt")["font_path"].endswith("/A-Latin.ttf")

print("PASS shared profiles, legacy priority, heading labels, isolated margins, EPUB language, and system fonts")
