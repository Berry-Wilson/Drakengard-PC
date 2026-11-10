#!/usr/bin/env python3
"""Build the player's zip: the launcher, the sources and tools setup needs. The only thing from the game
in it is the crest used as the launcher's icon.

The fork in external/PS2Recomp is taken at its HEAD commit. The zip is refused if any file derived from
the game is found in it.

Usage: python tools/make_release.py [--allow-dirty] [--name DrakengardPC-x.y] [--no-zip]
Build the launcher first (see launcher/README.md).
"""
import argparse
import datetime
import io
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from setup_game import find_tools  # noqa: E402

DIST = ROOT / "out" / "dist"
FORK = ROOT / "external" / "PS2Recomp"
LAUNCHER_BUILD = ROOT / "launcher" / "out" / "build" / "Release"
GAME_ICON = ROOT / "launcher" / "res" / "drakengard.ico"
PYTHON_VERSION = "3.12.10"
PYTHON_URL = f"https://www.python.org/ftp/python/{PYTHON_VERSION}/python-{PYTHON_VERSION}-embed-amd64.zip"

PROJECT_FILES = [
    "LICENSE",
    "recomp/drakengard.toml",
    "recomp/drakengard_analyzer.toml",
    "recomp/functions.csv",
    "src/drakengard_overrides.cpp",
    "tools/generate_recomp_config.py",
    "tools/pack_recomp_sources.py",
    "tools/setup_game.py",
    "tools/vu1_disasm.py",
    "tools/vu1_recomp.py",
]
# FetchContent names; setup_game.py passes deps/<name> as FETCHCONTENT_SOURCE_DIR_<NAME>.
DEPENDENCIES = ["elfio", "fmt", "imgui", "libdwarf", "nlohmann_json", "rabbitizer", "raylib", "rlimgui", "toml11"]
# Test data and samples nothing builds, some of it foreign ELFs.
SKIP_DEPENDENCY_DIRS = {"tests", "test", "fuzz", "benchmarks"}

PLAYER_README = """Drakengard PC (work in progress)
================================

You need your own Drakengard disc image (USA, SLUS-20732, .iso). No game files are included; the
launcher's icon is the game's crest.

1. Unzip this folder to a short path, such as C:\\Games\\Drakengard.
2. Run drakengard-launcher.exe. Windows may warn about an unknown publisher:
   choose More info > Run anyway.
3. Choose your disc image and press "Set up the game". The game is built on this PC from your disc:
   20 to 60 minutes the first time, about 10 GB of disk space, and internet for one download.
   Microsoft's C++ Build Tools are installed if you don't have them (about 6 GB more).
4. Press Play.

Saves are in iso_extracted\\mc0. If setup fails, setup.log in this folder has the details.
"""

NOTICES = """Third-party software in this package
=====================================

Qt 6 (Qt6Core, Qt6Gui, Qt6Widgets and plugins) is used under the GNU LGPL version 3, dynamically linked.
Source: https://download.qt.io/official_releases/qt/ and https://github.com/qt/qtbase

Python is in python\\ under the Python Software Foundation License (python\\LICENSE.txt).

Microsoft Visual C++ runtime DLLs are redistributed under the Visual Studio license terms.

The sources in deps\\ and external\\ carry their own license files: raylib, Dear ImGui, rlImGui, ELFIO, fmt,
toml11, nlohmann/json, libdwarf, rabbitizer and PS2Recomp.
FFmpeg (LGPL build) is downloaded during setup from https://github.com/System233/ffmpeg-msvc-prebuilt

This project: GPL-3.0, see LICENSE.

The launcher's icon is the crest from Drakengard's memory card icon, (C) cavia inc. / Square Enix Co., Ltd.
It isn't covered by the licenses above.
"""

GUARD_SUFFIXES = (".iso", ".irx", ".img")
GUARD_FOLDERS = ("recomp/output/", "recomp/batched/", "iso_extracted/", "unpacked/", "mc0/", "analysis/", "input_recordings/")
GUARD_PREFIXES = ("SLUS_", "SCUS_", "SLES_", "vu0_programs", "vu1_programs")
MAX_FILE = 200 * 1024 * 1024


def git(*args, cwd=ROOT):
    return subprocess.run(["git", "-C", str(cwd), *args], capture_output=True, check=True).stdout


def copy_tracked(source, target, recurse=False, skip=None):
    args = ["ls-files", "-z"] + (["--recurse-submodules"] if recurse else [])
    for name in git(*args, cwd=source).decode("utf-8").split("\0"):
        path = source / name
        if not name or not path.is_file() or (skip and skip(name)):
            continue
        out = target / name
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, out)


def stage_fork(target):
    head = git("rev-parse", "--short", "HEAD", cwd=FORK).decode().strip()
    dirty = git("status", "--short", cwd=FORK).strip()
    print(f"PS2Recomp at commit {head}" + (" (uncommitted changes in its working tree are NOT included)" if dirty else ""))
    target.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", "HEAD", cwd=FORK))) as tar:
        tar.extractall(target, filter="data")


def stage_python(target):
    cache = DIST / f"python-{PYTHON_VERSION}-embed-amd64.zip"
    if not cache.is_file():
        print(f"Downloading {PYTHON_URL}")
        urllib.request.urlretrieve(PYTHON_URL, cache)
    with zipfile.ZipFile(cache) as z:
        z.extractall(target)


def stage_vc_runtime(target):
    tools = find_tools()
    if tools is None:
        sys.exit("Visual Studio wasn't found")
    crt = sorted(Path(tools["install"], "VC", "Redist", "MSVC").glob("*/x64/Microsoft.VC*.CRT"))
    if not crt:
        sys.exit("The Visual C++ redistributable DLLs weren't found")
    for dll in crt[-1].glob("*.dll"):
        shutil.copy2(dll, target / dll.name)


def guard(stage):
    """Lists anything in the package that looks like it came from the disc."""
    problems = []
    for path in stage.rglob("*"):
        if not path.is_file():
            continue
        name = path.relative_to(stage).as_posix()
        if path.suffix.lower() in GUARD_SUFFIXES or path.name.startswith(GUARD_PREFIXES):
            problems.append(f"{name}: named like a disc file or translated game code")
        if name.startswith(GUARD_FOLDERS):
            problems.append(f"{name}: in a generated or game folder")
        size = path.stat().st_size
        if size > MAX_FILE:
            problems.append(f"{name}: over 200 MB")
        with open(path, "rb") as f:
            head = f.read(20)
            if len(head) == 20 and head[:4] == b"\x7fELF" and struct.unpack("<H", head[18:20])[0] == 8:
                problems.append(f"{name}: a MIPS ELF")
            if size > 32774:
                f.seek(32769)
                if f.read(5) == b"CD001":
                    problems.append(f"{name}: an ISO 9660 image")
    return problems


def write_zip(stage, target):
    # A day back, so no file looks newer than the build in any time zone.
    when = (datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1)).timetuple()[:6]
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo((Path(stage.name) / path.relative_to(stage)).as_posix(), when)
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, "rb") as src, z.open(info, "w", force_zip64=True) as dst:
                    shutil.copyfileobj(src, dst, 1024 * 1024)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--allow-dirty", action="store_true", help="package uncommitted project files")
    parser.add_argument("--name", default="DrakengardPC")
    parser.add_argument("--no-zip", action="store_true", help="stop after the folder, for testing")
    args = parser.parse_args()

    if not (LAUNCHER_BUILD / "drakengard-launcher.exe").is_file():
        sys.exit("Build the launcher first: see launcher/README.md")
    if not args.allow_dirty and git("status", "--short").strip():
        sys.exit("The project has uncommitted changes. Commit them or pass --allow-dirty.")
    if not GAME_ICON.is_file():
        sys.exit("The launcher's icon is missing. Run tools/make_launcher_icon.py and rebuild the launcher.")
    if GAME_ICON.stat().st_mtime > (LAUNCHER_BUILD / "drakengard-launcher.exe").stat().st_mtime:
        sys.exit("The launcher's icon is newer than its build. Rebuild the launcher.")

    started = time.time()
    DIST.mkdir(parents=True, exist_ok=True)
    stage = DIST / args.name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir()

    shutil.copytree(LAUNCHER_BUILD, stage, dirs_exist_ok=True, ignore=shutil.ignore_patterns("*.pdb"))
    stage_vc_runtime(stage)
    for name in PROJECT_FILES:
        out = stage / name
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, out)
    (stage / "README.txt").write_text(PLAYER_README, encoding="utf-8")
    (stage / "THIRD_PARTY_NOTICES.txt").write_text(NOTICES, encoding="utf-8")

    stage_fork(stage / "external" / "PS2Recomp")
    for dep in DEPENDENCIES:
        copy_tracked(FORK / "out" / "build" / "_deps" / f"{dep}-src", stage / "deps" / dep,
                     skip=lambda name: name.split("/")[0].lower() in SKIP_DEPENDENCY_DIRS)
    stage_python(stage / "python")

    problems = guard(stage)
    if problems:
        print("\n".join(problems[:40]))
        sys.exit(f"Refused: {len(problems)} file(s) look like game data or code. No zip was written.")
    print("Checked: no file in the package looks like it came from the game.")

    if not args.no_zip:
        target = DIST / f"{args.name}-windows.zip"
        write_zip(stage, target)
        print(f"{target} ({target.stat().st_size // (1024 * 1024)} MB)")
    print(f"Done in {time.time() - started:.0f} s")


if __name__ == "__main__":
    main()
