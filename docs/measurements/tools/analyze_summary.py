#!/usr/bin/env python3
"""MobileBench の「要約をコピー」のテキストを読み、実機検証の観点の数値を出す。

読める書式:
  - 0.2.1 以降: パートに分けた要約。各パートは見出し「MobileBench v… | run … | part i/n」で始まり、
    終端の行「END part i/n chars=N」で終わる。パートごとに別のファイルでも、1 つのファイルに
    続けて保存したものでもよい。同じ実行のパートを見出しでまとめ、欠けや切れを報告する。
  - 0.2.0: 分割していない 1 本の要約 (1 ファイル = 1 回の実行)。

見る観点: V2・V3・V7 (テストごとの時間、busy、回復待ち)、V21 (クラスタ別の単位の案)、
V22 (状態を確かめるクロックの 2 つの求め方)。

使い方: python3 -I analyze_summary.py run1-part1.txt run1-part2.txt [run2.txt ...]
"""
import re
import statistics
import sys

HEADER_RE = re.compile(r"^(MobileBench v\S+ \|.*) \| part (\d+)/(\d+)$")
END_RE = re.compile(r"^END part (\d+)/(\d+) chars=(\d+)$")


def split_runs(paths):
    """ファイルを読み、実行ごとの本文 (行のリスト) と、パートの検査結果を返す。"""
    runs = {}  # key -> {"parts": {i: lines}, "total": n, "problems": [], "paths": set(), "legacy": bool}
    order = []
    for path in paths:
        text = open(path, encoding="utf-8").read().replace("\r\n", "\n")
        lines = text.split("\n")
        if not any(HEADER_RE.match(ln) for ln in lines):
            key = f"{path} (0.2.0 の書式)"
            runs[key] = {"parts": {1: lines}, "total": 1, "problems": [], "paths": {path}, "legacy": True,
                         "chars": len(text)}
            order.append(key)
            continue
        i = 0
        while i < len(lines):
            m = HEADER_RE.match(lines[i])
            if not m:
                i += 1
                continue
            key, part, total = m.group(1), int(m.group(2)), int(m.group(3))
            run = runs.get(key)
            if run is None:
                run = runs[key] = {"parts": {}, "total": total, "problems": [], "paths": set(), "legacy": False}
                order.append(key)
            run["paths"].add(path)
            body = []
            consumed = [lines[i]]
            j = i + 1
            ended = False
            while j < len(lines) and not HEADER_RE.match(lines[j]):
                e = END_RE.match(lines[j])
                if e:
                    chars = len("\n".join(consumed)) + 1
                    if (int(e.group(1)), int(e.group(2))) != (part, total):
                        run["problems"].append(f"part {part}: 終端の行の番号が違う ({lines[j]})")
                    elif int(e.group(3)) != chars:
                        run["problems"].append(f"part {part}: 文字数が合わない (END は {e.group(3)}、実際は {chars})")
                    ended = True
                    j += 1
                    break
                body.append(lines[j])
                consumed.append(lines[j])
                j += 1
            if not ended:
                run["problems"].append(f"part {part}: 終端の行がない (切れている)")
            if part in run["parts"]:
                run["problems"].append(f"part {part}: 2 回出てきた (後のほうを使う)")
            run["parts"][part] = body
            i = j
    out = []
    for key in order:
        run = runs[key]
        missing = [p for p in range(1, run["total"] + 1) if p not in run["parts"]]
        if missing:
            run["problems"].append(f"欠けているパート: {missing}")
        lines = []
        for p in sorted(run["parts"]):
            lines += run["parts"][p]
        out.append((key, run, lines))
    return out


def parse_conditions(text):
    if text in ("-", "="):
        return {}
    out = {}
    for item in text.split(" "):
        k, _, v = item.partition("=")
        out[k] = v
    return out


def parse(key, run, lines):
    out = {"key": key, "paths": sorted(run["paths"]), "parts": len(run["parts"]), "total_parts": run["total"],
           "problems": run["problems"], "legacy": run["legacy"],
           "chars": run.get("chars", sum(len(ln) + 1 for ln in lines)),
           "tests": [], "boost": [], "clusters": [], "probe": [], "cond_base": {}}
    cur = None
    cluster = None
    for ln in lines:
        m = re.match(r"device: (.*?) (\S+) (\S+)(?: build=(.*?))? \| (.*)", ln)
        if m and "device" not in out:
            out["device"] = ln[len("device: "):]
            out["os_build"] = m.group(4)
            continue
        m = re.match(r"os (.*)", ln)
        if m:
            out["os"] = m.group(1)
            continue
        m = re.match(r"cond\(base\) (.*)", ln)
        if m:
            out["cond_base"] = parse_conditions(m.group(1))
            continue
        m = re.match(r"baseline ([\d.]+)GHz\(all-core\) precondition (\d+)ms -> ([\d.]+)GHz (\S+)", ln)
        if m:
            out["baseline"] = float(m.group(1))
            out["precondition_ms"] = int(m.group(2))
            out["precondition_ghz"] = float(m.group(3))
            out["settled"] = m.group(4)
        m = re.match(r"topology max_mhz (.*) capacity (.*)", ln)
        if m:
            out["max_mhz"], out["capacity"] = m.group(1), m.group(2)
        m = re.match(
            r"\[(peak|sustained) (\S+)\] median ([\d.]+)ms score ([\d.]+) threads (\d+) @([\d.]+)G spread ([\d.]+)%"
            r"( UNSTABLE)? attempts (\d+)(?: wait (\d+)ms(\(NOT recovered\))? rec ([\d.]+)G)?",
            ln,
        )
        if m:
            cur = {
                "phase": m.group(1), "id": m.group(2), "median": float(m.group(3)), "score": float(m.group(4)),
                "threads": int(m.group(5)), "ghz": float(m.group(6)), "spread": float(m.group(7)),
                "unstable": bool(m.group(8)), "attempts": int(m.group(9)),
                "wait": int(m.group(10)) if m.group(10) else None, "not_recovered": bool(m.group(11)),
                "rec": float(m.group(12)) if m.group(12) else None, "runs": [],
            }
            out["tests"].append(cur)
            continue
        m = re.match(r"  cond (.*) -> (.*)", ln)
        if m and cur is not None:
            if out["legacy"]:  # 0.2.0: 全項目
                cur["cond_before"], cur["cond_after"] = parse_conditions(m.group(1)), parse_conditions(m.group(2))
            else:  # 0.2.1 以降: base と違う項目だけ
                cur["cond_before"] = {**out["cond_base"], **parse_conditions(m.group(1))}
                cur["cond_after"] = {**out["cond_base"], **parse_conditions(m.group(2))}
            continue
        # cap は 0.2.1 以降、最大周波数と違うときだけ出る
        m = re.match(r"  (W |#\d) +([\d.]+)ms cpu(\S+)(?: ghz ([\d.]+)>([\d.]+)(?:\(([\d.]+)-([\d.]+)\))?)? cur (.*?)"
                     r"(?: cap (.*?))?(?: u ((?:[\d.]+ ?)+)| units (\d+)-(\d+) busy ([\d.]+)-([\d.]+)ms)$", ln)
        if m and cur is not None:
            run_ = {
                "label": m.group(1).strip(), "ms": float(m.group(2)), "cpus": m.group(3),
                "ghz_before": float(m.group(4)) if m.group(4) else 0.0,
                "ghz_after": float(m.group(5)) if m.group(5) else 0.0,
                "after_min": float(m.group(6)) if m.group(6) else None,
                "after_max": float(m.group(7)) if m.group(7) else None,
                "cur": m.group(8), "cap": m.group(9) if m.group(9) is not None else "(max)",
            }
            if m.group(10):
                run_["units_ms"] = [float(x) for x in m.group(10).split()]
            else:
                run_["units_min"], run_["units_max"] = int(m.group(11)), int(m.group(12))
                run_["busy_min"], run_["busy_max"] = float(m.group(13)), float(m.group(14))
            cur["runs"].append(run_)
            continue
        m = re.match(r"  boost idle (\d+)ms .*?\) (.*)", ln)
        if m:
            segs = []
            for seg in m.group(2).split(" | "):
                sm = re.match(r"(\d+):([\d.]+)G u([\d.]+) cpu(\S+)", seg)
                if sm:
                    segs.append((int(sm.group(1)), float(sm.group(2)), float(sm.group(3)), sm.group(4)))
            out["boost"].append({"idle": int(m.group(1)), "segs": segs})
            continue
        m = re.match(r"  probe rest (\d+)ms load (\d+)ms workers (\d+)", ln)
        if m:
            out["probe_rest"], out["probe_load"], out["probe_workers"] = (int(m.group(i)) for i in (1, 2, 3))
            continue
        m = re.match(r"    (\d+)ms current ([\d.]+) \(([\d.]+)-([\d.]+)\) counted ([\d.]+) \(([\d.]+)-([\d.]+)\)", ln)
        if m:
            v = [float(m.group(i)) for i in range(2, 8)]
            out["probe"].append({"t": int(m.group(1)), "current": v[0], "current_min": v[1], "current_max": v[2],
                                 "counted": v[3], "counted_min": v[4], "counted_max": v[5]})
            continue
        m = re.match(r"  cpu(\S+) max(\d+|\?) cap(\d+|\?):", ln)
        if m:
            cluster = {"cpus": m.group(1), "max": m.group(2), "cap": m.group(3), "measures": []}
            out["clusters"].append(cluster)
            continue
        m = re.match(r"    (\S+) (\d+)T(\(unpinned\))? ([\d.]+)ms @([\d.]+)G = ([\d.]+)Mc"
                     r"(?: \| ([\d.]+)GF ([\d.]+)f/c)? cpu(\S+)( error=.*)?", ln)
        if m and cluster is not None:
            cluster["measures"].append({
                "kernel": m.group(1), "threads": int(m.group(2)), "unpinned": bool(m.group(3)),
                "ms": float(m.group(4)), "ghz": float(m.group(5)), "mc": float(m.group(6)),
                "gf": float(m.group(7)) if m.group(7) else None, "fc": float(m.group(8)) if m.group(8) else None,
                "cpus": m.group(9), "error": m.group(10).strip() if m.group(10) else None,
            })
    return out


def spread(values):
    mean = statistics.mean(values)
    return mean, (max(values) - min(values)) / mean * 100


def report(r):
    print(f"===== {r['key']}")
    print(f"files {r['paths']} | parts {r['parts']}/{r['total_parts']} | body {r['chars']} chars")
    for p in r["problems"]:
        print(f"!! {p}")
    if "device" in r:
        print(f"device {r['device']}")
    if "os" in r:
        print(f"os {r['os']}")
    if "max_mhz" not in r:
        print("(先頭の情報がない。パートが欠けている可能性がある)")
        print()
        return
    print(f"topology max_mhz {r['max_mhz']} | capacity {r['capacity']}")
    print(f"baseline {r['baseline']} | precondition {r['precondition_ms']}ms -> {r['precondition_ghz']} {r['settled']}")
    caps = {run["cap"] for t in r["tests"] for run in t["runs"]}
    print("cap (scaling_max_freq) の種類:", caps, *(["((max) = 最大周波数と同じ)"] if "(max)" in caps else []))
    print()
    print("-- テストごと (本計測 5 回)")
    for t in r["tests"]:
        runs = [x for x in t["runs"] if x["label"] != "W"]
        ms = [x["ms"] for x in runs] or [0.0]
        line = (f"{t['phase']:9s} {t['id']:18s} median {t['median']:6.1f} min {min(ms):6.1f} max {max(ms):6.1f} "
                f"spread {t['spread']:4.1f}%{' UNSTABLE' if t['unstable'] else ''} attempts {t['attempts']}")
        if t["wait"] is not None:
            line += f" wait {t['wait']} rec {t['rec']}"
            if r.get("baseline"):
                line += f" ({t['rec'] / r['baseline'] * 100:.1f}% of baseline)"
            if t["not_recovered"]:
                line += " NOT-RECOVERED"
        print(line)
        if "cond_after" in t:
            c = t["cond_after"]
            print(f"    cond after: thermal={c.get('thermal_status', c.get('thermal_state', '?'))} "
                  f"temp={c.get('battery_temp_c', '?')} headroom={c.get('thermal_headroom_10s', '?')}")
        if not runs:
            continue
        if t["threads"] == 1 and all("units_ms" in x for x in t["runs"]):
            first = [x["units_ms"][0] for x in runs]
            rest = [u for x in runs for u in x["units_ms"][1:]]
            w = t["runs"][0]
            if rest:
                print(f"    unit ms: 1 個目 {statistics.mean(first):.2f} / 2 個目以降 {statistics.mean(rest):.2f} "
                      f"(min {min(rest):.1f} max {max(rest):.1f}) -> 1 個目の遅れ "
                      f"{(statistics.mean(first) / statistics.mean(rest) - 1) * 100:+.2f}% | "
                      f"W: 1 個目 {w['units_ms'][0]} cpu{w['cpus']} | 本計測の cpu {sorted({x['cpus'] for x in runs})}")
        else:
            for x in t["runs"]:
                if "busy_min" not in x:
                    continue
                ratio = x["busy_min"] / x["ms"]
                print(f"    {x['label']:2s} {x['ms']:6.1f}ms cpu{x['cpus']:9s} units {x['units_min']}-{x['units_max']} "
                      f"busy {x['busy_min']:6.1f}-{x['busy_max']:6.1f} busy_min/elapsed {ratio:.2f}"
                      f"{'  <0.7' if ratio < 0.7 else ''} ghz {x['ghz_before']:.2f}>{x['ghz_after']:.2f} cur {x['cur']}")
    print()
    print("-- 全コアのプローブ (回復の確認)")
    recs = [t["rec"] for t in r["tests"] if t["rec"] is not None]
    if recs and r.get("baseline"):
        print("rec:", recs, "min/baseline", f"{min(recs) / r['baseline']:.3f}",
              "max/baseline", f"{max(recs) / r['baseline']:.3f}")
    print()
    print("-- ブースト")
    for b in r["boost"]:
        print(f"idle {b['idle']}ms:", " ".join(f"{t}:{g}G/{u}ms/cpu{c}" for t, g, u, c in b["segs"]))
    print()
    if r["probe"]:
        print(f"-- 状態を確かめるクロック (V22): rest {r.get('probe_rest')}ms load {r.get('probe_load')}ms "
              f"workers {r.get('probe_workers')}")
        cur_mean, cur_range = spread([p["current"] for p in r["probe"]])
        cnt_mean, cnt_range = spread([p["counted"] for p in r["probe"]])
        print(f"{len(r['probe'])} samples: current mean {cur_mean:.3f} range {cur_range:.1f}% | "
              f"counted mean {cnt_mean:.3f} range {cnt_range:.1f}% (range = (max-min)/mean。目標は 2% 以内)")
        print()
    print("-- クラスタ")
    for c in r["clusters"]:
        for m in c["measures"]:
            extra = f" {m['gf']}GF {m['fc']}f/c" if m["gf"] else ""
            err = f" {m['error']}" if m["error"] else ""
            print(f"cpu{c['cpus']:4s} max{c['max']} {m['kernel']:9s} {m['threads']}T{'(unpinned)' if m['unpinned'] else ''} "
                  f"{m['ms']}ms @{m['ghz']}G {m['mc']}Mc{extra} cpu{m['cpus']}{err}")
    v21 = [(c, m) for c in r["clusters"] for m in c["measures"] if m["kernel"] in ("sieve1m", "matmul576")]
    if v21:
        print()
        print("-- 単位の案 (V21。規則 4: 最も遅いコアで 1 単位が 10 ms 以下)")
        for c, m in v21:
            unit = m["ms"] if m["kernel"] == "sieve1m" else m["ms"] / 81
            label = "篩 上限 100 万の 1 回" if m["kernel"] == "sieve1m" else "行列積 n=576 の 1/81 (1 回 ÷ 81 の換算)"
            print(f"cpu{c['cpus']:4s} {m['threads']}T{'(unpinned)' if m['unpinned'] else ''} {label}: {unit:.2f} ms"
                  f"{'  > 10 ms' if unit > 10 else ''}")
    print()


def main():
    results = [parse(*x) for x in split_runs(sys.argv[1:])]
    for r in results:
        report(r)
    # まとめ: 全コアのプローブのばらつき (現行の求め方。回復の確認と事前負荷に使われた値)
    probes = []
    for r in results:
        if "baseline" in r:
            probes += [r["baseline"], r["precondition_ghz"]]
        probes += [t["rec"] for t in r["tests"] if t["rec"] is not None]
    if len(probes) >= 2:
        mean = statistics.mean(probes)
        sd = statistics.stdev(probes)
        print(f"全コアのプローブ {len(probes)} 個: mean {mean:.3f} sd {sd:.3f} (CV {sd / mean * 100:.1f}%) "
              f"min {min(probes)} max {max(probes)} range {(max(probes) - min(probes)) / mean * 100:.1f}%")
    # まとめ: テストごとの中央値の回ごとの差
    by = {}
    for r in results:
        for t in r["tests"]:
            by.setdefault((t["phase"], t["id"]), []).append(t["median"])
    print("-- 回ごとの中央値")
    for k, v in by.items():
        d = (max(v) - min(v)) / min(v) * 100 if min(v) > 0 else 0.0
        print(f"{k[0]:9s} {k[1]:18s} {v} 差 {d:.1f}%")


if __name__ == "__main__":
    main()
