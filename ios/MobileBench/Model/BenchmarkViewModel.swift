import Foundation
import UIKit

@MainActor
final class BenchmarkViewModel: ObservableObject {
    struct RunningInfo {
        var fraction: Double = 0
        var testName: String = ""
        var testIndex: Int = 0
        var testCount: Int = 0
        var warmup: Bool = true
    }

    enum State {
        case idle
        case running(RunningInfo)
        case done(BenchmarkResult, rawJson: String)
        case failed(String, rawJson: String?)
    }

    @Published private(set) var state: State = .idle
    /// 診断モード (行列サイズ掃引など)。スコアには影響しない
    @Published var diagnostics = false
    private var bridge: MBBenchmarkBridge?

    var isRunning: Bool {
        if case .running = state { return true }
        return false
    }

    func start() {
        guard !isRunning else { return }
        let bridge = MBBenchmarkBridge(
            deviceInfo: Self.deviceInfo(),
            scratchDir: NSTemporaryDirectory(),
            diagnostics: diagnostics
        )
        self.bridge = bridge
        state = .running(RunningInfo())
        // 計測中は画面を消さない
        UIApplication.shared.isIdleTimerDisabled = true

        // 計測は UI (メイン) スレッドをブロックしないよう専用スレッドで実行する。
        // QoS は core のワーカースレッドと同じ userInitiated。
        // (ViewModel はアプリと同じ寿命なので self は強参照でよい)
        let thread = Thread {
            let json = bridge.run { step, total, testIndex, testCount, testName, warmup in
                let info = RunningInfo(
                    fraction: total > 0 ? Double(step) / Double(total) : 0,
                    testName: testName,
                    testIndex: testIndex,
                    testCount: testCount,
                    warmup: warmup
                )
                Task { @MainActor in
                    guard self.isRunning else { return }
                    self.state = .running(info)
                }
            }
            Task { @MainActor in
                self.finish(json)
            }
        }
        thread.qualityOfService = .userInitiated
        thread.name = "MobileBench.runner"
        thread.start()
    }

    func cancel() {
        bridge?.cancel()
    }

    private func finish(_ json: String) {
        bridge = nil
        UIApplication.shared.isIdleTimerDisabled = false
        do {
            state = .done(try BenchmarkResult.parse(json), rawJson: json)
        } catch {
            state = .failed(error.localizedDescription, rawJson: json)
        }
    }

    private static func deviceInfo() -> [String: String] {
        var systemInfo = utsname()
        uname(&systemInfo)
        let machine = withUnsafeBytes(of: &systemInfo.machine) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        let device = UIDevice.current
        return [
            "platform": "ios",
            "manufacturer": "Apple",
            "model": machine,  // 例: "iPhone15,2" (シミュレータでは "arm64" / "x86_64")
            "model_name": device.model,
            "os_version": device.systemVersion,
            "app_version": AppInfo.version,
            "app_build": AppInfo.build,
        ]
    }
}

enum AppInfo {
    static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
    }

    /// CI では GitHub Actions の実行番号 (ios.yml で CURRENT_PROJECT_VERSION に渡している)
    static var build: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
    }
}
