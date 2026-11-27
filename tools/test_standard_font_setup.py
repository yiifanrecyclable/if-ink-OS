#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise first-install prompting and checked font installation on private cards."""
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NAME = "PicoSans-Medium.ttf"
TTF = ROOT / "assets/fonts" / NAME
DEFAULT_PATH = "/sdcard/fonts/" + NAME
OPEN = "t,342,1005,342,1005"
PROCEED = "t,515,1080,515,1080"
LATER = "t,170,1080,170,1080"
COMPLETE = 2


def scenario(base_name, verify):
    with TemporaryDirectory(prefix=base_name, dir=ROOT / "build") as directory:
        work = Path(directory)
        card = work / "card"
        out = work / "build/simulator"
        for folder in (card / "fonts", card / "books", card / "markdown", out):
            folder.mkdir(parents=True)
        (out / "titles.txt").touch()
        shutil.copyfile(TTF, work / "font-source.ttf")
        nvs = out / "nvs/read_pico"

        def render(view="baseline-home", events="", connected=False, absent=False, **faults):
            env = dict(os.environ, PICO_SIM_CARD_ROOT=str(card), PICO_SIM_FRESH_FONT_SETUP="1",
                       PICO_SIM_STANDARD_FONT_SOURCE="font-source.ttf", **faults)
            if connected:
                env["PICO_SIM_STANDARD_FONT_WIFI"] = "1"
            if absent:
                env["PICO_SIM_CARD_ABSENT"] = "1"
            command = [str(ROOT / "build/simulator/render"), view, "", "0", "48",
                       "build/simulator/frame.bin", "build/simulator/titles.txt", "0", "0",
                       "builtin", events, ""]
            result = subprocess.run(command, cwd=work, env=env, text=True, capture_output=True,
                                    timeout=30, check=True)
            return json.loads(result.stdout)

        verify(render, work, card / "fonts" / NAME, nvs)


def fresh(render, work, target, nvs):
    assert render(absent=True)["standard_font_setup"] == [0, 0, 0]
    assert (nvs / "font_setup").read_bytes() == b"\x01"
    assert render()["standard_font_setup"] == [1, 0, 0]
    wifi = render(events=PROCEED)
    assert wifi["route"] == 5 and wifi["transfer_entry"] == 4 and not target.exists()
    assert render(events=LATER)["standard_font_setup"] == [0, 0, 0]
    assert (nvs / "font_setup").read_bytes() == b"\x00"
    assert render()["standard_font_setup"] == [0, 0, 0]
    installed = render("settings-5", OPEN + ";" + PROCEED, connected=True)
    assert installed["standard_font_setup"] == [0, 1, COMPLETE]
    assert installed["font_path"] == DEFAULT_PATH and installed["system_font_path"] == ""
    assert target.read_bytes() == TTF.read_bytes()
    assert not (target.parent / ".ifink-standard-font.part").exists()
    assert render("settings-5", OPEN)["standard_font_setup"] == [0, 0, 0]


def upgraded(render, work, target, nvs):
    nvs.mkdir(parents=True)
    (nvs / "language").write_bytes(b"\0")
    assert render()["standard_font_setup"] == [0, 0, 0]
    custom = target.with_name("Custom.ttf")
    shutil.copyfile(ROOT / "main/assets/builtin.ttf", custom)
    (nvs / "sys_font").write_bytes(b"/sdcard/fonts/Custom.ttf\0")
    (nvs / "font").write_bytes(b"/sdcard/fonts/Custom.ttf\0")
    for content in (b"short payload", b"x" * TTF.stat().st_size):
        (work / "font-source.ttf").write_bytes(content)
        state = render("settings-5", OPEN + ";" + PROCEED, connected=True)
        assert state["standard_font_setup"][2] in (5, 6), state["standard_font_setup"]
        assert not target.exists() and not (target.parent / ".ifink-standard-font.part").exists()
    shutil.copyfile(TTF, work / "font-source.ttf")
    state = render("settings-5", OPEN + ";" + PROCEED, connected=True, PICO_SIM_CANCEL_FONT_DOWNLOAD="1")
    assert state["standard_font_setup"][2] == 3
    assert not target.exists() and not (target.parent / ".ifink-standard-font.part").exists()
    state = render("settings-5", OPEN + ";" + PROCEED, connected=True, PICO_TEST_FSYNC_FAIL="1")
    assert state["standard_font_setup"][2] == 7
    assert not target.exists() and not (target.parent / ".ifink-standard-font.part").exists()
    target.write_bytes(b"existing file")
    assert render("settings-5", OPEN + ";" + PROCEED, connected=True)["standard_font_setup"][2] == 0
    assert target.read_bytes() == b"existing file"
    target.unlink()
    state = render("settings-5", OPEN + ";" + PROCEED, connected=True)
    assert state["standard_font_setup"][2] == COMPLETE
    assert state["font_path"] == state["system_font_path"] == state["reading_font_path"] == "/sdcard/fonts/Custom.ttf"
    assert target.read_bytes() == TTF.read_bytes()


def after_wifi(render, work, target, nvs):
    state = render(events=PROCEED + ";n,1000,0,0,0",
                   PICO_SIM_STANDARD_FONT_WIFI_ROUNDTRIP="1")
    assert state["route"] == -1 and state["standard_font_setup"] == [1, 0, COMPLETE]
    assert state["font_path"] == DEFAULT_PATH and target.read_bytes() == TTF.read_bytes()
    assert (nvs / "font_setup").read_bytes() == b"\x00"


scenario("font-setup-fresh-", fresh)
scenario("font-setup-upgrade-", upgraded)
scenario("font-setup-wifi-", after_wifi)
print("PASS fresh/absent-card prompts, Wi-Fi return and automatic download, skip persistence, checked install, cancellation, retry, bad data and sync failure, existing-file protection, and unchanged preferences")
