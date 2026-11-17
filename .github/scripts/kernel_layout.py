#!/usr/bin/env python3
"""Android の libmobilebench_jni.so から、カーネルの関数とループの位置を書き出す(docs/DESIGN.md §6 C8)。

ビルドによってカーネルの位置が動き、遅いコアの篩が約 3 割変わった例がある(§1.8 (4))。原因は確かめていない。
その調査(§6 V24)のために、ビルドごとの位置を事実として残す。原因の判断はしない。

書き出すもの:
  - .so の SHA-256、ビルド ID、.text のアドレス・ファイル内の位置・大きさ・SHA-256
  - カーネルの関数(名前に KERNEL_PATTERNS のどれかを含むもの)ごとに、アドレス、大きさ、アドレスを 64 で割った余り、
    関数の中の後ろ向きの分岐(ループ)の飛び先のアドレスと 64 で割った余り、関数を呼んでいる箇所、逆アセンブル
  - --apk: APK に入っている .so と、.text の中身・アドレスが同じか
  - --compare-apk / --compare-so: 別のビルド(実機で使った APK など)の .so と、ファイル全体と .text が同じか

使い方:
  python3 -I kernel_layout.py --search-dir android/app/build/intermediates --apk app-debug.apk \\
      --tools-dir "$ANDROID_HOME/ndk/27.2.12479018/toolchains/llvm/prebuilt/linux-x86_64/bin" \\
      --label "<名前>" --out kernel-layout.txt [--compare-apk 実機で使った.apk --compare-label "<名前>"]

--search-dir の下の arm64-v8a/libmobilebench_jni.so のうち、シンボル表(.symtab)の残っているものを使う
(.so を直接 --so で渡してもよい)。比べる対象の .text が違えば、終了コード 1 で終わる(--allow-mismatch で 0)。
"""
import argparse
import hashlib
import io
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

# 位置を出すカーネルの関数。後の PR で足すときは、ここに名前の一部を足す
KERNEL_PATTERNS = [
    "count_primes_segmented",
    "matmul_blocked",
]

SO_NAME = "libmobilebench_jni.so"
ABI = "arm64-v8a"
APK_SO_PATH = f"lib/{ABI}/{SO_NAME}"
LINE = 64  # 余りを出す単位(バイト)

# AArch64 の分岐命令(bl は呼び出しなので、ループの判定には使わない)
BRANCH_RE = re.compile(r"^\s*([0-9a-f]+):\s+(b|b\.\w+|bc\.\w+|cbz|cbnz|tbz|tbnz)\s+(.*)$")
# 呼び出し: bl と、末尾呼び出しの b(関数の外から、関数の先頭へ飛ぶもの)
# .so の外に見える関数への呼び出しは、PLT を通る(飛び先の表示は <マングルされた名前@plt>)
CALL_RE = re.compile(r"^\s*([0-9a-f]+):\s+(bl|b)\s+(?:0x)?([0-9a-f]+)\b(?: <([^>+]+)(?:\+0x[0-9a-f]+)?>)?")
FUNC_RE = re.compile(r"^([0-9a-f]{8,}) <(.*)>:$")
TARGET_RE = re.compile(r"(?:0x)?([0-9a-f]+) <")


class ElfInfo:
    """64 ビット・リトルエンディアンの ELF の、節の表とビルド ID だけを読む。"""

    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
            raise ValueError("64 ビット・リトルエンディアンの ELF ではない")
        self.data = data
        e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3A)
        raw = []
        for i in range(e_shnum):
            raw.append(struct.unpack_from("<IIQQQQIIQQ", data, e_shoff + i * e_shentsize))
        strtab = raw[e_shstrndx]
        names = data[strtab[4]:strtab[4] + strtab[5]]
        self.sections = {}
        for sh in raw:
            name = names[sh[0]:names.index(b"\0", sh[0])].decode()
            self.sections[name] = {"type": sh[1], "addr": sh[3], "offset": sh[4], "size": sh[5]}

    def section_bytes(self, name):
        s = self.sections[name]
        return self.data[s["offset"]:s["offset"] + s["size"]]

    def build_id(self):
        if ".note.gnu.build-id" not in self.sections:
            return "-"
        note = self.section_bytes(".note.gnu.build-id")
        namesz, descsz, _ = struct.unpack_from("<III", note, 0)
        start = 12 + ((namesz + 3) & ~3)
        return note[start:start + descsz].hex()

    def has_symtab(self):
        return ".symtab" in self.sections


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def text_summary(label, data):
    elf = ElfInfo(data)
    t = elf.sections[".text"]
    return elf, (f"{label}: file sha256 {sha256(data)} size {len(data)} | build-id {elf.build_id()} | "
                 f".text addr 0x{t['addr']:x} offset 0x{t['offset']:x} size 0x{t['size']:x} "
                 f"sha256 {sha256(elf.section_bytes('.text'))} | .symtab {'あり' if elf.has_symtab() else 'なし'}")


def so_from_apk(path):
    with zipfile.ZipFile(path) as z:
        return z.read(APK_SO_PATH)


def find_unstripped(search_dir, out):
    found = []
    for dirpath, _, files in os.walk(search_dir):
        if SO_NAME in files and os.path.basename(dirpath) == ABI:
            p = os.path.join(dirpath, SO_NAME)
            data = open(p, "rb").read()
            found.append((p, data, ElfInfo(data).has_symtab()))
    out.append(f"-- {search_dir} の下の {ABI}/{SO_NAME}")
    for p, data, sym in sorted(found):
        out.append(f"  {p}: sha256 {sha256(data)} .symtab {'あり' if sym else 'なし'}")
    with_sym = [(p, d) for p, d, s in found if s]
    if not with_sym:
        raise SystemExit(f"ERROR: {search_dir} の下に、シンボル表の残った {ABI}/{SO_NAME} がない")
    if len({sha256(d) for _, d in with_sym}) != 1:
        raise SystemExit("ERROR: シンボル表の残った .so が複数あり、中身が違う")
    return sorted(with_sym)[0]


def run(tool, *args):
    return subprocess.run([tool, *args], check=True, capture_output=True, text=True).stdout


def kernel_symbols(nm, so, dynamic=False):
    syms = []
    for ln in run(nm, *(["-D"] if dynamic else []), "-C", "--defined-only", "-S", "--numeric-sort", so).splitlines():
        parts = ln.split(maxsplit=3)
        if len(parts) != 4 or parts[2] not in "Tt":
            continue
        addr, size, _, name = parts
        if any(p in name for p in KERNEL_PATTERNS):
            syms.append((int(addr, 16), int(size, 16), name))
    return syms


def parse_disassembly(text):
    """llvm-objdump -d の出力 -> {関数の開始アドレス: [(アドレス, 行), ...]}。"""
    funcs, cur = {}, None
    for ln in text.splitlines():
        m = FUNC_RE.match(ln)
        if m:
            cur = int(m.group(1), 16)
            funcs[cur] = []
            continue
        if cur is not None and re.match(r"^\s+[0-9a-f]+:", ln):
            funcs[cur].append((int(ln.split(":")[0], 16), ln.rstrip()))
    return funcs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--so", help="シンボル表の残った .so")
    src.add_argument("--search-dir", help="この下から、シンボル表の残った arm64-v8a の .so を探す")
    ap.add_argument("--apk", help="同じビルドの APK(中の .so と .text を比べる)")
    ap.add_argument("--compare-apk", help="比べる別のビルドの APK(実機で使ったものなど)")
    ap.add_argument("--compare-so", help="比べる別のビルドの .so")
    ap.add_argument("--compare-label", default="比べる対象")
    ap.add_argument("--tools-dir", default="", help="llvm-nm と llvm-objdump のあるディレクトリ(NDK のもの)")
    ap.add_argument("--label", default="このビルド")
    ap.add_argument("--out", required=True)
    ap.add_argument("--allow-mismatch", action="store_true", help="比べる対象と違っても終了コードを 0 にする")
    args = ap.parse_args()

    nm = os.path.join(args.tools_dir, "llvm-nm") if args.tools_dir else "llvm-nm"
    objdump = os.path.join(args.tools_dir, "llvm-objdump") if args.tools_dir else "llvm-objdump"
    out = [f"# カーネルの位置: {args.label}", ""]
    def version(tool):
        lines = [x.strip() for x in run(tool, "--version").splitlines() if x.strip()]
        return next((x for x in lines if "version" in x.lower()), lines[0] if lines else "?")
    out.append(f"tools: {nm}: {version(nm)} / {objdump}: {version(objdump)}")
    out.append(f"KERNEL_PATTERNS: {', '.join(KERNEL_PATTERNS)} | 余りは {LINE} バイト単位")
    out.append("")

    if args.search_dir:
        so_path, so_data = find_unstripped(args.search_dir, out)
        out.append("")
    else:
        so_path, so_data = args.so, open(args.so, "rb").read()
    elf, line = text_summary(f"使った .so ({so_path})", so_data)
    out.append("-- .so")
    out.append(line)
    text = elf.section_bytes(".text")
    mismatches = []

    def compare(label, other, must_match):
        other_elf, other_line = text_summary(label, other)
        out.append(other_line)
        same_text = other_elf.section_bytes(".text") == text
        same_addr = other_elf.sections[".text"]["addr"] == elf.sections[".text"]["addr"]
        out.append(f"  -> .text の中身: {'同じ' if same_text else '違う'} / .text のアドレス: "
                   f"{'同じ' if same_addr else '違う'} / ファイル全体: {'同じ' if other == so_data else '違う'}"
                   f"(使った .so と比べて)")
        if must_match and not (same_text and same_addr):
            mismatches.append(label)
        # シンボル表(.symtab)を除いた .so でも、外に見える関数は動的シンボル(.dynsym)に残る
        with tempfile.NamedTemporaryFile(suffix=".so") as tmp:
            tmp.write(other)
            tmp.flush()
            dyn = kernel_symbols(nm, tmp.name, dynamic=True)
        out.append(f"  カーネルの関数の動的シンボル ({len(dyn)} 個):")
        for addr, size, name in dyn:
            out.append(f"    addr 0x{addr:x} (mod {LINE} = {addr % LINE}) size {size} {name}")
        return other_elf

    apk_so = None
    if args.apk:
        apk_so = so_from_apk(args.apk)
        compare(f"APK の {APK_SO_PATH} ({os.path.basename(args.apk)})", apk_so, True)
    for kind, path in (("apk", args.compare_apk), ("so", args.compare_so)):
        if not path:
            continue
        other = so_from_apk(path) if kind == "apk" else open(path, "rb").read()
        compare(f"{args.compare_label} ({os.path.basename(path)}{' の ' + APK_SO_PATH if kind == 'apk' else ''})",
                other, True)
        if apk_so is not None:
            out.append(f"  -> このビルドの APK の .so とファイル全体が: {'同じ' if other == apk_so else '違う'}")
    out.append("")

    syms = kernel_symbols(nm, so_path)
    full = parse_disassembly(run(objdump, "-d", "--no-show-raw-insn", "-C", "--section=.text", so_path))
    callers, plt_callers = {}, {}
    for start, insns in full.items():
        for addr, ln in insns:
            m = CALL_RE.match(ln)
            if not m or int(m.group(3), 16) == start:
                continue
            callers.setdefault(int(m.group(3), 16), []).append((start, addr, m.group(2)))
            if m.group(4) and m.group(4).endswith("@plt"):
                plt_callers.setdefault(m.group(4)[:-4], []).append((start, addr, m.group(2) + " (PLT)"))
    names, mangled = {}, {}
    for demangle, table in ((True, names), (False, mangled)):
        for ln in run(nm, *(["-C"] if demangle else []), "--defined-only", "--numeric-sort", so_path).splitlines():
            parts = ln.split(maxsplit=2)
            if len(parts) == 3 and parts[1] in "Tt":
                table.setdefault(int(parts[0], 16), parts[2])

    out.append(f"-- カーネルの関数 ({len(syms)} 個)")
    if not syms:
        mismatches.append("カーネルの関数が見つからない")
    for addr, size, name in syms:
        out.append(f"{name}")
        out.append(f"  addr 0x{addr:x} (mod {LINE} = {addr % LINE}) size {size} (0x{size:x}) end 0x{addr + size:x}")
        loops = []
        for iaddr, ln in full.get(addr, []):
            m = BRANCH_RE.match(ln)
            if not m:
                continue
            t = TARGET_RE.search(m.group(3))
            if not t:
                continue
            target = int(t.group(1), 16)
            if addr <= target <= iaddr < addr + size:
                loops.append((target, iaddr, m.group(2)))
        out.append(f"  後ろ向きの分岐 {len(loops)} 個 (飛び先 / mod {LINE} / 分岐の位置 / 命令 / ループの長さ):")
        for target, iaddr, mnem in sorted(loops):
            out.append(f"    0x{target:x} / {target % LINE:2d} / 0x{iaddr:x} / {mnem} / {iaddr - target + 4} バイト")
        calls = callers.get(addr, []) + plt_callers.get(mangled.get(addr, ""), [])
        out.append(f"  この関数を bl / b で呼ぶ箇所(先頭か PLT へ飛ぶもの) {len(calls)} 個:" + ("" if calls else " なし"))
        for fstart, caddr, mnem in calls:
            out.append(f"    0x{caddr:x} {mnem} in {names.get(fstart, hex(fstart))}")
        if not calls:
            out.append("    (この .so の中に、この関数の先頭や PLT へ飛ぶ bl / b はない。関数ポインタでの呼び出しは数えていない)")
    out.append("")
    out.append("-- 逆アセンブル")
    for addr, size, name in syms:
        out.append(f"0x{addr:x} <{name}>:")
        out += [ln for _, ln in full.get(addr, [])]
        out.append("")

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    # ログには、逆アセンブルの前までを出す
    print("\n".join(out[:out.index("-- 逆アセンブル")]))
    if mismatches:
        print("NG: " + ", ".join(mismatches))
        if not args.allow_mismatch:
            sys.exit(1)


if __name__ == "__main__":
    main()
