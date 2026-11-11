#!/usr/bin/env python3
"""Android の NDK ビルドで core が「フラグプロファイル 1」でコンパイルされたか検査する。

AGP が生成する compile_commands.json を読み、core/ 配下の全ソースについて
  - 最後に指定された -O フラグが -O2 であること
  - -ffp-contract=on と MB_FLAGS_PROFILE=1 が付いていること
を確認する。iOS 側は ios.yml の "Show core compile settings" で確認している。
"""
import glob
import json
import re
import shlex
import sys

files = glob.glob("android/app/.cxx/**/compile_commands.json", recursive=True)
if not files:
    sys.exit("compile_commands.json not found under android/app/.cxx")

checked = 0
errors = []
for path in files:
    for entry in json.load(open(path)):
        src = entry["file"].replace("\\", "/")
        if "/core/src/" not in src:
            continue
        args = entry.get("arguments") or shlex.split(entry["command"])
        opts = [a for a in args if re.fullmatch(r"-O[0-3sz]?", a)]
        problems = []
        if not opts or opts[-1] != "-O2":
            problems.append(f"optimization={opts}")
        if "-ffp-contract=on" not in args:
            problems.append("missing -ffp-contract=on")
        if "-DMB_FLAGS_PROFILE=1" not in args:
            problems.append("missing -DMB_FLAGS_PROFILE=1")
        if problems:
            errors.append(f"{src}: {', '.join(problems)}")
        checked += 1

print(f"checked {checked} core compile commands in {len(files)} file(s)")
if checked == 0:
    sys.exit("no core sources found in compile_commands.json")
if errors:
    print("\n".join(errors))
    sys.exit(1)
print("OK: all core sources use flag profile 1 (-O2 -ffp-contract=on)")
