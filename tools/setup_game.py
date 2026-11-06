#!/usr/bin/env python3
"""Turn a Drakengard (USA) disc image into a playable game on this PC.

Checks the disc, extracts it to iso_extracted/, recompiles the game's code to C++ and builds the
runner. Apart from the launcher's icon, nothing derived from the game is shipped: it is all made here
from the disc.
The launcher's first-run setup is a front end over this script, which also works by hand.

Usage:
  python tools/setup_game.py check-disc ISO     check that ISO is the supported disc
  python tools/setup_game.py doctor             report build tools, disk space and folder problems (JSON)
  python tools/setup_game.py install-tools      install Microsoft's C++ Build Tools (about 6 GB)
  python tools/setup_game.py run --iso ISO      do everything; a finished step is skipped next time

Lines starting with "@" are for the launcher: @step N TOTAL LABEL, @progress PERCENT, @error MESSAGE.
"""
import argparse
import ctypes
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = ROOT / "iso_extracted"
RECOMP_DIR = ROOT / "recomp"
SOURCE_DIR = ROOT / "external" / "PS2Recomp"
BUILD_DIR = SOURCE_DIR / "out" / "build"
DEPS_DIR = ROOT / "deps"
RUNNER_DIR = BUILD_DIR / "ps2xRuntime" / "Release"
RUNNER_EXE = RUNNER_DIR / "ps2EntryRunner.exe"
RECOMP_EXE = BUILD_DIR / "ps2xRecomp" / "Release" / "ps2_recomp.exe"
GENERATED_STAMP = RECOMP_DIR / "generated.stamp"
LOG_FILE = ROOT / "setup.log"

ELF_NAME = "SLUS_207.32"
ELF_SHA256 = "61a651dc07186bcf2c8196792702cd885b5ea6e4f69e95f8cf33035a130d48fc"
SECTOR = 2048

BUILD_TOOLS_URL = "https://aka.ms/vs/stable/vs_BuildTools.exe"
BUILD_TOOLS_ARGS = [
    "--passive", "--wait", "--norestart",
    "--add", "Microsoft.VisualStudio.Workload.VCTools",
    "--add", "Microsoft.VisualStudio.Component.VC.CMake.Project",
    "--includeRecommended",
]
GENERATORS = {17: "Visual Studio 17 2022", 18: "Visual Studio 18 2026"}

NEED_FREE_GB = 10
MAX_ROOT_CHARS = 70  # generated build paths add about 170 characters; MSBuild stops at 260
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


def find_tools():
    """Returns {"generator", "cmake", "name"} for a usable Visual Studio, or None."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    out = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-utf8", "-format", "json"], capture_output=True, encoding="utf-8", errors="replace")
    try:
        instance = json.loads(out.stdout)[0]
    except (ValueError, IndexError):
        return None
    generator = GENERATORS.get(int(instance["installationVersion"].split(".")[0]))
    if generator is None:
        return None
    install = Path(instance["installationPath"])
    cmake = install / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe"
    if not cmake.is_file():
        found = shutil.which("cmake")
        if not found:
            return None
        cmake = Path(found)
    kits = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "Include"
    if not glob.glob(str(kits / "*" / "um" / "windows.h")):
        return None
    return {"generator": generator, "cmake": str(cmake), "name": instance.get("displayName", generator), "install": str(install)}


def install_tools():
    target = ROOT / "vs_BuildTools.exe"
    say("Downloading Microsoft's C++ Build Tools installer")
    urllib.request.urlretrieve(BUILD_TOOLS_URL, target)
    say("Running the installer (about 6 GB; Windows asks for permission)")
    result = subprocess.run([str(target)] + BUILD_TOOLS_ARGS)
    target.unlink(missing_ok=True)
    if result.returncode == 1602:
        raise SetupError("The build tools weren't installed: the installer was cancelled.")
    if result.returncode not in (0, 3010):
        raise SetupError(f"The build tools installer failed with code {result.returncode}.")
    if find_tools() is None:
        raise SetupError("The build tools installed, but weren't found. Restart Windows and try again.")


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


def run(command, cwd=ROOT, on_line=None):
    say("> " + " ".join(str(c) for c in command))
    process = subprocess.Popen([str(c) for c in command], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               encoding="utf-8", errors="replace")
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


def configure(tools):
    run([tools["cmake"], "-S", SOURCE_DIR, "-B", BUILD_DIR, "-G", tools["generator"], "-A", "x64",
         "-DPS2X_BUILD_ANALYZER=OFF", "-DPS2X_BUILD_TEST=OFF", "-DPS2X_BUILD_STUDIO=OFF",
         f"-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}", *bundled_dependencies()])


def build_target(tools, target, total_files=0):
    compiled = [0]
    pattern = re.compile(r"^\s+[\w.\-]+\.(cpp|cc|cxx|c)\s*$")

    def on_line(line):
        if total_files and pattern.match(line):
            compiled[0] += 1
            progress(min(99, 100 * compiled[0] / total_files))

    run([tools["cmake"], "--build", BUILD_DIR, "--config", "Release", "--target", target, "--parallel", parallel_jobs(),
         "--", "/nr:false", "/v:minimal"], on_line=on_line)


def generate_game_code():
    """The translated game code is only made again for a different disc or changed tools."""
    tool_hash = hashlib.sha256()
    for path in sorted((ROOT / "tools").glob("*.py")) + sorted(RECOMP_DIR.glob("*.toml")) + [RECOMP_EXE]:
        tool_hash.update(path.read_bytes())
    stamp = ELF_SHA256 + tool_hash.hexdigest()
    if GENERATED_STAMP.is_file() and GENERATED_STAMP.read_text() == stamp:
        say("The game code is already translated.")
        return
    GENERATED_STAMP.unlink(missing_ok=True)
    run_python("generate_recomp_config.py")
    progress(10)
    run([RECOMP_EXE, Path("recomp") / "drakengard.toml"])
    progress(70)
    run_python("pack_recomp_sources.py")
    progress(80)
    run_python("vu1_recomp.py", "--unit", "vu1")
    run_python("vu1_recomp.py", "--unit", "vu0")
    GENERATED_STAMP.write_text(stamp)
    progress(100)


def count_compile_units(target):
    project = next(BUILD_DIR.rglob(target + ".vcxproj"), None)
    if project is None:
        return 0
    return project.read_text(encoding="utf-8", errors="replace").count("<ClCompile Include=")


def copy_vc_runtime(tools):
    """The runner needs Microsoft's C++ runtime, which a Build Tools install doesn't always put in Windows."""
    pattern = str(Path(tools["install"]) / "VC" / "Redist" / "MSVC" / "*" / "x64" / "Microsoft.VC*.CRT" / "*.dll")
    copied = set()
    for dll in sorted(glob.glob(pattern), reverse=True):
        name = os.path.basename(dll)
        if name not in copied:
            shutil.copy2(dll, RUNNER_DIR / name)
            copied.add(name)


def run_setup(iso, may_install_tools):
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
    tools = find_tools()
    if tools is None:
        if not may_install_tools:
            raise SetupError("Microsoft's C++ Build Tools are needed. Run setup with --install-tools.")
        install_tools()
        tools = find_tools()
    say(f"Using {tools['name']}")
    configure(tools)

    step(4, total, "Building the recompiler")
    build_target(tools, "ps2_recomp", count_compile_units("ps2_recomp"))

    step(5, total, "Translating the game's code to C++")
    generate_game_code()
    configure(tools)

    step(6, total, "Compiling the game (the longest step)")
    build_target(tools, "ps2EntryRunner", count_compile_units("ps2EntryRunner"))
    if not RUNNER_EXE.is_file():
        raise SetupError(f"The build finished without making the game. The full log is in {LOG_FILE}")
    copy_vc_runtime(tools)
    progress(100)
    say("Setup finished.")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check-disc").add_argument("iso")
    commands.add_parser("doctor")
    commands.add_parser("install-tools")
    run_parser = commands.add_parser("run")
    run_parser.add_argument("--iso", required=True)
    run_parser.add_argument("--install-tools", action="store_true")
    args = parser.parse_args()

    try:
        if args.command == "check-disc":
            print(f"OK {len(check_disc(args.iso))} files")
        elif args.command == "doctor":
            print(json.dumps(doctor()))
        elif args.command == "install-tools":
            install_tools()
        else:
            LOG_FILE.write_text("", encoding="utf-8")
            run_setup(args.iso, args.install_tools)
    except SetupError as e:
        print("@error " + str(e).replace("\n", "\\n"), flush=True)
        sys.exit(1)
    except OSError as e:
        print(f"@error {e}", flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
