#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# 中文：宿主验证，无固件打包。/ English: Host verification without firmware packaging.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Imain/ui tools/ui_i18n_host_test.c main/ui/ui_i18n.c -o "$out/test"
"$out/test"
python3 -B - <<'PY'
from pathlib import Path
import re

catalog = Path("main/ui/ui_messages.inc").read_text()
literal = r'"(?:\\.|[^"\\])*"'
keys = set(re.findall(r"\{\s*(" + literal + r")\s*,", catalog))
marked = re.compile(r"\bUI_MSG\(\s*(" + literal + r")\s*\)")
missing = {}
for root in ("main", "components"):
    for path in Path(root).rglob("*"):
        if path.suffix not in {".c", ".h", ".inc"}:
            continue
        for key in marked.findall(path.read_text(errors="replace")):
            if key not in keys:
                missing.setdefault(key, set()).add(str(path))
assert not missing, "\n".join(f"{key}: {', '.join(sorted(paths))}" for key, paths in sorted(missing.items()))
print("PASS: all marked firmware messages have catalog entries")
PY
