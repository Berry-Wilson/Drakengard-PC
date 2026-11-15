#!/usr/bin/env python3
"""Turn a Drakengard (USA) disc image into a playable game on this PC.

Checks the disc, extracts it to iso_extracted/, recompiles the game's code to C++ and builds the
runner. Apart from the launcher's icon, nothing derived from the game is shipped: it is all made here
from the disc.
The launcher's first-run setup is a front end over this script, which also works by hand.

Usage:
  python tools/setup_game.py check-disc ISO     check that ISO is the supported disc
  python tools/setup_game.py doctor             report build tools, disk space and folder problems (JSON)
  python tools/setup_game.py get-tools          download the build tools (llvm-mingw, CMake, Ninja) into toolchain/
  python tools/setup_game.py run --iso ISO      do everything; a finished step is skipped next time

Lines starting with "@" are for the launcher: @step N TOTAL LABEL, @progress PERCENT, @error MESSAGE.
"""
import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = ROOT / "iso_extracted"
RECOMP_DIR = ROOT / "recomp"
SOURCE_DIR = ROOT / "external" / "PS2Recomp"
# Setup's own build, with the downloaded tools. A developer's Visual Studio build in out/build is separate.
BUILD_DIR = SOURCE_DIR / "out" / "build-game"
DEPS_DIR = ROOT / "deps"
RUNNER_DIR = BUILD_DIR / "ps2xRuntime"
RUNNER_EXE = RUNNER_DIR / "ps2EntryRunner.exe"
RECOMP_EXE = BUILD_DIR / "ps2xRecomp" / "ps2_recomp.exe"
ANALYZER_EXE = BUILD_DIR / "ps2xAnalyzer" / "ps2_analyzer.exe"
# The renderer (PCSX2's GS as ps2x-gs.dll) is loaded from this folder beside the runner. A release
# brings it built, in pcsx2-gs at its top, with the Visual C++ runtime it needs; in a clone it is built
# from a PCSX2 source tree into the Visual Studio build's runner folder.
RENDERER_DIR = RUNNER_DIR / "pcsx2-gs"
PACKAGED_RENDERER = ROOT / "pcsx2-gs"
DEVELOPER_RENDERER = SOURCE_DIR / "out" / "build" / "ps2xRuntime" / "Release" / "pcsx2-gs"
GENERATED_STAMP = RECOMP_DIR / "generated.stamp"
LOG_FILE = ROOT / "setup.log"

ELF_NAME = "SLUS_207.32"
ELF_SHA256 = "61a651dc07186bcf2c8196792702cd885b5ea6e4f69e95f8cf33035a130d48fc"
SECTOR = 2048

# The build tools, downloaded into toolchain/ from their official releases. Each is pinned to a
# tested version and checked against its SHA-256 before it is unpacked. "bin" is the folder, inside
# the unpacked archive, that goes on PATH for the build.
TOOLCHAIN_DIR = ROOT / "toolchain"
TOOLS = {
    "llvm-mingw": {
        "version": "20261104",
        "url": "https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip",
        "sha256": "552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f",
        "size": 191037046,
        "bin": "llvm-mingw-20261104-ucrt-x86_64/bin",
    },
    "cmake": {
        "version": "4.4.4",
        "url": "https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip",
        "sha256": "bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536",
        "size": 54400562,
        "bin": "cmake-4.4.4-windows-x86_64/bin",
    },
    "ninja": {
        "version": "1.13.2",
        "url": "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
        "sha256": "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65",
        "size": 291570,
        "bin": ".",
    },
}
TOOLS_DOWNLOAD_MB = sum(t["size"] for t in TOOLS.values()) // (1024 * 1024)

NEED_FREE_GB = 10
MAX_ROOT_CHARS = 70  # generated build paths add about 170 characters; Windows tools stop at 260
GB = 1024 ** 3


class SetupError(Exception):
    """A problem to show the player as it is."""


def say(line=""):
    print(line, flush=True)
    with open(LOG_FILE, "a", encoding="utf-8", errors="replace") as log:
        log.write(line + "\n")


def step(index, total, label):
    say(f"@step {index} {total} {label}")


def progress(percent):
    print(f"@progress {int(percent)}", flush=True)


# --- Disc ---------------------------------------------------------------------------------------


def read_iso_files(iso):
    """Returns (relative path, first sector, size) for every file on an ISO 9660 image."""
    size = os.path.getsize(iso)
    with open(iso, "rb") as f:
        f.seek(16 * SECTOR)
        volume = f.read(SECTOR)
        if len(volume) < SECTOR or volume[0] != 1 or volume[1:6] != b"CD001":
            raise SetupError("This isn't a PS2 DVD image. Choose an uncompressed .iso of the disc.")

        files = []

        def walk(sector, length, prefix):
            f.seek(sector * SECTOR)
            data = f.read(length)
            pos = 0
            while pos < len(data):
                record = data[pos]
                if record == 0:
                    pos = (pos // SECTOR + 1) * SECTOR
                    continue
                entry = data[pos:pos + record]
                pos += record
                name = entry[33:33 + entry[32]]
                if name in (b"\0", b"\1"):
                    continue
                first = struct.unpack("<I", entry[2:6])[0]
                length_ = struct.unpack("<I", entry[10:14])[0]
                text = name.decode("ascii", "replace").split(";")[0]
                if entry[25] & 2:
                    walk(first, length_, prefix + text + "/")
                else:
                    if first * SECTOR + length_ > size:
                        raise SetupError("This disc image is incomplete: it's shorter than the files on it. Make the image again.")
                    files.append((prefix + text, first, length_))

        root = volume[156:190]
        walk(struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0], "")
    return files


def check_disc(iso):
    """Returns the disc's file list, or raises SetupError saying why it can't be used."""
    if not os.path.isfile(iso):
        raise SetupError(f"The disc image wasn't found: {iso}")
    files = read_iso_files(iso)
    elf = next((f for f in files if f[0] == ELF_NAME), None)
    if elf is None:
        raise SetupError("This disc isn't Drakengard (USA, SLUS-20732). Other regions and games aren't supported.")
    with open(iso, "rb") as f:
        f.seek(elf[1] * SECTOR)
        digest = hashlib.sha256(f.read(elf[2])).hexdigest()
    if digest != ELF_SHA256:
        raise SetupError("This Drakengard disc has a different game program than the one supported (USA, SLUS-20732, version 1.00).")
    return files


def extract(iso, files):
    total = sum(f[2] for f in files)
    done = 0
    with open(iso, "rb") as src:
        for name, first, size in files:
            target = GAME_DIR / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_file() and target.stat().st_size == size:
                done += size
                progress(100 * done / total)
                continue
            say(f"Extracting {name}")
            partial = target.with_name(target.name + ".part")
            src.seek(first * SECTOR)
            left = size
            with open(partial, "wb") as dst:
                while left:
                    chunk = src.read(min(left, 4 * 1024 * 1024))
                    if not chunk:
                        raise SetupError("Reading the disc image failed. Make the image again.")
                    dst.write(chunk)
                    left -= len(chunk)
                    done += len(chunk)
                    progress(100 * done / total)
            os.replace(partial, target)
    (GAME_DIR / "mc0").mkdir(exist_ok=True)


# --- Build tools --------------------------------------------------------------------------------


def tool_folder(name):
    return TOOLCHAIN_DIR / name


def tool_ready(name):
    """A tool is ready when it was unpacked from the pinned archive (its stamp holds that hash)."""
    stamp = tool_folder(name) / "downloaded.sha256"
    return stamp.is_file() and stamp.read_text(encoding="ascii").strip() == TOOLS[name]["sha256"]


def find_tools():
    """Returns the build tools' paths, or None until all of them are downloaded."""
    if not all(tool_ready(name) for name in TOOLS):
        return None
    bins = {name: (tool_folder(name) / TOOLS[name]["bin"]).resolve() for name in TOOLS}
    return {
        "name": f"llvm-mingw {TOOLS['llvm-mingw']['version']}, CMake {TOOLS['cmake']['version']}, Ninja {TOOLS['ninja']['version']}",
        "cmake": str(bins["cmake"] / "cmake.exe"),
        "ninja": str(bins["ninja"] / "ninja.exe"),
        "cc": str(bins["llvm-mingw"] / "clang.exe"),
        "cxx": str(bins["llvm-mingw"] / "clang++.exe"),
        "path": [str(b) for b in bins.values()],
    }


def download(name):
    tool = TOOLS[name]
    folder = tool_folder(name)
    TOOLCHAIN_DIR.mkdir(exist_ok=True)
    archive = TOOLCHAIN_DIR / (name + ".zip.part")
    say(f"Downloading {name} {tool['version']} ({tool['size'] // (1024 * 1024)} MB) from {tool['url']}")
    digest = hashlib.sha256()
    with urllib.request.urlopen(tool["url"], timeout=60) as response, open(archive, "wb") as out:
        done = 0
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            out.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            progress(min(99, 100 * done / tool["size"]))
    if digest.hexdigest() != tool["sha256"]:
        archive.unlink(missing_ok=True)
        raise SetupError(f"The download of {name} was damaged or changed (its checksum doesn't match). Try again.")
    say(f"Unpacking {name}")
    if folder.exists():
        shutil.rmtree(folder)
    unpacked = Path(tempfile.mkdtemp(prefix=name + "-", dir=TOOLCHAIN_DIR))
    with zipfile.ZipFile(archive) as z:
        z.extractall(unpacked)
    os.replace(unpacked, folder)
    archive.unlink()
    (folder / "downloaded.sha256").write_text(tool["sha256"], encoding="ascii")


def get_tools():
    """Downloads whichever build tools are missing or not the pinned version."""
    for name in TOOLS:
        if not tool_ready(name):
            download(name)
    tools = find_tools()
    if tools is None:
        raise SetupError("The build tools were downloaded, but aren't usable. Delete the toolchain folder and try again.")
    return tools


def folder_problem():
    root = str(ROOT)
    if not root.isascii():
        return f"Move this folder to a path with only plain English letters, such as C:\\Games\\Drakengard. It's now in:\n{root}"
    if len(root) > MAX_ROOT_CHARS:
        return f"Move this folder to a shorter path, such as C:\\Games\\Drakengard. It's now in:\n{root}"
    return None


def doctor():
    tools = find_tools()
    free = shutil.disk_usage(ROOT).free / GB
    return {
        "tools": tools["name"] if tools else "",
        "toolsDownloadMb": TOOLS_DOWNLOAD_MB,
        "freeGb": int(free),
        "needGb": NEED_FREE_GB,
        "folderProblem": folder_problem() or "",
    }


# --- Build --------------------------------------------------------------------------------------


def parallel_jobs():
    class Memory(ctypes.Structure):
        _fields_ = [("length", ctypes.c_uint32), ("load", ctypes.c_uint32), ("total", ctypes.c_uint64), ("rest", ctypes.c_uint64 * 6)]

    memory = Memory()
    memory.length = ctypes.sizeof(Memory)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory))
    return max(1, min(os.cpu_count() or 1, memory.total // (4 * GB)))


def run(command, cwd=ROOT, on_line=None, env=None):
    say("> " + " ".join(str(c) for c in command))
    process = subprocess.Popen([str(c) for c in command], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               encoding="utf-8", errors="replace", env=env)
    tail = []
    for line in process.stdout:
        line = line.rstrip()
        say(line)
        tail = (tail + [line])[-8:]
        if on_line:
            on_line(line)
    if process.wait() != 0:
        raise SetupError("A build step failed. The last lines were:\n" + "\n".join(tail) + f"\n\nThe full log is in {LOG_FILE}")


def run_python(script, *args):
    run([sys.executable, ROOT / "tools" / script, *args])


def bundled_dependencies():
    """Sources shipped in deps/ replace FetchContent's git clones, so the build needs no git."""
    if not DEPS_DIR.is_dir():
        return []
    defines = []
    for path in sorted(DEPS_DIR.iterdir()):
        if path.is_dir():
            defines.append(f"-DFETCHCONTENT_SOURCE_DIR_{path.name.upper()}={path.as_posix()}")
    return defines


def tool_environment(tools):
    """The build tools first on PATH, so CMake and the compiler find their own helpers."""
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(tools["path"] + [env.get("PATH", "")])
    return env


def configure(tools):
    run([tools["cmake"], "-S", SOURCE_DIR, "-B", BUILD_DIR, "-G", "Ninja",
         f"-DCMAKE_MAKE_PROGRAM={Path(tools['ninja']).as_posix()}",
         f"-DCMAKE_C_COMPILER={Path(tools['cc']).as_posix()}",
         f"-DCMAKE_CXX_COMPILER={Path(tools['cxx']).as_posix()}",
         "-DCMAKE_BUILD_TYPE=Release",
         "-DPS2X_BUILD_TEST=OFF", "-DPS2X_BUILD_STUDIO=OFF",
         f"-DPS2X_GAME_RECOMP_DIR={(ROOT / 'recomp' / 'batched').as_posix()}",
         f"-DPS2X_GAME_SRC_DIR={(ROOT / 'src').as_posix()}",
         f"-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}", *bundled_dependencies()],
        env=tool_environment(tools))


def build_target(tools, target):
    # Ninja numbers its steps: "[12/177] Building CXX object ..."
    pattern = re.compile(r"^\[(\d+)/(\d+)\]")

    def on_line(line):
        match = pattern.match(line)
        if match:
            progress(min(99, 100 * int(match.group(1)) / max(1, int(match.group(2)))))

    run([tools["cmake"], "--build", BUILD_DIR, "--target", target, "--parallel", parallel_jobs()],
        on_line=on_line, env=tool_environment(tools))


def generate_game_code():
    """The translated game code is only made again for a different disc or changed tools."""
    tool_hash = hashlib.sha256()
    for path in sorted((ROOT / "tools").glob("*.py")) + [RECOMP_DIR / "recompile.txt", ANALYZER_EXE, RECOMP_EXE]:
        tool_hash.update(path.read_bytes())
    stamp = ELF_SHA256 + tool_hash.hexdigest()
    if GENERATED_STAMP.is_file() and GENERATED_STAMP.read_text() == stamp:
        say("The game code is already translated.")
        return
    GENERATED_STAMP.unlink(missing_ok=True)
    run_python("generate_recomp_config.py", ANALYZER_EXE)
    progress(10)
    run([RECOMP_EXE, Path("recomp") / "drakengard.toml"])
    progress(70)
    run_python("pack_recomp_sources.py")
    progress(80)
    run_python("vu1_recomp.py", "--unit", "vu1")
    run_python("vu1_recomp.py", "--unit", "vu0")
    GENERATED_STAMP.write_text(stamp)
    progress(100)


def copy_renderer():
    for source in (PACKAGED_RENDERER, DEVELOPER_RENDERER):
        if (source / "ps2x-gs.dll").is_file():
            shutil.copytree(source, RENDERER_DIR, dirs_exist_ok=True, ignore=shutil.ignore_patterns("gs-data"))
            break
    if not (RENDERER_DIR / "ps2x-gs.dll").is_file():
        raise SetupError("The game is built, but its renderer is missing. Build it into\n"
                         f"{RENDERER_DIR}\n(see external\\PS2Recomp\\ps2xRuntime\\pcsx2-gs\\README.md).")


def run_setup(iso):
    total = 6
    problem = folder_problem()
    if problem:
        raise SetupError(problem)

    step(1, total, "Checking your disc")
    files = check_disc(iso)
    needed = sum(f[2] for f in files) / GB + 5
    if shutil.disk_usage(ROOT).free / GB < needed and not (GAME_DIR / ELF_NAME).is_file():
        raise SetupError(f"Setup needs about {NEED_FREE_GB} GB of free space on this drive.")
    say(f"Drakengard (USA) found: {len(files)} files")

    step(2, total, "Extracting the game files")
    extract(iso, files)

    step(3, total, "Getting the build tools ready")
    tools = get_tools()
    say(f"Using {tools['name']}")
    configure(tools)

    step(4, total, "Building the recompiler")
    build_target(tools, "ps2_recomp")
    build_target(tools, "ps2_analyzer")

    step(5, total, "Translating the game's code to C++")
    generate_game_code()
    configure(tools)

    step(6, total, "Compiling the game (the longest step)")
    build_target(tools, "ps2EntryRunner")
    if not RUNNER_EXE.is_file():
        raise SetupError(f"The build finished without making the game. The full log is in {LOG_FILE}")
    copy_renderer()
    progress(100)
    say("Setup finished.")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check-disc").add_argument("iso")
    commands.add_parser("doctor")
    commands.add_parser("get-tools")
    run_parser = commands.add_parser("run")
    run_parser.add_argument("--iso", required=True)
    args = parser.parse_args()

    try:
        if args.command == "check-disc":
            print(f"OK {len(check_disc(args.iso))} files")
        elif args.command == "doctor":
            print(json.dumps(doctor()))
        elif args.command == "get-tools":
            print(get_tools()["name"])
        else:
            LOG_FILE.write_text("", encoding="utf-8")
            run_setup(args.iso)
    except SetupError as e:
        print("@error " + str(e).replace("\n", "\\n"), flush=True)
        sys.exit(1)
    except OSError as e:
        print(f"@error {e}", flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
