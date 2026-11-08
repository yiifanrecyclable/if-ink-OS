#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 真实C实现的宿主边界测试与独立展开参照。/ Host boundaries and an independent expansion oracle for the real C implementation.
import ctypes
import importlib.util
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/search-tests"
BUILD.mkdir(parents=True, exist_ok=True)
source = ROOT / "components/read_pico_search/read_pico_search.c"
flags = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
         "-I" + str(ROOT / "components/read_pico_search/include")]
exe = BUILD / "search-host"
subprocess.run(flags + ["-g", "-fsanitize=address,undefined", str(ROOT / "tools/search_host_test.c"), str(source), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
shared = BUILD / "search.so"
subprocess.run(flags + ["-shared", "-fPIC", str(source), "-o", str(shared)], check=True)
lib = ctypes.CDLL(str(shared))
match = lib.read_pico_search_match
match.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
match.restype = ctypes.c_bool

# 测试时可选官方wheel，独立穷举短输入与状态机比较。/ Optional official wheel enables exhaustive short-input oracle comparisons.
if len(sys.argv) > 1:
    spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate_search_table.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    with zipfile.ZipFile(sys.argv[1]) as wheel:
        dictionary = json.loads(wheel.read("pypinyin/pinyin_dict.json"))
    rng = random.Random(20260927)
    for case in range(500):
        tokens = [rng.choice(["重", "长", "女", "绿", "行", "乐", "ABC", "Cat", "3"]) for _ in range(3)]
        alternatives = [sorted(set(generator.normalize(v) for v in dictionary[str(ord(token))].split(",")))
                        if len(token) == 1 and ord(token) > 127 else [token.lower()] for token in tokens]
        full = ["".join(parts) for parts in itertools.product(*alternatives)]
        initials = ["".join(part[0] for part in parts) for parts in itertools.product(*alternatives)]
        name = " ".join(tokens)
        sample = rng.choice(full + initials)
        a = rng.randrange(len(sample))
        query = sample[a:rng.randrange(a + 1, len(sample) + 1)]
        if case % 3 == 0:
            query += "xyz"
        expected = query in name.lower() or any(query in stream for stream in full + initials)
        actual = match((name + ".epub").encode(), query.encode())
        assert actual == expected, (name, query, expected, actual)
    print("search: 500 official-dictionary expansion-oracle comparisons PASS")
    expected_candidates = {}
    for cp, readings in dictionary.items():
        cp = int(cp)
        if 0x4e00 <= cp <= 0x9fff:
            for reading in set(generator.normalize(v) for v in readings.split(",")):
                expected_candidates.setdefault(reading, set()).add(cp)
    candidates = lib.read_pico_search_candidates
    candidates.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t, ctypes.c_size_t]
    candidates.restype = ctypes.c_size_t
    buffer = (ctypes.c_uint32 * (max(map(len, expected_candidates.values())) + 1))()
    for reading, expected in expected_candidates.items():
        encoded = reading.encode()
        n = candidates(encoded, buffer, len(buffer), 0)
        ordered = list(buffer[:n])
        assert n == len(expected) and set(ordered) == expected, reading
        for skip in range(0, n + 1, 5):
            count = candidates(encoded, buffer, 5, skip)
            assert list(buffer[:count]) == ordered[skip:skip+5], (reading, skip)
        assert not candidates(encoded, buffer, 5, n)
    print(f"candidates: all {len(expected_candidates)} supported syllables preserve membership and paging")

obj = BUILD / "search.o"
subprocess.run(flags + ["-fstack-usage", "-c", str(source), "-o", str(obj)], check=True)
subprocess.run(["size", str(obj)], check=True)
print(obj.with_suffix(".su").read_text(), end="")
