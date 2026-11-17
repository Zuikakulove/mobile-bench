#!/usr/bin/env python3
"""3 か所に手書きされたバージョンが一致することを検査する(docs/DESIGN.md §1.5-9)。

  - core/include/mobilebench/version.h の kBenchmarkVersion
  - android/app/build.gradle.kts の versionName
  - ios/project.yml の MARKETING_VERSION

あわせて、version.h の履歴のコメントに、その版の行(「//   0.2.1  …」の形)があることを確かめる。
どれかが食い違うか、見つからなければ、終了コード 1 で終わる。

使い方: python3 -I .github/scripts/check_versions.py [リポジトリのルート]
ルートを省くと、このスクリプトの位置から求める。
"""
import pathlib
import re
import sys

VERSION_RE = r"(\d+\.\d+\.\d+)"

# (表示名, ルートからのパス, 値を取り出す正規表現)。どれもファイルの中にちょうど 1 回だけあること
SOURCES = [
    ("version.h kBenchmarkVersion", "core/include/mobilebench/version.h",
     r'^\s*inline\s+constexpr\s+const\s+char\s*\*\s*kBenchmarkVersion\s*=\s*"' + VERSION_RE + r'"\s*;'),
    ("build.gradle.kts versionName", "android/app/build.gradle.kts",
     r'^\s*versionName\s*=\s*"' + VERSION_RE + r'"'),
    ("project.yml MARKETING_VERSION", "ios/project.yml",
     r'^\s*MARKETING_VERSION:\s*"?' + VERSION_RE + r'"?\s*$'),
]


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
    errors = []
    values = {}
    for label, rel, pattern in SOURCES:
        path = root / rel
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as e:
            errors.append(f"{rel}: 読めない ({e})")
            continue
        found = re.findall(pattern, text, flags=re.MULTILINE)
        if len(found) != 1:
            errors.append(f"{rel}: {label} が {len(found)} 個見つかった (ちょうど 1 個のはず)")
            continue
        values[label] = found[0]
        print(f"{label:32s} {found[0]}  ({rel})")

    if len(set(values.values())) > 1:
        errors.append("バージョンが一致しない: " + ", ".join(f"{k}={v}" for k, v in values.items()))

    version = values.get(SOURCES[0][0])
    if version is not None:
        header = (root / SOURCES[0][1]).read_text(encoding="utf-8")
        history = re.findall(r"^//\s+" + re.escape(version) + r"\s+\S", header, flags=re.MULTILINE)
        if not history:
            errors.append(f"version.h の履歴のコメントに {version} の行がない")
        else:
            print(f"version.h の履歴に {version} の行がある")

    if errors:
        print()
        for e in errors:
            print(f"ERROR: {e}")
        sys.exit(1)
    print(f"OK: 3 か所のバージョンが一致 ({version})")


if __name__ == "__main__":
    main()
