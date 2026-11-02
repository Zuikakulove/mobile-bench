import SwiftUI
import UIKit

private let posix = Locale(identifier: "en_US_POSIX")

struct ContentView: View {
    @ObservedObject var viewModel: BenchmarkViewModel

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    Text("アプリ \(AppInfo.version) (build \(AppInfo.build))")
                        .font(.caption)
                    HStack(spacing: 8) {
                        Button("計測開始") { viewModel.start() }
                            .buttonStyle(.borderedProminent)
                            .disabled(viewModel.isRunning)
                        if viewModel.isRunning {
                            Button("中止") { viewModel.cancel() }
                                .buttonStyle(.bordered)
                        }
                    }
                    Toggle("診断モード (ブースト推移・コア種類別・行列サイズ掃引。スコアには影響しません。+30秒程度)", isOn: $viewModel.diagnostics)
                        .font(.subheadline)
                        .disabled(viewModel.isRunning)

                    switch viewModel.state {
                    case .idle:
                        Text("「計測開始」を押すと CPU テストを「ピーク」と「持続」の 2 段階で実行します (1〜5 分かかります)。充電中・高温時はスコアが変わることがあります。")
                    case .running(let info):
                        ProgressSection(info: info)
                    case .done(let result, let raw):
                        ResultSection(result: result, rawJson: raw)
                    case .failed(let message, let raw):
                        Text("エラー: \(message)").foregroundStyle(.red)
                        if let raw { RawJsonView(json: raw) }
                    }
                }
                .padding()
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .navigationTitle("MobileBench")
        }
    }
}

private struct ProgressSection: View {
    let info: BenchmarkViewModel.RunningInfo

    private var status: String {
        if info.waitMs >= 0 {
            return info.phase == "peak" ? "回復待ち \(info.waitMs / 1000) 秒 (最大 60 秒)" : "事前負荷 \(info.waitMs / 1000) 秒"
        }
        return info.warmup ? "ウォームアップ" : "本計測"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            ProgressView(value: info.fraction)
            Text("\(Int(info.fraction * 100))%")
            Text(phaseLabel(info.phase)).font(.subheadline.bold())
            if !info.testName.isEmpty {
                let index = info.testIndex < info.testCount ? "(\(info.testIndex + 1)/\(info.testCount)) " : ""
                let retry = info.attempt > 1 ? " (測り直し \(info.attempt) 回目)" : ""
                Text("\(index)\(info.testName)\(retry)  [\(status)]")
            }
            Text("計測中は他のアプリを操作しないでください。").font(.footnote)
        }
    }
}

private struct ResultSection: View {
    let result: BenchmarkResult
    let rawJson: String

    var body: some View {
        VStack(spacing: 4) {
            HStack {
                ScoreColumn(label: "ピーク", score: result.scores?.peak?.score ?? result.overallScore,
                            stable: result.scores?.peak?.stable ?? true)
                ScoreColumn(label: "持続", score: result.scores?.sustained?.score ?? 0,
                            stable: result.scores?.sustained?.stable ?? true)
            }
            if let retention = result.scores?.retention, retention > 0 {
                Text("持続率 \(formatScore(retention * 100))%").font(.subheadline)
            }
            Text("benchmark v\(result.benchmarkVersion)").font(.caption)
            if !result.valid {
                Text(result.cancelled ? "中止されました" : "一部のテストが無効です (チェックサム不一致)")
                    .foregroundStyle(.red)
            }
        }
        .frame(maxWidth: .infinity)
        .padding()
        .background(Color.accentColor.opacity(0.15), in: RoundedRectangle(cornerRadius: 12))

        if let summary = result.summaryText, !summary.isEmpty {
            Button("要約をコピー (チャットに貼る用)") { UIPasteboard.general.string = summary }
                .buttonStyle(.borderedProminent)
        }
        HStack(spacing: 8) {
            Button("JSON をコピー") { UIPasteboard.general.string = rawJson }
                .buttonStyle(.bordered)
            ShareLink(item: rawJson) { Text("共有") }
                .buttonStyle(.bordered)
        }

        ForEach(["peak", "sustained"], id: \.self) { phase in
            let tests = result.tests.filter { ($0.phase ?? "peak") == phase }
            if !tests.isEmpty {
                Text(phaseLabel(phase)).font(.headline)
                ForEach(tests, id: \.key) { t in TestCard(test: t) }
            }
        }

        if let sweep = result.diagnostics?.matmulSweep, !sweep.isEmpty {
            SweepTable(points: sweep)
        }

        Divider()
        Text(buildInfoText)
            .font(.caption)
        RawJsonView(json: rawJson)
    }

    private var buildInfoText: String {
        var lines = [
            "os: \(result.build.os)",
            "arch: \(result.build.arch)",
            "compiler: \(result.build.compiler)",
            "flags: \(result.build.flags) (profile \(result.build.flagsProfile), optimized \(result.build.optimized))",
            "logical_cores: \(result.config.logicalCores)",
        ]
        for pl in result.cpuTopology?.perfLevels ?? [] {
            lines.append("\(pl.name): \(pl.logicalCpus) cpus, L1d \(BenchmarkResult.formatBytes(pl.l1dBytes)), "
                + "L2 \(BenchmarkResult.formatBytes(pl.l2Bytes)) / \(pl.cpusPerL2) cpus")
        }
        return lines.joined(separator: "\n")
    }
}

private struct TestCard: View {
    let test: BenchmarkResult.TestEntry
    @State private var expanded = false

    private var detailLine: String {
        var s = String(format: "median %.1f ms / threads %ld / %.2f GHz / ±%.1f%%", locale: posix,
                       Double(test.medianNs) / 1e6, test.threads, test.ghz ?? 0, (test.spread ?? 0) * 100)
        if let a = test.attempts, a > 1 { s += " / \(a) tries" }
        if let w = test.waitMs, w >= 0 { s += " / wait \(w / 1000)s" + (test.recovered == false ? " (not recovered)" : "") }
        if !test.valid { s += " / INVALID" }
        return s
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(test.name).font(.subheadline)
                Spacer()
                Text(formatScore(test.score)).font(.headline)
            }
            Text(detailLine)
                .font(.caption)
            if test.stable == false && test.valid {
                Text("計測中に端末の状態が変わり、ばらつきが大きいままでした (参考値)")
                    .foregroundStyle(.red).font(.caption)
            }
            if let e = test.error { Text(e).foregroundStyle(.red).font(.caption) }
            if let runs = test.runDetails, !runs.isEmpty {
                DisclosureGroup("各回の内訳", isExpanded: $expanded) {
                    Text(runs.enumerated().map { formatRun($0.offset, $0.element, multi: test.threads > 1) }
                        .joined(separator: "\n"))
                        .font(.system(size: 11, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .font(.caption)
            }
        }
        .padding(12)
        .background(Color.secondary.opacity(0.1), in: RoundedRectangle(cornerRadius: 12))
    }
}

private struct SweepTable: View {
    let points: [BenchmarkResult.SweepPoint]

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("診断: 行列サイズとキャッシュ").font(.subheadline)
            Text("WS = 作業領域 (A+B+C)。MT はスレッドごとに C を持つ。サイズが L2 を超えると GF (GFLOPS) が下がるかを見る。")
                .font(.caption)
            let header = [pad("n", 5), pad("WS(1T)", 9), pad("WS(MT)", 9), pad("1T GF", 7), pad("MT GF", 7), pad("MT/1T", 6)]
                .joined(separator: " ")
            let rows = points.map { p in
                [
                    pad(String(p.n), 5),
                    pad(BenchmarkResult.formatBytes(p.stWorkingSetBytes), 9),
                    pad(BenchmarkResult.formatBytes(p.mtWorkingSetBytes), 9),
                    pad(String(format: "%.2f", locale: posix, p.stGflops), 7),
                    pad(String(format: "%.2f", locale: posix, p.mtGflops), 7),
                    pad(String(format: "%.2fx", locale: posix, p.stGflops > 0 ? p.mtGflops / p.stGflops : 0), 6),
                ].joined(separator: " ") + (p.valid ? "" : " !")
            }
            Text(([header] + rows).joined(separator: "\n"))
                .font(.system(size: 11, design: .monospaced))
            Text("iOS では OS の周波数情報は読めないため、各回の実効クロック推定値 (GHz) を参照してください。")
                .font(.caption2)
        }
        .padding(12)
        .background(Color.secondary.opacity(0.1), in: RoundedRectangle(cornerRadius: 12))
    }
}

private struct RawJsonView: View {
    let json: String
    @State private var expanded = false

    var body: some View {
        DisclosureGroup("結果 JSON", isExpanded: $expanded) {
            Text(json)
                .font(.system(size: 11, design: .monospaced))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

/// 例: "#3   109.4ms 4.31→4.32GHz [27.1 27.4 27.5 27.4]" (iOS では CPU 番号は取得できない)
private func formatRun(_ index: Int, _ r: BenchmarkResult.RunDetail, multi: Bool) -> String {
    let label = r.warmup ? "W " : "#\(index)"
    let ghz = r.ghzAfter > 0
        ? String(format: "%.2f→%.2fGHz", locale: posix, r.ghzBefore, r.ghzAfter) : ""
    let tail: String
    if multi {
        let units = r.workers.map(\.units)
        tail = units.isEmpty ? "" : "units \(units.min()!)-\(units.max()!)"
    } else {
        tail = "[" + (r.unitNs ?? []).map { String(format: "%.1f", locale: posix, Double($0) / 1e6) }
            .joined(separator: " ") + "]"
    }
    return pad(label, 3, right: true) + " " + String(format: "%7.1fms ", locale: posix, Double(r.ns) / 1e6) + ghz + " " + tail
}

/// 等幅表示用に空白で幅を揃える (Foundation の %@ は幅指定が効かないため)
private func pad(_ s: String, _ width: Int, right: Bool = false) -> String {
    let fill = String(repeating: " ", count: max(0, width - s.count))
    return right ? s + fill : fill + s
}

private func formatScore(_ score: Double) -> String {
    String(format: "%.0f", locale: posix, score)
}

private struct ScoreColumn: View {
    let label: String
    let score: Double
    let stable: Bool

    var body: some View {
        VStack(spacing: 2) {
            Text(label).font(.headline)
            Text(formatScore(score))
                .font(.system(size: 40, weight: .bold, design: .rounded))
            if !stable { Text("不安定").font(.caption).foregroundStyle(.red) }
        }
        .frame(maxWidth: .infinity)
    }
}

private func phaseLabel(_ phase: String) -> String {
    switch phase {
    case "prepare": return "準備 (基準クロックの測定)"
    case "peak": return "ピーク (各テスト前に端末の回復を待って計測)"
    case "sustained": return "持続 (全コアに負荷をかけた後、休まず計測)"
    case "diagnostics": return "診断 (スコア対象外)"
    default: return phase
    }
}
