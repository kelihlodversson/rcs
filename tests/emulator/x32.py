#!/usr/bin/env python3
"""Open, edit, save and reload RCS on the resident libaes desktop.

Reuses the pinned pTOS desktop test's PS/2 driver and raw FAT disk fixture.
Requires its desktop-tests configuration with embedded console and libaes.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ptos", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf-code", required=True)
    parser.add_argument("--ovmf-vars", required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/emulator/x32")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    args.ptos = args.ptos.resolve()
    args.output = args.output.resolve()
    args.embedded_console = True
    spec = importlib.util.spec_from_file_location(
        "ptos_desktop", args.ptos / "tests/x32_desktop/run.py")
    desktop = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(desktop)
    os.chdir(args.ptos)
    original_run = subprocess.run

    def stage(command, *positional, **keywords):
        # Replace the fixture's test client before it snapshots the FAT image.
        # APPTEST retains the fixture's public desktop launch coordinates.
        if command[0] == "tools/mkhdisk.sh" and Path(command[1]).name == "system.img":
            folder = Path(command[3]).parent
            shutil.copy(ROOT / "build/x32/RCS.PRG", folder / "APPTEST.PRG")
            command = command + [str(ROOT / "build/x32" / name) for name in
                                 ("RCS.RSC", "RCSLOW.RSC", "RCS.DFN", "RCSLOW.DFN")]
        return original_run(command, *positional, **keywords)

    with patch.object(desktop.subprocess, "run", side_effect=stage):
        guest = desktop.Guest(args.output, args)
    try:
        guest.ready(1)
        guest.move(34, 45)
        guest.double_click()
        guest.wait("x32 desktop window OPEN")
        guest.move(135, 8)
        time.sleep(0.25)
        guest.move(135, 44)
        guest.click()
        text = guest.wait("x32 desktop program TARGET")
        point = re.findall(r"x32 desktop program TARGET (\d+) (\d+)", text)[-1]
        guest.move(*map(int, point))
        guest.click()
        guest.key("ctrl-o")
        guest.wait("x32 desktop LAUNCH")
        time.sleep(3)
        guest.screenshot("startup")

        def selection(filename):
            guest.move(600, 320)
            guest.click()
            guest.key("esc")
            for character in filename.lower():
                guest.key("dot" if character == "." else character)
            guest.move(540, 560)
            guest.click()
            time.sleep(3)

        guest.key("ctrl-o")
        time.sleep(1)
        selection("RCS.RSC")
        guest.screenshot("loaded")
        guest.key("ctrl-m")
        time.sleep(1)
        selection("ROUND.RSC")
        guest.screenshot("roundtrip")

        # Remove the first (menu) tree with RCS's standard Edit/Delete shortcut.
        guest.move(120, 60)
        guest.click()
        guest.key("alt-d")
        guest.key("ctrl-m")
        time.sleep(1)
        selection("RESULT.RSC")
        guest.screenshot("edited")
        guest.key("ctrl-o")
        time.sleep(1)
        selection("RESULT.RSC")
        guest.screenshot("reloaded")
        guest.key("ctrl-q")
        text = guest.wait(r"x32 desktop CH_EXIT \d+ 0000", regex=True)
        assert "process fault:" not in text, text[-3000:]
        guest.process.terminate()
        guest.process.wait(timeout=5)
        for name in ("ROUND.RSC", "RESULT.RSC", "RESULT.DFN"):
            original_run(["mcopy", "-o", "-i", str(guest.root / "system.img") +
                          "@@1048576", "::" + name, str(args.output / name)], check=True)
        golden = (ROOT / "tests/golden/rcs.out.rsc").read_bytes()
        assert (args.output / "ROUND.RSC").read_bytes() == golden
        edited = (args.output / "RESULT.RSC").read_bytes()
        assert struct.unpack_from(">H", edited, 22)[0] == struct.unpack_from(">H", golden, 22)[0] - 1
        assert struct.unpack_from(">H", edited, 20)[0] < struct.unpack_from(">H", golden, 20)[0]
        print("RCS x32: load, golden round trip, edit, save, reload and clean exit passed")
    finally:
        guest.close()


if __name__ == "__main__":
    main()
