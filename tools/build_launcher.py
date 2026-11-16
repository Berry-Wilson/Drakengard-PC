#!/usr/bin/env python3
"""Build the launcher with the toolchain setup downloads (llvm-mingw, CMake and Ninja in toolchain/).

Usage: python tools/build_launcher.py [--qt C:/Qt/6.10.3/llvm-mingw_64] [--fresh]

Qt must be the llvm-mingw build of Qt 6.5 or newer (see launcher/README.md). The launcher is built in
launcher/out/build-mingw, with the Qt DLLs and llvm-mingw's C++ runtime copied beside it.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from setup_game import find_tools  # noqa: E402

SOURCE = ROOT / "launcher"
BUILD = SOURCE / "out" / "build-mingw"
DEFAULT_QT = Path("C:/Qt/6.10.3/llvm-mingw_64")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qt", type=Path, default=Path(os.environ.get("QT_LLVM_MINGW", DEFAULT_QT)),
                        help="Qt's llvm-mingw folder (default %(default)s, or QT_LLVM_MINGW)")
    parser.add_argument("--fresh", action="store_true", help="configure again from scratch")
    args = parser.parse_args()

    tools = find_tools()
    if not tools:
        sys.exit("The build tools aren't downloaded. Run: python tools/setup_game.py get-tools")
    if not (args.qt / "bin" / "moc.exe").is_file():
        sys.exit(f"Qt's llvm-mingw build isn't in {args.qt}. See launcher/README.md, or pass --qt.")

    # Qt's own tools (moc, windeployqt) are llvm-mingw programs and need its C++ runtime on PATH,
    # while configuring and while building.
    env = dict(os.environ, PATH=os.pathsep.join(tools["path"] + [os.environ.get("PATH", "")]))
    if args.fresh and BUILD.exists():
        # GLFW's git checkout under _deps has read-only pack files.
        def make_writable(function, path, _):
            os.chmod(path, 0o666)
            function(path)
        shutil.rmtree(BUILD, onerror=make_writable)
    if not (BUILD / "CMakeCache.txt").is_file():
        subprocess.run([tools["cmake"], "-S", str(SOURCE), "-B", str(BUILD), "-G", "Ninja",
                        "-DCMAKE_BUILD_TYPE=Release",
                        f"-DCMAKE_MAKE_PROGRAM={tools['ninja']}",
                        f"-DCMAKE_C_COMPILER={tools['cc']}",
                        f"-DCMAKE_CXX_COMPILER={tools['cxx']}",
                        f"-DCMAKE_PREFIX_PATH={args.qt.as_posix()}"],
                       env=env, check=True)
    subprocess.run([tools["cmake"], "--build", str(BUILD)], env=env, check=True)
    print(f"Built {BUILD / 'drakengard-launcher.exe'}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
