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
                    Toggle("診断モード (行列サイズ掃引・クロック記録。スコアには影響しません。+数秒)", isOn: $viewModel.diagnostics)
                        .font(.subheadline)
                        .disabled(viewModel.isRunning)

                    switch viewModel.state {
                    case .idle:
                        Text("「計測開始」を押すと CPU テストを実行します (数十秒かかります)。充電中・高温時はスコアが変わることがあります。")
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

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            ProgressView(value: info.fraction)
            Text("\(Int(info.fraction * 100))%")
            if !info.testName.isEmpty {
                let index = info.testIndex < info.testCount ? "(\(info.testIndex + 1)/\(info.testCount)) " : ""
                Text("\(index)\(info.testName)  [\(info.warmup ? "ウォームアップ" : "本計測")]")
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
            Text("総合スコア").font(.headline)
            Text(formatScore(result.overallScore))
                .font(.system(size: 48, weight: .bold, design: .rounded))
            Text("benchmark v\(result.benchmarkVersion)").font(.caption)
            if !result.valid {
                Text(result.cancelled ? "中止されました" : "一部のテストが無効です (チェックサム不一致)")
                    .foregroundStyle(.red)
            }
        }
        .frame(maxWidth: .infinity)
        .padding()
        .background(Color.accentColor.opacity(0.15), in: RoundedRectangle(cornerRadius: 12))

        HStack(spacing: 8) {
            Button("JSON をコピー") { UIPasteboard.general.string = rawJson }
                .buttonStyle(.bordered)
            ShareLink(item: rawJson) { Text("共有") }
                .buttonStyle(.bordered)
        }

        ForEach(result.tests) { t in TestCard(test: t) }

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

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(test.name).font(.subheadline)
                Spacer()
                Text(formatScore(test.score)).font(.headline)
            }
            Text(String(format: "median %.1f ms / threads %ld", locale: posix,
                        Double(test.medianNs) / 1e6, test.threads) + (test.valid ? "" : " / INVALID"))
                .font(.caption)
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
