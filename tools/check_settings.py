"""Keeps tools/settings.json, the code and docs/settings.md in step.

  python tools/check_settings.py           fails if an option is read by the code but not listed,
                                           listed but read nowhere, or set by the launcher but not
                                           listed; or if docs/settings.md is out of date
  python tools/check_settings.py --write   also rewrites docs/settings.md
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tools" / "settings.json"
DOC = ROOT / "docs" / "settings.md"
# Where options are read: the game's own sources, the runtime, the renderer DLL and the IOP.
CODE = [ROOT / "src", ROOT / "external/PS2Recomp/ps2xRuntime/src", ROOT / "external/PS2Recomp/ps2xRuntime/include",
        ROOT / "external/PS2Recomp/ps2xRuntime/pcsx2-gs", ROOT / "external/PS2Recomp/ps2xIOP/src"]
LAUNCHER = ROOT / "launcher" / "src"
NAME = re.compile(r'"(PS2X_[A-Z0-9_]+)"')


def names_in(folders, suffixes=(".cpp", ".h")):
    found = set()
    for folder in folders:
        for path in folder.rglob("*"):
            # Translated VU programs are generated and read no options.
            if path.suffix in suffixes and not re.match(r"vu\d_programs_", path.name):
                found.update(NAME.findall(path.read_text(encoding="utf-8", errors="replace")))
    return found


def document(settings):
    lines = ["# Options", "",
             "Generated from `tools/settings.json` by `tools/check_settings.py --write`. Do not edit by hand.", "",
             "Every option is an environment variable. The launcher sets the common ones from its tabs and",
             "takes any other on its Environment tab. Without the launcher, put `NAME=value` lines in",
             "`ps2x.ini` in the game's folder (`iso_extracted`), or set them in the shell; the environment",
             "wins over the file.", ""]
    group = None
    for item in settings:
        if item["group"] != group:
            group = item["group"]
            lines += [f"## {group}", "", "| Option | Values | Default | Effect |", "| --- | --- | --- | --- |"]
        values = item["values"].replace("|", "\\|")
        lines.append(f"| `{item['name']}` | {values} | {item['default'] or 'unset'} | {item['help']} |")
        nxt = settings.index(item) + 1
        if nxt == len(settings) or settings[nxt]["group"] != group:
            lines.append("")
    return "\n".join(lines)


def main():
    settings = json.loads(TABLE.read_text(encoding="utf-8"))["settings"]
    listed = [item["name"] for item in settings]
    problems = [f"listed twice: {name}" for name in sorted({n for n in listed if listed.count(n) > 1})]
    read = names_in(CODE)
    problems += [f"read by the code but not in settings.json: {name}" for name in sorted(read - set(listed))]
    problems += [f"in settings.json but read nowhere: {name}" for name in sorted(set(listed) - read)]
    problems += [f"set by the launcher but not in settings.json: {name}" for name in sorted(names_in([LAUNCHER]) - set(listed))]

    text = document(settings) + "\n"
    if "--write" in sys.argv:
        DOC.write_text(text, encoding="utf-8", newline="\n")
    elif not DOC.is_file() or DOC.read_text(encoding="utf-8") != text:
        problems.append("docs/settings.md is out of date: run tools/check_settings.py --write")

    for problem in problems:
        print(problem)
    print(f"{len(listed)} options listed, {len(read)} read by the code" + ("" if problems else ", all consistent"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
