import SwiftUI

struct ContentView: View {
    @ObservedObject var viewModel: BenchmarkViewModel

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    HStack(spacing: 8) {
                        Button("計測開始") { viewModel.start() }
                            .buttonStyle(.borderedProminent)
                            .disabled(viewModel.isRunning)
                        if viewModel.isRunning {
                            Button("中止") { viewModel.cancel() }
                                .buttonStyle(.bordered)
                        }
                    }

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
            if info.testCount > 0 {
                Text("(\(info.testIndex + 1)/\(info.testCount)) \(info.testName)  [\(info.warmup ? "ウォームアップ" : "本計測")]")
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
            Text("v\(result.benchmarkVersion)").font(.caption)
            if !result.valid {
                Text(result.cancelled ? "中止されました" : "一部のテストが無効です (チェックサム不一致)")
                    .foregroundStyle(.red)
            }
        }
        .frame(maxWidth: .infinity)
        .padding()
        .background(Color.accentColor.opacity(0.15), in: RoundedRectangle(cornerRadius: 12))

        ForEach(result.tests) { t in
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text(t.name).font(.subheadline)
                    Spacer()
                    Text(formatScore(t.score)).font(.headline)
                }
                Text(String(format: "median %.1f ms / threads %ld", locale: Locale(identifier: "en_US_POSIX"),
                            Double(t.medianNs) / 1e6, t.threads) + (t.valid ? "" : " / INVALID"))
                    .font(.caption)
                if let e = t.error { Text(e).foregroundStyle(.red).font(.caption) }
            }
            .padding(12)
            .background(Color.secondary.opacity(0.1), in: RoundedRectangle(cornerRadius: 12))
        }

        Divider()
        Text("""
            os: \(result.build.os)
            arch: \(result.build.arch)
            compiler: \(result.build.compiler)
            flags: \(result.build.flags) (profile \(result.build.flagsProfile), optimized \(String(result.build.optimized)))
            logical_cores: \(result.config.logicalCores)
            """)
            .font(.caption)
        RawJsonView(json: rawJson)
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

private func formatScore(_ score: Double) -> String {
    String(format: "%.0f", locale: Locale(identifier: "en_US_POSIX"), score)
}
