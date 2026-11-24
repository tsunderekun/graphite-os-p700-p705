#!/usr/bin/env python3
"""Render every Metro shell screen at 2x (960x1600) into artifacts/shell/hidpi/.

Screens are plain 480x800 pages opened over file://, so Chrome's headless
screenshot is the whole device. Run:

    python shell/render_hidpi.py            # all screens
    python shell/render_hidpi.py m-home     # one or more screens

The script starts Chrome with a throw-away profile, waits for the PNG to stop
growing (Chrome's launcher process exits before the child finishes writing),
then verifies the exact image size with PIL and checks the borders for
white strips / scrollbars. Nothing in the pages is modified.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SHELL = ROOT / "shell"
OUT = ROOT / "artifacts" / "shell" / "hidpi"

CHROME = Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe")

# 480x800 CSS pixels at 2x => exactly 960x1600 device pixels.
CSS_W, CSS_H, SCALE = 480, 800, 2
EXPECTED = (CSS_W * SCALE, CSS_H * SCALE)

SCREENS = [
    "m-home",
    "m-player",
    "m-queue",
    "m-info",
    "m-settings",
    "m-apps",
    "m-edit",
    "m-shsettings",
    "m-terminal",
    "m-notes",
    "m-files",
    "m-browser",
    "m-shade",
    "m-lock",
    "m-power",
]


def wait_for_png(path: Path, timeout: float = 90.0) -> None:
    """Wait until the screenshot exists and its size has settled."""
    deadline = time.time() + timeout
    last = -1
    stable = 0
    while time.time() < deadline:
        if path.exists():
            try:
                size = path.stat().st_size
            except OSError:
                size = -1
            if size > 0 and size == last:
                stable += 1
                if stable >= 3:
                    return
            else:
                stable = 0
                last = size
        time.sleep(0.15)
    raise TimeoutError(f"{path.name}: screenshot was not written within {timeout:.0f}s")


def probe_borders(im: Image.Image) -> list[str]:
    """Report suspiciously light edges (scrollbars, letterboxing, clipping)."""
    problems: list[str] = []
    w, h = im.size
    px = im.load()
    edges = {
        "left": [(0, y) for y in range(h)],
        "right": [(w - 1, y) for y in range(h)],
        "top": [(x, 0) for x in range(w)],
        "bottom": [(x, h - 1) for x in range(w)],
    }
    for name, coords in edges.items():
        light = sum(1 for p in coords if min(px[p][:3]) > 200)
        if light / len(coords) > 0.6:
            problems.append(f"{name} edge is {light * 100 // len(coords)}% light")
    return problems


def render(name: str, timeout: float, keep_profile: bool = False) -> tuple[bool, str]:
    src = SHELL / f"{name}.html"
    dst = OUT / f"{name}.png"
    if not src.exists():
        return False, f"missing page: {src}"

    if dst.exists():
        dst.unlink()

    profile = Path(tempfile.mkdtemp(prefix="chrome-hidpi-"))
    try:
        cmd = [
            str(CHROME),
            "--headless=new",
            "--disable-gpu",
            "--hide-scrollbars",
            "--virtual-time-budget=2000",
            f"--window-size={CSS_W},{CSS_H}",
            f"--force-device-scale-factor={SCALE}",
            f"--user-data-dir={profile}",
            f"--screenshot={dst}",
            src.as_uri(),
        ]
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            # Chrome's launcher may exit immediately; do not rely on the code.
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
        wait_for_png(dst, timeout=timeout)
    finally:
        if not keep_profile:
            shutil.rmtree(profile, ignore_errors=True)

    try:
        with Image.open(dst) as im:
            im.load()
            size, mode = im.size, im.mode
            problems = probe_borders(im.convert("RGB"))
    except Exception as exc:  # noqa: BLE001 - want the raw reason in the report
        return False, f"{name}: unreadable PNG ({exc})"

    notes = []
    if size != EXPECTED:
        return False, f"{name}: {size[0]}x{size[1]}, expected {EXPECTED[0]}x{EXPECTED[1]}"
    if problems:
        notes.append("; ".join(problems))
    msg = f"{name}: {size[0]}x{size[1]} {mode} {dst.stat().st_size // 1024} KB"
    if notes:
        msg += "  [warn] " + " | ".join(notes)
    return not problems, msg


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("screens", nargs="*", default=[], help="screen ids, e.g. m-home")
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("--keep-profile", action="store_true", help="do not delete Chrome profiles")
    args = ap.parse_args()

    if not CHROME.exists():
        print(f"Chrome not found: {CHROME}", file=sys.stderr)
        return 2

    names = args.screens or SCREENS
    unknown = [n for n in names if n not in SCREENS]
    if unknown:
        print(f"unknown screens: {', '.join(unknown)}", file=sys.stderr)
        return 2

    OUT.mkdir(parents=True, exist_ok=True)
    failures = 0
    for i, name in enumerate(names, 1):
        ok, msg = render(name, args.timeout, args.keep_profile)
        print(f"[{i:02d}/{len(names)}] {msg}", flush=True)
        failures += 0 if ok else 1

    print(f"\ndone: {len(names) - failures}/{len(names)} clean -> {OUT}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
