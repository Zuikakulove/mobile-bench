#!/usr/bin/env python3
"""Kotlin の JNI の宣言と jni_bridge.cpp が食い違っていないかを検査する(docs/PHASE1_PLAN.md J7 の 1)。

JNI の関数名とメソッドのシグネチャは、実行時にしか検査されない(食い違うと実機でアプリが落ちる)。
ビルドの前に、文字列として照合する。

  1. Kotlin の `external fun` ごとに、jni_bridge.cpp に `Java_<パッケージ>_<クラス>_<名前>` の関数があること。
     あわせて、C++ の引数の型が Kotlin の宣言から求めた JNI の型と同じであること
     (先頭の JNIEnv* と、object のインスタンスを受ける jobject を除く)と、戻り値の型が同じであること。
  2. jni_bridge.cpp の `GetMethodID(…, "<名前>", "<シグネチャ>")` ごとに、Kotlin の `fun interface` に
     その名前のメソッドがあり、宣言から求めたシグネチャと一致すること。

どれかが食い違えば、終了コード 1 で終わる。対応していない Kotlin の型が出てきたときも、推測せずに失敗させる
(このスクリプトに型を足す)。

使い方: python3 -I .github/scripts/check_jni.py [リポジトリのルート]
"""
import pathlib
import re
import sys

KOTLIN_DIR = "android/app/src/main/java"
NATIVE_KT = "android/app/src/main/java/com/mobilebench/app/bench/NativeBench.kt"
BRIDGE_CPP = "android/app/src/main/cpp/jni_bridge.cpp"

# Kotlin の型 -> (JVM の型記述子, JNI の C++ の型)
PRIMITIVES = {
    "Int": ("I", "jint"), "Long": ("J", "jlong"), "Boolean": ("Z", "jboolean"), "Double": ("D", "jdouble"),
    "Float": ("F", "jfloat"), "Short": ("S", "jshort"), "Byte": ("B", "jbyte"), "Char": ("C", "jchar"),
}
PRIMITIVE_ARRAYS = {
    "IntArray": ("[I", "jintArray"), "LongArray": ("[J", "jlongArray"), "BooleanArray": ("[Z", "jbooleanArray"),
    "DoubleArray": ("[D", "jdoubleArray"), "FloatArray": ("[F", "jfloatArray"), "ShortArray": ("[S", "jshortArray"),
    "ByteArray": ("[B", "jbyteArray"), "CharArray": ("[C", "jcharArray"),
}


class JniError(Exception):
    pass


def strip_comments(text):
    """// と /* */ のコメントを取り除く(文字列の中の // は考えない。対象のファイルにはない)。"""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def split_top_level(s, sep=","):
    """<> と () の外側の区切りで分ける。"""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out if x.strip()]


def kotlin_type(t, interfaces, package):
    """Kotlin の型を (記述子, C++ の型) にする。"""
    t = t.strip()
    nullable = t.endswith("?")
    base = t[:-1].strip() if nullable else t
    if base in PRIMITIVES:
        if nullable:
            raise JniError(f"null 許容の基本型 {t} には対応していない")
        return PRIMITIVES[base]
    if base in PRIMITIVE_ARRAYS:
        return PRIMITIVE_ARRAYS[base]
    if base == "String":
        return ("Ljava/lang/String;", "jstring")
    m = re.fullmatch(r"Array<(.+)>", base)
    if m:
        inner, _ = kotlin_type(m.group(1), interfaces, package)
        return ("[" + inner, "jobjectArray")
    if base in interfaces:
        return ("L" + interfaces[base].replace(".", "/") + ";", "jobject")
    raise JniError(f"対応していない Kotlin の型: {t}")


def return_type(t, interfaces, package):
    if t is None or t.strip() == "Unit":
        return ("V", "void")
    return kotlin_type(t, interfaces, package)


def parse_params(params):
    out = []
    for p in split_top_level(params):
        name, _, typ = p.partition(":")
        typ = typ.split("=")[0].strip()  # 既定値は無視する
        out.append((name.strip(), typ))
    return out


def block_end(text, open_index):
    """text[open_index] の '{' に対応する '}' の位置。"""
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise JniError("括弧が閉じていない")


FUN_RE = r"fun\s+(\w+)\s*\(([^)]*)\)\s*(?::\s*([\w<>?, .]+?))?\s*(?=[\n{=]|$)"


def kotlin_interfaces(root):
    """全ソースの `fun interface` -> {名前: (完全な名前, {メソッド名: (引数, 戻り値)})}。"""
    found = {}
    for path in sorted((root / KOTLIN_DIR).rglob("*.kt")):
        text = strip_comments(path.read_text(encoding="utf-8"))
        pkg = re.search(r"^\s*package\s+([\w.]+)", text, flags=re.MULTILINE)
        package = pkg.group(1) if pkg else ""
        for m in re.finditer(r"\bfun\s+interface\s+(\w+)[^{]*\{", text):
            body = text[m.end() - 1:block_end(text, m.end() - 1) + 1]
            methods = {}
            for f in re.finditer(FUN_RE, body):
                methods[f.group(1)] = (parse_params(f.group(2)), f.group(3))
            name = m.group(1)
            if name in found:
                raise JniError(f"fun interface {name} が 2 か所にある")
            found[name] = ((package + "." if package else "") + name, methods, path)
    return found


def kotlin_externals(root):
    """NativeBench.kt の `external fun` -> [(JNI の関数名, 名前, 引数, 戻り値)]。"""
    path = root / NATIVE_KT
    text = strip_comments(path.read_text(encoding="utf-8"))
    package = re.search(r"^\s*package\s+([\w.]+)", text, flags=re.MULTILINE).group(1)
    out = []
    for m in re.finditer(r"\b(object|class)\s+(\w+)[^{\n]*\{", text):
        start = m.end() - 1
        body = text[start:block_end(text, start) + 1]
        for f in re.finditer(r"\bexternal\s+" + FUN_RE, body):
            jni_name = "Java_" + package.replace("_", "_1").replace(".", "_") + "_" + m.group(2) + "_" + \
                f.group(1).replace("_", "_1")
            out.append((jni_name, f.group(1), parse_params(f.group(2)), f.group(3), m.group(1)))
    return out


def cpp_functions(text):
    """`extern "C" JNIEXPORT <型> JNICALL <名前>(<引数>)` -> {名前: (戻り値の型, [引数の型])}。"""
    out = {}
    for m in re.finditer(r'extern\s+"C"\s+JNIEXPORT\s+(\w+)\s+JNICALL\s+(\w+)\s*\(([^)]*)\)', text):
        types = []
        for p in split_top_level(m.group(3)):
            p = re.sub(r"/\*.*?\*/", "", p).strip()
            # 最後の識別子が引数の名前。名前のない引数(型だけ)にも対応する
            mm = re.fullmatch(r"(.+?[\s*&])(\w+)", p)
            typ = mm.group(1) if mm and mm.group(1).strip() not in ("", "const") else p
            types.append(re.sub(r"\s+", "", typ))
        out[m.group(2)] = (m.group(1), types)
    return out


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
    errors = []
    cpp = strip_comments((root / BRIDGE_CPP).read_text(encoding="utf-8"))
    try:
        interfaces = kotlin_interfaces(root)
        iface_names = {k: v[0] for k, v in interfaces.items()}
        externals = kotlin_externals(root)
    except JniError as e:
        print(f"ERROR: {e}")
        sys.exit(1)
    functions = cpp_functions(cpp)

    print(f"-- external fun ({NATIVE_KT}) と {BRIDGE_CPP}")
    if not externals:
        errors.append("external fun が 1 つも見つからない")
    for jni_name, name, params, ret, kind in externals:
        if jni_name not in functions:
            errors.append(f"{name}: {jni_name} が {BRIDGE_CPP} にない")
            continue
        cpp_ret, cpp_params = functions[jni_name]
        try:
            # object / class の external fun は(@JvmStatic がなければ)インスタンスのメソッドになり、
            # 第 2 引数はそのインスタンス(jobject)になる
            want_params = ["JNIEnv*", "jobject"]
            want_params += [kotlin_type(t, iface_names, "")[1] for _, t in params]
            want_ret = return_type(ret, iface_names, "")[1]
        except JniError as e:
            errors.append(f"{name}: {e}")
            continue
        ok = cpp_params == want_params and cpp_ret == want_ret
        print(f"{'OK ' if ok else 'NG '} {jni_name}  ({', '.join(cpp_params)}) -> {cpp_ret}")
        if not ok:
            errors.append(f"{name}: 型が違う。Kotlin から求めた型は ({', '.join(want_params)}) -> {want_ret}、"
                          f"C++ は ({', '.join(cpp_params)}) -> {cpp_ret}")
    known = {e[0] for e in externals}
    for fn in functions:
        if fn.startswith("Java_") and fn not in known:
            print(f"WARN {fn} は C++ にあるが、Kotlin の external fun に対応するものがない")

    print()
    print("-- GetMethodID と fun interface")
    calls = re.findall(r'GetMethodID\s*\([^,]+,\s*"(\w+)"\s*,\s*"([^"]+)"\s*\)', cpp)
    if "GetMethodID" in cpp and not calls:
        errors.append("GetMethodID の呼び出しを読めなかった(引数が文字列の定数でない)")
    for method, sig in calls:
        owners = [(iname, info) for iname, info in interfaces.items() if method in info[1]]
        if not owners:
            errors.append(f'GetMethodID "{method}": この名前のメソッドを持つ fun interface がない')
            continue
        if len(owners) > 1:
            errors.append(f'GetMethodID "{method}": 複数の fun interface にある ({[o[0] for o in owners]})')
            continue
        iname, (_, methods, _) = owners[0]
        params, ret = methods[method]
        try:
            want = "(" + "".join(kotlin_type(t, iface_names, "")[0] for _, t in params) + ")" + \
                return_type(ret, iface_names, "")[0]
        except JniError as e:
            errors.append(f"{iname}.{method}: {e}")
            continue
        ok = want == sig
        print(f"{'OK ' if ok else 'NG '} {iname}.{method}  C++ {sig}  Kotlin {want}")
        if not ok:
            errors.append(f"{iname}.{method}: シグネチャが違う。C++ は {sig}、Kotlin の宣言から求めると {want}")

    print()
    if errors:
        for e in errors:
            print(f"ERROR: {e}")
        sys.exit(1)
    print(f"OK: external fun {len(externals)} 個、GetMethodID {len(calls)} 個が一致")


if __name__ == "__main__":
    main()
