#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the release fence against actual compile commands, ELF/map and binary."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument("--expect", choices=("public", "local"), default="public")
parser.add_argument("--out", type=Path)
args = parser.parse_args()

build = args.build
image = (build / "Read_Pico_IF.bin").read_bytes()
elf = build / "Read_Pico_IF.elf"
elf_hash = hashlib.sha256(elf.read_bytes()).hexdigest()
version = image[48:80].split(b"\0")[0].decode("ascii")
project = image[80:112].split(b"\0")[0].decode("ascii")
assert project == "Read_Pico_IF" and image[176:208].hex() == elf_hash
commands = json.loads((build / "compile_commands.json").read_text())
names = {"app_inkbuddy.c", "inkbuddy_wire.c"}
compiled = {Path(row["file"]).name for row in commands} & names
flags = any(re.search(r"(?:^|\s)-DIFINK_INKBUDDY(?:=|\s|$)", row.get("command", "")) for row in commands)
tool = shutil.which("xtensa-esp32s3-elf-nm") or shutil.which("xtensa-esp-elf-nm")
if not tool:
    parser.error("Load ESP-IDF export.sh before checking the ELF")
symbols = subprocess.check_output([tool, "--defined-only", str(elf)], text=True)
symbols_present = bool(re.search(r"\b(?:app_inkbuddy|inkbuddy_[A-Za-z0-9_]+)\b", symbols))
mapped = bool(re.search(r"(?:app_inkbuddy|inkbuddy_wire)\.c\.(?:o|obj)\b",
                        (build / "Read_Pico_IF.map").read_text()))
markers = any(mark in image for mark in (b"inkbuddy", b"\x49\x4e\x4b\x31\0",
                                        "终端模式".encode(), b"/frame?v=1&dev="))
feature = bool(compiled or flags or symbols_present or mapped or markers)
if args.expect == "public":
    assert not feature, "inkbuddy reached a public artifact"
    assert not re.search(r"-r\d+t\d+$", version), "A local test version cannot be published"
else:
    assert version == "0.2.1-beta-r2t2"
    assert compiled == names and flags and symbols_present and mapped and markers

receipt = dict(version=version, features={"inkbuddy": feature}, app_size=len(image),
               app_sha256=hashlib.sha256(image).hexdigest(), elf_sha256=elf_hash,
               compiled_modules=sorted(compiled), feature_define=flags, mapped=mapped,
               symbols=symbols_present, image_markers=markers)
if args.out:
    args.out.write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt))
