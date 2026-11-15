#!/usr/bin/env python3
"""MobileBench 0.2.0 の「要約をコピー」のテキストを読み、V2・V3・V7 の観点の数値を出す。

使い方: python3 -I analyze_summary.py run1.txt [run2.txt ...]
"""
import re
import statistics
import sys


def parse(path):
    text = open(path, encoding="utf-8").read()
    lines = text.split("\n")
    out = {"path": path, "chars": len(text), "tests": [], "boost": [], "clusters": []}
    cur = None
    cluster = None
    for ln in lines:
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
        m = re.match(r"  (W |#\d) +([\d.]+)ms cpu(\S+) ghz ([\d.]+)>([\d.]+)(?:\(([\d.]+)-([\d.]+)\))? cur (.*?) cap (.*?)"
                     r"(?: u ((?:[\d.]+ ?)+)| units (\d+)-(\d+) busy ([\d.]+)-([\d.]+)ms)$", ln)
        if m and cur is not None:
            run = {
                "label": m.group(1).strip(), "ms": float(m.group(2)), "cpus": m.group(3),
                "ghz_before": float(m.group(4)), "ghz_after": float(m.group(5)),
                "after_min": float(m.group(6)) if m.group(6) else None,
                "after_max": float(m.group(7)) if m.group(7) else None,
                "cur": m.group(8), "cap": m.group(9),
            }
            if m.group(10):
                run["units_ms"] = [float(x) for x in m.group(10).split()]
            else:
                run["units_min"], run["units_max"] = int(m.group(11)), int(m.group(12))
                run["busy_min"], run["busy_max"] = float(m.group(13)), float(m.group(14))
            cur["runs"].append(run)
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
        m = re.match(r"  cpu(\S+) max(\d+) cap(\d+):", ln)
        if m:
            cluster = {"cpus": m.group(1), "max": int(m.group(2)), "cap": int(m.group(3)), "measures": []}
            out["clusters"].append(cluster)
            continue
        m = re.match(r"    (sieve|matmul128) (\d+)T(\(unpinned\))? ([\d.]+)ms @([\d.]+)G = ([\d.]+)Mc"
                     r"(?: \| ([\d.]+)GF ([\d.]+)f/c)? cpu(\S+)", ln)
        if m and cluster is not None:
            cluster["measures"].append({
                "kernel": m.group(1), "threads": int(m.group(2)), "unpinned": bool(m.group(3)),
                "ms": float(m.group(4)), "ghz": float(m.group(5)), "mc": float(m.group(6)),
                "gf": float(m.group(7)) if m.group(7) else None, "fc": float(m.group(8)) if m.group(8) else None,
                "cpus": m.group(9),
            })
    return out


def report(r):
    print(f"===== {r['path']}  ({r['chars']} chars)")
    print(f"topology max_mhz {r['max_mhz']} | capacity {r['capacity']}")
    print(f"baseline {r['baseline']} | precondition {r['precondition_ms']}ms -> {r['precondition_ghz']} {r['settled']}")
    caps = {run["cap"] for t in r["tests"] for run in t["runs"]}
    print("cap (scaling_max_freq) の種類:", caps)
    print()
    print("-- テストごと (本計測 5 回)")
    for t in r["tests"]:
        runs = [x for x in t["runs"] if x["label"] != "W"]
        ms = [x["ms"] for x in runs]
        line = (f"{t['phase']:9s} {t['id']:18s} median {t['median']:6.1f} min {min(ms):6.1f} max {max(ms):6.1f} "
                f"spread {t['spread']:4.1f}%{' UNSTABLE' if t['unstable'] else ''} attempts {t['attempts']}")
        if t["wait"] is not None:
            line += f" wait {t['wait']} rec {t['rec']} ({t['rec'] / r['baseline'] * 100:.1f}% of baseline)"
            if t["not_recovered"]:
                line += " NOT-RECOVERED"
        print(line)
        if t["threads"] == 1:
            first = [x["units_ms"][0] for x in runs]
            rest = [u for x in runs for u in x["units_ms"][1:]]
            w = t["runs"][0]
            print(f"    unit ms: 1 個目 {statistics.mean(first):.2f} / 2 個目以降 {statistics.mean(rest):.2f} "
                  f"(min {min(rest):.1f} max {max(rest):.1f}) -> 1 個目の遅れ "
                  f"{(statistics.mean(first) / statistics.mean(rest) - 1) * 100:+.2f}% | "
                  f"W: 1 個目 {w['units_ms'][0]} cpu{w['cpus']} | 本計測の cpu {sorted({x['cpus'] for x in runs})}")
        else:
            for x in t["runs"]:
                ratio = x["busy_min"] / x["ms"]
                print(f"    {x['label']:2s} {x['ms']:6.1f}ms cpu{x['cpus']:9s} units {x['units_min']}-{x['units_max']} "
                      f"busy {x['busy_min']:6.1f}-{x['busy_max']:6.1f} busy_min/elapsed {ratio:.2f}"
                      f"{'  <0.7' if ratio < 0.7 else ''} ghz {x['ghz_before']:.2f}>{x['ghz_after']:.2f} cur {x['cur']}")
    print()
    print("-- 全コアのプローブ (回復の確認)")
    recs = [t["rec"] for t in r["tests"] if t["rec"] is not None]
    print("rec:", recs, "min/baseline", f"{min(recs) / r['baseline']:.3f}", "max/baseline", f"{max(recs) / r['baseline']:.3f}")
    print()
    print("-- ブースト")
    for b in r["boost"]:
        print(f"idle {b['idle']}ms:", " ".join(f"{t}:{g}G/{u}ms/cpu{c}" for t, g, u, c in b["segs"]))
    print()
    print("-- クラスタ")
    for c in r["clusters"]:
        for m in c["measures"]:
            extra = f" {m['gf']}GF {m['fc']}f/c" if m["gf"] else ""
            print(f"cpu{c['cpus']:4s} max{c['max']} {m['kernel']:9s} {m['threads']}T{'(unpinned)' if m['unpinned'] else ''} "
                  f"{m['ms']}ms @{m['ghz']}G {m['mc']}Mc{extra} cpu{m['cpus']}")
    print()


def main():
    results = [parse(p) for p in sys.argv[1:]]
    for r in results:
        report(r)
    # まとめ: 全コアのプローブのばらつき
    probes = []
    for r in results:
        probes += [r["baseline"], r["precondition_ghz"]] + [t["rec"] for t in r["tests"] if t["rec"] is not None]
    if probes:
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
        d = (max(v) - min(v)) / min(v) * 100
        print(f"{k[0]:9s} {k[1]:18s} {v} 差 {d:.1f}%")


if __name__ == "__main__":
    main()
