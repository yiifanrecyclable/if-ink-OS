#!/usr/bin/env python3
"""Build IF Edition's self-contained UI fallback from pinned Noto Sans SC."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHARSET = ROOT / "main/font/charset.txt"
OUT = ROOT / "main/assets/builtin.ttf"
SCAN_DIRS = [
    ROOT / "main/ui",
    ROOT / "main/app",
    ROOT / "main/apps",
    ROOT / "main/factory",
    ROOT / "main/assets",
    ROOT / "main/app_main.c",
    ROOT / "main/display.c",
    ROOT / "main/sleep.c",
    ROOT / "main/settings.c",
    ROOT / "main/ota_online.c",
    ROOT / "main/book/content_library.c",
    ROOT / "main/book/content_library.h",
    ROOT / "main/book/reader_input.c",
    ROOT / "components/sy7636a",
    ROOT / "components/sc7a20h",
    ROOT / "components/read_pico",
    ROOT / "components/read_pico_pmu",
    ROOT / "components/pico_weread",
]
TEXT_SUFFIXES = {".md", ".txt"}
# 约3755个GB2312一级常用汉字加菜单/标点；完整标题字库另随TF卡资源提供。
# GB2312 level-one Han plus UI/punctuation; the standard face is an SD resource.
MAX_BYTES = 768 * 1024
BUILTIN_WEIGHT = 500
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')


def ascii_printable() -> str:
    return "".join(chr(i) for i in range(0x20, 0x7F))

def common_characters(include_han: bool = True) -> str:
    chars=[]
    if include_han:
        for lead in range(0xb0,0xd8):
            for trail in range(0xa1,0xff):
                try: chars.append(bytes((lead,trail)).decode('gb2312'))
                except UnicodeDecodeError: pass
    chars.extend(chr(cp) for a,b in ((0xa0,0x100),(0x2000,0x2070),(0x3000,0x3040),(0xff00,0xfff0)) for cp in range(a,b))
    return ''.join(chars)


def load_preset(path: Path) -> str:
    chars: list[str] = []
    if not path.is_file():
        return ""
    for line in path.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        chars.append(s)
    return "".join(chars)


def unescape(literal: str) -> str:
    body = literal[1:-1]
    return (
        body.replace("\\\\", "\0")
        .replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace("\\\"", '"')
        .replace("\0", "\\")
    )


def scan_sources() -> str:
    texts: list[str] = []
    files: list[Path] = []
    for item in SCAN_DIRS:
        if item.is_file():
            files.append(item)
        elif item.is_dir():
            files.extend(item.rglob("*.c"))
            files.extend(item.rglob("*.h"))
            files.extend(item.rglob("*.md"))
            files.extend(item.rglob("*.txt"))
    for path in files:
        data = path.read_text(encoding="utf-8", errors="ignore")
        if path.suffix in TEXT_SUFFIXES:
            texts.append(data)
            continue
        for match in STRING_RE.finditer(data):
            texts.append(unescape(match.group(0)))
    return "".join(texts)


def unique_text(*parts: str) -> str:
    seen: set[str] = set()
    out: list[str] = []
    for part in parts:
        for ch in part:
            if ch in seen or ch in "\n\r\t":
                continue
            seen.add(ch)
            out.append(ch)
    return "".join(sorted(out, key=lambda c: ord(c)))


def subset_variable(src: Path, text: str, dest: Path) -> int:
    from fontTools.subset import Subsetter, Options
    from fontTools.ttLib import TTFont

    font = TTFont(src)
    options = Options()
    options.layout_features = ["*"]
    options.notdef_outline = True
    options.recommended_glyphs = True
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.glyph_names = False
    options.ignore_missing_glyphs = True
    subsetter = Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    dest.parent.mkdir(parents=True, exist_ok=True)
    font.save(dest)
    font.close()
    return dest.stat().st_size


def subset_medium(src: Path, text: str, dest: Path) -> int:
    from fontTools.subset import Subsetter, Options
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    vf = TTFont(src)
    if "fvar" in vf:
        inst = instantiateVariableFont(vf, {"wght": BUILTIN_WEIGHT}, inplace=False)
        vf.close()
        vf = inst
    if vf["OS/2"].usWeightClass != BUILTIN_WEIGHT:
        raise ValueError(f"source cannot provide wght={BUILTIN_WEIGHT}")
    for name_id, value in (
        (1, "Noto Sans SC"),
        (2, "Medium"),
        (4, "Noto Sans SC Medium"),
        (6, "NotoSansSC-Medium"),
    ):
        vf["name"].setName(value, name_id, 3, 1, 0x409)
    options = Options()
    # stb_truetype only consumes horizontal outlines/metrics and pair kerning.
    # stb_truetype只读取横向轮廓/度量及字偶间距；不保留竖排和替代字形数据。
    options.layout_features = ["kern"]
    options.drop_tables += ["BASE", "vhea", "vmtx"]
    options.notdef_outline = True
    options.recommended_glyphs = True
    options.ignore_missing_glyphs = True
    subsetter = Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(vf)
    dest.parent.mkdir(parents=True, exist_ok=True)
    vf.save(dest)
    vf.close()
    return dest.stat().st_size


def default_src() -> Path:
    for path in (
        ROOT / "assets/fonts/PicoSans-Medium.ttf",
        ROOT / "sdcard/fonts/Hei.ttf",
        ROOT / "flash/fonts/Hei.ttf",
    ):
        if path.is_file():
            return path
    return ROOT / "sdcard/fonts/Hei.ttf"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", type=Path, default=default_src())
    parser.add_argument("--out", type=Path, default=OUT)
    parser.add_argument("--common-han", action="store_true", help="Include GB2312 level-one Han for reference builds")
    args = parser.parse_args()
    if not args.src.is_file():
        print(f"missing source font: {args.src}", file=sys.stderr)
        return 1

    text = unique_text(ascii_printable(), common_characters(args.common_han), load_preset(CHARSET), scan_sources())
    cjk = sum(1 for ch in text if "\u4e00" <= ch <= "\u9fff")
    print(f"charset {len(text)} chars ({cjk} CJK)")

    size = subset_medium(args.src, text, args.out)
    mode = f"Medium (wght={BUILTIN_WEIGHT})"
    print(f"wrote {args.out} ({size} bytes, {mode})")
    if size > MAX_BYTES:
        print("subset still too large", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
